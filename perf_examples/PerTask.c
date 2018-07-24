/*
 * task_inherit.c - example of a task counting event in a tree of child processes
 *
 * Copyright (c) 2009 Google, Inc
 * Contributed by Stephane Eranian <eranian@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/wait.h>
#include <locale.h>
#include <err.h>

#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <errno.h>

#include "perf_util.h"

#define MAX_GROUPS 10
#define MAX_COUNTERS 50

int memfd;       // file descriptor, from shm_open()
char *membase;   // base address, from mmap()
char *memptr;        // shm_base is fixed, ptr is movable 

struct countervalues {
    double ratio;
    uint64_t val, delta;
    uint64_t auxval1, auxval2;
    char name[64];
    uint64_t seqno;
};

struct options_t {
	const char *events[MAX_GROUPS];
	int num_groups;
	int format_group;
	int inherit;
	int print;
	int pin;
	pid_t pid;
    struct countervalues counters[MAX_COUNTERS];
    int countercount;
    int lock;
};

volatile int quit;

int
child(char **arg)
{
	/*
	 * execute the requested command
	 */
	execvp(arg[0], arg);
	errx(1, "cannot exec: %s\n", arg[0]);
	/* not reached */
}

static void
read_groups(perf_event_desc_t *fds, int num, struct options_t *options)
{
	uint64_t *values = NULL;
	size_t new_sz, sz = 0;
	int i, evt;
	ssize_t ret;

	/*
	 * 	{ u64		nr;
	 * 	  { u64		time_enabled; } && PERF_FORMAT_ENABLED
	 * 	  { u64		time_running; } && PERF_FORMAT_RUNNING
	 * 	  { u64		value;
	 * 	    { u64	id;           } && PERF_FORMAT_ID
	 * 	  }		cntr[nr];
	 * 	} && PERF_FORMAT_GROUP
	 *
	 * we do not use FORMAT_ID in this program
	 */

	for (evt = 0; evt < num; ) {
		int num_evts_to_read;

		if (options->format_group) {
			num_evts_to_read = perf_get_group_nevents(fds, num, evt);
			new_sz = sizeof(uint64_t) * (3 + num_evts_to_read);
		} else {
			num_evts_to_read = 1;
			new_sz = sizeof(uint64_t) * 3;
		}

		if (new_sz > sz) {
			sz = new_sz;
			values = realloc(values, sz);
		}

		if (!values)
			err(1, "cannot allocate memory for values\n");

		ret = read(fds[evt].fd, values, new_sz);
		if (ret != (ssize_t)new_sz) { /* unsigned */
			if (ret == -1)
				err(1, "cannot read values event %s", fds[evt].name);

			/* likely pinned and could not be loaded */
			warnx("could not read event %d, tried to read %zu bytes, but got %zd",
				evt, new_sz, ret);
		}

		/*
		 * propagate to save area
		 */
		for (i = evt; i < (evt + num_evts_to_read); i++) {
			if (options->format_group)
				values[0] = values[3 + (i - evt)];
			/*
			 * scaling because we may be sharing the PMU and
			 * thus may be multiplexed
			 */
			fds[i].values[0] = values[0];
			fds[i].values[1] = values[1];
			fds[i].values[2] = values[2];
		}
		evt += num_evts_to_read;
	}
	if (values)
		free(values);
}

static void
print_counts(perf_event_desc_t *fds, int num, struct options_t *options)
{
	double ratio;
	uint64_t val, delta;
	int i;

	read_groups(fds, num, options);

	for(i=0; i < num; i++) {

		val   = perf_scale(fds[i].values);
		delta = perf_scale_delta(fds[i].values, fds[i].prev_values);
		ratio = perf_scale_ratio(fds[i].values);

		/* separate groups */ 
		/*
		if (perf_is_group_leader(fds, i))
			putchar('\n');

			
        if (options->print)
			printf("%'20"PRIu64" %'20"PRIu64" %s (%.2f%% scaling, ena=%'"PRIu64", run=%'"PRIu64")\n",
				val,
				delta,
				fds[i].name,
				(1.0-ratio)*100.0,
				fds[i].values[1],
				fds[i].values[2]);
		else
			printf("%'20"PRIu64" %s (%.2f%% scaling, ena=%'"PRIu64", run=%'"PRIu64")\n",
				val,
				fds[i].name,
				(1.0-ratio)*100.0,
				fds[i].values[1],
				fds[i].values[2]);
       */ 
        while (__sync_val_compare_and_swap(&options->lock, 0, 1) == 1)
            usleep(10);
        options->lock = 1;
        options->counters[i].val = val;
        options->counters[i].ratio = ratio;
        options->counters[i].delta = delta;
        options->counters[i].auxval1 = fds[i].values[1];
        options->counters[i].auxval2 = fds[i].values[2];
        options->counters[i].seqno++;
        strcpy(options->counters[i].name, fds[i].name);
        options->lock = 0;

		fds[i].prev_values[0] = fds[i].values[0];
		fds[i].prev_values[1] = fds[i].values[1];
		fds[i].prev_values[2] = fds[i].values[2];
	}
}

static void sig_handler(int n)
{
	quit = 1;
}

int
parent(struct options_t *options, pid_t pid)
{
	perf_event_desc_t *fds = NULL;
	int i, num_fds = 0, grp, group_fd;
	int go[2];

	go[0] = go[1] = -1;

	if (pfm_initialize() != PFM_SUCCESS)
		errx(1, "libpfm initialization failed");

	for (grp = 0; grp < options->num_groups; grp++) {
		int ret;
		ret = perf_setup_list_events(options->events[grp], &fds, &num_fds);
		if (ret || !num_fds)
			exit(1);
	}


    /* Set up the counters once before reading
    till PID is alive */
	for(i=0; i < num_fds; i++) {
		int is_group_leader; /* boolean */

		is_group_leader = perf_is_group_leader(fds, i);
		if (is_group_leader) {
			/* this is the group leader */
			group_fd = -1;
		} else {
			group_fd = fds[fds[i].group_leader].fd;
		}

		/*
		 * create leader disabled with enable_on-exec
		 */

		fds[i].hw.read_format = PERF_FORMAT_SCALE;
		/* request timing information necessary for scaling counts */
		if (is_group_leader && options->format_group)
			fds[i].hw.read_format |= PERF_FORMAT_GROUP;

		if (options->inherit)
			fds[i].hw.inherit = 1;

		if (options->pin && is_group_leader)
			fds[i].hw.pinned = 1;
		fds[i].fd = perf_event_open(&fds[i].hw, pid, -1, group_fd, 0);
		if (fds[i].fd == -1) {
			warn("cannot attach event%d %s", i, fds[i].name);
			goto error;
		}
	}

    while(quit == 0) {
        sleep(1);
        print_counts(fds, num_fds, options);
        if (kill(pid, 0) < 0)
            if (errno == ESRCH)
            {
                //printf ("Process %d does not exist \n", pid);
                goto PROCEED;
            }
            /*else
                printf ("Hmmm what the hell happened??? \n");
        else
            printf(" Process exists so just continuing \n");*/
    }
PROCEED:
	for(i=0; i < num_fds; i++)
		close(fds[i].fd);

	perf_free_fds(fds, num_fds);

	/* free libpfm resources cleanly */
	pfm_terminate();

	return 0;
error:
	free(fds);
	pfm_terminate();

	return -1;
}

static void
usage(void)
{
	printf("usage: task [-h] [-i] [-g] [-p] [-P] [-t pid] [-e event1,event2,...] cmd\n"
		"-h\t\tget help\n"
		"-i\t\tinherit across fork\n"
		"-f\t\tuse PERF_FORMAT_GROUP for reading up counts (experimental, not working)\n"
		"-p\t\tprint counts every second\n"
		"-P\t\tpin events\n"
		"-t pid\tmeasure existing pid\n"
		"-e ev,ev\tgroup of events to measure (multiple -e switches are allowed)\n"
		);
}

int
monitor(int argc, char **argv)
{
	int c;
    pid_t pid;
    const int memsize = sizeof(struct options_t);        // file size
    char *memname;

    struct options_t *options;
/*./PerTask -e cycles,instructions,PERF_COUNT_HW_CACHE_ITLB,L2_TRANS,RESOURCE_STALLS:ANY 
            -e cycles,instructions,MEM_LOAD_UOPS_RETIRED:L1_HIT,MEM_LOAD_UOPS_RETIRED:L2_HIT 
            -e cycles,instructions,MEM_LOAD_UOPS_RETIRED:L1_MISS,LLC_MISSES -p -t 83285 */
	setlocale(LC_ALL, "");

	while ((c=getopt(argc, argv,"+he:ifpPt:")) != -1) {
		switch(c) {
			case 't':
                printf(" Here \n");
                memname = optarg;  // file name    
                printf ("path to write to %s \n", memname);

                /* create the shared memory segment as if it was a file */
                memfd = shm_open(memname, O_CREAT | O_RDWR, 0666);
                if (memfd == -1) {
                    printf("prod: Shared memory failed: %s\n", strerror(errno));
                    exit(1);
                }

                /* configure the size of the shared memory segment */
                ftruncate(memfd, memsize);

                /* map the shared memory segment to the address space of the process */
                membase = mmap(0, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
                if (membase == MAP_FAILED) {
                    printf("prod: Map failed: %s\n", strerror(errno));
                    exit(1);
                }
                options = (struct options_t*) membase;
                options->pid = atoi(optarg);

				break;
			case 'h':
				usage();
				exit(0);
			default:
				errx(1, "unknown error");
		}
	}
    options->events[0] = "cycles,instructions,PERF_COUNT_HW_CACHE_ITLB,L2_TRANS,RESOURCE_STALLS:ANY"; // 5
    options->events[1] = "MEM_LOAD_UOPS_RETIRED:L3_MISS,MEM_LOAD_UOPS_RETIRED:L3_HIT"; // 4
    //options->events[1] = "cycles,instructions,MEM_LOAD_UOPS_RETIRED:L1_HIT,MEM_LOAD_UOPS_RETIRED:L2_HIT"; // 4
    options->events[2] = "MEM_LOAD_UOPS_RETIRED:L2_MISS,LLC_MISSES";
	options->events[3] = "MEM_LOAD_UOPS_LLC_MISS_RETIRED:REMOTE_HITM,MEM_LOAD_UOPS_LLC_MISS_RETIRED:REMOTE_DRAM"; // 4
    //options->events[2] = "cycles,instructions,MEM_LOAD_UOPS_RETIRED:L1_MISS,LLC_MISSES"; // 4
    options->num_groups = 4;
    options->inherit = 0;
    options->print = 1;
    options->countercount = 11;
    for (c = 0; c < MAX_COUNTERS; c++) {
        options->counters[c].seqno = 0; }
	
	if (!argv[optind] && !options->pid) {
		errx(1, "you must specify a command to execute or a thread to attach to\n"); }

	signal(SIGINT, sig_handler);

    

    pid = options->pid;
	pid = parent(options, pid);

    /* remove the mapped memory segment from the address space of the process */
    if (munmap(membase, memsize) == -1) {
        printf("prod: Unmap failed: %s\n", strerror(errno));
        exit(1);
    }

    /* close the shared memory segment as if it was a file */
    if (close(memfd) == -1) {
        printf("prod: Close failed: %s\n", strerror(errno));
        exit(1);
    }
    return pid;
}

int main(int argc, char **argv)
{
    monitor(argc, argv);
    return 0;
}
