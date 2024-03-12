import os
from math import ceil
import csv
import sys
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

filename = sys.argv[1]
filepath_to_time_to_pfns = {}

prefixes = ["/usr/lib/", "/usr/bin/", "/home/gjia/yolo/venv/lib/python3.10/site-packages/"]

# Open the file and read its lines
with open(filename, "r") as file:
    reader = csv.reader(file)
    # next(reader)  # Skip header
    for row in reader:
        if len(row) != 4:
            # print(f'weird row: {repr(row)}')
            continue
        time = int(row[1])
        hex_value = int(row[2], 16)
        filepath = row[3]
        
        for prefix in prefixes: # merging filepaths with same/similar directories
            if filepath.startswith(prefix):
                suffix = filepath[len(prefix):]
                filepath = suffix.split('/')[0].lower()
                if filepath.endswith(".libs"):
                    filepath = filepath[:-5]
                elif filepath.startswith("pil"):
                    filepath = "pillow"
                if filepath.startswith("torch"):
                    filepath = "torch"
                break
        
        if filepath not in filepath_to_time_to_pfns:
            filepath_to_time_to_pfns[filepath] = {}
        if time not in filepath_to_time_to_pfns[filepath]:
            filepath_to_time_to_pfns[filepath][time] = []
        filepath_to_time_to_pfns[filepath][time].append(hex_value)

# Record all PFNs
y_values = []

# Create scatter plot
fig = plt.figure(figsize=(14,8))
axes = fig.add_subplot(111)

NUM_COLORS = len(filepath_to_time_to_pfns)
cm = plt.get_cmap('gist_ncar')
axes.set_prop_cycle(color=[cm(i/NUM_COLORS) for i in range(NUM_COLORS)])
# axes.set_prop_cycle(color=plt.get_cmap('tab20b').colors)
                         
for filepath, time_to_pfns in sorted(filepath_to_time_to_pfns.items()):
    x = []
    y = []
    for time, hex_list in time_to_pfns.items():
        x.extend([time] * len(hex_list))
        y.extend(hex_list)
        y_values.extend(hex_list)
    axes.scatter(x, y, s=2, marker='s', label=filepath)

axes.set_ylim([0, ceil(max(y_values)/10)*10])
axes.get_yaxis().set_major_locator(ticker.MultipleLocator(16 ** 4))
axes.get_yaxis().set_major_formatter(ticker.FuncFormatter(lambda x, p: '0x%08x' % int(x)))

axes.set_xlabel("Lookup #")
axes.set_ylabel("PFN")
axes.set_title("Page accesses by each embedding lookup")

axes.legend(loc='lower center', bbox_to_anchor=(0.5, -0.2), ncol=6, markerscale=4)

axes.grid(True)
fig.tight_layout()
plt.savefig(f"{filename}.pdf")
plt.show()