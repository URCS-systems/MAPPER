#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#include <time.h>

#define MAX(a,b)    ((a) > (b) ? (a) : (b))

#define MIN(a,b)    ((a) < (b) ? (a) : (b))

#if defined(__cplusplus)
extern "C" {
#endif

char *intlist_to_string(const int *list, 
                        size_t length,
                        char *buf,
                        size_t buflen,
                        const char *delim);

void cpuset_to_intlist(const cpu_set_t *set,
                       int num_cpus, 
                       int **listp, 
                       size_t *list_l);

void intlist_to_cpuset(const int *list,
                       size_t length,
                       cpu_set_t **setp,
                       int max_cpus);

int string_to_intlist(const char *str, 
                      int **value_in, size_t *length_in);

static inline struct timespec timespec_sub(struct timespec ts1, struct timespec ts2) {
    struct timespec diff = {
        .tv_sec = ts1.tv_sec - ts2.tv_sec,
        .tv_nsec = ts1.tv_nsec - ts2.tv_nsec
    };
    if (ts1.tv_nsec < ts2.tv_nsec) {
        diff.tv_sec -= 1;
        diff.tv_nsec = 1000000000 - diff.tv_nsec;
    }
    return diff;
}

static inline struct timespec timespec_add(struct timespec ts1, struct timespec ts2) {
    return (struct timespec) {
        .tv_sec = ts1.tv_sec + ts2.tv_sec + (ts1.tv_nsec + ts2.tv_nsec) / 1000000000,
        .tv_nsec = (ts1.tv_nsec + ts2.tv_nsec) % 1000000000
    };
}

static inline double timespec_to_secs(struct timespec ts)
{
    return ts.tv_sec + (double) ts.tv_nsec / 1e+9;
}

#if defined(__cplusplus)
};
#endif

#endif
