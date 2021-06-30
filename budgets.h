#ifndef BUDGETS_H
#define BUDGETS_H

#include <stddef.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Budgeter function type.
 */
typedef void (*budgeter_t)(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset, 
                           bool bottleneck_unchanged,
                           cpu_set_t *remaining_cpus, const size_t cpus_sz,
                           int per_app_cpu_budget,
                           int *per_app_socket_orders);

/**
 * A budgeting strategy that prefers spreading threads across multiple sockets.
 */
void
budget_spread(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
              bool bottleneck_unchanged,
              cpu_set_t *remaining_cpus, const size_t cpus_sz,
              int per_app_cpu_budget,
              int *per_app_socket_orders);

/**
 * A budgeting strategy that avoids collocating threads on the same core.
 */
void
budget_no_hyperthread(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
                      bool bottleneck_unchanged,
                      cpu_set_t *remaining_cpus, const size_t cpus_sz,
                      int per_app_cpu_budget,
                      int *per_app_socket_orders);

/**
 * The default budgeting strategy, which prefers collocating threads on the same core.
 */
void
budget_default(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
               bool bottleneck_unchanged,
               cpu_set_t *remaining_cpus, const size_t cpus_sz,
               int per_app_cpu_budget,
               int *per_app_socket_orders);

extern budgeter_t budgeter_functions[];

#if defined(__cplusplus)
};
#endif

#endif
