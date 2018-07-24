#ifndef BUDGETS_H
#define BUDGETS_H

#include <stddef.h>
#include <sched.h>

/*
 * Budgeter function type.
 */
typedef void (*budgeter_t)(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset, 
                           cpu_set_t *remaining_cpus, const size_t cpus_sz,
                           int per_app_cpu_budget);

void
budget_default(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
               cpu_set_t *remaining_cpus, const size_t cpus_sz,
               int per_app_cpu_budget);

void
budget_collocate(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
               cpu_set_t *remaining_cpus, const size_t cpus_sz,
               int per_app_cpu_budget);

void
budget_spread(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
               cpu_set_t *remaining_cpus, const size_t cpus_sz,
               int per_app_cpu_budget);

void
budget_no_hyperthread(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
               cpu_set_t *remaining_cpus, const size_t cpus_sz,
               int per_app_cpu_budget);


// extern budgeter_t budgeter_functions[];

#endif
