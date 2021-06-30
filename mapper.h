/**
 * mapper.h
 *
 * Contains types used by the schedulers and budgeters.
 */
#ifndef MAPPER_H
#define MAPPER_H

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <sched.h>

enum metric {
    METRIC_ACTIVE,
    METRIC_AVGIPC,
    METRIC_MEM,
    METRIC_INTRA,
    METRIC_INTER,
    N_METRICS,
};

/*
 * These other metrics aren't used for sorting.
 */
enum extra_metric {
    EXTRA_METRIC_REMOTE,
    EXTRA_METRIC_IpCOREpS,
    EXTRA_METRIC_IPS,
    EXTRA_METRIC_DRAM_REQUESTS,
    EXTRA_METRIC_LLC_MISSES,
    N_EXTRA_METRICS,
};

// SAM (pre-computed thresholds)
#define MAX_COUNTERS 50
#define SHAR_MEM_THRESH 30000000
#define SHAR_COHERENCE_THRESH 450000
#define SHAR_HCOH_THRESH 800000
#define SHAR_REMOTE_THRESH 2700000
#define SHAR_CYCLES 1900000000.0
#define SHAR_IPC_THRESH 700
#define SHAR_COH_IND SHAR_COHERENCE_THRESH / 2
#define SHAR_REMOTE_THRESH 2700000
#define SHAR_IPC_THRESH 700
#define SAM_MIN_CONTEXTS 4
#define SAM_MIN_QOS 0.75
#define SAM_PERF_THRESH 0.05 /* in fraction of previous performance */
#define SAM_PERF_STEP 4 /* in number of CPUs */
#define SAM_DISTURB_PROB 0.3 /* probability of a disturbance */
#define SAM_INITIAL_ALLOCS 4 /* number of initial allocations before exploring */
#define SAM_MIN_THREADS 4

struct OMPdata {
  double progress;
  int valid_progress;
  int numthreads;
  int valid_threads;
};

struct appinfo {
  /**
   * application PID
   */
  pid_t pid;
  uint64_t metric[N_METRICS];
  uint64_t extra_metric[N_EXTRA_METRICS];
  uint64_t bottleneck[N_METRICS];
  uint64_t value[MAX_COUNTERS];

  /**
   * The number of PerfData's that refer to this application.
   */
  uint64_t refcount;
  /**
   * The current bottleneck for this application.
   */
  enum metric curr_bottleneck;
  /**
   * The previous bottleneck for this application.
   */
  enum metric prev_bottleneck;
  /**
   * The history of CPU sets for this application.
   * cpuset[0] is the latest CPU set.
   */
  cpu_set_t *cpuset[2];
  /**
   * This is the average performance for each CPU count.
   * The size of this array is equal to the total number of CPUs (cpuinfo->total_cpus) + 1.
   * Uninitialized values are 0.
   * The performance is based on the METRIC_IPS.
   * The second value is the number of times the application has been given this allocation,
   * which we use for computing the average.
   */
  uint64_t (*perf_history)[2];
  /**
   * The current fair share for this application. It can change if the number of applications
   * changes.
   */
  int curr_fair_share;
  /**
   * Number of times the application has been given an allocation.
   */
  int times_allocated;
  /**
   * The last time this application was measured.
   */
  struct timespec ts;
  /**
   * Whether the application is currently exploring resources for better performance.
   */
  bool exploring;
  /**
   * This is the [appno]'th app, starting from 0.
   */
  int appno;
  struct appinfo *prev, *next;

  /* Added for HILL CLIMBING */
  /**
   * 1 means positive direction, -1 means negative direction, store direction in order to 
   * resume
   */
  int hill_direction;
  /**
   * count the number of iterations suspended
   */
  int suspend_iter;
  /**
   * whether resuming hill climbing or not
   */
  bool hill_resume;
  /**
   * resource allocated
   */
  int bin_search_resource;
  int bin_direction;
  /* Shared memory for OpenMP communication */
  struct OMPdata *OMPptr;
  char OMPname[100];
  int OMPfd;
  int OMPvalid;

  // NuPoCo:
  bool needs_profiling;
};

/**
 * Reverse comparison. Produces a sorted list from largest to smallest element.
 */
static inline int compare_apps_by_metric_desc(const void *a_ptr, const void *b_ptr, void *arg)
{
  const struct appinfo *a = *(struct appinfo *const *)a_ptr;
  const struct appinfo *b = *(struct appinfo *const *)b_ptr;
  int met = *(int *)arg;

  return (int)((long)b->metric[met] - (long)a->metric[met]);
}

/**
 * Reverse comparison. Produces a sorted list from largest to smallest element.
 */
static inline int compare_apps_by_extra_metric_desc(const void *a_ptr, const void *b_ptr, void *arg)
{
  const struct appinfo *a = *(struct appinfo *const *)a_ptr;
  const struct appinfo *b = *(struct appinfo *const *)b_ptr;
  int met = *(int *)arg;

  return (int)((long)b->extra_metric[met] - (long)a->extra_metric[met]);
}

#endif
