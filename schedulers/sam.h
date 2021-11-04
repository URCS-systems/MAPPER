#ifndef SAM_SCHEDULER_H
#define SAM_SCHEDULER_H

#include <stdlib.h>

#include "../cpuinfo.h"
#include "../mapper.h"

#if defined(__cplusplus)
extern "C" {
#endif


static inline int guess_optimization(const int cpus_per_socket, const int budget, enum metric bottleneck)
{
  int f = random() / (double)RAND_MAX < 0.5 ? -1 : 1;

  if (bottleneck == METRIC_INTER || bottleneck == METRIC_INTRA) {
    f = random() / (double)RAND_MAX < 0.8 ? -1 : 1;
    if (budget % cpus_per_socket) {
      int step = cpus_per_socket - (budget % cpus_per_socket);
      if (f < 0) {
        step = -(budget % cpus_per_socket);
        if (budget < cpus_per_socket)
          step = -SAM_PERF_STEP;
      }
      return step;
    }
    if (f == -1 && budget == cpus_per_socket)
      return -SAM_PERF_STEP;
    return f * cpus_per_socket;
  }

  return f * SAM_PERF_STEP;
}

/**
 * SAM-MAP fair share variant
 */
void
sam_policy_fair(const int         j,
                struct appinfo   *apps_sorted[],
                int               per_app_cpu_budget[],
                const int         fair_share);

/**
 * SAM-MAP hill climbing variant
 */
void
sam_policy_hillclimb(const int              j,
                     struct appinfo        *apps_sorted[],
                     int                    per_app_cpu_budget[],
                     const int              fair_share,
                     const int              curr_alloc_len,
                     const size_t           rem_cpus_sz,
                     const struct cpuinfo  *cpuinfo,
                     const int              i,
                     const enum metric      counter_order[]);

/**
 * Default SAM-MAP allocation policy
 */
void
sam_policy_default(const int                 j,
                   struct appinfo           *apps_sorted[],
                   int                       per_app_cpu_budget[],
                   const int                 fair_share,
                   const int                 curr_alloc_len,
                   const size_t              rem_cpus_sz,
                   const struct cpuinfo     *cpuinfo,
                   const int                 i,
                   const enum metric         counter_order[]);

/**
 * The main procedure for the SAM-MAP scheduler.
 */
void
sam_allocate(const int                   num_apps,
             struct appinfo             *apps_sorted[],
             const int                   range_ends[N_METRICS],
             const struct cpuinfo *const cpuinfo,
             const size_t                rem_cpus_sz,
             int                         initial_remaining_cpus,
             int                         fair_share,
             const int                   num_counter_orders,
             const enum metric           counter_order[],
             int                        *per_app_socket_orders[],
             cpu_set_t                  *new_cpusets[],
             cpu_set_t                  *remaining_cpus);

#if defined(__cplusplus)
};
#endif

#endif /* SAM_SCHEDULER_H */
