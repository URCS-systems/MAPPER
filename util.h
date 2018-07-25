#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

#define MAX(a,b)    ((a) > (b) ? (a) : (b))

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
                       int *list_l);

void intlist_to_cpuset(const int *list,
                       size_t length,
                       cpu_set_t **setp,
                       int *num_cpus);

#if defined(__cplusplus)
};
#endif

#endif
