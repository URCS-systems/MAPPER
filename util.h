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

double timespec_diff(const struct timespec *start, 
                     const struct timespec *end);

#if defined(__cplusplus)
};
#endif

#endif
