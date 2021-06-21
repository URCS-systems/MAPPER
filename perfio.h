#ifndef PERFIO_H
#define PERFIO_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* See feature_test_macros(7) */
#endif
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <asm/unistd.h>
#include <error.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stddef.h>

/*
#define EVENT1  0x3c    //UNHALTED_CORE_CYCLE
#define EVENT2 0xc0     //INSTRUCTION_RETIRED
#define EVENT3 0x82d0   //STORES
#define EVENT4 0x20d3  //REMOTE_FWD
#define EVENT5 0x10d3  //REMOTE_HITM
#define EVENT6 0x412e  //LLC MISSES
#define EVENT7 0x13c  //UNHALTED_REFERENCE_CYCLES
#define EVENT8 0x4f2e  //LLC_REFERENCES
*/

enum perf_event {
    EVENT_SNP,	
    EVENT_INSTRUCTIONS,
    EVENT_REMOTE_HITM,
    EVENT_UNHALTED_CYCLES,
    EVENT_LLC_MISSES,
    N_EVENTS,
};

#define TIME_IN_MILLI_SEC                                                                          \
  500                // total measurement time per iteration is 2 times
                       // TIME_IN_MILLI_SEC(example 500 millisecond *2 = 1 second)
#define NUM_THREAD 500000 // Total Threads being monitored
#define ITER 1         // Number of iterations

// Data structure to collect information per TID performance counters
struct perfThread {
    pid_t tid[NUM_THREAD];
    int index_tid;
    uint64_t event[NUM_THREAD][N_EVENTS];
};
extern struct perfThread THREADS;

// Data structure for reading counters
struct read_format {
    uint64_t nr;
    struct {
        uint64_t value;
        uint64_t id;
    } values[];
};

#define MAX_EVENT_GROUP_SZ 10

struct perf_group {
    enum perf_event items[MAX_EVENT_GROUP_SZ];
    int size;
};

struct perf_stat {
    // values of event count
    uint64_t val[N_EVENTS];

    struct perf_event_attr pea; // perf_event attribute

    // file descriptor returned for each perf_event_open call
    int fd[N_EVENTS];

    // id of each event
    uint64_t id[N_EVENTS];
};

#if defined(__cplusplus)
extern "C" {
#endif

extern const char *event_names[];
extern uint64_t event_codes[];
extern struct perf_group event_groups[];
extern struct perf_stat *threads;

void setPerfAttr(struct perf_event_attr *pea, enum perf_event event, int group_fd, int *fd, uint64_t *id,
                 int cpu, pid_t tid);

void start_event(int fd);

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
void stop_read_counters(const int fds[], size_t num_fds, const uint64_t ids[], uint64_t *valptrs[]);

void count_event_perfMultiplex(pid_t tid[], int index_tid);

void displayTIDEvents(pid_t tid[], int index_tid);

int searchTID(int tid);

#if defined(__cplusplus)
};
#endif

#endif  /* PERFIO_H */
