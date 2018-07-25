#include "util.h"
#include <stdio.h>
#include <stdlib.h>

char *intlist_to_string(const int *list, 
                        size_t length,
                        char *buf,
                        size_t buflen,
                        const char *delim) {
    size_t p = 0;

    for (size_t i=0; i<length; ++i) {
        p += snprintf(&buf[p], buflen - p, "%d%s", 
                list[i], i == length - 1 ? "" : delim);
    }
    
    if (!length)
        snprintf(buf, buflen, "%s", "");

    return buf;
}

void cpuset_to_intlist(const cpu_set_t *set,
                       int num_cpus, 
                       int **listp, 
                       int *list_l) {
    const size_t cpus_sz = CPU_ALLOC_SIZE(num_cpus);
    *listp = (int*) calloc(num_cpus, sizeof **listp);
    *list_l = 0;

    for (int i = 0; i < num_cpus; ++i) {
        if (CPU_ISSET_S(i, cpus_sz, set)) {
            (*listp)[*list_l] = i;
            (*list_l)++;
        }
    }
}

void intlist_to_cpuset(const int *list,
                       size_t length,
                       cpu_set_t **setp,
                       int *num_cpus) {
    if ((*num_cpus = length) == 0)
        return;

    const size_t cpus_sz = CPU_ALLOC_SIZE(*num_cpus);
    *setp = CPU_ALLOC(*num_cpus);

    for (size_t i = 0; i < length; ++i) {
        CPU_SET_S(list[i], cpus_sz, *setp);
    }
}

