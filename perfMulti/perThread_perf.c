/*Compile with- gcc perf_test.c -o PERF_TEST
 * Run- sudo ./PERF_TEST
 */

#include "perThread_perf.h"
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#define PRINT true

#define MAX_RFVALUES 8  /* this is the maximum number of values we're reading at a time */

uint64_t event_codes[N_EVENTS] = {
    [EVENT_SNP]		    = 0x06d2,
    [EVENT_INSTRUCTIONS]    = 0xc0,
    [EVENT_REMOTE_HITM]     = 0x10d3,    
    
    [EVENT_UNHALTED_CYCLES] = 0x3c,
    [EVENT_LLC_MISSES]      = 0x412e,
    
};


const char *event_names[N_EVENTS] = {
    [EVENT_SNP]                 = "Local snoops",	
    [EVENT_INSTRUCTIONS]        = "instructions",
    [EVENT_REMOTE_HITM]         = "remote-hitm",
    [EVENT_UNHALTED_CYCLES]     = "cycles (unhalted)",
    [EVENT_LLC_MISSES]          = "LLC misses",
        
};

struct perf_group event_groups[] = {
    { .items = { EVENT_SNP, EVENT_INSTRUCTIONS, EVENT_REMOTE_HITM }, .size = 3 },
    { .items = { EVENT_UNHALTED_CYCLES, EVENT_LLC_MISSES }, .size = 2 }
};

#define N_GROUPS (sizeof(event_groups) / sizeof(event_groups[0]))

struct perf_stat *threads = NULL;

static int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd,
                            unsigned long flags)
{
    int ret;
    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
}

void setPerfAttr(struct perf_event_attr *pea, enum perf_event event, int group_fd, int *fd, uint64_t *id,
                 int cpu, pid_t tid)
{

    memset(pea, 0, sizeof *pea); // allocating memory
    pea->type = PERF_TYPE_RAW;
    pea->size = sizeof(struct perf_event_attr);
    pea->config = event_codes[event];
    pea->disabled = 1;  //set to 0 when not using start stop
    // pea[cpu].exclude_kernel=1;
    // pea[cpu].exclude_hv=1;
    pea->read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
    *fd = perf_event_open(pea, tid, cpu, group_fd, 0); // group leader has group id -1
    if (*fd == -1)  //Don't start orstopt read events on this
		*fd = -1;
        // fprintf(stderr, "Error! perf_event_open not set for TID %6d for event %s: %s\n", 
        //        tid, event_names[event], strerror(errno));
    else
        ioctl(*fd, PERF_EVENT_IOC_ID, id); // retrieve identifier for first counter
}

void start_event(int fd)
{
    if (fd != -1) {
        ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
}

void stop_read_counters(const int fds[], size_t num_fds, const uint64_t ids[], uint64_t *valptrs[]) {
    if (num_fds < 1)
        return;

    /* the first fd is the group leader */
    if (fds[0] != -1) {
        struct read_format *rf;
        char buf[offsetof(struct read_format, values) + MAX_RFVALUES * sizeof(rf->values[0])];

        rf = (void *) buf;   /* alias rf as buf */
        ioctl(fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP); //no need to stop since we are not using it
        read(fds[0], buf, sizeof buf);

        for (uint64_t i = 0; i < rf->nr && i < num_fds && i < MAX_RFVALUES; ++i) {
            for (size_t j = 0; j < num_fds; ++j)
                if (rf->values[i].id == ids[j]) {
                    *valptrs[j] = rf->values[i].value;
                    break;
                }
        }
    } else {
        /*
         * File descriptor was -1, hence no monitoring happened.
         * Set these unmonitored event counts to 0.
         */
        for (size_t i = 0; i < num_fds; ++i)
            *valptrs[i] = 0;
    }

    /* close all events */
    for (size_t i = 0; i < num_fds; ++i)
        close(fds[i]);
}

void count_event_perfMultiplex(pid_t tid[], int index_tid)
{
    // Initialize time interval to count
    int millisec = TIME_IN_MILLI_SEC;
    struct timespec tim = { millisec / 1000, (millisec % 1000) * 1000000 };

    threads = calloc(index_tid, sizeof *threads);

    // iterate through all threads
    int i;
    for (i = 0; i < index_tid; i++) {
        // set up performance counters
        for (size_t grp = 0; grp < N_GROUPS; grp++) {
            for (int k = 0; k < event_groups[grp].size; ++k) {
                int evt = event_groups[grp].items[k];
                int group_fd = k == 0 ? -1 : threads[i].fd[event_groups[grp].items[0]];
                setPerfAttr(&threads[i].pea, evt, group_fd, &threads[i].fd[evt], &threads[i].id[evt], -1, tid[i]);
            }
        }
    }
    //start counters

    // duration of count
    //  nanosleep(&tim, NULL);

    //Read two buffers
    size_t grp;
    for (grp = 0; grp < N_GROUPS; grp++) {
        int i; 

        for (i = 0; i < index_tid; i++)
            start_event(threads[i].fd[event_groups[grp].items[0]]);

        // duration of count
        nanosleep(&tim, NULL);

        // stop counters and read counter values
        for (i = 0; i < index_tid; i++) {
            int fds[MAX_EVENT_GROUP_SZ];
            uint64_t ids[MAX_EVENT_GROUP_SZ];
            uint64_t *vps[MAX_EVENT_GROUP_SZ];

            for (int k = 0; k < event_groups[grp].size; ++k) {
                int evt = event_groups[grp].items[k];
                fds[k] = threads[i].fd[evt];
                ids[k] = threads[i].id[evt];
                vps[k] = &threads[i].val[evt];
            }

            stop_read_counters(fds, event_groups[grp].size, ids, vps);
        }
    }
}

void displayTIDEvents(pid_t tid[], int index_tid)
{
    printf("CountEvents Index:%d\n", index_tid);

    int i;
    for (i = 0; i < index_tid; i++) {
        THREADS.tid[i] = tid[i];
        THREADS.index_tid = index_tid;

        int j;
        for (j = 0; j < N_EVENTS; j++)
            THREADS.event[i][j] = (threads[i].val[j]*2 );

        if (PRINT) {
	  printf("\n");
          printf("THREAD: %d UNHALTED_CORE_CYCLE: %"PRIu64"\n",tid[i], (threads[i].val[EVENT_UNHALTED_CYCLES])*2);
          printf("THREAD: %d INSTRUCTION_RETIRED: %"PRIu64"\n",tid[i], (threads[i].val[EVENT_INSTRUCTIONS])*2);
          printf("THREAD: %d REMOTE_HITM: %"PRIu64"\n",tid[i], (threads[i].val[EVENT_REMOTE_HITM])*2);
          printf("THREAD: %d SNP: %"PRIu64"\n",tid[i], (threads[i].val[EVENT_SNP])*2);
	  printf("THREAD: %d LLC MISSES: %"PRIu64"\n",tid[i], (threads[i].val[EVENT_LLC_MISSES])*2);
	  printf("\n");
	  printf("-------------------------------------------------------------------------\n");
           
        }
    } // for close

    free(threads);
    // printf("=================================================================\n");
}

int searchTID(int tid)
{
    int i = 0;
    for (i = 0; i < THREADS.index_tid; i++) {
        if ((int)THREADS.tid[i] == tid) return i;
    }

    return -1;
}

void copyValues(pid_t tid[], int index_tid)
{

    int i;
    for (i = 0; i < index_tid; i++) {
        THREADS.tid[i] = tid[i];
        THREADS.index_tid = index_tid;

        int j;
        for (j = 0; j < N_EVENTS; j++)
            THREADS.event[i][j] = (threads[i].val[j]) * 4;

        /*	//FIND PerfData Instance of this particular TID and then populate
           options_t struct of that THREADS.tid[i]; //TID of the THREAD BEING
           MONITORED THREADS.event[i][0];   //UNHALTED_CORE_CYCLES
                THREADS.event[i][1];    //INSTRUCTIONS_RETIRED
                 THREADS.event[i][2];    //REMOTE_HITM
                THREADS.event[i][3];       //REMOTE_DRAM
                 THREADS.event[i][4];      //LLC_MISSES
                THREADS.event[i][5];       //L2_MISSES
                 THREADS.event[i][6];    //L3_MISSES
                THREADS.event[i][7];      //L3_HITS
            */
    }
}
