#ifndef NUPOCO_SCHEDULER_H
#define NUPOCO_SCHEDULER_H

#include "../cpuinfo.h"
#include "../mapper.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Set the NuPoCo scheduler to the profiling phase.
 */
void nupoco_set_profiling(void);

/**
 * The main procedure for the NuPoCo scheduler.
 */
void
nupoco_allocate(const int                   num_apps,
                struct appinfo             *apps_sorted[],
                const struct cpuinfo *const cpuinfo,
                const size_t                rem_cpus_sz,
                int                        *per_app_socket_orders[],
                cpu_set_t                  *new_cpusets[],
                cpu_set_t                  *remaining_cpus);

#if defined(__cplusplus)
};
#endif

#endif /* NUPOCO_SCHEDULER_H */
