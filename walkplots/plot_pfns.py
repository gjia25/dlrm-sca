import os
from math import ceil, floor
import csv
import sys
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import multiprocessing

VA_UPPER_BOT = 0x7f0000000000
VA_LOWER_TOP = 0x100000000000

filename = sys.argv[1]
NUM_LOOKUPS = eval(sys.argv[2]) # 26 for kaggle
NUM_RUNS = eval(sys.argv[3]) # 3 for kaggle
interest = ["[heap]", "[stack]"]

def get_processed_filename(filename, pfn_or_va, run):
    return f"{filename}_{pfn_or_va}_{run}"

# Create scatter plot
def scatterplot(ylabel, filepath_to_time_to_values, run):
    fig = plt.figure(figsize=(15,9))
    axes = fig.add_subplot(111)
    axes.grid(True)
    
    # ylims
    max_y = 0
    min_y = sys.maxsize
                                
    for filepath, time_to_values in sorted(filepath_to_time_to_values.items()):
        x = []
        y = []
        for time, hex_list in time_to_values.items():
            if len(hex_list) == 0:
                continue
            x.extend([time] * len(hex_list))
            y.extend(hex_list)
            max_y = max(max_y, max(hex_list))
            min_y = min(min_y, min(hex_list))
            print(f"accesses to {filepath} in lookup {time}: {len(hex_list)}")
        axes.scatter(x, y, s=2, marker='s', label=filepath)

    print(f"min_y: {min_y}, max_y: {max_y}")
    axes.set_ylim([floor(min_y/10)*10, ceil(max_y/10)*10])
    # axes.get_yaxis().set_major_locator(ticker.MultipleLocator(16 ** 4))
    axes.get_yaxis().set_major_formatter(ticker.FuncFormatter(lambda x, p: '0x%08x' % int(x)))
    axes.set_ylabel(ylabel)

    axes.set_xlim([-0.99, NUM_RUNS - 0.01])
    axes.set_xticks(list(range(0, NUM_RUNS)))
    axes.set_xlabel("Inference #")
    
    axes.set_title("Distinct page accesses per embedding lookup")

    axes.legend(loc='lower center', bbox_to_anchor=(0.5, -0.2), ncol=6, markerscale=4)

    fig.tight_layout()
    plt.savefig(f"{get_processed_filename(filename, ylabel, run)}.png")
    # plt.show()

def check_no_distinct_pages_both(filepath_to_time_to_pfns, filepath_to_time_to_vas):
    no_distinct_pfns = True
    no_distinct_vas = True
    for filepath in interest:
        if filepath not in filepath_to_time_to_pfns or filepath not in filepath_to_time_to_vas:
            continue
        for time in range(1, NUM_LOOKUPS + 1):
            pfn_timevals = filepath_to_time_to_pfns[filepath].get(time, []) # filepath_to_time_to_pfns[filepath][time]
            va_timevals = filepath_to_time_to_vas[filepath].get(time, []) 
            print(f"run {time}, {filepath}: {len(pfn_timevals)} PFNs, {len(va_timevals)} VAs, {len(pfn_timevals) == len(va_timevals)}")
            # print(f"run {run}, lookup {time}, {filepath}: {len(pfn_timevals)} PFNs, {len(va_timevals)} VAs, {len(pfn_timevals) == len(va_timevals)}")
            no_distinct_pfns = no_distinct_pfns and len(pfn_timevals) == 0
            no_distinct_vas = no_distinct_vas and len(va_timevals) == 0
    return no_distinct_pfns and no_distinct_vas

# Remove pages accessed across all lookups
def remove_repeated_pages(filepath_to_time_to_values):
    for filepath, time_to_values in filepath_to_time_to_values.items():
        repeated_values = set.intersection(*time_to_values.values())
        for time, values in time_to_values.items():
            time_to_values[time] = values - repeated_values
    print(repeated_values)

def check_no_distinct_pages(filepath_to_run_to_pfns):
    no_distinct_pfns = True
    for filepath in interest:
        if filepath not in filepath_to_run_to_pfns:
            continue
        for run in range(NUM_RUNS):
            pfn_timevals = filepath_to_run_to_pfns[filepath].get(run, []) # filepath_to_time_to_pfns[filepath][time]
            print(f"run {run}, {filepath}: {len(pfn_timevals)} PFNs")
            no_distinct_pfns = no_distinct_pfns and len(pfn_timevals) == 0
    return no_distinct_pfns

def save_distinct_pages(filepath_to_time_to_values, pfn_or_va):
    with open(f"{get_processed_filename(filename, pfn_or_va, "distinct")}", "w") as file:
        writer = csv.writer(file)
        for filepath, time_to_values in filepath_to_time_to_values.items():
            for time, values in time_to_values.items():
                for value in values:
                    writer.writerow([filepath, time, value])

def task(args):
    run, filepath_to_time_to_pfns, filepath_to_time_to_vas = args
    remove_repeated_pages(filepath_to_time_to_pfns)
    remove_repeated_pages(filepath_to_time_to_vas)
        
    if check_no_distinct_pages(filepath_to_time_to_pfns, filepath_to_time_to_vas):
        return run, False
    
    scatterplot("PFN", filepath_to_run_to_pfns, "overall")
    scatterplot("VA", filepath_to_run_to_vas, "overall")    
    # scatterplot("PFN", filepath_to_time_to_pfns, run)
    # scatterplot("VA", filepath_to_time_to_vas, run)
    return run, True

def get_filepath_to_run_to_values(lst_filepath_to_time_to_values):
    filepath_to_run_to_values = {
        filepath: {run: set() for run in range(NUM_RUNS)}
        for filepath in interest
    }
    for run, filepath_to_time_to_values in enumerate(lst_filepath_to_time_to_values):
        for filepath, time_to_values in filepath_to_time_to_values.items():
            for time, values in time_to_values.items():
                filepath_to_run_to_values[filepath][run] |= values
    return filepath_to_run_to_values

if __name__ == '__main__':
    lst_filepath_to_time_to_pfns = [{} for _ in range(NUM_RUNS)]
    lst_filepath_to_time_to_vas = [{} for _ in range(NUM_RUNS)]

    # Open the file and read its lines
    with open(filename, "r") as file:
        reader = csv.reader(file)
        # next(reader)  # Skip header
        for row in reader:
            if len(row) != 5:
                continue
            run = (int(row[1]) - 1) // NUM_LOOKUPS
            time = int(row[1]) % NUM_LOOKUPS or NUM_LOOKUPS
            if run >= NUM_RUNS:
                break
            hex_value_virt = int(row[2], 16) # 2 for VA
            hex_value = int(row[3], 16) # 3 for PFN
            filepath = row[4]
            
            if filepath not in interest:
                continue
            
            # filepath_to_time_to_pfns = lst_filepath_to_time_to_pfns[run]
            # if filepath not in filepath_to_time_to_pfns:
            #     filepath_to_time_to_pfns[filepath] = {}
            # if time not in filepath_to_time_to_pfns[filepath]:
            #     filepath_to_time_to_pfns[filepath][time] = set()
            # filepath_to_time_to_pfns[filepath][time].add(hex_value)

            filepath_to_time_to_vas = lst_filepath_to_time_to_vas[run]
            if filepath not in filepath_to_time_to_vas:
                filepath_to_time_to_vas[filepath] = {}
            if time not in filepath_to_time_to_vas[filepath]:
                filepath_to_time_to_vas[filepath][time] = set()
            filepath_to_time_to_vas[filepath][time].add(hex_value_virt)

    print(f"Parsed output file: expected {NUM_RUNS} runs, got {len(lst_filepath_to_time_to_pfns)}")

    # filepath_to_run_to_pfns = get_filepath_to_run_to_values(lst_filepath_to_time_to_pfns)
    # del lst_filepath_to_time_to_pfns
    filepath_to_run_to_vas = get_filepath_to_run_to_values(lst_filepath_to_time_to_vas)
    del lst_filepath_to_time_to_vas
    print("De-duplicating pages across lookups")
    # remove_repeated_pages(filepath_to_run_to_pfns)
    remove_repeated_pages(filepath_to_run_to_vas)
    if check_no_distinct_pages(filepath_to_run_to_vas):
        print("No distinct pages accessed across inference requests :(")
        sys.exit(0)

    # scatterplot("PFN", filepath_to_run_to_pfns, "overall")
    scatterplot("VA", filepath_to_run_to_vas, "overall")

    # inputs = zip(range(NUM_RUNS), lst_filepath_to_time_to_pfns, lst_filepath_to_time_to_vas)
    # with multiprocessing.Pool(processes=NUM_RUNS) as pool: 
    #     print(pool.map(task, inputs))
    # for input in inputs:
    #     run, filepath_to_time_to_pfns, filepath_to_time_to_vas = input
    #     if run > 0:
    #         del prev_pfns
    #         del prev_vas
    #     task(input)
    #     prev_pfns = filepath_to_time_to_pfns
    #     prev_vas = filepath_to_time_to_vas