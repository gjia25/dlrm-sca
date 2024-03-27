import os
from math import ceil, floor
import csv
import sys
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import concurrent.futures

NUM_LOOKUPS = 26
NUM_RUNS = 3
VA_UPPER_BOT = 0x7f0000000000
VA_LOWER_TOP = 0x100000000000

filename = sys.argv[1]
lst_filepath_to_time_to_pfns = [{} for _ in range(NUM_RUNS)]
lst_filepath_to_time_to_vas = [{} for _ in range(NUM_RUNS)]

prefixes = ["/usr/lib/", "/usr/bin/", "/home/gjia/yolo/venv/lib/python3.10/site-packages/"]
interest = ["[heap]", "[stack]"]


# Open the file and read its lines
with open(filename, "r") as file:
    reader = csv.reader(file)
    # next(reader)  # Skip header
    for row in reader:
        if len(row) != 5:
            continue
        if NUM_RUNS <= 1:
            run = 0
        else:
            run = (int(row[1]) - 1) // NUM_LOOKUPS
        time = int(row[1]) % NUM_LOOKUPS or NUM_LOOKUPS

        hex_value_virt = int(row[2], 16) # 2 for VA
        hex_value = int(row[3], 16) # 3 for PFN
        filepath = row[4]
        
        if filepath not in interest:
            continue
        
        filepath_to_time_to_pfns = lst_filepath_to_time_to_pfns[run]
        if filepath not in filepath_to_time_to_pfns:
            filepath_to_time_to_pfns[filepath] = {}
        if time not in filepath_to_time_to_pfns[filepath]:
            filepath_to_time_to_pfns[filepath][time] = set()
        filepath_to_time_to_pfns[filepath][time].add(hex_value)

        filepath_to_time_to_vas = lst_filepath_to_time_to_vas[run]
        if filepath not in filepath_to_time_to_vas:
            filepath_to_time_to_vas[filepath] = {}
        if time not in filepath_to_time_to_vas[filepath]:
            filepath_to_time_to_vas[filepath][time] = set()
        filepath_to_time_to_vas[filepath][time].add(hex_value_virt)

print("Parsed output file")

# Remove pages accessed across all lookups
def remove_repeated_pages(filepath_to_time_to_values):
    for filepath, time_to_values in filepath_to_time_to_values.items():
        repeated_values = set.intersection(*time_to_values.values())
        for time, values in time_to_values.items():
            time_to_values[time] = values - repeated_values

# Create scatter plot
def scatterplot(ylabel, filepath_to_time_to_values, run):
    fig = plt.figure(figsize=(15,9))
    axes = fig.add_subplot(111)

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

    axes.set_xlabel("Lookup #")
    axes.set_ylabel(ylabel)
    axes.set_title("Distinct page accesses per embedding lookup")

    axes.legend(loc='lower center', bbox_to_anchor=(0.5, -0.2), ncol=6, markerscale=4)

    axes.grid(True)
    fig.tight_layout()
    plt.savefig(f"{filename}_{ylabel}_{run}.png")
    # plt.show()

def task(run, dict_pair):
    filepath_to_time_to_pfns, filepath_to_time_to_vas = dict_pair
    remove_repeated_pages(filepath_to_time_to_pfns)
    remove_repeated_pages(filepath_to_time_to_vas)

    # Check if there are no distinct page accesses
    no_distinct_pfns = True
    no_distinct_vas = True
    for filepath in interest:
        for time in range(1, NUM_LOOKUPS + 1):
            print(f"run {run} lookup {time}: {len(filepath_to_time_to_pfns[filepath][time])} PFNs, {len(filepath_to_time_to_vas[filepath][time])} VAs, {len(filepath_to_time_to_pfns[filepath][time]) == len(filepath_to_time_to_vas[filepath][time])}")
            no_distinct_pfns = no_distinct_pfns and len(filepath_to_time_to_pfns[filepath][time]) == 0
            no_distinct_vas = no_distinct_vas and len(filepath_to_time_to_vas[filepath][time]) == 0
        
    if no_distinct_pfns and no_distinct_vas:
        return run, False
    
    scatterplot("PFN", filepath_to_time_to_pfns, run)
    scatterplot("VA", filepath_to_time_to_vas, run)
    return run, True

dict_pairs = zip(lst_filepath_to_time_to_pfns, lst_filepath_to_time_to_vas)
with concurrent.futures.ThreadPoolExecutor(max_workers=NUM_RUNS) as executor:
    futures = [executor.submit(task, run, dict_pair) for run, dict_pair in enumerate(dict_pairs)]

    for future in concurrent.futures.as_completed(futures):
        run, result = future.result()
        print(f"Run {run}: {'done' if result else 'no distinct accesses :('}")