import os
from math import ceil, floor
import csv
import sys
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

filename = sys.argv[1]
filepath_to_time_to_pfns = {}
filepath_to_time_to_vas = {}

prefixes = ["/usr/lib/", "/usr/bin/", "/home/gjia/yolo/venv/lib/python3.10/site-packages/"]
interest = ["[heap]", "[stack]"]


# Open the file and read its lines
with open(filename, "r") as file:
    reader = csv.reader(file)
    # next(reader)  # Skip header
    for row in reader:
        if len(row) != 5:
            continue
        time = int(row[1])
        if time > 26: # TODO: delete this block for multi batch
            break
        hex_value_virt = int(row[2], 16) # 2 for VA
        hex_value = int(row[3], 16) # 3 for PFN
        filepath = row[4]
        
        if filepath not in interest:
            continue
        
        if filepath not in filepath_to_time_to_pfns:
            filepath_to_time_to_pfns[filepath] = {}
        if time not in filepath_to_time_to_pfns[filepath]:
            filepath_to_time_to_pfns[filepath][time] = set()
        filepath_to_time_to_pfns[filepath][time].add(hex_value)

        if filepath not in filepath_to_time_to_vas:
            filepath_to_time_to_vas[filepath] = {}
        if time not in filepath_to_time_to_vas[filepath]:
            filepath_to_time_to_vas[filepath][time] = set()
        filepath_to_time_to_vas[filepath][time].add(hex_value_virt)

# Create scatter plot
def scatterplot(ylabel, filepath_to_time_to_values, auto_range=True):
    fig = plt.figure(figsize=(15,9))
    axes = fig.add_subplot(111)
                         
    # ylims
    max_y = 0
    min_y = sys.maxsize
                             
    for filepath, time_to_values in sorted(filepath_to_time_to_values.items()):
        x = []
        y = []
        for time, hex_list in time_to_values.items():
            x.extend([time] * len(hex_list))
            y.extend(hex_list)
            max_y = max(max_y, max(hex_list))
            min_y = min(min_y, min(hex_list))
            print(f"accesses to {filepath} in lookup {time}: {len(hex_list)}")
        # =============================================================================
        # When using auto_range, create multiple plots for each range, then combine
        #  - Makes points more visible when address space is spread out
        #  - Create new range whenever a space between addresses > 1% entire space
        # =============================================================================
        y_ranges = []
        total_points = len(y)
        if auto_range:
            # Sort y, x by y
            y, x = zip(*sorted(zip(y, x), key=lambda a: a[0]))
            total_dist = y[-1] - y[0]
            y_prev = None

            # Separate points into ranges
            #  - Each range has a y_color dict and a y_range class
            curr_range = [None, None]
            points = 0
            range_low = y[0]
            for y_pt, x_pt in zip(y, x):
                if y_prev is not None and y_pt - y_prev > total_dist * 0.01:
                    # if points/total_points > 0.001:
                    curr_range = [range_low, y_prev]
                    y_ranges.append(curr_range)
                    curr_range = [None, None]
                    range_low = y_pt
                    points = 0

                # Iterate and add point to dict
                y_prev = y_pt
                points += 1
            y_ranges.append(curr_range)
            del y
            del x
        
        axes.scatter(x, y, s=2, marker='s', label=filepath)

    print(f"min_y: {min_y}, max_y: {max_y}")
    axes.set_ylim([floor(min_y/10)*10, ceil(max_y/10)*10])
    # axes.get_yaxis().set_major_locator(ticker.MultipleLocator(16 ** 4))
    axes.get_yaxis().set_major_formatter(ticker.FuncFormatter(lambda x, p: '0x%08x' % int(x)))

    axes.set_xlabel("Lookup #")
    axes.set_ylabel(ylabel)
    axes.set_title("Page accesses by each embedding lookup")

    axes.legend(loc='lower center', bbox_to_anchor=(0.5, -0.2), ncol=6, markerscale=4)

    axes.grid(True)
    fig.tight_layout()
    plt.savefig(f"{filename}_{ylabel}.png")
    plt.show()

scatterplot("PFN", filepath_to_time_to_pfns)
scatterplot("VA", filepath_to_time_to_vas)