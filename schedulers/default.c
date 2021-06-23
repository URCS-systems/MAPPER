#define _GNU_SOURCE
#include "../scheduler.h"
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <stdlib.h>

#include "../util.h"

static inline int determine_step_size(const int cpus_per_socket, enum metric bottleneck, int curr_alloc, int dir)
{
  if (bottleneck == METRIC_INTER || bottleneck == METRIC_INTRA) {
    if (curr_alloc <= cpus_per_socket)
      return SAM_PERF_STEP;
    else {
      if (dir == 1)
        return cpus_per_socket - (curr_alloc % cpus_per_socket);
      else {
        if (curr_alloc % cpus_per_socket)
          return (curr_alloc % cpus_per_socket);
        else
          return cpus_per_socket;
      }
    }
  }
  return SAM_PERF_STEP;
}

void
scheduler_policy_default(const int          j,
                         struct appinfo    *apps_sorted[],
                         int                per_app_cpu_budget[],
                         const int          fair_share,
                         const int          curr_alloc_len,
                         const size_t       rem_cpus_sz,
                         struct cpuinfo    *cpuinfo,
                         const int          i,
                         enum metric        counter_order[])
{
    const int cpus_per_socket = cpuinfo->sockets[0].num_cpus;

    /*
     * If this app has already been given an allocation, then we can compute history
     * and excess cores.
     */
    if (apps_sorted[j]->times_allocated > SAM_INITIAL_ALLOCS) {
        /* compute performance history */
        uint64_t history[2];
        memcpy(history, apps_sorted[j]->perf_history[curr_alloc_len], sizeof history);
        history[1]++;
        history[0] = apps_sorted[j]->extra_metric[EXTRA_METRIC_IPS] * (1 / (double)history[1]) +
            history[0] * ((history[1] - 1) / (double)history[1]);

        /*
         * Change application's fair share count if the creation of new applications
         * changes the fair share.
         */
        if (apps_sorted[j]->curr_fair_share != fair_share && apps_sorted[j]->perf_history[fair_share] != 0)
            apps_sorted[j]->curr_fair_share = fair_share;

        uint64_t curr_perf = history[0];

        if (apps_sorted[j]->times_allocated > 1) {
            /*
             * Compare current performance with previous performance, if this application
             * has at least two items in history.
             */
            int prev_alloc_len = CPU_COUNT_S(rem_cpus_sz, apps_sorted[j]->cpuset[1]);
            uint64_t prev_perf = apps_sorted[j]->perf_history[prev_alloc_len][0];

            /*
             * Original decision making:
             * Change requested resources.
             */
            if (curr_perf > prev_perf && (curr_perf - prev_perf) / (double)prev_perf >= SAM_PERF_THRESH &&
                    apps_sorted[j]->exploring && (prev_alloc_len != curr_alloc_len)) {
                /* Keep going in the same direction. */
                printf("[APP %6d] continuing in same direction \n", apps_sorted[j]->pid);
                if (prev_alloc_len < curr_alloc_len)
                    per_app_cpu_budget[j] =
                        MIN(per_app_cpu_budget[j] + determine_step_size(cpus_per_socket, counter_order[i], curr_alloc_len, 1),
                                cpuinfo->total_cpus);
                else
                    per_app_cpu_budget[j] = MAX(
                            per_app_cpu_budget[j] - determine_step_size(cpus_per_socket, counter_order[i], curr_alloc_len, -1), SAM_MIN_CONTEXTS);
                if (prev_alloc_len == 0 && per_app_cpu_budget[j] == cpuinfo->total_cpus)
                    apps_sorted[j]->exploring = false;
            } else {
                if (curr_perf < prev_perf && (prev_perf - curr_perf) / (double)prev_perf >= SAM_PERF_THRESH &&
                        (prev_alloc_len != curr_alloc_len)) {
                    if (apps_sorted[j]->exploring) {
                        /*
                         * Revert to previous count if performance reduction was great enough.
                         */
                        per_app_cpu_budget[j] = prev_alloc_len;
                    } else {
                        int guess = per_app_cpu_budget[j] + guess_optimization(cpus_per_socket, per_app_cpu_budget[j], counter_order[i]);
                        guess = MAX(MIN(guess, cpuinfo->total_cpus), SAM_MIN_CONTEXTS);
                        apps_sorted[j]->exploring = true;
                        per_app_cpu_budget[j] = guess;
                    }
                    printf("[APP %6d] exploring %d -> %d\n", apps_sorted[j]->pid, curr_alloc_len, per_app_cpu_budget[j]);
                } else {
                    apps_sorted[j]->exploring = false;
                    printf("[APP %6d] exploring no more \n", apps_sorted[j]->pid);
                    if (random() / (double)RAND_MAX <= SAM_DISTURB_PROB) {
                        int guess = per_app_cpu_budget[j] + guess_optimization(cpus_per_socket, per_app_cpu_budget[j], counter_order[i]);
                        guess = MAX(MIN(guess, cpuinfo->total_cpus), SAM_MIN_CONTEXTS);
                        apps_sorted[j]->exploring = true;
                        per_app_cpu_budget[j] = guess;
                        printf("[APP %6d] random disturbance: %d -> %d\n", apps_sorted[j]->pid, curr_alloc_len,
                                per_app_cpu_budget[j]);
                    }
                }
            }

            /* save performance history */
            memcpy(apps_sorted[j]->perf_history[curr_alloc_len], history,
                    sizeof apps_sorted[j]->perf_history[curr_alloc_len]);
        } else if (!apps_sorted[j]->exploring && random() / (double)RAND_MAX <= SAM_DISTURB_PROB) {
            /*
             * Introduce random disturbances.
             */
            int guess = per_app_cpu_budget[j] + guess_optimization(cpus_per_socket, per_app_cpu_budget[j], counter_order[i]);
            guess = MAX(MIN(guess, cpuinfo->total_cpus), SAM_MIN_CONTEXTS);
            apps_sorted[j]->exploring = true;
            per_app_cpu_budget[j] = guess;
            printf("[APP %6d] random disturbance: %d -> %d\n", apps_sorted[j]->pid, curr_alloc_len,
                    per_app_cpu_budget[j]);
        }
    } else {
        /*
         * If this app has never been given an allocation, the first allocation we should 
         * give it is the fair share.
         */
        per_app_cpu_budget[j] = fair_share;
        printf("[APP %6d] setting fair share \n", apps_sorted[j]->pid);
    }
}
