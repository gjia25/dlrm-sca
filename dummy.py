import time
import os
import signal
import argparse

NUM_LOOKUPS = 26

def signal_handler(signal_number, frame):
    pass

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Dummy accesses')
    parser.add_argument("--parent-pid", type=int)
    args = parser.parse_args()

    arr1 = list(range(NUM_LOOKUPS))

    signal.signal(signal.SIGUSR1, signal_handler)
    for i in range(NUM_LOOKUPS):
        os.kill(args.parent_pid, signal.SIGUSR1)
        signal.pause()

        n = arr1[i]

        os.kill(args.parent_pid, signal.SIGUSR1)
        signal.pause()
        print(f"Accessed {n} at arr1[{i}]")