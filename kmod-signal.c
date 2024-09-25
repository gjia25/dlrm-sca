#define _GNU_SOURCE

#include <assert.h>
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

#define PROC_CLEAR_ACCESSED "/proc/clear_accessed_bits"
struct clear_request {
    pid_t pid;
    unsigned long start_vaddr;
    unsigned long end_vaddr;
};

#define PROC_READ_ACCESSED "/proc/read_accessed"
struct read_request {
    pid_t pid;
    unsigned long start_vaddr;
    unsigned long end_vaddr;
};
struct result_entry {
    unsigned long vaddr;
};

#define PATHSIZE		128
#define LINESIZE		256
#define PAGEMAP_CHUNK_SIZE	8
#define CHAR_BIT 8
#define MAX_REQUESTS 1024

#define INPUT_ADDRS "/dev/shm/addrs"
#define NUM_FEATURES 26
#define SIZE_LIST (int[26]){1460,583,10131227,2202608,305,24,12517,633,3,93145,5683,8351593,3194,27,14992,5461306,10,5652,2173,4,7046547,18,15,286181,105,142572}
#define EMB_SIZE 64

// globals
char *g_outdir = "/run/user/1000/dlrm-sca";
unsigned long g_input_addrs[NUM_FEATURES];
static struct timeval g_ts0;
int g_got_inputs = 0;
int g_in_lookup = 0;
int g_num_lookups = 0;
pid_t g_pid;
struct read_request g_requests[MAX_REQUESTS];
int g_num_requests = 0;

void read_input_addrs(unsigned long *list) {
    FILE *file;
    int count = 0;

    file = fopen(INPUT_ADDRS, "r");
    if (!file) {
        perror("failed to open input_addrs");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < NUM_FEATURES; i++) {
        if (fscanf(file, "%lu\n", &list[i]) != 1) {
            break;
        }
        count++;
    }
    fclose(file);

    if (count != NUM_FEATURES) {
        perror("failed to read all input_addrs");
        exit(EXIT_FAILURE);
    }
}

void clear_accessed_bits(unsigned long start_vaddr, unsigned long end_vaddr) {
    int fd;
    struct clear_request req;

    req.pid = g_pid;
    req.start_vaddr = start_vaddr;
    req.end_vaddr = end_vaddr;

    fd = open(PROC_CLEAR_ACCESSED, O_WRONLY);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    if (write(fd, &req, sizeof(req)) != sizeof(req)) {
        perror("write");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    close(fd);
    
    if (g_num_requests >= MAX_REQUESTS){
        printf("WARNING! Max requests exceeded\n");
        return;
    }
    g_requests[g_num_requests].pid = req.pid;
    g_requests[g_num_requests].start_vaddr = req.start_vaddr;
    g_requests[g_num_requests].end_vaddr = req.end_vaddr;
    g_num_requests++;
}

void clear_bits_for_lookups() {
    unsigned long start, end;
    int i;

    for (int i = 0; i < NUM_FEATURES; i++) {
        start = g_input_addrs[i] & 0xFFFFFFFFFFFFF000;
        end = (g_input_addrs[i] + EMB_SIZE * SIZE_LIST[i]) & 0xFFFFFFFFFFFFF000;
        printf("Clearing accessed bits for %lx-%lx", start, end);
        clear_accessed_bits(start, end);
    }

}

void append_accessed_pages(int request_idx) {
    int fd, ret;
    struct result_entry results[1024];
    ssize_t count;
    FILE *file;

    struct read_request req = g_requests[request_idx];
    
    fd = open(PROC_READ_ACCESSED, O_RDWR);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    ret = write(fd, &req, sizeof(req));
    if (ret != sizeof(req)) {
        perror("write");
        close(fd);
        exit(EXIT_FAILURE);
    }
    count = read(fd, results, sizeof(results));
    if (count == -1) {
        perror("read");
        close(fd);
        exit(EXIT_FAILURE);
    }

    int num_entries = count / sizeof(struct result_entry);

    char filename[PATHSIZE];
    sprintf(filename, "%s/dlrm-%llu", g_outdir, g_ts0.tv_sec * (uint64_t)1000000 + g_ts0.tv_usec);
    file = fopen(filename, "a");
    if (file == NULL) {
        perror("Unable to open output file");
        close(fd);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_entries; i++) {
        fprintf(file, "%d, 0x%lx\n", g_num_lookups, results[i].vaddr);
    }

    fclose(file);
    close(fd);
}


// Expected signal usage:
// child process (python script) sends SIGUSR1 to parent process (this program) when it is about to start a lookup
// parent process clears page table entry flags, then sends SIGUSR1 back to child
// child performs lookup, then sends SIGUSR1 to parent
// parent reads page table entry flags, then sends SIGUSR1 back to child
void signal_handler(int signal_num)
{
    if (signal_num == SIGUSR1) {
        if (g_got_inputs == 0) {
            read_input_addrs(g_input_addrs);
            g_got_inputs = 1;
        } else { // g_got_inputs = 1
            if (g_in_lookup == 0) {
                g_in_lookup = 1;
                g_num_lookups++;
                clear_bits_for_lookups();
            } else {
                for (int i = 0; i < g_num_requests; i++) {
                    append_accessed_pages(i);
                }
                g_num_requests = 0;
                g_in_lookup = 0;
            }
            kill(g_pid, SIGUSR1);
        }
    }
}

int main(int argc, char *argv[])
{
	int status, err = 0;
	double mbytes;
	pid_t ppid;
	
	// options
	if (argc < 3) {
		printf("USAGE: walksign <python_bin_path> <script>\n");
		exit(0);
	}
    printf("RUNNING: %s %s\n", argv[1], argv[2]);
	ppid = getpid(); // parent PID

	gettimeofday(&g_ts0, NULL);
	g_pid = fork(); // child PID

	if (g_pid == -1) {
		// Fork failed
		perror("fork");
		exit(EXIT_FAILURE);
	} else if (g_pid == 0) {
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

		printf("Parent: Watching PID %d page references...\n", g_pid);

		
		signal(SIGUSR1, signal_handler); // Set signal handler for SIGUSR1
		while (waitpid(g_pid, NULL, WNOHANG) >= 0) { // Loop until child process exits
			;
		}

		printf("Parent: Child process exited with status %d\n", status);
		printf("Parent: g_num_lookups = %d\n", g_num_lookups);
	}	
	return 1;
}
