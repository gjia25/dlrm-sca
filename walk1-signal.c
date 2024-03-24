/*
 * Adapted from the following tool, described below: https://github.com/brendangregg/wss
 *
 * wss-v2.c	Estimate the working set size (WSS) for a process on Linux.
 *		Version 2: suited for large processes.
 *
 * This is a proof of concept that uses idle page tracking from Linux 4.3+, for
 * a page-based WSS estimation. This version snapshots the entire system's idle
 * page flags, which is efficient for analyzing large processes, but not tiny
 * processes. For those, see wss-v1.c. There is also wss.pl, which uses can be
 * over 10x faster and works on older Linux, however, uses the referenced page
 * flag and has its own caveats.
 *
 * Currently written for x86_64 and default page size only. Early version:
 * probably has bugs.
 *
 *
 * Copyright 2018 Netflix, Inc.
 * Licensed under the Apache License, Version 2.0 (the "License")
 *
 * 13-Jan-2018	Brendan Gregg	Created this.
 *
 */

#define _GNU_SOURCE

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

// see Documentation/vm/pagemap.txt:
#define PFN_MASK		(~(0x1ffLLU << 55))

#define PATHSIZE		128
#define LINESIZE		256
#define PAGEMAP_CHUNK_SIZE	8
#define IDLEMAP_CHUNK_SIZE	8
#define IDLEMAP_BUF_SIZE	4096

// big enough to span 740 Gbytes:
#define MAX_IDLEMAP_SIZE	(20 * 1024 * 1024)

// from mm/page_idle.c:
#ifndef BITMAP_CHUNK_SIZE
#define BITMAP_CHUNK_SIZE	8
#endif

#ifndef PAGE_OFFSET
#define PAGE_OFFSET		0xffff880000000000LLU
#endif

// globals
int g_debug = 0;		// 1 == some, 2 == verbose
int g_activepages = 0;
int g_walkedpages = 0;
char *g_idlepath = "/sys/kernel/mm/page_idle/bitmap";
unsigned long long *g_idlebuf;
unsigned long long g_idlebufsize;
static struct timeval ts0;

FILE* output_file;
pid_t pid, ppid;
int in_lookup = 0;
int walked_once = 0;
int num_lookups = 0;

/*
 * This code must operate on bits in the pageidle bitmap and process pagemap.
 * Doing this one by one via syscall read/write on a large process can take too
 * long, eg, 7 minutes for a 130 Gbyte process. Instead, I copy (snapshot) the
 * idle bitmap and pagemap into our memory with the fewest syscalls allowed,
 * and then process them with load/stores. Much faster, at the cost of some memory.
 */

int mapidle(pid_t pid, uint64_t time, unsigned long long mapstart, unsigned long long mapend, const char *filepath, FILE *output_file, int num_lookups)
{
	char pagepath[PATHSIZE];
	int pagefd;
	char *line;
	unsigned long long offset, i, pagemapp, pfn, vaddr, idlemapp, idlebits;
	int pagesize;
	int err = 0;
	unsigned long long *pagebuf, *p;
	unsigned long long pagebufsize;
	ssize_t len;
	
	// XXX: handle huge pages
	pagesize = getpagesize();

	pagebufsize = (PAGEMAP_CHUNK_SIZE * (mapend - mapstart)) / pagesize;
	if ((pagebuf = malloc(pagebufsize)) == NULL) {
		printf("Can't allocate memory for pagemap buf (%lld bytes)",
		    pagebufsize);
		return 1;
	}

	// open pagemap for virtual to PFN translation
	if (sprintf(pagepath, "/proc/%d/pagemap", pid) < 0) {
		printf("Can't allocate memory.");
		return 1;
	}
	if ((pagefd = open(pagepath, O_RDONLY)) < 0) {
		perror("Can't read pagemap file");
		return 2;
	}

	// cache pagemap to get PFN, then operate on PFN from idlemap
	offset = PAGEMAP_CHUNK_SIZE * mapstart / pagesize;
	if (lseek(pagefd, offset, SEEK_SET) < 0) {
		printf("Can't seek pagemap file\n");
		err = 1;
		goto out;
	}
	p = pagebuf;

	// optimized: read this in one syscall
	if (read(pagefd, p, pagebufsize) < 0) {
		perror("Read page map failed.");
		err = 1;
		goto out;
	}

	for (i = 0; i < pagebufsize / sizeof (unsigned long long); i++) {
		vaddr = mapstart + i * pagesize;
		// convert virtual address p to physical PFN
		pfn = p[i] & PFN_MASK;
		if (pfn == 0)
			continue;

		// read idle bit
		idlemapp = (pfn / 64) * BITMAP_CHUNK_SIZE;
		if (idlemapp > g_idlebufsize) {
			printf("ERROR: bad PFN read from page map.\n");
			err = 1;
			goto out;
		}
		idlebits = g_idlebuf[idlemapp];
		if (g_debug > 1) {
			printf("R: p %llx pfn %llx idlebits %llx\n",
			    p[i], pfn, idlebits);
		}

		if (!(idlebits & (1ULL << (pfn % 64)))) {
			g_activepages++;

			// Append pfn, filepath, and current time to the output file
			fprintf(output_file, "%llu,%d,%llx,%llx,%s\n", time, num_lookups, vaddr, pfn, filepath);
		}
		g_walkedpages++;
	}

out:
	close(pagefd);
	return err;
}

int walkmaps(pid_t pid, FILE *output_file, int num_lookups)
{
	FILE *mapsfile;
	char mapspath[PATHSIZE];
	char line[LINESIZE];
	size_t len = 0;
	unsigned long long mapstart, mapend;
	char filepath[PATHSIZE];
	static struct timeval curr_ts;
	uint64_t time;

	// get time since start
	gettimeofday(&curr_ts, NULL);
	time = 1000000 * (curr_ts.tv_sec - ts0.tv_sec) + (curr_ts.tv_usec - ts0.tv_usec);
	
	// read virtual mappings
	if (sprintf(mapspath, "/proc/%d/maps", pid) < 0) {
		printf("Can't allocate memory. Exiting.");
		exit(1);
	}
	if ((mapsfile = fopen(mapspath, "r")) == NULL) {
		perror("Can't read maps file");
		exit(2);
	}
	
	while (fgets(line, sizeof (line), mapsfile) != NULL) {
		sscanf(line, "%llx-%llx %*s %*s %*s %*s %s", &mapstart, &mapend, filepath);
		if (g_debug)
			printf("MAP %llx-%llx %s\n", mapstart, mapend, filepath);
		if (mapstart > PAGE_OFFSET)
			continue;	// page idle tracking is user mem only
		if (mapidle(pid, time, mapstart, mapend, filepath, output_file, num_lookups)) {
			printf("Error setting map %llx-%llx. Exiting.\n",
			    mapstart, mapend);
		}
	}

	fclose(mapsfile);

	return 0;
}

int setidlemap()
{
	char *p;
	int idlefd, i;
	// optimized: large writes allowed here:
	char buf[IDLEMAP_BUF_SIZE];

	for (i = 0; i < sizeof (buf); i++)
		buf[i] = 0xff;

	// set entire idlemap flags
	if ((idlefd = open(g_idlepath, O_WRONLY)) < 0) { // open "/sys/kernel/mm/page_idle/bitmap"
		perror("Can't write idlemap file");
		exit(2);
	}
	while (write(idlefd, &buf, sizeof(buf)) > 0) {;}

	close(idlefd);

	return 0;
}

int loadidlemap()
{
	unsigned long long *p;
	int idlefd;
	ssize_t len;

	if ((g_idlebuf = malloc(MAX_IDLEMAP_SIZE)) == NULL) {
		printf("Can't allocate memory for idlemap buf (%d bytes)",
		    MAX_IDLEMAP_SIZE);
		exit(1);
	}

	// copy (snapshot) idlemap to memory
	if ((idlefd = open(g_idlepath, O_RDONLY)) < 0) {
		perror("Can't read idlemap file");
		exit(2);
	}
	p = g_idlebuf;
	// unfortunately, larger reads do not seem supported
	while ((len = read(idlefd, p, IDLEMAP_CHUNK_SIZE)) > 0) {
		p += IDLEMAP_CHUNK_SIZE;
		g_idlebufsize += len;
	}
	close(idlefd);

	return 0;
}

// Expected signal usage:
// child process (python script) sends SIGUSR1 to parent process (this program) when it is about to start a lookup
// parent process clears page table entry flags, then sends SIGUSR1 back to child
// child performs lookup, then sends SIGUSR1 to parent
// parent reads page table entry flags, then sends SIGUSR1 back to child
void signal_handler(int signal_num)
{
    if (signal_num == SIGUSR1) {
        if (in_lookup == 0) {
			static struct timeval ts1, ts2;
			unsigned long long set_us;
			in_lookup = 1;
            num_lookups++;
			setidlemap(); // set idle flags to 1
			loadidlemap();
			walkmaps(pid, output_file, num_lookups);
			printf("g_walkedpages = %d", g_walkedpages);
        } else {
			static struct timeval ts3, ts4;
			unsigned long long read_us;
			loadidlemap(); // cache page idle map
			walkmaps(pid, output_file, num_lookups); // read page flags
			in_lookup = 0;
        }
		// kill(pid, SIGUSR1);
    }
}

int main(int argc, char *argv[])
{
	int status, err = 0;
	double mbytes;
	
	// options
	if (argc < 3) {
		printf("USAGE: walksign <python_bin_path> <script>\n");
		exit(0);
	}
    printf("RUNNING: %s %s\n", argv[1], argv[2]);
	ppid = getpid(); // parent PID

	gettimeofday(&ts0, NULL);
	pid = fork(); // child PID

	if (pid == -1) {
		// Fork failed
		perror("fork");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		printf("In child process\n");
		
		// Set child process to run on core 1
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(1, &cpuset); 

		if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
			perror("sched_setaffinity");
			exit(EXIT_FAILURE);
		}
		
		// Pass parent PID to child
		char pid_arg[20];
		sprintf(pid_arg, "--parent-pid=%d", ppid);
		execlp(argv[1], argv[1], argv[2], pid_arg, NULL);
		
		// If execlp returns, it means it failed
		perror("execlp");
		exit(EXIT_FAILURE);
	} else {
		printf("In parent process\n");
		
		// Set parent process to run on core 0
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(0, &cpuset);

		if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == -1) {
			perror("sched_setaffinity");
			exit(EXIT_FAILURE);
		}

		printf("Parent: Watching PID %d page references...\n", pid);
		
		// Open output file for appending
		char filename[PATHSIZE];
		sprintf(filename, "o-%s-%llu", argv[2], ts0.tv_sec * (uint64_t)1000000 + ts0.tv_usec);
		output_file = fopen(filename, "a");
		if (output_file == NULL) {
			perror("Unable to open output file");
			err = 1;
			goto out;
		}
		
		signal(SIGUSR1, signal_handler); // Set signal handler for SIGUSR1
		while (waitpid(pid, NULL, WNOHANG) >= 0) { // Loop until child process exits
			;
		}

		printf("Parent: Child process exited with status %d\n", status);
		printf("Parent: num_lookups = %d\n", num_lookups);
		out:
		if (output_file != NULL) {
			fclose(output_file);
		}
	}	
	return 1;
}
