import time
import os
import signal
import argparse

NUM_LOOKUPS = 26

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Dummy accesses')
    parser.add_argument("--parent-pid", type=int)
    args = parser.parse_args()

    arr1 = list(range(NUM_LOOKUPS))

    for i in range(NUM_LOOKUPS):
        os.kill(args.parent_pid, signal.SIGUSR1)
        time.sleep(1)

        n = arr1[i]

        os.kill(args.parent_pid, signal.SIGUSR1)
        time.sleep(1)