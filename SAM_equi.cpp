#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> // For random().
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>

#include <locale.h>
#include <sched.h>
#include <stddef.h>
#include <assert.h>

#include "config.h"
#include "budgets.h"
#include "cgroup.h"
#include "cpuinfo.h"
#include "mapper.h"
#include "util.h"
#include "perfMulti/perThread_perf.h"
#include <sys/time.h>
// SAM
#define MAX_COUNTERS 50
#define SHAR_MEM_THRESH 100000000
#define SHAR_COHERENCE_THRESH 550000
#define SHAR_HCOH_THRESH 1100000
#define SHAR_REMOTE_THRESH 2700000
#define SHAR_IPC_THRESH 700
#define SAM_MIN_CONTEXTS 4
#define SAM_MIN_QOS 0.75
#define SAM_PERF_THRESH 0.05 /* in fraction of previous performance */
#define SAM_PERF_STEP 4 /* in number of CPUs */
#define SAM_DISTURB_PROB 0.3 /* probability of a disturbance */
#define SAM_INITIAL_ALLOCS 4 /* number of initial allocations before exploring */

#define PRINT_BOTTLENECK false
#define HILL_CLIMBING false
#define HILL_SUSPEND 5 //suspend for these many iterations when local optima found
#define FAIR false
#define BIN_INITIAL_RESOURCE 12
// Will be initialized anyway
int num_counter_orders = 6;
int random_seed = 0xFACE;

const char *cgroot = "/sys/fs/cgroup";
const char *cntrlr = "cpuset";

int thresh_pt[N_METRICS];
enum metric counter_order[MAX_COUNTERS];
int ordernum = 0;
int init_thresholds = 0;

struct timespec start_time,finish_time;
struct timespec perf_start, perf_finish;

struct counter {
  double ratio;
  uint64_t val, delta;
  uint64_t auxval1, auxval2;
};

/**
 * Information about a process.
 */
struct procinfo {
  public:
  void readCounters(int index); // argument added for copying
  void printCounters(int index); // argument added for copying

  bool init;
  struct counter counters[MAX_COUNTERS];
  int num_counters;
  bool touched;
  pid_t app_pid;
  pid_t pid;
  int bottleneck[MAX_COUNTERS];
  int active;
  double val[MAX_COUNTERS];
  struct procinfo *prev, *next;
};

struct procinfo *procs_list;
struct procinfo **procs_array;

const char *metric_names[N_METRICS] = {
    [METRIC_ACTIVE] = "Active",
    [METRIC_AVGIPC] = "Average IPC",
    [METRIC_MEM]    = "Memory",
    [METRIC_INTRA]  = "Intra-socket communication",
    [METRIC_INTER]  = "Inter-socket communication",
};

struct OMPdata
{
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
};

bool stoprun = false;
bool print_counters = false;
bool print_proc_creation = false;
struct cpuinfo *cpuinfo;

struct appinfo **apps_array;
struct appinfo *apps_list;
int num_apps = 0;
int num_procs = 0;

static inline int guess_optimization(const int budget, enum metric bottleneck)
{
  const int cpus_per_socket = cpuinfo->sockets[0].num_cpus;
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

static inline int determine_step_size(enum metric bottleneck, int curr_alloc, int dir)
{
	const int cpus_per_socket = cpuinfo->sockets[0].num_cpus;
	
	if (bottleneck ==  METRIC_INTER || bottleneck == METRIC_INTRA) {
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

/**
 * Reverse comparison. Produces a sorted list from largest to smallest element.
 */
static int compare_apps_by_metric_desc(const void *a_ptr, const void *b_ptr, void *arg)
{
  const struct appinfo *a = *(struct appinfo * const *)a_ptr;
  const struct appinfo *b = *(struct appinfo * const *)b_ptr;
  int met = *(int *)arg;

  return (int)((long)b->metric[met] - (long)a->metric[met]);
}

/**
 * Reverse comparison. Produces a sorted list from largest to smallest element.
 */
static int compare_apps_by_extra_metric_desc(const void *a_ptr, const void *b_ptr, void *arg)
{
  const struct appinfo *a = *(struct appinfo * const *)a_ptr;
  const struct appinfo *b = *(struct appinfo * const *)b_ptr;
  int met = *(int *)arg;

  return (int)((long)b->extra_metric[met] - (long)a->extra_metric[met]);
}

static int compare_ints_mapped(const void *arg1, const void *arg2, void *ptr)
{
  const int *map = (const int *)ptr;
  return map[*(const int *)arg1] > map[*(const int *)arg2];
}

void sigterm_handler(int sig)
{
  stoprun = true;
}
void siginfo_handler(int sig)
{
  printf("[DEBUG] Received %s.", strsignal(sig));
  if ((print_counters = !print_counters))
    printf(" Enabled printing counters.\n");
  else
    printf(" Disabled printing counters.\n");
}

static void manage(pid_t pid, pid_t app_pid)
{
  assert(procs_array[pid] == NULL);

  /* add new process info */
  struct procinfo *pnode = new procinfo();

  procs_array[pid] = pnode;
  if (procs_list)
    procs_list->prev = pnode;
  pnode->next = procs_list;
  procs_list = pnode;

  pnode->pid = pid;
  pnode->num_counters = 10;
  pnode->app_pid = app_pid;
  pnode->init = true;

  num_procs++;

  /* add to apps list and array */

  if (!apps_array[app_pid]) {
    struct appinfo *anode = (struct appinfo *)calloc(1, sizeof *anode);
    size_t sz = CPU_ALLOC_SIZE(cpuinfo->total_cpus);

    anode->pid = app_pid;
    anode->refcount = 1;
    anode->next = apps_list;
    anode->cpuset[0] = CPU_ALLOC(cpuinfo->total_cpus);
    anode->cpuset[1] = CPU_ALLOC(cpuinfo->total_cpus);
    CPU_ZERO_S(sz, anode->cpuset[0]);
    CPU_ZERO_S(sz, anode->cpuset[1]);
    anode->perf_history = (uint64_t(*)[2])calloc(cpuinfo->total_cpus + 1, sizeof *anode->perf_history);
    if (apps_list)
      apps_list->prev = anode;
    apps_list = anode;
    apps_array[app_pid] = anode;
    num_apps++;

		/* Check if OMP shared memory is opened */
		anode->OMPvalid = 0;
		anode->OMPfd = 0;
		anode->OMPptr = NULL;
		sprintf(anode->OMPname, "MAP-%d",app_pid);

		anode->OMPfd = shm_open (anode->OMPname, O_RDWR, 0777);
	  if (anode->OMPfd == -1) 
  	  printf("Error. |%s| shared memory segment does not exist\n", anode->OMPname);
		else {
	  	anode->OMPptr = (struct OMPdata*) mmap(NULL, sizeof(struct OMPdata),
  	  	PROT_READ | PROT_WRITE, MAP_SHARED, anode->OMPfd, 0);
		  if (anode->OMPptr == MAP_FAILED) 
  		  printf("Mmap failed \n");
			else
				anode->OMPvalid = 1;
		} 
    printf("Managing new application %d\n", app_pid);
  } else
    apps_array[app_pid]->refcount++;

  /* add this new task to the cgroup */
  char cg_name[256];

  snprintf(cg_name, sizeof cg_name, SAM_CGROUP_NAME "/app-%d", app_pid);
  if (cg_write_string(cgroot, cntrlr, cg_name, "tasks", "%d", pid) != 0) {
    fprintf(stderr, "Failed to add task %d to %s: %s\n", pid, cg_name, strerror(errno));
  }
}

static void unmanage(pid_t pid, pid_t app_pid)
{
  assert(procs_array[pid] != NULL);

  /* remove process */
  struct procinfo *pnode = procs_array[pid];

  procs_array[pid] = NULL;

  if (pnode->prev) {
    struct procinfo *prev = pnode->prev;
    prev->next = pnode->next;
  }
  if (pnode->next) {
    struct procinfo *next = pnode->next;
    next->prev = pnode->prev;
  }

  if (pnode == procs_list)
    procs_list = pnode->next;

  num_procs--;

  delete pnode;

  /* remove app from array and unlink */
  if (apps_array[app_pid]) {
    assert(apps_array[app_pid]->refcount > 0);
    apps_array[app_pid]->refcount--;
  }

  if (apps_array[app_pid] && apps_array[app_pid]->refcount == 0) {
    struct appinfo *anode = apps_array[app_pid];

    apps_array[app_pid] = NULL;
    num_apps--;
    if (anode->prev) {
      struct appinfo *prev = anode->prev;
      prev->next = anode->next;
    }
    if (anode->next) {
      struct appinfo *next = anode->next;
      next->prev = anode->prev;
    }

    if (anode == apps_list)
      apps_list = anode->next;

		if (anode->OMPvalid) {
			shm_unlink(anode->OMPname);
			anode->OMPptr = NULL;
			anode->OMPvalid = 0;
			anode->OMPfd = 0;
		}

    printf("Unmanaged application %d\n", app_pid);

    CPU_FREE(anode->cpuset[0]);
    CPU_FREE(anode->cpuset[1]);
    anode->cpuset[0] = NULL;
    anode->cpuset[1] = NULL;
    free(anode->perf_history);
    anode->perf_history = NULL;
    free(anode);
  }
}

/**
 * @app_pid = an app to traverse its tree
 */
void update_children(pid_t app_pid)
{
  pid_t frontier[8192];
  int frontier_l = 0;

  frontier[frontier_l++] = app_pid;
  while (frontier_l > 0) {
    pid_t cur_pid = frontier[--frontier_l];
    char path[512];
    DIR *dir;
    struct dirent *ent;

    snprintf(path, sizeof path, "/proc/%d/task", cur_pid);

    if (!(dir = opendir(path))) {
      if (errno != ENOENT)
        fprintf(stderr, "%s: could not open %s: %s\n", __func__, path, strerror(errno));
      continue;
    }

    while ((ent = readdir(dir))) {
      char *endptr;
      pid_t task, npid;
      FILE *children_f;
      errno = 0;

      task = strtol(ent->d_name, &endptr, 0);

      if (endptr == ent->d_name || errno != 0 || task == 0)
        continue;

      /* this is a valid pid, so add a perfdata for it if there
       * isn't already one */
      if (!procs_array[task]) {
        manage(task, app_pid);
      } else if (procs_array[task]->app_pid != app_pid) {
        /* 
         * this PID was reused under another application
         * before we could detect the change.
         */
        unmanage(task, procs_array[task]->app_pid);
        manage(task, app_pid);
      }

      if (procs_array[task])
        procs_array[task]->touched = true;

      snprintf(path, sizeof path, "/proc/%d/task/%d/children", cur_pid, task);

      if (!(children_f = fopen(path, "r"))) {
        if (errno != ENOENT)
          fprintf(stderr, "%s: could not open %s: %s\n", __func__, path, strerror(errno));
        continue;
      }

      while (fscanf(children_f, "%d", &npid) == 1)
        frontier[frontier_l++] = npid;

      fclose(children_f);
    }

    closedir(dir);
  }
}

void procinfo::printCounters(int index)
{
  const int counter_event_pairs[][2] = {
      { 0, EVENT_UNHALTED_CYCLES },
      { 1, EVENT_INSTRUCTIONS },
      { 7, EVENT_SNP },
      { 8, EVENT_LLC_MISSES },
      { 9, EVENT_REMOTE_HITM }
  };
  const int num_pairs = sizeof(counter_event_pairs) / sizeof(counter_event_pairs[0]);

  for (int i = 0; i < num_pairs; ++i) {
      int ctr = counter_event_pairs[i][0];
      int evt = counter_event_pairs[i][1];

      counters[ctr].delta = THREADS.event[index][evt];
  }

  int i;
  active = 0;

  if (print_counters)
    printf("%20s: %20d\n", "TID", THREADS.tid[index]);

  for (i = 0; i < num_counters; i++) {
    counters[i].val += counters[i].delta;
    bottleneck[i] = 0;
    if (apps_array[app_pid])
      apps_array[app_pid]->value[i] += counters[i].delta;
  }

  for (int k = 0; k < num_pairs; ++k) {
      int ctr = counter_event_pairs[k][0];
      int evt = counter_event_pairs[k][1];

      if (print_counters)
        printf("%20s: %'20" PRIu64 " %'20" PRIu64 " (%.2f%% scaling, ena=%'" PRIu64 ", run=%'" PRIu64 ")\n",
                event_names[evt], counters[ctr].val, counters[ctr].delta,
                (1.0 - counters[ctr].ratio) * 100.0, counters[ctr].auxval1, 
                counters[ctr].auxval2);
  }

  i = 0;
  if (counters[i].delta > (uint64_t)thresh_pt[i]) {
    active = 1;
    val[i] = counters[i].delta;
    bottleneck[i] = 1;
    if (apps_array[app_pid])
      apps_array[app_pid]->bottleneck[i] += 1;
  }

  i = 1;
  if ((counters[i].delta * 1000) / (1 + counters[0].delta) > (uint64_t)thresh_pt[i]) {
    val[i] = (1000 * counters[1].delta) / (counters[0].delta + 1);
    bottleneck[i] = 1;
    if (apps_array[app_pid])
      apps_array[app_pid]->bottleneck[i] += 1;
    if (PRINT_BOTTLENECK)
      printf("[PID %6d] detected counter %s\n", pid, metric_names[i]);
  }

  i = 2; // Mem
  long tempvar = (((double)cpuinfo->clock_rate * counters[8].delta) / (counters[0].delta + 1));
  if (tempvar > thresh_pt[i]) {
    val[i] = tempvar;
    bottleneck[i] = 1;
    if (apps_array[app_pid])
      apps_array[app_pid]->bottleneck[i] += 1;
    if (PRINT_BOTTLENECK)
      printf("[PID %6d] detected counter %s\n", pid, metric_names[i]);
  }

  i = 3; // snp
  tempvar = (((double)cpuinfo->clock_rate * (counters[7].delta)) / (counters[0].delta + 1));
  if (tempvar > thresh_pt[i]) {
    val[i] = tempvar;
    bottleneck[i] = 1;
    if (apps_array[app_pid])
      apps_array[app_pid]->bottleneck[i] += 1;
    if (PRINT_BOTTLENECK)
      printf("[PID %6d] detected counter %s\n", pid, metric_names[i]);
  }

  i = 4; // cross soc
  tempvar = (((double)cpuinfo->clock_rate * counters[9].delta) / (counters[0].delta + 1));
  if (tempvar > thresh_pt[i]) {
    val[i] = tempvar;
    bottleneck[i] = 1;
    if (apps_array[app_pid])
      apps_array[app_pid]->bottleneck[i] += 1;
    if (PRINT_BOTTLENECK)
      printf("[PID %6d] detected counter %s\n", pid, metric_names[i]);
  }
}
void procinfo::readCounters(int index)
{
  printf("[APP %6d | TID %5d] readCounters():\n", app_pid, pid);
  if (pid == 0)
  {
    return;
  }
  if (kill(pid, 0) < 0)
    if (errno == ESRCH) {
      printf("Process %d does not exist \n", pid);
      return;
    } else
      printf("Hmmm what the hell happened??? \n");
  else
    printf(" Process exists so just continuing \n");

  printCounters(index);
  return;
}

void setup_file_limits()
{
  struct rlimit limit;

  limit.rlim_cur = 65535;
  limit.rlim_max = 65535;
  printf("Setting file limits:\n");
  if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
    perror("setrlimit");
    exit(1);
  }

  if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
    perror("getrlimit");
    exit(1);
  }
  printf("\tThe soft limit is %lu\n", limit.rlim_cur);
  printf("\tThe hard limit is %lu\n", limit.rlim_max);
  return;
}

int main(int argc, char *argv[])
{
  int init_error = 0;

  size_t rem_cpus_sz;
  int pids_to_monitor_l = 0;

  setlocale(LC_ALL, "");

  if (geteuid() != 0) {
    fprintf(stderr, "I need root access for %s\n", cgroot);
    return 1;
  }

  srandom(random_seed);

  setup_file_limits();
  // Initialize what event we want

  signal(SIGTERM, &sigterm_handler);
  signal(SIGQUIT, &sigterm_handler);
  signal(SIGINT, &sigterm_handler);
  signal(SIGUSR1, &siginfo_handler);

  if (init_thresholds == 0) {
    /* create array */
    FILE *pid_max_fp;
    int pid_max = 0;

    if (!(pid_max_fp = fopen("/proc/sys/kernel/pid_max", "r")) || fscanf(pid_max_fp, "%d", &pid_max) != 1) {
      perror("Could not get pid_max");
      return 1;
    }
    printf("pid_max = %d\n", pid_max);
    apps_array = (struct appinfo **)calloc(pid_max, sizeof *apps_array);
    procs_array = (struct procinfo **)calloc(pid_max, sizeof *procs_array);
    fclose(pid_max_fp);

    /* get CPU topology */
    if ((cpuinfo = get_cpuinfo())) {
      printf("CPU Info\n========\n");
      printf("Max clock rate: %'lu Hz\n", cpuinfo->clock_rate);
      printf("Topology: %d threads across %d sockets:\n", cpuinfo->total_cpus, cpuinfo->num_sockets);
      for (int i = 0; i < cpuinfo->num_sockets; ++i) {
        printf(" socket %d has threads:", i);
        for (int j = 0; j < cpuinfo->sockets[i].num_cpus; ++j)
          printf(" %d", cpuinfo->sockets[i].cpus[j].tnumber);
        printf("\n");
      }
    } else {
      fprintf(stderr, "Failed to get CPU topology.\n");
      init_error = -1;
      goto END;
    }
    mode_t oldmask = umask(0);

    /* initialize thresholds */
    thresh_pt[METRIC_ACTIVE] = 1000000; // cycles
    thresh_pt[METRIC_AVGIPC] = 70; // instructions scaled to 100
    thresh_pt[METRIC_MEM] = SHAR_MEM_THRESH / cpuinfo->total_cores; // Mem
    thresh_pt[METRIC_INTRA] = SHAR_COHERENCE_THRESH;
    thresh_pt[METRIC_INTER] = SHAR_COHERENCE_THRESH;
    // thresh_pt[METRIC_REMOTE] = SHAR_REMOTE_THRESH;

    ordernum = 0;
    counter_order[ordernum++] = METRIC_INTER; // LLC_MISSES
    counter_order[ordernum++] = METRIC_INTRA;
    counter_order[ordernum++] = METRIC_MEM;
    counter_order[ordernum++] = METRIC_AVGIPC;
    num_counter_orders = ordernum;


    /* create run directory */
    if (mkdir(SAM_RUN_DIR, 01777) < 0 && errno != EEXIST) {
      fprintf(stderr, "Failed to create %s: %s\n", SAM_RUN_DIR, strerror(errno));
      exit(EXIT_FAILURE);
    }

    /* create cgroup */
    char *mems_string = NULL;
    char *cpus_string = NULL;
    if (cg_create_cgroup(cgroot, cntrlr, SAM_CGROUP_NAME) < 0 && errno != EEXIST) {
      perror("Failed to create cgroup");
      init_error = -1;
      goto END;
    }

    if (cg_read_string(cgroot, cntrlr, ".", "cpuset.mems", &mems_string) < 0 ||
        cg_write_string(cgroot, cntrlr, SAM_CGROUP_NAME, "cpuset.mems", "%s", mems_string) < 0 ||
        cg_read_string(cgroot, cntrlr, ".", "cpuset.cpus", &cpus_string) < 0 ||
        cg_write_string(cgroot, cntrlr, SAM_CGROUP_NAME, "cpuset.cpus", "%s", cpus_string) < 0) {
      perror("Failed to create cgroup");
      free(mems_string);
      free(cpus_string);
      init_error = -1;
      goto END;
    }
    umask(oldmask);
    free(mems_string);
    free(cpus_string);
    init_thresholds = 1;
  }
  if (init_error == -1)
    goto END;

  while (!stoprun) {
    pid_t pids_to_monitor[8192];

    cpu_set_t *remaining_cpus;

    /* get iteration start time */
    clock_gettime(CLOCK_MONOTONIC_RAW, &start_time);

    /* check for new applications / threads */
    {
      DIR *dr;
      struct dirent *de;
      dr = opendir(SAM_RUN_DIR);

      if (dr == NULL) {
        perror("Could not open " SAM_RUN_DIR);
        return 0;
      }

      for (struct procinfo *pd = procs_list; pd; pd = pd->next)
        pd->touched = false;

      while ((de = readdir(dr)) != NULL) {
        if (!((strcmp(de->d_name, ".") == 0) || (strcmp(de->d_name, "..") == 0))) {
          int app_pid = atoi(de->d_name);
          update_children(app_pid);
        }
      }

      /* remove all untouched children */
      for (struct procinfo *pd = procs_list; pd;) {
        struct procinfo *next = pd->next;
        if (!pd->touched)
          unmanage(pd->pid, pd->app_pid);
        pd = next;
      }

      closedir(dr);
    }

    // printf("PIDs tracked:\n");
		pids_to_monitor_l = 0;
    for (struct procinfo *pd = procs_list; pd; pd = pd->next) {
      pids_to_monitor[pids_to_monitor_l++] = pd->pid;
      //printf("%d\n", pd->pid);
    }

    clock_gettime(CLOCK_MONOTONIC_RAW, &perf_start);
    count_event_perfMultiplex(pids_to_monitor, pids_to_monitor_l); 
    clock_gettime(CLOCK_MONOTONIC_RAW, &perf_finish); 

    // count for all tids for a particular interval of time
    displayTIDEvents(pids_to_monitor, pids_to_monitor_l); // required to copy values to my data structures
    

    /* read counters */
    for (struct procinfo *pd = procs_list; pd; pd = pd->next) {
      int my_index = searchTID(pd->pid);
      if (my_index != -1)
        pd->printCounters(my_index);
    }

    /* derive app statistics */
    for (struct appinfo *an = apps_list; an; an = an->next) {
      if (an == apps_list)
          an->appno = num_apps - 1;
      else
          an->appno = an->prev->appno - 1;

			if (an->OMPvalid) {
				if (an->OMPptr) {
					if (an->OMPptr->valid_progress)
						printf("[APP %6d] has valid progress of %f \n", an->pid, an->OMPptr->progress);
					else
						printf("[APP %6d] has no valid progress \n", an->pid);
				}
			}
			else {
				an->OMPfd = shm_open (an->OMPname, O_RDWR, 0777);
				if (an->OMPfd == -1)
					printf("Error. |%s| shared memory segment does not exist\n", an->OMPname);
				else {
					an->OMPptr = (struct OMPdata*)mmap(NULL, sizeof(struct OMPdata),
						PROT_READ | PROT_WRITE, MAP_SHARED, an->OMPfd, 0);
					if (an->OMPptr == MAP_FAILED)
						printf("Mmap failed \n");
					else
						an->OMPvalid = 1;
				}
			}
      an->metric[METRIC_ACTIVE] = an->value[0];
      an->metric[METRIC_AVGIPC] = (an->value[1] * 1000) / (1 + an->value[0]);
      an->metric[METRIC_MEM] = an->value[8];
      an->metric[METRIC_INTRA] = an->value[7] - (an->value[5] + an->value[6]);
      an->metric[METRIC_INTER] = an->value[9];
      if (an->times_allocated > 0)
        an->extra_metric[EXTRA_METRIC_IpCOREpS] =
          an->value[1] / CPU_COUNT_S(CPU_ALLOC_SIZE(cpuinfo->total_cpus), an->cpuset[0]);

      if (!(an->ts.tv_sec == 0 && an->ts.tv_nsec == 0)) {
        struct timespec diff_ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &diff_ts);

        if (diff_ts.tv_nsec < an->ts.tv_nsec) {
          diff_ts.tv_sec = diff_ts.tv_sec - an->ts.tv_sec - 1;
          diff_ts.tv_nsec = 1000000000 - (an->ts.tv_nsec - diff_ts.tv_nsec);
        } else {
          diff_ts.tv_sec -= an->ts.tv_sec;
          diff_ts.tv_nsec -= an->ts.tv_nsec;
        }
        printf("[APP %6d] perf metric %lu \n", an->pid, an->metric[EXTRA_METRIC_IPS]);
        /*
         * Compute instructions / second
         */
        an->extra_metric[EXTRA_METRIC_IPS] =
          an->value[EVENT_INSTRUCTIONS] / (diff_ts.tv_sec + (double)diff_ts.tv_nsec / 1000000000);
      } else
        clock_gettime(CLOCK_MONOTONIC_RAW, &an->ts);
      //Added for Hill climbing, initialize
      an->hill_direction =
        1; /* 1 means positive direction, -1 means negative direction, store direction in order to resume*/
      an->suspend_iter = 0; /*count the number of iterations suspended*/
      an->hill_resume = false;

      an->bin_search_resource = BIN_INITIAL_RESOURCE;
      an->bin_direction = 1;
    }

    /* map applications */
    remaining_cpus = CPU_ALLOC(cpuinfo->total_cpus);
    rem_cpus_sz = CPU_ALLOC_SIZE(cpuinfo->total_cpus);

    CPU_ZERO_S(rem_cpus_sz, remaining_cpus);
    for (int i = 0; i < cpuinfo->total_cpus; ++i)
      CPU_SET_S(i, rem_cpus_sz, remaining_cpus);

    if (num_apps > 0) {
      const float budget_f = cpuinfo->total_cpus / (float)num_apps;
      const int fair_share = MAX(floorf(budget_f), SAM_MIN_CONTEXTS);
      struct appinfo **apps_unsorted = (struct appinfo **)calloc(num_apps, sizeof *apps_unsorted);
      struct appinfo **apps_sorted = (struct appinfo **)calloc(num_apps, sizeof *apps_sorted);
      int *per_app_cpu_budget = (int *)calloc(num_apps, sizeof *per_app_cpu_budget);
      cpu_set_t **new_cpusets = (cpu_set_t **)calloc(num_apps, sizeof *new_cpusets);
      int *needs_more = (int *)calloc(num_apps, sizeof *needs_more);
      int initial_remaining_cpus = cpuinfo->total_cpus;
      int **per_app_socket_orders = (int **)calloc(num_apps, sizeof *per_app_socket_orders);
      char buf[256];

      int range_ends[N_METRICS + 1] = { 0 };

      {
        int i = 0;
        for (struct appinfo *an = apps_list; an; an = an->next) {
          apps_unsorted[i] = an;
          per_app_socket_orders[i] = (int *)calloc(cpuinfo->num_sockets, sizeof *per_app_socket_orders[i]);
          initial_remaining_cpus -= CPU_COUNT_S(rem_cpus_sz, an->cpuset[0]);
          i++;
        }
      }

      /* this really shouldn't be necessary */
      initial_remaining_cpus = MIN(MAX(initial_remaining_cpus, 0), cpuinfo->total_cpus);

      /*
       * apps_sorted[] will look like:
       * [ apps sorted by bottleneck 1 | apps sorted by bottleneck 2 | ... |
       * remaining ]
       */
      for (int i = 0; i < N_METRICS; ++i) {
        range_ends[i + 1] = range_ends[i];
        for (int j = 0; j < num_apps; ++j) {
          if (apps_unsorted[j] 
#if !(FAIR || HILL_CLIMBING)
                  && (i >= num_counter_orders || apps_unsorted[j]->bottleneck[counter_order[i]] > 0)) {
#else
                  && counter_order[i] == METRIC_AVGIPC) {
#endif
            apps_sorted[range_ends[i + 1]++] = apps_unsorted[j];
            apps_unsorted[j] = NULL;
          }
        }

        /*
         * We sort the apps that have bottlenecks. For the remaining apps
         * (i >= num_counter_orders), we do not sort them.
         */
        if (i < num_counter_orders) {
          int met = counter_order[i];
          qsort_r(&apps_sorted[range_ends[i]], range_ends[i + 1] - range_ends[i], sizeof *apps_sorted,
                  &compare_apps_by_metric_desc, (void *)&met);
        }
      }

      /*
       * Each application computes its ideal budget.
       */
      for (int i = 0; i < N_METRICS; ++i) {
        for (int j = range_ends[i]; j < range_ends[i + 1]; ++j) {
          const int curr_alloc_len = CPU_COUNT_S(rem_cpus_sz, apps_sorted[j]->cpuset[0]);

          //per_app_cpu_budget[j] = MAX((int) apps_sorted[j]->bottleneck[METRIC_ACTIVE], SAM_MIN_CONTEXTS);
          initial_remaining_cpus += curr_alloc_len;
          per_app_cpu_budget[j] = curr_alloc_len;
#if FAIR
        /*If application has already been sorted, then just check for adjusting fair share*/
	  if (apps_sorted[j]->times_allocated > SAM_INITIAL_ALLOCS) {

              //If fair share has changed then adjust to new fair share
	        if (apps_sorted[j]->curr_fair_share != fair_share && apps_sorted[j]->perf_history[fair_share] != 0) {
			                apps_sorted[j]->curr_fair_share = fair_share;
                                         printf("FAIR SHARE [APP %6d] changing fair share\n", apps_sorted[j]->pid); 
					} 


	  }
	  else {
                //Give the application fair share
                       per_app_cpu_budget[j] = fair_share;
		       printf("FAIR SHARE [APP %6d] setting fair share \n", apps_sorted[j]->pid);

              }		  
	



#elif HILL_CLIMBING
   //HILL_CLIMBING IS THE SAME
          /* If this app has already been given an allocation, then we can compute history
	   * and excess cores.
	   */
	    if (apps_sorted[j]->times_allocated > SAM_INITIAL_ALLOCS) {
	            /* compute performance history */
	            uint64_t history[2];
	            memcpy(history, apps_sorted[j]->perf_history[curr_alloc_len], sizeof history);
	            history[1]++;
	            history[0] = apps_sorted[j]->extra_metric[EXTRA_METRIC_IPS] * (1 / (double)history[1]) +
	                             history[0] * ((history[1] - 1) / (double)history[1]);

	/* Change application's fair share count if the creation of new applications
	 * changes the fair share. */
	    if (apps_sorted[j]->curr_fair_share != fair_share && apps_sorted[j]->perf_history[fair_share] != 0)
	        apps_sorted[j]->curr_fair_share = fair_share;

	         uint64_t curr_perf = history[0];

	     if (apps_sorted[j]->times_allocated > 1) {
		  /* Compare current performance with previous performance, if this application
		   * has at least two items in history.  */

		  int prev_alloc_len = CPU_COUNT_S(rem_cpus_sz, apps_sorted[j]->cpuset[1]);
		  uint64_t prev_perf = apps_sorted[j]->perf_history[prev_alloc_len][0];

		/* Original decision making:
		 * * Change requested resources. */
         	  if (curr_perf > prev_perf && (curr_perf - prev_perf) / (double)prev_perf >= SAM_PERF_THRESH &&
									                         apps_sorted[j]->exploring) {
			  /* Keep going in the same direction. */
		                 printf("HILL CLIMBING [APP %6d] continuing in same direction \n", apps_sorted[j]->pid);
		                  if (prev_alloc_len < curr_alloc_len)
		                       per_app_cpu_budget[j] = MIN(per_app_cpu_budget[j] + SAM_PERF_STEP, cpuinfo->total_cpus);
			           else
			               per_app_cpu_budget[j] = MAX(per_app_cpu_budget[j] - SAM_PERF_STEP, SAM_MIN_CONTEXTS);
	            } else {
		   
      			    if (curr_perf < prev_perf && (prev_perf - curr_perf) / (double)prev_perf >= SAM_PERF_THRESH) {
			  
			   			  if (apps_sorted[j]->exploring) {
			                                /* Revert to previous count if performance reduction was great enough.
							   */
							   per_app_cpu_budget[j] = prev_alloc_len;
						     } else {
					 int guess = per_app_cpu_budget[j] + guess_optimization(per_app_cpu_budget[j], counter_order[i]);
					       guess = MAX(MIN(guess, cpuinfo->total_cpus), SAM_MIN_CONTEXTS);
					                apps_sorted[j]->exploring = true;
					                 per_app_cpu_budget[j] = guess;
					              }
				 printf("HILL CLIMBING [APP %6d] exploring %d -> %d\n", apps_sorted[j]->pid, curr_alloc_len, per_app_cpu_budget[j]);
		              } else {
			              apps_sorted[j]->exploring = false;
			              printf("HILL CLIMBING [APP %6d] exploring no more \n", apps_sorted[j]->pid);
			                  if (random() / (double)RAND_MAX <= SAM_DISTURB_PROB) {
			                      int guess = per_app_cpu_budget[j] + guess_optimization(per_app_cpu_budget[j], counter_order[i]);
			                      guess = MAX(MIN(guess, cpuinfo->total_cpus), SAM_MIN_CONTEXTS);
			                      apps_sorted[j]->exploring = true;
			                       per_app_cpu_budget[j] = guess;
					        printf("HILL CLIMBING [APP %6d] random disturbance: %d -> %d\n", apps_sorted[j]->pid, curr_alloc_len,
								                           per_app_cpu_budget[j]);
					      }
					}
			         }
                               /* save performance history */
	               memcpy(apps_sorted[j]->perf_history[curr_alloc_len], history,
						                     sizeof apps_sorted[j]->perf_history[curr_alloc_len]);
				            } else if (!apps_sorted[j]->exploring && random() / (double)RAND_MAX <= SAM_DISTURB_PROB) {
	                                        /* Introduce random disturbances.
						    */
				 int guess = per_app_cpu_budget[j] + guess_optimization(per_app_cpu_budget[j], counter_order[i]);
				                            guess = MAX(MIN(guess, cpuinfo->total_cpus), SAM_MIN_CONTEXTS);
						             apps_sorted[j]->exploring = true;
						             per_app_cpu_budget[j] = guess;
			printf("HILL CLIMBING [APP %6d] random disturbance: %d -> %d\n", apps_sorted[j]->pid, curr_alloc_len, per_app_cpu_budget[j]);
															              }
	               } else {
			    /* If this app has never been given an allocation, the first allocation we should
			     * give it is the fair share.  */

			                   per_app_cpu_budget[j] = fair_share;
			               printf("HILL CLIMBING [APP %6d] setting fair share \n", apps_sorted[j]->pid);
                              }
#else
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
                  per_app_cpu_budget[j] = MIN(per_app_cpu_budget[j] + 
										determine_step_size(counter_order[i], curr_alloc_len, 1), cpuinfo->total_cpus);
                else
                  per_app_cpu_budget[j] = MAX(per_app_cpu_budget[j] - 
										determine_step_size(counter_order[i], curr_alloc_len, -1), SAM_MIN_CONTEXTS);
              } else {
                if (curr_perf < prev_perf && (prev_perf - curr_perf) / (double)prev_perf >= SAM_PERF_THRESH &&
										(prev_alloc_len != curr_alloc_len)) {
                  if (apps_sorted[j]->exploring) {
                    /*
                     * Revert to previous count if performance reduction was great enough.
                     */
                    per_app_cpu_budget[j] = prev_alloc_len;
                  } else {
                    int guess = per_app_cpu_budget[j] + guess_optimization(per_app_cpu_budget[j], counter_order[i]);
                    guess = MAX(MIN(guess, cpuinfo->total_cpus), SAM_MIN_CONTEXTS);
                    apps_sorted[j]->exploring = true;
                    per_app_cpu_budget[j] = guess;
                  }
                  printf("[APP %6d] exploring %d -> %d\n", apps_sorted[j]->pid, curr_alloc_len, per_app_cpu_budget[j]);
                } else {
                  apps_sorted[j]->exploring = false;
                  printf("[APP %6d] exploring no more \n", apps_sorted[j]->pid);
                  if (random() / (double)RAND_MAX <= SAM_DISTURB_PROB) {
                    int guess = per_app_cpu_budget[j] + guess_optimization(per_app_cpu_budget[j], counter_order[i]);
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
              int guess = per_app_cpu_budget[j] + guess_optimization(per_app_cpu_budget[j], counter_order[i]);
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
            struct appinfo **candidates = new struct appinfo *[num_apps]();
            int *candidates_map = new int[num_apps]();
            int num_candidates = 0;
            int *spare_cores = new int[num_apps]();
            struct appinfo **spare_candidates = new struct appinfo *[num_apps]();
            int *spare_candidates_map = new int[num_apps]();
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
              int *amt_taken = new int[num_apps]();

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

              delete[] amt_taken;
            }

            if (needs_more[j] > 0)
              printf("[APP %6d] could not find %d extra contexts\n", apps_sorted[j]->pid, needs_more[j]);

            if (per_app_cpu_budget[j] < SAM_MIN_CONTEXTS) {
              fprintf(stderr, "%s:%d: APP %6d: per_app_cpu_budget[%d] (%d) < %d (SAM_MIN_CONTEXTS) !\n",
                      __FILE__, __LINE__, apps_sorted[j]->pid, j, per_app_cpu_budget[j], SAM_MIN_CONTEXTS);
              abort();
            }

            delete[] spare_cores;
            delete[] candidates;
            delete[] candidates_map;
            delete[] spare_candidates;
            delete[] spare_candidates_map;
          }

          if (per_app_cpu_budget[j] < SAM_MIN_CONTEXTS) {
            fprintf(stderr, "%s:%d: APP %6d: per_app_cpu_budget[%d] (%d) < %d (SAM_MIN_CONTEXTS) !\n",
                    __FILE__, __LINE__, apps_sorted[j]->pid, j, per_app_cpu_budget[j], SAM_MIN_CONTEXTS);
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
            (*budgeter_functions[met])(apps_sorted[j]->cpuset[0], new_cpuset, apps_sorted[j]->curr_bottleneck,
                                       apps_sorted[j]->prev_bottleneck, remaining_cpus, rem_cpus_sz,
                                       per_app_cpu_budget[j], per_app_socket_orders[j]);
          } else {
            budget_default(apps_sorted[j]->cpuset[0], new_cpuset, apps_sorted[j]->curr_bottleneck,
                           apps_sorted[j]->prev_bottleneck, remaining_cpus, rem_cpus_sz, per_app_cpu_budget[j],
                           per_app_socket_orders[j]);
          }

          /* subtract allocated cpus from remaining cpus,
           * [new_cpuset] is already a subset of [remaining_cpus] */
          CPU_XOR_S(rem_cpus_sz, remaining_cpus, remaining_cpus, new_cpuset);
          per_app_cpu_budget[j] = CPU_COUNT_S(rem_cpus_sz, new_cpuset);
          new_cpusets[j] = new_cpuset;
        }
      }

      /*
       * Iterate again. This time, apply the budgets.
       */
      for (int i = 0; i < N_METRICS; ++i) {
        if (i < num_counter_orders) {
          int met = counter_order[i];
          printf("%d apps sorted by %s:\n", range_ends[i + 1] - range_ends[i], metric_names[met]);
        } else {
          printf("%d apps unsorted:\n", range_ends[i + 1] - range_ends[i]);
        }

        for (int j = range_ends[i]; j < range_ends[i + 1]; ++j) {
          int *mybudget = NULL;
          size_t mybudget_l = 0;
          int *intlist = NULL;
          size_t intlist_l = 0;
          char cg_name[256];

          snprintf(cg_name, sizeof cg_name, SAM_CGROUP_NAME "/app-%d", apps_sorted[j]->pid);
          if (cg_read_intlist(cgroot, cntrlr, cg_name, "cpuset.cpus", &intlist, &intlist_l) != 0) {
              fprintf(stderr, "Failed to read APP %6d's cpuset.cpus: %s\n", apps_sorted[j]->pid,
                      strerror(errno));
              goto final_stage_failed;
          }

          cpuset_to_intlist(new_cpusets[j], cpuinfo->total_cpus, &mybudget, &mybudget_l);
          intlist_to_string(mybudget, mybudget_l, buf, sizeof buf, ",");

          if (i < num_counter_orders) {
            int met = counter_order[i];
            printf("\t[APP %6d] = %'" PRIu64 " (cpuset = %s)\n", apps_sorted[j]->pid, apps_sorted[j]->metric[met],
                   intlist_to_string(intlist, intlist_l, buf, sizeof buf, ","));
          } else {
            printf("\t[APP %6d] (cpuset = %s)\n", apps_sorted[j]->pid,
                   intlist_to_string(intlist, intlist_l, buf, sizeof buf, ","));
          }

          /* set the cpuset */
          if (mybudget_l > 0) {
            intlist_to_string(mybudget, mybudget_l, buf, sizeof buf, ",");
            if (cg_write_intlist(cgroot, cntrlr, cg_name, "cpuset.cpus", mybudget, mybudget_l) != 0) {
              fprintf(stderr, "\t\tfailed to set CPU budget to %s: %s\n", buf, strerror(errno));
            } else {
              /* save history */
              if (!CPU_EQUAL_S(rem_cpus_sz, apps_sorted[j]->cpuset[0], new_cpusets[j]) ||
                  (enum metric)i != apps_sorted[j]->curr_bottleneck) {
                memcpy(apps_sorted[j]->cpuset[1], apps_sorted[j]->cpuset[0], rem_cpus_sz);
                memcpy(apps_sorted[j]->cpuset[0], new_cpusets[j], rem_cpus_sz);
                apps_sorted[j]->prev_bottleneck = apps_sorted[j]->curr_bottleneck;
                apps_sorted[j]->curr_bottleneck = (enum metric)i;
              }

              apps_sorted[j]->times_allocated++;
              if ((int) mybudget_l == fair_share)
                apps_sorted[j]->curr_fair_share = mybudget_l;
              printf("\t\tset CPU budget to %s\n", buf);

							if (apps_sorted[j]->OMPvalid) {
								if (apps_sorted[j]->OMPptr) {
									apps_sorted[j]->OMPptr->numthreads = CPU_COUNT_S(rem_cpus_sz, apps_sorted[j]->cpuset[0]);
									apps_sorted[j]->OMPptr->valid_threads = 1;
									printf("[App %6d]: Updated the shared memory with %d CPUs \n", 
											apps_sorted[j]->pid, apps_sorted[j]->OMPptr->numthreads);
								}
							}
            }
          }

final_stage_failed:
          CPU_FREE(new_cpusets[j]);
          free(mybudget);
          free(intlist);
          free(per_app_socket_orders[j]);
        }
      }

      free(new_cpusets);
      free(apps_unsorted);
      free(apps_sorted);
      free(per_app_cpu_budget);
      free(needs_more);
      free(per_app_socket_orders);
    }

    CPU_FREE(remaining_cpus);

    /* reset app metrics and values */
    for (struct appinfo *an = apps_list; an; an = an->next) {
        memset(an->metric, 0, sizeof an->metric);
        memset(an->extra_metric, 0, sizeof an->extra_metric);
        memset(an->bottleneck, 0, sizeof an->bottleneck);
        memset(an->value, 0, sizeof an->value);
    }

    /* get iteration finish time */
    clock_gettime(CLOCK_MONOTONIC_RAW, &finish_time);

    printf("Elapsed time perf setup/start/stop/read: %.7f s, "
            "total: %.7f s\n",
            timespec_diff(&perf_start, &perf_finish),
            timespec_diff(&start_time, &finish_time));
  }

  printf("Stopping...\n");

  if (cg_remove_cgroup(cgroot, cntrlr, SAM_CGROUP_NAME) != 0)
    perror("Failed to remove cgroup");
END:
  printf("Exiting.\n");

  return 0;
}
