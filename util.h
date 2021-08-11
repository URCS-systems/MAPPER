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

static inline double timespec_diff(struct timespec start, struct timespec end) {
    struct timespec diff_ts = end;
    if (diff_ts.tv_nsec < start.tv_nsec) {
        diff_ts.tv_sec = diff_ts.tv_sec - start.tv_sec - 1;
        diff_ts.tv_nsec = 1000000000 - (start.tv_nsec - diff_ts.tv_nsec);
    } else {
        diff_ts.tv_sec -= start.tv_sec;
        diff_ts.tv_nsec -= start.tv_nsec;
    }
    return diff_ts.tv_sec + (double) diff_ts.tv_nsec / 1e+9;
}

#if defined(__cplusplus)
};
#endif

#endif
