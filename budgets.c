/*This file contains information regarding the buf=dgetting of resources (CPUs, logical context in this scenario)*/
#include "budgets.h"
#include "cpuinfo.h"
#include "mapper.h"

#include <string.h>
#include <assert.h>
#include <stdbool.h>

extern struct cpuinfo *cpuinfo;

static void
cpu_truncate(cpu_set_t *cpuset, int num_cpus, int max_set)
{
    const size_t sz = CPU_ALLOC_SIZE(num_cpus);
    int num_set = 0;

    for (int i = 0; i < num_cpus; ++i) {
        if (CPU_ISSET_S(i, sz, cpuset))
            num_set++;

        if (num_set > max_set)
            CPU_CLR_S(i, sz, cpuset);
    }
}

static void
budget_collocate(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
                 bool bottleneck_unchanged,
                 cpu_set_t *remaining_cpus, const size_t cpus_sz,
                 int per_app_cpu_budget,
                 int *per_app_socket_orders)
{
    int i = 0, j = 0;
    cpu_set_t *old_cpuset2 = NULL;
    int sockets_in_old = 0;
    int k = 0;

    while (i < cpuinfo->num_sockets && j < per_app_cpu_budget) {
        int s = per_app_socket_orders[i];
        for (int c = 0; c < cpuinfo->sockets[s].num_cpus 
                && j < per_app_cpu_budget; ++c) {
            struct cpu hw = cpuinfo->sockets[s].cpus[c];

            if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus)) {
                CPU_SET_S(hw.tnumber, cpus_sz, new_cpuset);
                j++;
            }
        }
        i++;
    }

    if (bottleneck_unchanged) {
        old_cpuset2 = CPU_ALLOC(cpuinfo->total_cpus);
        CPU_ZERO_S(cpus_sz, old_cpuset2);

        CPU_AND_S(cpus_sz, old_cpuset2, old_cpuset, remaining_cpus);
        k = CPU_COUNT_S(cpus_sz, old_cpuset2);

        if (k > per_app_cpu_budget) {
            cpu_truncate(old_cpuset2, cpuinfo->total_cpus, per_app_cpu_budget);
            k = CPU_COUNT_S(cpus_sz, old_cpuset2);
        }

        for (int sock_id = 0; sock_id < cpuinfo->num_sockets; ++sock_id) {
            for (int c = 0; c < cpuinfo->sockets[sock_id].num_cpus; ++c) {
                struct cpu hw = cpuinfo->sockets[sock_id].cpus[c];

                if (CPU_ISSET_S(hw.tnumber, cpus_sz, old_cpuset2)) {
                    sockets_in_old++;
                    break;
                }
            }
        }

        if (sockets_in_old <= i && k >= j) {
            memcpy(new_cpuset, old_cpuset2, cpus_sz);
        }

        CPU_FREE(old_cpuset2);
    }
}

void
budget_spread(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
              bool bottleneck_unchanged,
              cpu_set_t *remaining_cpus, const size_t cpus_sz,
              int per_app_cpu_budget,
              int *per_app_socket_orders)
{
    int socket_i = 0;
    int i = 0, j = 0;
    cpu_set_t *old_cpuset2 = NULL;
    int sockets_in_old = 0;
    int socket_is[cpuinfo->num_sockets];
    unsigned int sockets = 0;
    unsigned int filled = 0;
    const unsigned int sockfull = ~0u >> (8*sizeof(int) - cpuinfo->num_sockets);
    int k = 0;

    memset(socket_is, 0, cpuinfo->num_sockets * sizeof socket_is[0]);

    while (filled != sockfull && j < per_app_cpu_budget) {
        int s = per_app_socket_orders[socket_i];
        struct cpu hw = cpuinfo->sockets[s].cpus[socket_is[s]];

        if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus)) {
            CPU_SET_S(hw.tnumber, cpus_sz, new_cpuset);
            j++;
            if (!(sockets & (1u << s))) {
                sockets |= 1u << s;
                i++;
            }
        }

        if (socket_is[s] == cpuinfo->sockets[s].num_cpus - 1) {
            filled |= 1u << s;
        } else
            socket_is[s]++;

        socket_i = (socket_i + 1) % cpuinfo->num_sockets;
    }

    if (bottleneck_unchanged) {
        old_cpuset2 = CPU_ALLOC(cpuinfo->total_cpus);
        CPU_ZERO_S(cpus_sz, old_cpuset2);

        CPU_AND_S(cpus_sz, old_cpuset2, old_cpuset, remaining_cpus);
        k = CPU_COUNT_S(cpus_sz, old_cpuset2);

        if (k > per_app_cpu_budget) {
            cpu_truncate(old_cpuset2, cpuinfo->total_cpus, per_app_cpu_budget);
            k = CPU_COUNT_S(cpus_sz, old_cpuset2);
        }

        for (int sock_id = 0; sock_id < cpuinfo->num_sockets; ++sock_id) {
            for (int c = 0; c < cpuinfo->sockets[sock_id].num_cpus; ++c) {
                struct cpu hw = cpuinfo->sockets[sock_id].cpus[c];

                if (CPU_ISSET_S(hw.tnumber, cpus_sz, old_cpuset2)) {
                    sockets_in_old++;
                    break;
                }
            }
        }

        if (i > sockets_in_old && j >= k) {
            memcpy(new_cpuset, old_cpuset2, cpus_sz);
        }

        CPU_FREE(old_cpuset2);
    }
}

void
budget_no_hyperthread(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
                      bool bottleneck_unchanged,
                      cpu_set_t *remaining_cpus, const size_t cpus_sz,
                      int per_app_cpu_budget,
                      int *per_app_socket_orders)
{
    int i = 0, j = 0;
    cpu_set_t *old_cpuset2 = NULL;
    unsigned int ctxs[cpuinfo->num_sockets], ctxs_old2[cpuinfo->num_sockets];
    unsigned int sockets = 0;
    unsigned int filled = 0;
    const unsigned int sockfull = ~0u >> (8*sizeof(int) - cpuinfo->num_sockets);
    int k = 0;
    const float perf_loss_factor = 0.3;     /* tweak this */
    int m = 0;
    bool old_cpuset2_valid = true;

    memset(ctxs, 0, cpuinfo->num_sockets * sizeof ctxs[0]);
    memset(ctxs_old2, 0, cpuinfo->num_sockets * sizeof ctxs_old2[0]);

    while (filled != sockfull && j < per_app_cpu_budget) {
        int s = per_app_socket_orders[i];
        for (int c = !!(sockets & (1u << s)); c < cpuinfo->sockets[s].num_cpus
                && j < per_app_cpu_budget; c += 2) {
            struct cpu hw = cpuinfo->sockets[s].cpus[c];

            if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus)) {
                CPU_SET_S(hw.tnumber, cpus_sz, new_cpuset);
                ctxs[s] |= 1u << c;
                j++;
            }
        }

        if (!(sockets & (1u << s)))
            sockets |= 1u << s;
        else
            filled |= 1u << s;

        i = (i + 1) % cpuinfo->num_sockets;
    }

    if (bottleneck_unchanged) {
        old_cpuset2 = CPU_ALLOC(cpuinfo->total_cpus);
        CPU_ZERO_S(cpus_sz, old_cpuset2);

        CPU_AND_S(cpus_sz, old_cpuset2, old_cpuset, remaining_cpus);
        m = CPU_COUNT_S(cpus_sz, old_cpuset2);

        if (m > per_app_cpu_budget) {
            cpu_truncate(old_cpuset2, cpuinfo->total_cpus, per_app_cpu_budget);
            m = CPU_COUNT_S(cpus_sz, old_cpuset2);
        }

        for (int sock_id = 0; sock_id < cpuinfo->num_sockets; ++sock_id) {
            for (int c = 0; c < cpuinfo->sockets[sock_id].num_cpus; ++c) {
                struct cpu hw = cpuinfo->sockets[sock_id].cpus[c];

                if (CPU_ISSET_S(hw.tnumber, cpus_sz, old_cpuset2)) {
                    ctxs_old2[sock_id] |= 1u << c;
                    old_cpuset2_valid &= CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus);
                }
            }
        }
        
        // only consider keeping the old cpuset if it's still valid
        if (old_cpuset2_valid) {
            i = 0;

            for (int l = 0; l < cpuinfo->num_sockets; ++l) {
                i += __builtin_popcount(ctxs[l] & (ctxs[l] >> 1));
                k += __builtin_popcount(ctxs_old2[l] & (ctxs_old2[l] >> 1));
            }

            /**
             * j = (new) number of CPUs
             * m = (old) number of CPUs
             * i = (new) number of hyperthread CPUs
             * k = (old) number of hyperthread CPUs
             *
             */
            if (perf_loss_factor*(k - i) + (j - m) <= 0.f) {
                memcpy(new_cpuset, old_cpuset2, cpus_sz);
            }
        }

        CPU_FREE(old_cpuset2);
    }
}

void
budget_default(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
               bool bottleneck_unchanged,
               cpu_set_t *remaining_cpus, const size_t cpus_sz,
               int per_app_cpu_budget,
               int *per_app_socket_orders)
{
    budget_no_hyperthread(old_cpuset, new_cpuset,
            bottleneck_unchanged,
            remaining_cpus, cpus_sz,
            per_app_cpu_budget,
            per_app_socket_orders);
}

budgeter_t budgeter_functions[] = {
    [METRIC_INTER]      = &budget_collocate,
    [METRIC_INTRA]      = &budget_collocate,
    [METRIC_MEM]        = &budget_spread,
    [METRIC_AVGIPC]     = &budget_no_hyperthread
};
