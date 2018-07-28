#ifndef BUDGETS_H
#define BUDGETS_H

#include <stddef.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Budgeter function type.
 */
typedef void (*budgeter_t)(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset, 
                           int *new_cpu_orders,
                           cpu_set_t *remaining_cpus, const size_t cpus_sz,
                           int per_app_cpu_budget);

void
budget_default(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
               int *new_cpu_orders,
               cpu_set_t *remaining_cpus, const size_t cpus_sz,
               int per_app_cpu_budget);

extern budgeter_t budgeter_functions[];

#if defined(__cplusplus)
};
#endif

#endif
