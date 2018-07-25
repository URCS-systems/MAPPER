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
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

using namespace std;
using std::string;
using std::vector;

#define MAX_GROUPS 10
#define TOT_CPUS 16
#define MAX_COUNTERS 50
#define MAX_APPS 50
#define TOT_COUNTERS 10
#define TOT_SOCKETS 2
#define TOT_PROCESSORS 10
#define TOT_CONTEXTS 20
// SAM
#define SHAR_PROCESSORS_CORE 20
#define SHAR_MEM_THRESH 100000000
#define SHAR_COHERENCE_THRESH 550000
#define SHAR_HCOH_THRESH 1100000
#define SHAR_REMOTE_THRESH 2700000
#define SHAR_CYCLES 2200000000.0
#define SHAR_IPC_THRESH 700
#define SHAR_COH_IND SHAR_COHERENCE_THRESH / 2
#define SHAR_PHY_CORE 10
#define SAM_MIN_CONTEXTS 1

#define PRINT_COUNT false

// Will be initialized anyway
int num_counter_orders = 6;

struct perf_counter {
    char name[128];
    double ratio;
    uint64_t vals[3];
    uint64_t delta;
    uint64_t seqno;
};

struct perf_data {
    int num_counters;
    struct perf_counter ctr[MAX_COUNTERS];
};

int thresh_pt[MAX_COUNTERS], limits_soc[MAX_COUNTERS], thresh_soc[MAX_COUNTERS];
int counter_order[MAX_COUNTERS];
int ordernum = 0;
int init_thresholds = 0;
int appIDmap[MAX_APPS];
struct PerApp {
    pid_t pid;
    unsigned long value[MAX_COUNTERS];
    double valshare[MAX_COUNTERS];
    int bottleneck[MAX_COUNTERS];
    int maxCPUS[MAX_COUNTERS];
    unsigned long metric[MAX_COUNTERS];
    int num_threads = 0;
    int active_threads = 0;
};
struct PerApp apps[MAX_APPS];
struct PerApp sockets[TOT_SOCKETS];
struct PerApp systemlvl;

struct countervalues {
    double ratio;
    uint64_t val, delta;
    uint64_t auxval1, auxval2;
    char name[64];
    uint64_t seqno;
};

struct options_t {
    const char *events[MAX_GROUPS];
    int num_groups;
    int format_group;
    int inherit;
    int print;
    int pin;
    pid_t pid;
    struct countervalues counters[MAX_COUNTERS];
    int countercount;
    int lock;
};

const char *metric_names[N_METRICS] = {[METRIC_ACTIVE] = "Active",
                                       [METRIC_AVGIPC] = "Average IPC",
                                       [METRIC_MEM] = "Memory",
                                       [METRIC_INTRA] = "Intra-socket communication",
                                       [METRIC_INTER] = "Inter-socket communication"};

struct appinfo {
    pid_t pid; /* application PID */
    uint64_t metric[N_METRICS];
    uint64_t bottleneck[N_METRICS];
    uint64_t value[MAX_COUNTERS];
    uint64_t refcount;
    cpu_set_t *cpuset;
    uint32_t appnoref;
    struct appinfo *prev, *next;
};

bool stoprun = false;
bool print_counters = false;

struct cpuinfo *cpuinfo;

struct appinfo **apps_array;
struct appinfo *apps_list;
int num_apps = 0;

void resetApps()
{
    for (int i = 0; i < MAX_APPS; i++) {
        for (int j = 0; j < MAX_COUNTERS; j++) {
            apps[i].value[j] = 0;
            apps[i].valshare[j] = 0;
            apps[i].bottleneck[j] = 0;
            apps[i].maxCPUS[j] = 0;
            apps[i].metric[j] = 0;
            apps[i].active_threads = 0;
            apps[i].num_threads = 0;
        }
    }
    return;
}

void deriveAppStatistics(int num)
{
    int i = 0;
    // Need to add reasoning about its current placement and number of hardware
    // contexts
    for (int appiter = 0; appiter < num; appiter++) {
        i = 0;
        apps[appiter].metric[0] = apps[appiter].value[0];
        i = 1; // Avg IPC
        apps[appiter].metric[i] = (apps[appiter].value[1] * 1000) / (1 + apps[appiter].value[0]);
        i = 2; // Mem
        apps[appiter].metric[i] = apps[appiter].value[8];
        i = 3; // snp
        apps[appiter].metric[i] =
            apps[appiter].value[7] - (apps[appiter].value[5] + apps[appiter].value[6]);
        i = 4;
        apps[appiter].metric[i] = apps[appiter].value[9];
        apps[appiter].pid = appIDmap[appiter];

        for (i = 0; i < ordernum; i++) {
            if (apps[appiter].bottleneck[counter_order[i]] > 0)
                std::cout << "App: " << appiter + 1 << '(' << apps[appiter].pid
                          << ") has bottleneck: " << counter_order[i]
                          << " with metric: " << apps[appiter].metric[counter_order[i]] << '\n';
            // Do the appropriate moving here. Do not move if already placed properly
        }
    }

    for (struct appinfo *an = apps_list; an; an = an->next) {
        /*an->metric[METRIC_ACTIVE] = an->value[0];
        an->metric[METRIC_AVGIPC] = (an->value[1] * 1000) / (1 + an->value[0]);
        an->metric[METRIC_MEM] = an->value[8];
        an->metric[METRIC_INTRA] = an->value[7] - (an->value[5] + an->value[6]);
        an->metric[METRIC_INTER] = an->value[9];*/

        an->metric[METRIC_ACTIVE] = apps[an->appnoref].metric[METRIC_ACTIVE];
        an->metric[METRIC_AVGIPC] = apps[an->appnoref].metric[METRIC_AVGIPC];
        an->metric[METRIC_MEM] = apps[an->appnoref].metric[METRIC_MEM];
        an->metric[METRIC_INTRA] = apps[an->appnoref].metric[METRIC_INTRA];
        an->metric[METRIC_INTER] = apps[an->appnoref].metric[METRIC_INTER];

        for (int i = 0; i < num_counter_orders; ++i) {
            if (an->bottleneck[counter_order[i]] > 0) {
                printf("Added: App %d has bottleneck %s with metric %" PRIu64 "\n", an->pid,
                       metric_names[counter_order[i]], an->metric[counter_order[i]]);
            }
        }
    }
    return;
}
static int compare_apps_by_metric(const void *a_ptr, const void *b_ptr, void *arg)
{
    const struct appinfo *a = *(struct appinfo *const *)a_ptr;
    const struct appinfo *b = *(struct appinfo *const *)b_ptr;
    int met = *(int *)arg;

    return (int)((long)b->metric[met] - (long)a->metric[met]);
}

void sigterm_handler(int sig) { stoprun = true; }
void siginfo_handler(int sig) { print_counters = !print_counters; }

static void manage(pid_t pid, pid_t app_pid, int appno_in)
{
    /* add to apps list and array */

    if (!apps_array[app_pid]) {
        struct appinfo *anode = (struct appinfo *)calloc(1, sizeof *anode);

        anode->pid = app_pid;
        anode->refcount = 1;
        anode->appnoref = appno_in;
        anode->next = apps_list;
        anode->cpuset = CPU_ALLOC(cpuinfo->total_cpus);
        if (apps_list) apps_list->prev = anode;
        apps_list = anode;
        apps_array[app_pid] = anode;
        num_apps++;
    } else
        apps_array[app_pid]->refcount++;
    std::cout << "Manage (" << pid << ", " << app_pid << ", " << appno_in << ") done \n";
}

static void unmanage(pid_t pid, pid_t app_pid)
{
    /* remove from array and unlink */
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

        if (anode == apps_list) apps_list = anode->next;

        CPU_FREE(anode->cpuset);
        free(anode);
    }
}

int SharGetDir(string dir, string taskname, std::map<int, int> &files)
{
    DIR *dp;
    struct dirent *dirp;
    int tmpchild = 0;
    if ((dp = opendir(dir.c_str())) == NULL) {
        std::cout << "Error(" << errno << ") opening " << dir << std::endl;
        return errno;
    }

    while ((dirp = readdir(dp)) != NULL) {
        if (!(string(dirp->d_name).compare(".") == 0 || string(dirp->d_name).compare("..") == 0))
            files.insert(std::pair<int, int>(atoi(dirp->d_name), 0));
    }

    string childfilename = "/proc/" + taskname + "/task/" + taskname + "/children";
    std::ifstream childrd(childfilename);
    std::cout << "Children traversal ";

    if (childrd.is_open()) {
        while (childrd >> tmpchild) {
            std::cout << tmpchild << '\t';
            files.insert(std::pair<int, int>(tmpchild, 0));
        }
        childrd.close();
    }
    closedir(dp);

    std::cout << "\n";
    return 0;
}

int SharGetDescendants(string dirpath, string taskname, std::map<int, int> &files, int exec)
{
    SharGetDir(dirpath, taskname, files);

    std::map<int, int>::iterator i = files.find(stoi(taskname));
    if (i != files.end())
        i->second = exec;
    else
        std::cout << "Unexpected error " << taskname << " not found \n";

    int proceed = 0;
    /* Proceed = 0, dont consider
       Proceed = 1, just matched, still dont consider
       Proceed = 2, Go ahead */
    string tempfile;

    std::cout << "Get Descendants " << taskname << "::  "
              << "eXEC: " << exec;
    for (i = files.begin(); i != files.end(); ++i) {
        // TO DO: Have to avoid reiteration of parents
        tempfile = std::to_string(i->first);
        std::cout << tempfile << '\t';

        if (tempfile.compare(taskname) == 0) proceed = 1;
        if (proceed == 2 && i->second == 0) {
            string filename = "/proc/" + tempfile + "/task";
            SharGetDescendants(filename, tempfile, files, exec);
        }
        if (proceed == 1) proceed = 2;
    }
    std::cout << '\n';
    return 0;
}

class PerfData
{
  public:
    void initialize(int tid, int app_tid, int appno_in);
    ~PerfData();
    void readCounters(int index);  // argument added for copying
    void printCounters(int index); // argument added for copying

    int init;
    struct options_t *options;
    int memfd;     // file descriptor, from shm_open()
    char *membase; // base address, from mmap()
    int touch;
    int pid;
    int apppid;
    size_t memsize;
    int appid;
    int bottleneck[MAX_COUNTERS];
    int active;
    double val[MAX_COUNTERS];
};

void PerfData::initialize(int tid, int app_tid, int appno_in)
{
    std::cout << "Opening shared memmory for thread TID " << tid << std::endl;
    std::string pidnum = std::to_string(tid);
    pid = tid;
    memsize = sizeof(struct options_t);
    std::cout << "Opening shared memmory for thread " << pidnum << std::endl;
    memfd = shm_open(pidnum.c_str(), O_CREAT | O_RDWR, 0666);
    if (memfd == -1) {
        printf("TID: %s Shared memory failed: %s\n", pidnum.c_str(), strerror(errno));
        exit(1);
    }
    /* configure the size of the shared memory segment */
    ftruncate(memfd, memsize);
    std::string commandstring = "sudo /u/srikanth/libpfm-4.9.0/perf_examples/./PerTask -t ";
    commandstring = commandstring + pidnum + " &";

    std::cout << "Command to be executed " << commandstring.c_str() << std::endl;
    /* map the shared memory segment to the address space of the process */
    membase = (char *)mmap(0, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (membase == MAP_FAILED) {
        printf("cons: Map failed: %s\n", strerror(errno));
        exit(1);
    }
    appid = 0;
    // system(commandstring.c_str());
    options = (struct options_t *)membase;
    apppid = app_tid;
    manage(tid, app_tid, appno_in);
    std::cout << "Done with manage \n";
    init = 1;
    return;
}
void PerfData::printCounters(int index)
{
    // code added to populate PerfData per thread options->counter.delta with
    // values
    options->counters[0].delta = THREADS.event[index][0]; // UNHALTED_CYCLES
    options->counters[1].delta = THREADS.event[index][1]; // INSTR ;
    options->counters[5].delta = THREADS.event[index][6]; // L3_MISSES
    options->counters[6].delta = THREADS.event[index][7]; // L3_HIT
    options->counters[7].delta = THREADS.event[index][5]; // L2_MISSES
    options->counters[8].delta = THREADS.event[index][4]; // LLC_MISSES
    options->counters[9].delta = THREADS.event[index][2]; // REMOTE_HITM
    //	                           THREADS.event[index][3]; //REMOTE DRAM

    if (PRINT_COUNT) // set to false in Macro to disable
    {
        printf("==============PrintCounters========\n");
        printf(" TID:%d\n", THREADS.tid[index]);
        printf("UNHALTED CYCLES: %'" PRIu64 "\n",
               options->counters[0].delta);                            // UNHALTED_CYCLES
        printf("INSTRUCTIONS: %'" PRIu64 "\n", options->counters[1].delta);    // INSTR ;
        printf("L3 MISSES: %'" PRIu64 "\n", options->counters[5].delta);       // L3_MISSES
        printf("L3 HITS: %'" PRIu64 "\n", options->counters[6].delta);         // L3_HIT
        printf("L2 MISSES: %'" PRIu64 "\n", options->counters[7].delta);       // L2_MISSES
        printf("LLC MISSES: %'" PRIu64 "\n", options->counters[8].delta);      // LLC_MISSES
        printf("REMOTE_HITM: %'" PRIu64 "\n", options->counters[9].delta);     // REMOTE_HITM
        /*printf("REMOTE DRAM: %" PRIu64 "\n", options->counters[2].delta);*/ // REMOTE_DRAM
    }

    //

    int i;
    int num = options->countercount;
    active = 0;

    for (i = 0; i < num; i++) {

        printf("%'20" PRIu64 " %'20" PRIu64 " %s (%.2f%% scaling, ena=%'" PRIu64 ", run=%'" PRIu64
               ")\n",
               options->counters[i].val, options->counters[i].delta, options->counters[i].name,
               (1.0 - options->counters[i].ratio) * 100.0, options->counters[i].auxval1,
               options->counters[i].auxval2);
        bottleneck[i] = 0;
        apps[appid].value[i] += options->counters[i].delta;
        if (apps_array[apppid]) apps_array[apppid]->value[i] += apps[appid].value[i];
    }

    i = 0;
    if (options->counters[i].delta > (uint64_t)thresh_pt[i]) {
        active = 1;
        val[i] = options->counters[i].delta;
        bottleneck[i] = 1;
        if (apps_array[apppid]) apps_array[apppid]->bottleneck[i] += 1;
        apps[appid].bottleneck[i] += 1;
    }

    i = 1;
    if ((options->counters[i].delta * 1000) / (1 + options->counters[0].delta) >
        (uint64_t)thresh_pt[i]) {
        val[i] = (1000 * options->counters[1].delta) / (options->counters[0].delta + 1);
        bottleneck[i] = 1;
        apps[appid].bottleneck[i] += 1;
        if (apps_array[apppid]) apps_array[apppid]->bottleneck[i] += 1;
        std::cout << " Detected counter " << i << '\n';
    }

    i = 2; // Mem
    long tempvar = ((SHAR_CYCLES * options->counters[8].delta) / (options->counters[0].delta + 1));
    if (tempvar > thresh_pt[i]) {
        val[i] = tempvar;
        bottleneck[i] = 1;
        apps[appid].bottleneck[i] += 1;
        if (apps_array[apppid]) apps_array[apppid]->bottleneck[i] += 1;
        std::cout << " Detected counter " << i << '\n';
    }

    i = 3; // snp
    tempvar = ((SHAR_CYCLES * (options->counters[7].delta -
                               (options->counters[6].delta + options->counters[5].delta))) /
               (options->counters[0].delta + 1));
    if (tempvar > thresh_pt[i]) {
        val[i] = tempvar;
        bottleneck[i] = 1;
        apps[appid].bottleneck[i] += 1;
        if (apps_array[apppid]) apps_array[apppid]->bottleneck[i] += 1;
        std::cout << " Detected counter " << i << '\n';
    }

    i = 4; // cross soc
    tempvar = ((SHAR_CYCLES * options->counters[9].delta) / (options->counters[0].delta + 1));
    if (tempvar > thresh_pt[i]) {
        val[i] = tempvar;
        bottleneck[i] = 1;
        apps[appid].bottleneck[i] += 1;
        if (apps_array[apppid]) apps_array[apppid]->bottleneck[i] += 1;
        std::cout << " Detected counter " << i << '\n';
    }
    std::cout << "Updated the app arrays too \n";
}
void PerfData::readCounters(int index)
{
    printf("\nreadCounters PID:: %d App ID %d \n", pid, appid);
    if (pid == 0) // options->pid
    {
        return;
    }
    if (kill(pid, 0) < 0) // options->pid
        if (errno == ESRCH) {
            printf("Process %d does not exist \n", pid);
            return;
        } else
            printf("Hmmm what the hell happened??? \n");
    else
        printf(" Process exists so just continuing \n");

    //  printf("ReadCounter printCounters\n");
    printCounters(index);
    return;
}

PerfData::~PerfData()
{
    /* remove the mapped shared memory segment from the address space of the
     * process */
    if (munmap(membase, memsize) == -1) {
        printf("PID %d Unmap failed: %s\n", pid, strerror(errno));
        exit(1);
    }

    /* close the shared memory segment as if it was a file */
    if (close(memfd) == -1) {
        printf("TID %d Close failed: %s\n", pid, strerror(errno));
        exit(1);
    }
    unmanage(pid, apppid);
    std::cout << " Done with unmanage for " << pid << " of application PID " << apppid << '\n';
    /* remove the shared memory segment from the file system
    if (shm_unlink(memname) == -1) {
        printf("TID %s Error removing %s\n", memname, strerror(errno));
        exit(1);
    } */
}

std::map<int, PerfData *> perfdata;
int main(int argc, char *argv[])
{
    int init_error = 0;
    int appno = 1;
    std::map<int, int> files;
    struct dirent *de;
    std::map<int, int>::iterator i;
    std::map<int, PerfData *>::iterator perfiter;
    int appiter = 0;
    DIR *dr;
    const char *cgroot = "/sys/fs/cgroup";
    const char *cntrlr = "cpuset";

    setlocale(LC_ALL, "");

    if (geteuid() != 0) {
        fprintf(stderr, "I need root access for %s\n", cgroot);
        return 1;
    }

    if (mkdir(SAM_RUN_DIR, 0) < 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create %s: %s\n", SAM_RUN_DIR, strerror(errno));
        exit(EXIT_FAILURE);
    }


    // Code Added
    pid_t tid[200];
    int this_index = 0;
    // Initialize what event we want
    initialize_events();

    signal(SIGTERM, &sigterm_handler);
    signal(SIGQUIT, &sigterm_handler);
    signal(SIGINT, &sigterm_handler);
    signal(SIGUSR1, &siginfo_handler);

    cpu_set_t *remaining_cpus;
    size_t rem_cpus_sz;
    if (init_thresholds == 0) {
        thresh_pt[METRIC_ACTIVE] = 1000000; // cycles
        thresh_pt[METRIC_AVGIPC] = 70;      // instructions scaled to 100
        thresh_pt[METRIC_MEM] = SHAR_MEM_THRESH / SHAR_PROCESSORS_CORE; // Mem
        thresh_pt[METRIC_INTRA] = SHAR_COHERENCE_THRESH;
        thresh_pt[METRIC_INTER] = SHAR_COHERENCE_THRESH;
        thresh_pt[METRIC_REMOTE] = SHAR_REMOTE_THRESH;

        ordernum = 0;
        counter_order[ordernum++] = METRIC_INTER; // LLC_MISSES
        counter_order[ordernum++] = METRIC_INTRA;
        counter_order[ordernum++] = METRIC_MEM;
        counter_order[ordernum++] = METRIC_AVGIPC;
        num_counter_orders = ordernum;
        std::cout << " Ordering of counters by piority: " << ordernum;

        /* create array */
        FILE *pid_max_fp;
        int pid_max = 0;

        if (!(pid_max_fp = fopen("/proc/sys/kernel/pid_max", "r")) ||
            fscanf(pid_max_fp, "%d", &pid_max) != 1) {
            perror("Could not get pid_max");
            return 1;
        }
        printf("pid_max = %d\n", pid_max);
        apps_array = (struct appinfo **)calloc(pid_max, sizeof *apps_array);
        fclose(pid_max_fp);

        /* get CPU topology */
        if ((cpuinfo = get_cpuinfo())) {
            printf("CPU Info\n========\n");
            printf("Max clock rate: %lu Hz\n", cpuinfo->clock_rate);
            printf("Topology: %d threads across %d sockets:\n", cpuinfo->total_cpus,
                   cpuinfo->num_sockets);
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

        /* create cgroup */
        char *mems_string = NULL;
        char *cpus_string = NULL;
        if (cg_create_cgroup(cgroot, cntrlr, "sam") < 0 && errno != EEXIST) {
            perror("Failed to create cgroup");
            init_error = -1;
            goto END;
        }

        if (cg_read_string(cgroot, cntrlr, ".", "cpuset.mems", &mems_string) < 0 ||
            cg_write_string(cgroot, cntrlr, "sam", "cpuset.mems", "%s", mems_string) < 0 ||
            cg_read_string(cgroot, cntrlr, ".", "cpuset.cpus", &cpus_string) < 0 ||
            cg_write_string(cgroot, cntrlr, "sam", "cpuset.cpus", "%s", cpus_string) < 0) {
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
    if (init_error == -1) goto END;
RESUME:
    appno = 1;
    files.clear();
    resetApps();
    dr = opendir(SAM_RUN_DIR);

    if (dr == NULL) {
        printf("Could not open " SAM_RUN_DIR);
        return 0;
    }

    while ((de = readdir(dr)) != NULL) {
        if (!((strcmp(de->d_name, ".") == 0) || (strcmp(de->d_name, "..") == 0))) {
            printf("%s\n", de->d_name);

            string appname(de->d_name);
            string filename = "/proc/" + appname + "/task";
            std::cout << " Container launcher PID " << atoi(de->d_name) << " " << filename << "\n";
            SharGetDescendants(filename, appname, files, appno);
            // appmap.insert(std::pair<int, int>(appno-1, atoi(de->d_name)));
            appIDmap[appno - 1] = atoi(de->d_name);
            appno++;
        }
    }
    appiter = 0;

    std::cout << "\n PIDs tracked: ";
    // Code added
    this_index = 0;
    for (i = files.begin(); i != files.end(); ++i) {
        tid[this_index++] = i->first;
        printf("TID:%d\n", i->first);
    }

    // Comment added by Sayak Chakraborti:
    count_event_perfMultiplex(
        tid,
        this_index); // Comment added by Sayak Chakraborti, call this function to
                     // count for all tids for a particular interval of time
    displayTIDEvents(tid,
                     this_index); // required to copy values to my data structures

    for (i = files.begin(); i != files.end(); ++i) {

        int pid = i->first;
        int my_index = searchTID(pid); // Code added to get index in the THREADS data structure
        //  printf("Main my_index:%d\n",my_index);

        std::cout << " " << i->first;
        perfiter = perfdata.find(pid);

        if (perfiter == perfdata.end()) {
            std::cout << pid << " is not found in perfdata. Adding it under " << i->second << "\n";

            PerfData *perfcreate = new PerfData;
            perfcreate->initialize(pid, appIDmap[((i->second) - 1)], i->second - 1);
            perfcreate->appid = i->second - 1;
            perfdata.insert(std::pair<int, PerfData *>(pid, perfcreate));
            perfiter = perfdata.find(pid);
            perfcreate = NULL;
        }
        PerfData *temp;

        temp = perfiter->second;
        temp->appid = i->second - 1;
        std::cout << "PID: " << pid << "App ID: " << temp->appid << '\n';

        if (my_index != -1) temp->readCounters(my_index);

        temp->touch = 1;
        appiter++;
    }

    for (perfiter = perfdata.begin(); perfiter != perfdata.end();) {

        int flag_touch = 0;
        if (perfiter->second->touch == 0) {
            std::cout << perfiter->first << " has not been touched this round. Deleting it.\n";
            if (kill(perfiter->first, 0) < 0) {
                if (errno == ESRCH)
                    printf("Process %d  %d does not exist \n", perfiter->first,
                           perfiter->second->options->pid);
                else
                    printf("Hmmm what the hell happened??? \n");
            } else
                printf(" Process exists so just continuing \n");

            delete perfiter->second;
            perfiter = perfdata.erase(perfiter);
            flag_touch = 1;
        }

        if (flag_touch == 0) {
            perfiter->second->touch = 0;
            ++perfiter;
        }
    }
    closedir(dr);
    deriveAppStatistics(appno - 1);

    /* map applications */
    remaining_cpus = CPU_ALLOC(cpuinfo->total_cpus);
    rem_cpus_sz = CPU_ALLOC_SIZE(cpuinfo->total_cpus);

    CPU_ZERO_S(rem_cpus_sz, remaining_cpus);
    for (int i = 0; i < cpuinfo->total_cpus; ++i)
        CPU_SET_S(i, rem_cpus_sz, remaining_cpus);

    if (num_apps > 0) {
        const float budget_f = cpuinfo->total_cpus / (float)num_apps;
        const int per_app_cpu_budget = MAX(ceilf(budget_f), SAM_MIN_CONTEXTS);
        struct appinfo **apps_unsorted = (struct appinfo **)calloc(num_apps, sizeof *apps_unsorted);
        struct appinfo **apps_sorted = (struct appinfo **)calloc(num_apps, sizeof *apps_sorted);
        char cg_name[256];
        char buf[256];

        int range_ends[N_METRICS + 1] = {0};

        {
            int i = 0;
            for (struct appinfo *an = apps_list; an; an = an->next) {
                apps_unsorted[i++] = an;
            }
        }

        /*
         * apps_sorted[] will look like:
         * [ apps sorted by bottleneck 1 | apps sorted by bottleneck 2 | ... |
         * remaining ]
         */
        for (int i = 0; i < N_METRICS; ++i) {
            range_ends[i + 1] = range_ends[i];
            for (int j = 0; j < num_apps; ++j) {
                if (apps_unsorted[j] && (i >= num_counter_orders ||
                                         apps_unsorted[j]->bottleneck[counter_order[i]] > 0)) {
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
                qsort_r(&apps_sorted[range_ends[i]], range_ends[i + 1] - range_ends[i],
                        sizeof *apps_sorted, &compare_apps_by_metric, (void *)&met);

                printf("%d apps sorted by %s:\n", range_ends[i + 1] - range_ends[i],
                       metric_names[met]);
            } else {
                printf("%d apps unsorted:\n", range_ends[i + 1] - range_ends[i]);
            }

            for (int j = range_ends[i]; j < range_ends[i + 1]; ++j) {
                cpu_set_t *new_cpuset;
                int *intlist = NULL;
                size_t intlist_l = 0;

                new_cpuset = CPU_ALLOC(cpuinfo->total_cpus);
                CPU_ZERO_S(rem_cpus_sz, new_cpuset);

                snprintf(cg_name, sizeof cg_name, "sam/app-%d", apps_sorted[j]->pid);
                cg_read_intlist(cgroot, cntrlr, cg_name, "cpuset.cpus", &intlist, &intlist_l);

                if (i < num_counter_orders) {
                    int met = counter_order[i];
                    printf("\t[APP %5d] = %" PRIu64 " (cpuset = %s)\n", apps_sorted[j]->pid,
                           apps_sorted[j]->metric[met],
                           intlist_to_string(intlist, intlist_l, buf, sizeof buf, ","));
                    /*
                     * compute the CPU budget for this application, given its bottleneck
                     * [met]
                     */
                    if (met == METRIC_INTER || met == METRIC_INTRA)
                        budget_collocate(apps_sorted[j]->cpuset, new_cpuset, remaining_cpus,
                                         rem_cpus_sz, per_app_cpu_budget);
                    if (met == METRIC_MEM)
                        budget_spread(apps_sorted[j]->cpuset, new_cpuset, remaining_cpus,
                                      rem_cpus_sz, per_app_cpu_budget);
                    if (met == METRIC_AVGIPC)
                        budget_no_hyperthread(apps_sorted[j]->cpuset, new_cpuset, remaining_cpus,
                                              rem_cpus_sz, per_app_cpu_budget);
                    /* (*budgeter_functions[counter_order[i]])(apps_sorted[j]->cpuset,
                            new_cpuset, remaining_cpus, rem_cpus_sz,
                       per_app_cpu_budget);*/
                } else {
                    printf("\t[APP %5d] (cpuset = %s)\n", apps_sorted[j]->pid,
                           intlist_to_string(intlist, intlist_l, buf, sizeof buf, ","));

                    budget_default(apps_sorted[j]->cpuset, new_cpuset, remaining_cpus, rem_cpus_sz,
                                   per_app_cpu_budget);
                }

                /* subtract allocated cpus from remaining cpus,
                 * [new_cpuset] is already a subset of [remaining_cpus] */
                CPU_XOR_S(rem_cpus_sz, remaining_cpus, remaining_cpus, new_cpuset);

                int *mybudget = NULL;
                int mybudget_l = 0;

                cpuset_to_intlist(new_cpuset, cpuinfo->total_cpus, &mybudget, &mybudget_l);
                intlist_to_string(mybudget, mybudget_l, buf, sizeof buf, ",");

                /* set the cpuset */
                if (mybudget_l > 0) {
                    if (cg_write_intlist(cgroot, cntrlr, cg_name, "cpuset.cpus", mybudget,
                                         mybudget_l) != 0) {
                        fprintf(stderr, "\t\tfailed to set CPU budget to %s: %s\n", buf,
                                strerror(errno));
                    } else {
                        memcpy(apps_sorted[j]->cpuset, new_cpuset, rem_cpus_sz);
                        printf("\t\tset CPU budget to %s\n", buf);
                    }
                }

                CPU_FREE(new_cpuset);
                free(intlist);
                free(mybudget);
            }
        }

        free(apps_unsorted);
        free(apps_sorted);
    }

    CPU_FREE(remaining_cpus);

    /* reset app metrics and values */
    for (struct appinfo *an = apps_list; an; an = an->next) {
        memset(an->metric, 0, sizeof an->metric);
        memset(an->bottleneck, 0, sizeof an->bottleneck);
        memset(an->value, 0, sizeof an->value);
    }
    sleep(1);
    if (stoprun == false)
        goto RESUME;
    else
        std::cout << "Stopping .. \n";
END:
    std::cout << " Exiting .. \n";
}
