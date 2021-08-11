#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
                       size_t *list_l) {
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
                       int max_cpus) {
    const size_t cpus_sz = CPU_ALLOC_SIZE(max_cpus);
    *setp = CPU_ALLOC(max_cpus);
    CPU_ZERO_S(cpus_sz, *setp);

    for (size_t i = 0; i < length; ++i) {
        if (list[i] < max_cpus)
            CPU_SET_S(list[i], cpus_sz, *setp);
    }
}

int string_to_intlist(const char *str, int **value_in, size_t *length_in) {
    size_t buflen = 2;
    size_t length = 0;
    char *p = NULL;
    char *token = NULL;
    const char *delim = NULL;
    char *buf = strdup(str);

    if (!(*value_in = (int*) realloc(*value_in, buflen * sizeof(**value_in))))
        goto error;

    /*
     * Some cgroup parameters are newline-separated lists.
     * Examples are /sys/fs/cgroup/cpuset/<cg>/tasks
     * 12994
     * 13094
     * 13093
     * 12234
     *
     * Others are comma-separated lists.
     * Examples are /sys/fs/cgroup/cpuset/<cg>/cpuset.cpus
     * In addition, parts of these lists may be abbreviated
     * with hyphens:
     * 0-3,5-9,13-15
     *
     * And some may be space-separated lists.
     */

    if (strchr(buf, ',') != NULL)
        delim = ",";
    else if (strchr(buf, ' ') != NULL)
        delim = " ";
    else
        delim = "\n";

    token = strtok_r(buf, delim, &p);
    do {
        if (length >= buflen) {
            buflen *= 2;
            if (!(*value_in = (int*) realloc(*value_in, buflen * sizeof(**value_in))))
                goto error;
        }
        int min, max;
        if (sscanf(token, "%d-%d", &min, &max) == 2) {
            for (int v=min; v<=max; ++v) {
                (*value_in)[length++] = v;
                if (length >= buflen) {
                    buflen *= 2;
                    if (!(*value_in = (int*) realloc(*value_in, buflen * sizeof(**value_in))))
                        goto error;
                }
            }
        } else {
            errno = 0;
            long v = strtol(token, NULL, 10);
            if (errno == 0)
                (*value_in)[length++] = v;
        }
    } while ((token = strtok_r(NULL, delim, &p)));

    free(buf);
    *value_in = realloc(*value_in, length * sizeof(**value_in));
    *length_in = length;
    return 0;

error:
    free(buf);
    *value_in = realloc(*value_in, 0);
    *length_in = 0;
    return -1;
}
