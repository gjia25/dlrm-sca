import os
from math import ceil, floor
import sys
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import matplotlib.transforms as transforms

filename = sys.argv[1]
NUM_LOOKUPS = eval(sys.argv[2]) # 26 for kaggle
NUM_RUNS = eval(sys.argv[3]) # 3 for kaggle

# file contents:
# for each run
#   for each lookup
#       assert src_data = ip + idx * 64
# remove dups across runs/samples (inference requests): expect src_data removed

# data viz: colour points by run? VA on y axis. lookups on x axis?
# then repeat at page granularity, i.e. mask lower 12 bits

# might need to handle ranges that are far apart

# WANT TO SHOW: inputs A,B,C identifiable by VAs accessed.
# idea: compare same input across runs. 

def get_stats(runs_to_addrs):
    stats = []
    for time, hex_tup_list in runs_to_addrs.items(): 
        assert len(hex_tup_list) == NUM_LOOKUPS, f"run {time}: {len(hex_tup_list)} lookups"
        for i in range(NUM_LOOKUPS):
            ip, src_data = hex_tup_list[i]
            stats.append((time, ip, "entry"))
            stats.append((time, src_data, "table"))
    return stats

class y_range:
    def __init__(self):
        self.label_to_xy = {
            "entry": [[], []],
            "table": [[], []],
        }
        self.y_low = None
        self.y_high = None
          
    def add_point(self, x, y, label):
        self.label_to_xy[label][0].append(x)
        self.label_to_xy[label][1].append(y)

    def set_range(self, y_low, y_high):
        self.y_low = y_low
        self.y_high = y_high
    
    def print_debug(self):
        y_low = 0 if self.y_low is None else self.y_low
        y_high = 0 if self.y_high is None else self.y_high
        print(f"-> range [{y_low},{y_high}] = {y_high - y_low}, num points = {len(self.label_to_xy['entry'][0]) + len(self.label_to_xy['table'][0])}")

def get_y_ranges(stats):
    y_ranges = []

    # Sort y, x, and colors by y
    x, y, colors = zip(*sorted(stats, key=lambda a: a[1]))
    total_dist = y[-1] - y[0]
    y_prev = None

    # Separate points into ranges
    curr_range = y_range()
    points = 0
    range_low = y[0]
    for y_pt, x_pt, c_pt in zip(y, x, colors):
        if y_prev is not None and y_pt - y_prev > total_dist * 0.00001:
            curr_range.set_range(range_low, y_prev)
            y_ranges.append(curr_range)
            curr_range = y_range()
            range_low = y_pt
            points = 0
        # Add point to range
        y_prev = y_pt
        points += 1
        curr_range.add_point(x_pt, y_pt, c_pt)
    y_ranges.append(curr_range)

    print(f"total yrange = {y[0]},{y[-1]}, total_dist = {total_dist}, num ranges = {len(y_ranges)}")
    for r in y_ranges:
        r.print_debug()
    return y_ranges


def plot_yranges(filename, y_ranges):
    n_subplots = len(y_ranges)
    fig = plt.figure(figsize=(10, 1.5*n_subplots))
    sub_axes = []

    colors = {
        "table": "orange",
        "entry": "blue",
    }
    for i, r in enumerate(y_ranges):
        # Create subplot
        if i == 0:
            r_ax = fig.add_subplot(n_subplots, 1, i+1)
        else:
            r_ax = fig.add_subplot(n_subplots, 1, i+1, sharex=sub_axes[0])
        sub_axes.append(r_ax)
        for label in ["entry"]:
            x, y = r.label_to_xy[label]
            r_ax.scatter(x, y, s=2, label=label, color=colors[label], marker='s')
        r_ax.get_yaxis().set_major_formatter(ticker.FuncFormatter(lambda x, p: '0x%08x' % int(x)))
        

    sub_axes[0].set_xlim([-0.99, NUM_RUNS - 0.01])
    sub_axes[0].set_xticks(list(range(0, NUM_RUNS)))

    sub_axes[0].set_ylabel("VA", fontsize=14)
    sub_axes[NUM_LOOKUPS//2].set_xlabel("Inference #", fontsize=14)    
    sub_axes[NUM_LOOKUPS//2].set_title("Embedding accesses per inference request")
    sub_axes[NUM_LOOKUPS//2].legend(markerscale=4)
    
    fig.tight_layout()
    plt.savefig(f"{filename}-yranges.png")
    plt.show()

def plot_lookups(filename, runs_to_addrs):
    fig = plt.figure(figsize=(NUM_LOOKUPS, 8))
    sub_axes = []
    colors = ["blue", "red", "green"]
    markers = ["s", "o", "^"]
    dx = .05
    offs = [-dx, 0, dx]
    # offset = lambda p: transforms.ScaledTranslation(p/72.,0, plt.gcf().dpi_scale_trans)
    for run, xy in runs_to_addrs.items():
        x, y = xy
        for i in range(NUM_LOOKUPS):
            translation = None
            if run == 0: # 2 if multi run kaggle
                ax = fig.add_subplot(1, NUM_LOOKUPS, i+1)
                sub_axes.append(ax)
            else:
                ax = sub_axes[i]
            ax.scatter(x[2*i] + offs[run], y[2*i], s=4, marker=markers[run], label=run, color=colors[run])
            ax.set_xlim([i - 4*dx, i + 4*dx])
            ax.set_xticks([i])
            ax.set_yticks([])
            # ax.get_yaxis().set_major_locator(ticker.MaxNLocator(2))
            # ax.get_yaxis().set_major_formatter(ticker.FuncFormatter(lambda x, p: '0x%08x' % int(x)))
            # ax.tick_params(axis='y', labelrotation=90)
    
    sub_axes[0].set_ylabel("VPN", fontsize=14)
    sub_axes[1].set_xlabel("Lookup #", fontsize=14)    
    sub_axes[NUM_LOOKUPS//2].set_title("Embedding accesses per inference request")
    sub_axes[NUM_LOOKUPS//2].legend(markerscale=4, ncols=3, bbox_to_anchor=(0, -0.25), loc="lower center", title="Inference #")
    plt.subplots_adjust(bottom=0.2)
    fig.tight_layout()
    # plt.savefig(f"{filename}-lookups.png")
    plt.savefig(f"{filename}-lookups-masked.png")
    plt.show()
    

def get_runs_to_addrs(filename):
    runs_to_addrs = {}

    with open(filename, "r") as file:
        curr_run = -1
        src_data = -1
        ip = -1
        idx = -1
        for line in file:
            if line.startswith("Sample"):
                curr_run = int(line.split(" ")[1])
                if curr_run > 0:
                    curr_run -= 1
            elif line.startswith("src="):
                src_data = int(line[line.find("src_data=")+len("src_data="):], 16)
            elif line.startswith("ip="):
                comma_idx = line.find(",")
                ip = int(line[len("ip="):comma_idx], 16)
                idx = int(line[comma_idx + 1 + len("idx="):-1])
                assert ip == src_data + idx * 64, f"run {curr_run}: src_data={src_data}, ip={ip}, idx={idx}"
                if curr_run not in runs_to_addrs:
                    runs_to_addrs[curr_run] = []
                runs_to_addrs[curr_run].append((ip, src_data))
    return runs_to_addrs

if __name__ == '__main__':
    runs_to_addrs = {}

    with open(filename, "r") as file:
        total_lookups = 0
        curr_run = -1
        src_data = -1
        ip = -1
        idx = -1
        for line in file:
            if line.startswith("Sample"):
                curr_run = int(line.split(" ")[1])
                if curr_run > 0:
                    curr_run -= 1

            elif line.startswith("src="):
                src_data = int(line[line.find("src_data=")+len("src_data="):], 16)
            elif line.startswith("ip="):
                comma_idx = line.find(",")
                ip = int(line[len("ip="):comma_idx], 16)
                idx = int(line[comma_idx + 1 + len("idx="):-1])
                assert ip == src_data + idx * 64, f"run {curr_run}: src_data={src_data}, ip={ip}, idx={idx}"
                if curr_run not in runs_to_addrs:
                    runs_to_addrs[curr_run] = [[], []]
                
                # mask out page offset
                ip = ip & 0xFFFFFFFFFFFFF000
                lookup = total_lookups % NUM_LOOKUPS
                curr_round = total_lookups // (NUM_LOOKUPS * NUM_RUNS)
                if curr_round > 0:
                    assert runs_to_addrs[curr_run][1][2*lookup] == ip, f"ip = {runs_to_addrs[curr_run][1][2*lookup]} but got {ip}"
                    assert runs_to_addrs[curr_run][1][2*lookup+1] == src_data, f"src_data = {runs_to_addrs[curr_run][1][2*lookup+1]} but got {src_data}"
                else:
                    runs_to_addrs[curr_run][0].extend([lookup] * 2)
                    runs_to_addrs[curr_run][1].append(ip)
                    runs_to_addrs[curr_run][1].append(src_data)
                total_lookups += 1
    
    plot_lookups(filename, runs_to_addrs)
    # stats = get_stats(runs_to_addrs)
    # y_ranges = get_y_ranges(stats)
    # y_ranges.reverse()
    # plot_yranges(filename, y_ranges)
    
