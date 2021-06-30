#define _GNU_SOURCE
#include "sam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../budgets.h"
#include "../util.h"

static int compare_ints_mapped(const void *arg1, const void *arg2, void *ptr)
{
  const int *map = (const int *)ptr;
  return map[*(const int *)arg1] > map[*(const int *)arg2];
}

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
             cpu_set_t                  *remaining_cpus)
{
    int *per_app_cpu_budget = calloc(num_apps, sizeof per_app_cpu_budget[0]);
    int *needs_more = calloc(num_apps, sizeof needs_more[0]);

    /*
     * Each application computes its ideal budget.
     */
    for (int i = 0; i < N_METRICS; ++i) {
        for (int j = range_ends[i]; j < range_ends[i + 1]; ++j) {
            const int curr_alloc_len = CPU_COUNT_S(rem_cpus_sz, apps_sorted[j]->cpuset[0]);

            //per_app_cpu_budget[j] = MAX((int) apps_sorted[j]->bottleneck[METRIC_ACTIVE], SAM_MIN_CONTEXTS);
            initial_remaining_cpus += curr_alloc_len;
            per_app_cpu_budget[j] = curr_alloc_len;
#if defined(FAIR)
            sam_policy_fair(j, apps_sorted, per_app_cpu_budget, fair_share);
#elif defined(HILL_CLIMBING)
            sam_policy_hillclimb(j, apps_sorted, per_app_cpu_budget,
                    fair_share, curr_alloc_len, rem_cpus_sz, cpuinfo, i,
                    counter_order);
#else
            sam_policy_default(j, apps_sorted, per_app_cpu_budget,
                    fair_share, curr_alloc_len, rem_cpus_sz, cpuinfo, i,
                    counter_order);
#endif
            /* this really shouldn't be necessary, but it is for some reason
             * I can't explain at the moment
             */
            per_app_cpu_budget[j] = MAX(MIN(per_app_cpu_budget[j], cpuinfo->total_cpus), SAM_MIN_CONTEXTS);
            printf("[APP %6d] requiring %d / %d remaining CPUs\n", apps_sorted[j]->pid, per_app_cpu_budget[j],
                    initial_remaining_cpus);
            printf("[APP %6d] current allocation is %d\n", apps_sorted[j]->pid, curr_alloc_len);
            int diff = initial_remaining_cpus - per_app_cpu_budget[j];
            needs_more[j] = MAX(-diff, 0);
            initial_remaining_cpus = MAX(diff, 0);
            per_app_cpu_budget[j] -= needs_more[j];
        }
    }

    /*
     * Now we adjust the budgets to fit the resources.
     */
    for (int i = 0; i < N_METRICS; ++i) {
        for (int j = range_ends[i]; j < range_ends[i + 1]; ++j) {
            /*
             * Make sure we have enough CPUs to give the budget.
             */
            if (initial_remaining_cpus > 0 && needs_more[j] > 0) {
                int added = MIN(needs_more[j], initial_remaining_cpus);
                initial_remaining_cpus -= added;
                needs_more[j] -= added;
                per_app_cpu_budget[j] += added;
                printf("[APP %6d] took %d from remaining CPUs\n", apps_sorted[j]->pid, added);
            }

            if (needs_more[j] > 0) {
                struct appinfo **candidates = calloc(num_apps, sizeof *candidates);
                int *candidates_map = calloc(num_apps, sizeof *candidates_map);
                int num_candidates = 0;
                int *spare_cores = calloc(num_apps, sizeof *spare_cores);
                struct appinfo **spare_candidates = calloc(num_apps, sizeof *spare_candidates);
                int *spare_candidates_map = calloc(num_apps, sizeof *spare_candidates_map);
                int num_spare_candidates = 0;

                printf("[APP %6d] requests %d more hardware contexts\n", apps_sorted[j]->pid, needs_more[j]);

                /*
                 * Find the least efficient application to steal CPUs from.
                 */
                for (int l = 0; l < num_apps; ++l) {
                    if (l == j)
                        continue;

                    int curr_alloc_len_l = CPU_COUNT_S(rem_cpus_sz, apps_sorted[l]->cpuset[0]);
                    uint64_t curr_perf = apps_sorted[l]->perf_history[curr_alloc_len_l][0];
                    uint64_t best_perf = apps_sorted[l]->perf_history[apps_sorted[l]->curr_fair_share][0];

                    if (curr_perf > SAM_MIN_QOS * best_perf) {
                        uint64_t extra_perf = curr_perf - SAM_MIN_QOS * best_perf;
                        spare_cores[l] = extra_perf / (double)curr_perf * curr_alloc_len_l;
                    }

                    if (apps_sorted[l]->times_allocated > 0) {
                        if (spare_cores[l] > 0) {
                            spare_candidates[num_spare_candidates] = apps_sorted[l];
                            spare_candidates_map[apps_sorted[l]->appno] = l;
                            num_spare_candidates++;
                        }
                        candidates[num_candidates] = apps_sorted[l];
                        candidates_map[apps_sorted[l]->appno] = l;
                        num_candidates++;
                    }
                }

                /*
                 * It's unlikely for us to have less than the fair share available
                 * without there being at least one other application.
                 * The one case is when the other applications are new.
                 */
                if (num_candidates + num_spare_candidates > 0) {
                    int *amt_taken = calloc(num_apps, sizeof *amt_taken);

                    /*
                     * Sort by efficiency.
                     */
                    int met = EXTRA_METRIC_IpCOREpS;
                    qsort_r(candidates, num_candidates, sizeof *candidates, &compare_apps_by_extra_metric_desc, (void *)&met);
                    qsort_r(spare_candidates, num_spare_candidates, sizeof *spare_candidates,
                            &compare_apps_by_extra_metric_desc, (void *)&met);

                    /*
                     * Start by taking away contexts from the least efficient applications.
                     */
                    for (int l = num_spare_candidates - 1; l >= 0 && needs_more[j] > 0; --l) {
                        int m = spare_candidates_map[spare_candidates[l]->appno];

                        for (int n = 0; n < spare_cores[m] && per_app_cpu_budget[m] > SAM_MIN_CONTEXTS && needs_more[j] > 0;
                                ++n) {
                            per_app_cpu_budget[m]--;
                            per_app_cpu_budget[j]++;
                            needs_more[j]--;
                            amt_taken[m]++;
                        }
                    }

                    /* 
                     * If there were no candidates with spares, take from other applications,
                     * but only if we really need to.
                     */
                    if (per_app_cpu_budget[j] < SAM_MIN_CONTEXTS || apps_sorted[j]->times_allocated < 1) {
                        int old_cpu_budget_j;
                        do {
                            old_cpu_budget_j = per_app_cpu_budget[j];
                            for (int l = num_candidates - 1; l >= 0 && needs_more[j] > 0; --l) {
                                int m = candidates_map[candidates[l]->appno];

                                if (per_app_cpu_budget[m] > SAM_MIN_CONTEXTS) {
                                    per_app_cpu_budget[m]--;
                                    per_app_cpu_budget[j]++;
                                    needs_more[j]--;
                                    amt_taken[m]++;
                                }
                            }
                        } while (old_cpu_budget_j < per_app_cpu_budget[j]);
                    }

                    for (int l = 0; l < num_apps; ++l) {
                        if (amt_taken[l] > 0)
                            printf("[APP %6d] took %d contexts from APP %6d\n", apps_sorted[j]->pid, amt_taken[l],
                                    apps_sorted[l]->pid);
                    }

                    free(amt_taken);
                }

                if (needs_more[j] > 0)
                    printf("[APP %6d] could not find %d extra contexts\n", apps_sorted[j]->pid, needs_more[j]);

                if (per_app_cpu_budget[j] < SAM_MIN_CONTEXTS) {
                    fprintf(stderr, "%s:%d: APP %6d: per_app_cpu_budget[%d] (%d) < %d (SAM_MIN_CONTEXTS) !\n", __FILE__,
                            __LINE__, apps_sorted[j]->pid, j, per_app_cpu_budget[j], SAM_MIN_CONTEXTS);
                    abort();
                }

                free(spare_cores);
                free(candidates);
                free(candidates_map);
                free(spare_candidates);
                free(spare_candidates_map);
            }

            if (per_app_cpu_budget[j] < SAM_MIN_CONTEXTS) {
                fprintf(stderr, "%s:%d: APP %6d: per_app_cpu_budget[%d] (%d) < %d (SAM_MIN_CONTEXTS) !\n", __FILE__,
                        __LINE__, apps_sorted[j]->pid, j, per_app_cpu_budget[j], SAM_MIN_CONTEXTS);
                abort();
            }

            /*
             * Here we compute the precedence for locating threads in a socket.
             * If an application is already in this socket, the precedence is increased.
             * If another application is in this socket, the precedence is reduced.
             */
            // for (int k = j + 1; k < num_apps; ++k) {
            for (int k = range_ends[i]; k < range_ends[i + 1]; ++k) {
                for (int s = 0; s < cpuinfo->num_sockets && j != k; ++s) {
                    for (int c = 0; c < cpuinfo->sockets[s].num_cpus; ++c) {
                        struct cpu hw = cpuinfo->sockets[s].cpus[c];

                        if (CPU_ISSET_S(hw.tnumber, rem_cpus_sz, apps_sorted[k]->cpuset[0])) {
                            per_app_socket_orders[j][s]++;
                        }
                    }
                }
            }

            int temp[cpuinfo->num_sockets];
            int socket = -1;

            for (int s = 0; s < cpuinfo->num_sockets; ++s)
                for (int c = 0; c < cpuinfo->sockets[s].num_cpus; ++c) {
                    struct cpu hw = cpuinfo->sockets[s].cpus[c];

                    if (CPU_ISSET_S(hw.tnumber, rem_cpus_sz, apps_sorted[j]->cpuset[0])) {
                        per_app_socket_orders[j][s]--;
                        if (socket == -1)
                            socket = s;
                    }
                }

            //if (socket != -1)
            //    per_app_socket_orders[j][socket] = 0;

            for (int s = 0; s < cpuinfo->num_sockets; ++s) {
                temp[s] = s;
            }

            qsort_r(temp, cpuinfo->num_sockets, sizeof temp[0], &compare_ints_mapped, per_app_socket_orders[j]);

            memcpy(per_app_socket_orders[j], temp, cpuinfo->num_sockets * sizeof *per_app_socket_orders[j]);

            printf("[APP %6d] score:  ", apps_sorted[j]->pid);
            for (int s = 0; s < cpuinfo->num_sockets; ++s) {
                printf(" %d ", per_app_socket_orders[j][s]);
            }
            printf("\n");
        }
    }

    /*
     * Compute the budgets.
     */
    for (int i = 0; i < N_METRICS; ++i) {
        for (int j = range_ends[i]; j < range_ends[i + 1]; ++j) {
            cpu_set_t *new_cpuset;

            new_cpuset = CPU_ALLOC(cpuinfo->total_cpus);
            CPU_ZERO_S(rem_cpus_sz, new_cpuset);

            if (i < num_counter_orders) {
                int met = counter_order[i];

                /*
                 * compute the CPU budget for this application, given its bottleneck
                 * [met]
                 */
                (*budgeter_functions[met])(apps_sorted[j]->cpuset[0], new_cpuset,
                        apps_sorted[j]->prev_bottleneck == apps_sorted[j]->curr_bottleneck,
                        remaining_cpus, rem_cpus_sz,
                        per_app_cpu_budget[j], per_app_socket_orders[j]);
            } else {
                budget_default(apps_sorted[j]->cpuset[0], new_cpuset,
                        apps_sorted[j]->prev_bottleneck == apps_sorted[j]->curr_bottleneck,
                        remaining_cpus, rem_cpus_sz, per_app_cpu_budget[j],
                        per_app_socket_orders[j]);
            }

            /* subtract allocated cpus from remaining cpus,
             * [new_cpuset] is already a subset of [remaining_cpus] */
            CPU_XOR_S(rem_cpus_sz, remaining_cpus, remaining_cpus, new_cpuset);
            per_app_cpu_budget[j] = CPU_COUNT_S(rem_cpus_sz, new_cpuset);
            new_cpusets[j] = new_cpuset;
        }
    }

    free(per_app_cpu_budget);
    free(needs_more);
}
