#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdlib.h>

#include "cpuinfo.h"
#include "mapper.h"

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
 * Fair share allocation policy
 */
void
scheduler_policy_fair(const int         j,
                      struct appinfo   *apps_sorted[],
                      int               per_app_cpu_budget[],
                      const int         fair_share);

/**
 * Hill climbing allocation policy
 */
void
scheduler_policy_hillclimb(const int        j,
                           struct appinfo  *apps_sorted[],
                           int              per_app_cpu_budget[],
                           const int        fair_share,
                           const int        curr_alloc_len,
                           const size_t     rem_cpus_sz,
                           struct cpuinfo  *cpuinfo,
                           const int        i,
                           enum metric      counter_order[]);

/**
 * Default SAM-MAP allocation policy
 */
void
scheduler_policy_default(const int          j,
                         struct appinfo    *apps_sorted[],
                         int                per_app_cpu_budget[],
                         const int          fair_share,
                         const int          curr_alloc_len,
                         const size_t       rem_cpus_sz,
                         struct cpuinfo    *cpuinfo,
                         const int          i,
                         enum metric        counter_order[]);

#if defined(__cplusplus)
};
#endif

#endif /* SCHEDULER_H */
