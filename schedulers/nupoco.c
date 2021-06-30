#define _GNU_SOURCE
#include "nupoco.h"

#include "../budgets.h"
#include "../util.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sched.h>

// --- NuPoCo queueing model -- taken from rte/src/perf_model.h and rte/src/heuristics_scheduler.cpp

static inline double
prob_nothing_in_the_system(int num_customers, double rho)
{
    double ret = 1.0;

    for (int k = 1; k <= num_customers; k++) {
        double each_prob = 1.0;
        int mult = num_customers;

        for (int j = 0; j < k; j++) {
            each_prob = each_prob * (double)mult;
            mult = mult - 1;
        }

        each_prob = each_prob * pow(rho, k);
        ret = ret + each_prob;
    }

    return (1.0 / ret);
}

static inline double
mm1nn_service_time(double service_rate,
    double arrival_rate_from_one_customer,
    int num_customers)
{
    double rho = arrival_rate_from_one_customer / service_rate;
    double prob_something_in_the_system =
        1.0 - prob_nothing_in_the_system(num_customers, rho);

    return (1.0 / service_rate) *
        (((double)num_customers / prob_something_in_the_system) - (1.0 / rho));
}

static inline double
mm1nn_server_utilization(
    double service_rate,
    double arrival_rate_from_one_customer,
    double num_customers)
{
    double rho = arrival_rate_from_one_customer / service_rate;
    double prob_something_in_the_system =
        1.0 - prob_nothing_in_the_system(num_customers, rho);

    return prob_something_in_the_system;
    //return (1.0 / service_rate) *
    //    (((double)num_customers / prob_something_in_the_system) - (1.0 / rho));
}

static double
compute_mct_utilization(
        int total_nodes,      /* total cpu-nodes */
        double dram_req_rate, /* per-node */
        double mct_delay,
        int num_memories)     /* total memory-nodes */
{
    double mct_utilization = mm1nn_server_utilization(
        1.0 / mct_delay,
        dram_req_rate / num_memories,
        total_nodes);

    return mct_utilization;
}

// static double
// compute_execution_cycles_from_two_level_mm1nn_model(
//         int nodes,
//         int cores_per_node,
//         double useful_work_cycles,
//         double mct_delay,
//         double bus_delay,
//         double llc_miss_rate,
//         double dram_req_rate,
//         double num_memories)
// {
//     /* compiler's register allocation technique will optimize this */
//     double each_work_cycles = useful_work_cycles / (double)(nodes * cores_per_node);
//     double each_llc_misses = each_work_cycles * llc_miss_rate;
//     double mct_latency = mm1nn_service_time(
//             1.0 / mct_delay,
//             dram_req_rate * cores_per_node * nodes / num_memories,
//             nodes);
//     double memory_access_cycles = mm1nn_service_time(
//             1.0 / (mct_latency + bus_delay),
//             dram_req_rate / num_memories,
//             cores_per_node);
// 
//     double memory_contention_cycles = memory_access_cycles * each_llc_misses;
//     double total_cycles = each_work_cycles + memory_contention_cycles;
// 
//     return total_cycles;
// }
// 
// static inline double 
// compute_execution_cycles_from_two_level_mm1nn_model_colocated(
//         int my_nodes,
//         int total_nodes, // competing nodes
//         int cores_per_node,
//         double useful_work_cycles,
//         double mct_delay,
//         double bus_delay,
//         double llc_miss_rate,
//         double dram_req_rate,
//         double dram_req_rate_avg, // average dram req rate among colocated workloads
//         double num_memories)
// {
//     /* compiler's register allocation technique will optimize this */
//     double each_work_cycles = useful_work_cycles / (double)(my_nodes * cores_per_node);
//     double each_llc_misses = each_work_cycles * llc_miss_rate;
//     double mct_latency = mm1nn_service_time(
//             1.0 / mct_delay,
//             dram_req_rate_avg * cores_per_node * total_nodes / num_memories,
//             total_nodes);
//     double memory_access_cycles = mm1nn_service_time(
//             1.0 / (mct_latency + bus_delay),
//             dram_req_rate / num_memories,
//             cores_per_node);
// 
//     double memory_contention_cycles = memory_access_cycles * each_llc_misses;
//     double total_cycles = each_work_cycles + memory_contention_cycles;
// 
//     return total_cycles;
// }

static inline double
compute_cpu_utilization(
        int my_nodes,
        int total_nodes, // competing nodes
        int cores_per_node,
        double useful_work_cycles,
        double mct_delay,
        double bus_delay,
        double llc_miss_rate,
        double dram_req_rate,
        double dram_req_rate_avg, // average dram req rate among colocated workloads
        double num_memories)
{
    /* compiler's register allocation technique will optimize this */
    double each_work_cycles = useful_work_cycles / (double)(my_nodes * cores_per_node);
    double each_llc_misses = each_work_cycles * llc_miss_rate;
    double mct_latency = mm1nn_service_time(
            1.0 / mct_delay,
            dram_req_rate_avg * cores_per_node * total_nodes / num_memories,
            total_nodes);
    double memory_access_cycles = mm1nn_service_time(
            1.0 / (mct_latency + bus_delay),
            dram_req_rate / num_memories,
            cores_per_node);

    double memory_contention_cycles = memory_access_cycles * each_llc_misses;
    double total_cycles = each_work_cycles + memory_contention_cycles;

    return each_work_cycles / total_cycles;
}
// --- end of NuPoCo queueing model

/**
 * TODO
 */
static bool app_is_parallel(const struct appinfo *app)
{
    (void) app;
    return true;
}

enum nupoco_phase {
    PROFILING_RUN,
    GREEDY_ALLOCATION,
    ADAPTIVE_ALLOCATION
};

static enum nupoco_phase scheduling_phase = PROFILING_RUN;

void nupoco_set_profiling(void) {
    scheduling_phase = PROFILING_RUN;
}

/**
 * The memory controller delay.
 *
 * Hardcoded machine-specific value. Must be determined from an offline
 * benchmark.
 */
const double mct_delay = 1.0;

/**
 * The bus delay.
 *
 * Hardcoded machine-specific value. Must be determined from an offline
 * benchmark.
 */
const double bus_delay = 1.0;

/**
 * Work cycles.
 *
 * Hardcoded machine-specific value. Must be determined from an offline
 * benchmark.
 */
const double work_cycles = 1.0;

struct socket_llcinfo {
    uint64_t total_misses;
    uint64_t min_llc_misses;
    uint64_t max_llc_misses;
    int appid_min_llc_misses;
    int cpuid_min_llc_misses;       // CPU associated with the app with minimum LLC misses in the socket
    int appid_max_llc_misses;
    int cpuid_max_llc_misses;       // CPU associated with the app with maximum LLC misses in the socket
    int id;
};

static int
sort_sockets_by_total_misses(const void *a_, const void *b_)
{
    const struct socket_llcinfo *a = a_;
    const struct socket_llcinfo *b = b_;

    return a->total_misses > b->total_misses ? -1 : a->total_misses == b->total_misses ? 0 : 1;
}

void
nupoco_allocate(const int                   num_apps,
                struct appinfo             *apps_sorted[],
                const struct cpuinfo *const cpuinfo,
                const size_t                rem_cpus_sz,
                int                        *per_app_socket_orders[],
                cpu_set_t                  *new_cpusets[],
                cpu_set_t                  *remaining_cpus)
{
    switch (scheduling_phase) {
    case PROFILING_RUN:
    {
        printf("NUPOCO Profiling. Will allocate one core for each app.\n");
        // allocate each app to one core during the profiling run
        for (int i = 0; i < num_apps; i++) {
            cpu_set_t *new_cpuset = CPU_ALLOC(cpuinfo->total_cpus);
            budget_default(apps_sorted[i]->cpuset[1], new_cpuset, true, remaining_cpus, rem_cpus_sz, 1, per_app_socket_orders[i]);
            new_cpusets[i] = new_cpuset;
        }
        scheduling_phase = GREEDY_ALLOCATION;
    }   break;

    case GREEDY_ALLOCATION:
    {
        int *per_app_cpu_budget = calloc(num_apps, sizeof per_app_cpu_budget[0]);
        int available_sockets = cpuinfo->num_sockets;
        const int cpus_per_socket = cpuinfo->sockets[0].num_cpus;

        printf("NUPOCO Greedy allocation. Will reserve one socket for each parallel app.\n");
        // reserve at least one socket for every app that's in its parallel phase
        for (int i = 0; i < num_apps && i < available_sockets; i++) {
            if (app_is_parallel(apps_sorted[i])) {
                per_app_cpu_budget[i] += cpus_per_socket;
                available_sockets--;
            }
        }

        double max_system_utilization = -1.0;
        while (available_sockets-- > 0) {
            // for each additional socket unreserved, find the app that would
            // achieve the best performance and allocate the socket to it
            int best_i = -1;

            for (int i = 0; i < num_apps; i++) {
                uint64_t dram_req_rate = 0;
                int used_cpus = 0;

                for (int j = 0; j < num_apps; j++) {
                    // compute the average DRAM request rate in a hypothetical
                    // scenario where app_i receives one more socket and the
                    // other apps' socket allocation is considered

                    if (!app_is_parallel(apps_sorted[j]))
                        continue;

                    // the proposed number of CPUs that this app will use
                    int app_used_cpus = per_app_cpu_budget[j] + cpus_per_socket * (j == i ? 1 : 0);

                    dram_req_rate += apps_sorted[j]->extra_metric[EXTRA_METRIC_DRAM_REQUESTS] * app_used_cpus;
                    used_cpus += app_used_cpus;
                }

                if (used_cpus == 0)
                    continue;

                double dram_req_rate_avg = dram_req_rate / (double) used_cpus;
                double mct_utilization_total =
                    compute_mct_utilization(used_cpus / cpus_per_socket,
                            dram_req_rate_avg * cpus_per_socket,
                            mct_delay,
                            cpuinfo->num_sockets /* num_sockets == num_memories */);


                double cpu_utilization = 0.0;
                for (int j = 0; j < num_apps; j++) {
                    // compute the average CPU utilization in a hypothetical
                    // scenario where app_i receives one more socket and the
                    // other apps' socket allocation is considered

                    if (!app_is_parallel(apps_sorted[j]))
                        continue;

                    // the proposed number of CPUs that this app will use
                    int app_used_cpus = per_app_cpu_budget[j] + cpus_per_socket * (j == i ? 1 : 0);

                    double cpu_utilization_avg =
                        compute_cpu_utilization(app_used_cpus / cpus_per_socket,
                                used_cpus,
                                cpus_per_socket / cpuinfo->sockets[0].num_cpus,
                                work_cycles,
                                mct_delay,
                                bus_delay,
                                apps_sorted[j]->extra_metric[EXTRA_METRIC_LLC_MISSES],
                                apps_sorted[j]->extra_metric[EXTRA_METRIC_DRAM_REQUESTS],
                                dram_req_rate_avg,
                                cpuinfo->num_sockets);

                    cpu_utilization += cpu_utilization_avg * app_used_cpus;
                }

                cpu_utilization /= cpuinfo->total_cpus;

                double system_utilization = mct_utilization_total + cpu_utilization;

                if (system_utilization > max_system_utilization) {
                    max_system_utilization = system_utilization;
                    best_i = i;
                }
            }

            if (best_i != -1) {
                per_app_cpu_budget[best_i] += cpus_per_socket;
                printf("NUPOCO [APP %6d] reserving one more socket\n", apps_sorted[best_i]->pid);
            }
        }

        // reserve one hardware context for every other app that's not in its
        // parallel phase
        for (int i = 0; i < num_apps; i++) {
            if (!app_is_parallel(apps_sorted[i]))
                per_app_cpu_budget[i] = 1;
        }

        // allocate a budget
        for (int i = 0; i < num_apps; ++i) {
            cpu_set_t *new_cpuset = CPU_ALLOC(cpuinfo->total_cpus);

            budget_default(apps_sorted[i]->cpuset[0], new_cpuset, true, remaining_cpus, rem_cpus_sz, per_app_cpu_budget[i], per_app_socket_orders[i]);

            /* subtract allocated cpus from remaining cpus,
             * [new_cpuset] is already a subset of [remaining_cpus] */
            CPU_XOR_S(rem_cpus_sz, remaining_cpus, remaining_cpus, new_cpuset);
            per_app_cpu_budget[i] = CPU_COUNT_S(rem_cpus_sz, new_cpuset);
            new_cpusets[i] = new_cpuset;
        }

        free(per_app_cpu_budget);

        scheduling_phase = ADAPTIVE_ALLOCATION;
    }   break;

    case ADAPTIVE_ALLOCATION:
    {
        struct socket_llcinfo *sockets = calloc(cpuinfo->num_sockets, sizeof sockets[0]);

        printf("NUPOCO Adaptive allocation. Will swap cores between busy and idle apps.\n");
        for (int i = 0; i < cpuinfo->num_sockets; i++) {
            sockets[i].appid_min_llc_misses = -1;
            sockets[i].cpuid_max_llc_misses = -1;
            sockets[i].appid_max_llc_misses = -1;
            sockets[i].cpuid_min_llc_misses = -1;
            sockets[i].id = i;
        }

        // iterate over all apps, finding the sockets with the largest and smallest number of missed LLC accesses
        for (int i = 0; i < num_apps; i++) {
            // iterate over all CPUs of this app and determine the sockets they belong to
            for (int s = 0; s < cpuinfo->num_sockets; s++) {
                for (int c = 0; c < cpuinfo->sockets[s].num_cpus; c++) {
                    struct cpu hw = cpuinfo->sockets[s].cpus[c];

                    if (CPU_ISSET_S(hw.tnumber, rem_cpus_sz, apps_sorted[i]->cpuset[1])) {
                        // misses for one CPU
                        uint64_t llc_misses = apps_sorted[i]->extra_metric[EXTRA_METRIC_LLC_MISSES];
                        sockets[s].total_misses += llc_misses;

                        // choose some CPU to associate with the min
                        if (sockets[s].appid_min_llc_misses == -1 || sockets[s].min_llc_misses > llc_misses) {
                            sockets[s].appid_min_llc_misses = i;
                            sockets[s].min_llc_misses = llc_misses;
                            sockets[s].cpuid_min_llc_misses = c;
                        }

                        // choose some CPU to associate with the max
                        if (sockets[s].appid_max_llc_misses == -1 || sockets[s].max_llc_misses < llc_misses) {
                            sockets[s].appid_max_llc_misses = i;
                            sockets[s].max_llc_misses = llc_misses;
                            sockets[s].cpuid_max_llc_misses = c;
                        }
                    }
                }
            }
        }

        // sort the sockets ascending by their total LLC misses
        qsort(sockets, cpuinfo->num_sockets, sizeof sockets[0], &sort_sockets_by_total_misses);

        // initialize all new CPU sets to the current CPU sets
        for (int i = 0; i < num_apps; i++) {
            cpu_set_t *new_cpuset = CPU_ALLOC(cpuinfo->total_cpus);

            memcpy(new_cpuset, apps_sorted[i]->cpuset[1], rem_cpus_sz);
            new_cpusets[i] = new_cpuset;
        }

        // pick the sockets with the highest and lowest number of misses
        for (int i = 0, j = cpuinfo->num_sockets - 1; i < j; i++, j--) {
            const struct socket_llcinfo idle_socket = sockets[i];
            const struct socket_llcinfo busy_socket = sockets[j];
            
            // swap cores from the busy and idle apps
            if (busy_socket.total_misses / (double) idle_socket.total_misses > 2.0) {
                // swap two CPUs
                CPU_CLR_S(idle_socket.cpuid_min_llc_misses, rem_cpus_sz, new_cpusets[idle_socket.appid_min_llc_misses]);
                CPU_CLR_S(busy_socket.cpuid_max_llc_misses, rem_cpus_sz, new_cpusets[busy_socket.appid_max_llc_misses]);
                
                CPU_SET_S(idle_socket.cpuid_min_llc_misses, rem_cpus_sz, new_cpusets[busy_socket.appid_min_llc_misses]);
                CPU_SET_S(busy_socket.cpuid_max_llc_misses, rem_cpus_sz, new_cpusets[idle_socket.appid_min_llc_misses]);

                printf("NUPOCO [APP %6d] giving CPU %4d --> APP %6d\n",
                        apps_sorted[idle_socket.appid_min_llc_misses]->pid,
                        idle_socket.cpuid_min_llc_misses,
                        apps_sorted[busy_socket.appid_max_llc_misses]->pid);
                printf("NUPOCO [APP %6d] giving CPU %4d --> APP %6d\n",
                        apps_sorted[busy_socket.appid_max_llc_misses]->pid,
                        busy_socket.cpuid_max_llc_misses,
                        apps_sorted[idle_socket.appid_min_llc_misses]->pid);
            }
        }

        for (int i = 0; i < num_apps; i++) {
            // subtract allocated CPUs from remaining CPUs
            CPU_XOR_S(rem_cpus_sz, remaining_cpus, remaining_cpus, new_cpusets[i]);

            // we don't use the budgeter during this phase
        }

        free(sockets);
    }   break;
    }
}
