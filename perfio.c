/*Compile with- gcc perf_test.c -o PERF_TEST
 * Run- sudo ./PERF_TEST
 */
//Header file contains description of data structures used and the events
#include "perfio.h"
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#define PRINT false

struct perfThread THREADS;

uint64_t event_codes[N_EVENTS] = {
    [EVENT_SNP]		    = 0x06d2,        //SNOOP HIT and SNOOP HITM for intra-socket communication
    [EVENT_INSTRUCTIONS]    = 0xc0,          //Number of instructions for IPC
    [EVENT_REMOTE_HITM]     = 0x10d3,        //REmote HIT Modified for Inter-socket communication
    
    [EVENT_UNHALTED_CYCLES] = 0x3c,          //Unhalted cycles for IPC
    [EVENT_LLC_MISSES]      = 0x412e,        //Last Level Cache misses for Memory contention
    
};


const char *event_names[N_EVENTS] = {
    [EVENT_SNP]                 = "Local snoops",	
    [EVENT_INSTRUCTIONS]        = "instructions",
    [EVENT_REMOTE_HITM]         = "remote-hitm",
    [EVENT_UNHALTED_CYCLES]     = "cycles (unhalted)",
    [EVENT_LLC_MISSES]          = "LLC misses",
        
};
//Events divided into two groups based on compatibility
struct perf_group event_groups[] = {
    { .items = { EVENT_SNP, EVENT_INSTRUCTIONS, EVENT_REMOTE_HITM }, .size = 3 },
    { .items = { EVENT_UNHALTED_CYCLES, EVENT_LLC_MISSES }, .size = 2 }
};

#define N_GROUPS (sizeof(event_groups) / sizeof(event_groups[0]))
#define SLEEP_TIME_MS 1000 / N_GROUPS

struct perf_stat *threads = NULL;
//perf event open function calls
static int perf_event_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd,
                            unsigned long flags)
{
    int ret;
    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
    return ret;
}

//Set up the perf event attribute and they are part of a group
void setPerfAttr(struct perf_event_attr *pea, enum perf_event event, int group_fd, int *fd, uint64_t *id,
                 int cpu, pid_t tid)
{

    memset(pea, 0, sizeof *pea); // allocating memory
    pea->type = PERF_TYPE_RAW;                             //Specifying event configuration as hex values
    pea->size = sizeof(struct perf_event_attr);
    pea->config = event_codes[event];                      //get event values from enum
    pea->disabled = 1;  //set to 0 when not using start stop
    // pea[cpu].exclude_kernel=1;
    // pea[cpu].exclude_hv=1;
    pea->read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;             //read as group
    *fd = perf_event_open(pea, tid, cpu, group_fd, 0); // group leader has group id -1
    if (*fd == -1)  //Don't start orstopt read events on this
		*fd = -1;
        // fprintf(stderr, "Error! perf_event_open not set for TID %6d for event %s: %s\n", 
        //        tid, event_names[event], strerror(errno));
    else
        ioctl(*fd, PERF_EVENT_IOC_ID, id); // retrieve identifier for first counter
}

//start event monitoring IO controller signal
void start_event(int fd)
{
    if (fd != -1) {
        ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
        ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
}

/**
 * Stop monitoring the events and read their values.
 *
 * @fds = list of perf event file descriptors, with first fd being group leader
 * @num_fds = number of file descriptors
 * @ids = list of corresponding IDs; size is num_fds
 * @valptrs = list of pointers to values to write perf counters into; size is num_fds
 * 
 * Note: each element of @ids is associated with an element of @valptrs, so that
 * a perf event with an ID == @ids[i] means the value will be written into @valptrs[i].
 */
void stop_read_counters(const int fds[], size_t num_fds, const uint64_t ids[], uint64_t *valptrs[]) {
    if (num_fds < 1)
        return;

    /* the first fd is the group leader */
    if (fds[0] != -1) {
        struct read_format *rf;
        char buf[offsetof(struct read_format, values) + MAX_EVENT_GROUP_SZ * sizeof(rf->values[0])];

        rf = (void *) buf;   /* alias rf as buf */
        ioctl(fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP); //no need to stop since we are not using it
        read(fds[0], buf, sizeof buf);

        for (uint64_t i = 0; i < rf->nr && i < num_fds && i < MAX_EVENT_GROUP_SZ; ++i) {
            for (size_t j = 0; j < num_fds; ++j)
                if (rf->values[i].id == ids[j])
                    *valptrs[j] = rf->values[i].value;
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

//master function that orchestrates the entire performance monitoring for threads
void perfio_read_counters(pid_t tid[], int index_tid, struct timespec *remaining_time)
{
    // Initialize time interval to count
    struct timespec sleep_time = { SLEEP_TIME_MS / 1000, (SLEEP_TIME_MS % 1000) * 1000000 };

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

    struct timespec rem = { 0 };
    // duration of count
    //  nanosleep(&tim, NULL);

    //Read two buffers
    size_t grp;
    for (grp = 0; grp < N_GROUPS; grp++) {
        int i; 

        for (i = 0; i < index_tid; i++)
            start_event(threads[i].fd[event_groups[grp].items[0]]);

        // duration of count
        struct timespec rem_group = { 0 };
        nanosleep(&sleep_time, &rem_group);
        rem = (struct timespec) {
            rem.tv_sec + rem_group.tv_sec,
            rem.tv_nsec + rem_group.tv_nsec
        };

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

    if (remaining_time)
        *remaining_time = rem;
}

void displayTIDEvents(pid_t tid[], int index_tid)
{
    // printf("CountEvents Index:%d\n", index_tid);

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
		threads = NULL;
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
