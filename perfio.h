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

/**
 * Read performance counters.
 *
 *
 * @param slept_time        (optional) if non-NULL, is filled with the time spent sleeping
 * @param setup_time        (optional) if non-NULL, is filled with the time it takes to setup counters
 * @param read_time         (optional) if non-NULL, is filled with the time it takes to read counters
 */
void perfio_read_counters(pid_t            tid[],
                          int              index_tid,
                          struct timespec *slept_time,
                          struct timespec *setup_time,
                          struct timespec *read_time);

void displayTIDEvents(pid_t tid[], int index_tid);

int searchTID(int tid);

#if defined(__cplusplus)
};
#endif

#endif  /* PERFIO_H */
