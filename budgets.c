#include "budgets.h"
#include "cpuinfo.h"
#include "mapper.h"

#include <string.h>

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

void
budget_collocate(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
                 enum metric curr_bottleneck,
                 enum metric prev_bottleneck,
                 cpu_set_t *remaining_cpus, const size_t cpus_sz,
                 int per_app_cpu_budget)
{
    int s = -1;
    int i = 0, j = 0;
    cpu_set_t *old_cpuset2 = NULL;
    int sockets_in_old = 0;
    int k = 0;

    for (int sock_id = 0; sock_id < cpuinfo->num_sockets && s == -1; ++sock_id) {
        for (int c = 0; c < cpuinfo->sockets[sock_id].num_cpus && s == -1; ++c) {
            struct cpu hw = cpuinfo->sockets[sock_id].cpus[c];
            if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus))
                s = sock_id;
        }
    }

    if (s == -1) {
        /* TODO */
        return;
    }

    while (i < cpuinfo->num_sockets && j < per_app_cpu_budget) {
        for (int c = 0; c < cpuinfo->sockets[s].num_cpus 
                && j < per_app_cpu_budget; ++c) {
            struct cpu hw = cpuinfo->sockets[s].cpus[c];

            if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus)) {
                CPU_SET_S(hw.tnumber, cpus_sz, new_cpuset);
                j++;
            }
        }
        s = (s + 1) % cpuinfo->num_sockets;
        i++;
    }

    if (curr_bottleneck == prev_bottleneck) {
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
              enum metric curr_bottleneck,
              enum metric prev_bottleneck,
              cpu_set_t *remaining_cpus, const size_t cpus_sz,
              int per_app_cpu_budget)
{
    int s = -1;
    int i = 0, j = 0;
    cpu_set_t *old_cpuset2 = NULL;
    int sockets_in_old = 0;
    int socket_is[cpuinfo->num_sockets];
    unsigned int sockets = 0;
    unsigned int filled = 0;
    const unsigned int sockfull = ~0u >> (8*sizeof(int) - cpuinfo->num_sockets);
    int k = 0;

    for (int sock_id = 0; sock_id < cpuinfo->num_sockets && s == -1; ++sock_id) {
        for (int c = 0; c < cpuinfo->sockets[sock_id].num_cpus && s == -1; ++c) {
            struct cpu hw = cpuinfo->sockets[sock_id].cpus[c];
            if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus))
                s = sock_id;
        }
    }

    if (s == -1) {
        /* TODO */
        return;
    }

    memset(socket_is, 0, cpuinfo->num_sockets * sizeof socket_is[0]);

    while (filled != sockfull && j < per_app_cpu_budget) {
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
        s = (s + 1) % cpuinfo->num_sockets;
    }

    if (curr_bottleneck == prev_bottleneck) {
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
                      enum metric curr_bottleneck,
                      enum metric prev_bottleneck,
                      cpu_set_t *remaining_cpus, const size_t cpus_sz,
                      int per_app_cpu_budget)
{
    int s = -1;
    int i = 0, j = 0;
    cpu_set_t *old_cpuset2 = NULL;
    unsigned int ctxs[cpuinfo->num_sockets], ctxs_old2[cpuinfo->num_sockets];
    unsigned int sockets = 0;
    unsigned int filled = 0;
    const unsigned int sockfull = ~0u >> (8*sizeof(int) - cpuinfo->num_sockets);
    int k = 0;
    const float perf_loss_factor = 0.3;     /* tweak this */
    int m = 0;

    for (int sock_id = 0; sock_id < cpuinfo->num_sockets && s == -1; ++sock_id) {
        for (int c = 0; c < cpuinfo->sockets[sock_id].num_cpus && s == -1; ++c) {
            struct cpu hw = cpuinfo->sockets[sock_id].cpus[c];
            if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus))
                s = sock_id;
        }
    }

    if (s == -1) {
        /* TODO */
        return;
    }

    memset(ctxs, 0, cpuinfo->num_sockets * sizeof ctxs[0]);
    memset(ctxs_old2, 0, cpuinfo->num_sockets * sizeof ctxs_old2[0]);

    while (filled != sockfull && j < per_app_cpu_budget) {
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

        s = (s + 1) % cpuinfo->num_sockets;
    }

    if (curr_bottleneck == prev_bottleneck) {
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
                }
            }
        }

        for (int l = 0; l < cpuinfo->num_sockets; ++l) {
            i += __builtin_popcount(ctxs[l] & (ctxs[l] >> 1));
            k += __builtin_popcount(ctxs_old2[l] & (ctxs_old2[l] >> 1));
        }

        if (perf_loss_factor*(k - i) + (j - m) <= 0.f) {
            memcpy(new_cpuset, old_cpuset2, cpus_sz);
        }

        CPU_FREE(old_cpuset2);
    }
}

void
budget_default(cpu_set_t *old_cpuset, cpu_set_t *new_cpuset,
               enum metric curr_bottleneck,
               enum metric prev_bottleneck,
               cpu_set_t *remaining_cpus, const size_t cpus_sz,
               int per_app_cpu_budget)
{
    int s = -1;
    int i = 0, j = 0;
    cpu_set_t *old_cpuset2 = NULL;
    int k = 0;

    for (int sock_id = 0; sock_id < cpuinfo->num_sockets && s == -1; ++sock_id) {
        for (int c = 0; c < cpuinfo->sockets[sock_id].num_cpus && s == -1; ++c) {
            struct cpu hw = cpuinfo->sockets[sock_id].cpus[c];
            if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus))
                s = sock_id;
        }
    }

    if (s == -1) {
        /* TODO */
        return;
    }

    while (i < cpuinfo->num_sockets && j < per_app_cpu_budget) {
        for (int c = 0; c < cpuinfo->sockets[s].num_cpus 
                && j < per_app_cpu_budget; ++c) {
            struct cpu hw = cpuinfo->sockets[s].cpus[c];
            
            if (CPU_ISSET_S(hw.tnumber, cpus_sz, remaining_cpus)) {
                CPU_SET_S(hw.tnumber, cpus_sz, new_cpuset);
                j++;
            }
        }
        s = (s + 1) % cpuinfo->num_sockets;
        i++;
    }

    if (curr_bottleneck == prev_bottleneck) {
        old_cpuset2 = CPU_ALLOC(cpuinfo->total_cpus);
        CPU_ZERO_S(cpus_sz, old_cpuset2);

        CPU_AND_S(cpus_sz, old_cpuset2, old_cpuset, remaining_cpus);
        k = CPU_COUNT_S(cpus_sz, old_cpuset2);

        if (k > per_app_cpu_budget) {
            cpu_truncate(old_cpuset2, cpuinfo->total_cpus, per_app_cpu_budget);
            k = CPU_COUNT_S(cpus_sz, old_cpuset2);
        }

        if (k >= j) {
            memcpy(new_cpuset, old_cpuset2, cpus_sz);
        }

        CPU_FREE(old_cpuset2);
    }
}
budgeter_t budgeter_functions[] = {
    [METRIC_INTER]       = &budget_collocate,
    [METRIC_INTRA]       = &budget_collocate,
    [METRIC_MEM]        = &budget_spread,
    [METRIC_AVGIPC]    = &budget_no_hyperthread
};
