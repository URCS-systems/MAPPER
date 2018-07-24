#include <errno.h>
#include <signal.h>
#include <stdlib.h> // For random().
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <locale.h>
#include <err.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <dirent.h>
 
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include <fstream>
#include <thread>
#include <iostream>
#include <map>

#include <stddef.h>
#include <sched.h>

#include <boost/filesystem.hpp>
#include "cgroup.h"
#include "budgets.h"
#include "util.h"

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
#define SHAR_COH_IND SHAR_COHERENCE_THRESH/2
#define SHAR_PHY_CORE 10

enum metric {
	METRIC_ACTIVE,
    METRIC_AVGIPC,
    METRIC_MEM,
    METRIC_INTRA,
    METRIC_INTER,
    N_METRICS
};
// Will be initialized anyway
int num_counter_orders = 6;

struct cpu_socket {
  struct cpu *cpus;
  int num_cpus;
};

struct cpu {
  int core_id;
  int sock_id;
  int tnumber; // thread number
};

struct cpuinfo {
  struct cpu_socket *sockets;
  int num_sockets;
  int total_cpus;
  int total_cores;
  unsigned long clock_rate;
};

struct cpuinfo *get_cpuinfo(void);

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

struct PerApp
{
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
	// Need to add reasoning about its current placement and number of hardware contexts
	for (int appiter = 0; appiter < num; appiter++) {
		i = 0;
		apps[appiter].metric[0] = apps[appiter].value[0];
		i = 1; // Avg IPC
		apps[appiter].metric[i] = (apps[appiter].value[i] * 1000)/(1 + apps[appiter].value[0]);	
		i = 2; // Mem
		apps[appiter].metric[i] = apps[appiter].value[8];
		i = 3; // snp
		apps[appiter].metric[i] = apps[appiter].value[7] - (apps[appiter].value[5] + apps[appiter].value[6]);
		i = 4;
		apps[appiter].metric[i] = apps[appiter].value[9];
		
		for (i = 0; i < ordernum; i++) {
			if (apps[appiter].bottleneck[counter_order[i]] > 0) 
				std::cout << "App: " << appiter + 1 << " has bottleneck: " 
						<< counter_order[i] << " with metric: " << apps[appiter].metric[counter_order[i]] << '\n';
			// Do the appropriate moving here. Do not move if already placed properly
		}
	}
	return;
}

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

const char *metric_names[N_METRICS] = {
    [METRIC_ACTIVE] = "Active",
    [METRIC_AVGIPC] = "Average IPC",
    [METRIC_MEM] = "Memory",
    [METRIC_INTRA] = "Intra-socket communication",
    [METRIC_INTER] = "Inter-socket communication"
};


struct appinfo {
    pid_t pid;                  /* application PID */
    uint64_t metric[N_METRICS];
    uint64_t bottleneck[N_METRICS];
    uint64_t value[MAX_COUNTERS];
    uint64_t refcount;
    cpu_set_t *cpuset;
    struct appinfo *prev, *next;
};

bool stoprun = false;
bool print_counters = false;

struct cpuinfo *cpuinfo;

struct appinfo **apps_array;
struct appinfo *apps_list;
int num_apps = 0;

static int 
compare_apps_by_metric(const void *a_ptr, 
                       const void *b_ptr, 
                       unsigned int arg) {
    const struct appinfo *a = *(struct appinfo *const *) a_ptr;
    const struct appinfo *b = *(struct appinfo *const *) b_ptr;
    unsigned int met = arg;

    return (int) ((long) b->metric[met] - (long) a->metric[met]);
}

void sigterm_handler(int sig) { stoprun = true; }
void siginfo_handler(int sig) { print_counters = !print_counters; }

static void manage(pid_t pid, pid_t app_pid) { 
    /* add to apps list and array */

    if (!apps_array[app_pid]) {
        struct appinfo *anode = (struct appinfo*) calloc(1, sizeof *anode);

        anode->pid = app_pid;
        anode->refcount = 1;
        anode->next = apps_list;
        anode->cpuset = CPU_ALLOC(cpuinfo->total_cpus);
        if (apps_list)
            apps_list->prev = anode;
        apps_list = anode;
        apps_array[app_pid] = anode;
        num_apps++;
    } else
        apps_array[app_pid]->refcount++;
}

static void unmanage(pid_t pid, pid_t app_pid) {
    /* remove from array and unlink */
    if (apps_array[app_pid]) {
        assert(apps_array[app_pid]->refcount > 0);
        apps_array[app_pid]->refcount--;
    }

    if (apps_array[app_pid]
     && apps_array[app_pid]->refcount == 0) {
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

        CPU_FREE(anode->cpuset);
        free(anode);
    }
}

int SharGetDir(string dir, string taskname, std::map<int,int> &files)
{
    DIR *dp;
    struct dirent *dirp;
    int tmpchild = 0;
    if((dp  = opendir(dir.c_str())) == NULL) {
        std::cout << "Error(" << errno << ") opening " << dir << std::endl;
        return errno;
    }

    while ((dirp = readdir(dp)) != NULL) {
        if (!(string(dirp->d_name).compare(".") == 0 ||
                string(dirp->d_name).compare("..") == 0))
            files.insert(std::pair<int,int>(atoi(dirp->d_name),0));
    }

    string childfilename = "/proc/" + taskname +
        "/task/" +  taskname + "/children";
    std::ifstream childrd(childfilename);
    std::cout << "Children traversal ";

    if (childrd.is_open()) {
        while(childrd >> tmpchild) {
            std::cout << tmpchild << '\t';
            files.insert(std::pair<int,int>(tmpchild,0));
        }
        childrd.close();
    }
    closedir(dp);

    std::cout << "\n";
    return 0;
}

int SharGetDescendants(string dirpath, string taskname, std::map<int,int> &files, int exec)
{
    SharGetDir(dirpath, taskname, files);

    std::map<int,int>::iterator i = files.find(stoi(taskname)); 
    if (i != files.end())
            i->second = exec;
    else
        std::cout << "Unexpected error " << taskname << " not found \n";

    int proceed = 0; 
    /* Proceed = 0, dont consider
       Proceed = 1, just matched, still dont consider
       Proceed = 2, Go ahead */
    string tempfile; 

    std::cout << "Get Descendants " << taskname << "::  " << "eXEC: " << exec; 
    for (i = files.begin(); i != files.end(); ++i) {
        // TO DO: Have to avoid reiteration of parents
        tempfile = std::to_string(i->first);
        std::cout << tempfile << '\t'; 

        if (tempfile.compare(taskname) == 0)
            proceed = 1;
        if (proceed == 2 && i->second == 0) {
            string filename = "/proc/" + tempfile + "/task";
            SharGetDescendants(filename, tempfile, files, exec);
        }
        if (proceed == 1)
            proceed = 2;
    }
   	std::cout << '\n';
    return 0;
}

class PerfData
{
public:
    void initialize(int tid);
    ~PerfData();
    void readCounters();
    void printCounters();

    int init;
    struct options_t *options;
    int memfd;       // file descriptor, from shm_open()
    char *membase;   // base address, from mmap()
    int touch;
    int pid;
    size_t memsize;
    int appid;
    int bottleneck[MAX_COUNTERS];
    int active;
    double val[MAX_COUNTERS];
};

void PerfData::initialize(int tid)
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
    membase = (char*) mmap(0, memsize, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (membase == MAP_FAILED) {
        printf("cons: Map failed: %s\n", strerror(errno));
        exit(1);
    }
    appid = 0;
    system(commandstring.c_str());
    options = (struct options_t*) membase;
    init = 1;
    return;
}
void PerfData::printCounters()
{
    int i;
    int num = options->countercount;
    active = 0;
    for(i=0; i < num; i++) {

        printf("%'20" PRIu64 " %'20" PRIu64 " %s (%.2f%% scaling, ena=%'" PRIu64 ", run=%'" PRIu64 ")\n",
        	options->counters[i].val,
            options->counters[i].delta,
            options->counters[i].name,
            (1.0-options->counters[i].ratio)*100.0,
            options->counters[i].auxval1,
            options->counters[i].auxval2);
        bottleneck[i] = 0;
		apps[appid].value[i] += options->counters[i].delta;
    }
   
   	i = 0;
	if (options->counters[i].delta > (uint64_t)thresh_pt[i]) {
		active = 1;
		val[i] = options->counters[i].delta;
		bottleneck[i] = 1;
		apps[appid].bottleneck[i] += 1;
	}

	i = 1;
	if ((options->counters[i].delta * 1000)/(1 + options->counters[0].delta) > (uint64_t)thresh_pt[i]) {
		val[i] = (1000 * options->counters[1].delta)/(options->counters[0].delta+1);
		bottleneck[i] = 1;
		apps[appid].bottleneck[i] += 1;
		std::cout << " Detected counter " << i << '\n';
	}

	i = 2; // Mem
	long tempvar = ((SHAR_CYCLES * options->counters[8].delta)/(options->counters[0].delta + 1));
	if (tempvar > thresh_pt[i]) {
		val[i] = tempvar;
		bottleneck[i] = 1;
		apps[appid].bottleneck[i] += 1;
		std::cout << " Detected counter " << i << '\n';
	}

	i = 3; // snp
	tempvar = ((SHAR_CYCLES * (options->counters[7].delta - (options->counters[6].delta + options->counters[5].delta)))/(options->counters[0].delta + 1));
	if (tempvar > thresh_pt[i]) {
		val[i] = tempvar;
		bottleneck[i] = 1;
		apps[appid].bottleneck[i] += 1;
		std::cout << " Detected counter " << i << '\n';
	}
	
	i = 4; // cross soc
	tempvar = ((SHAR_CYCLES * options->counters[9].delta)/(options->counters[0].delta + 1));
	if (tempvar > thresh_pt[i]) {
		val[i] = tempvar;
		bottleneck[i] = 1;
		apps[appid].bottleneck[i] += 1;
		std::cout << " Detected counter " << i << '\n';
	}
}
void PerfData::readCounters()
{
    printf ("\n PID:: %d App ID %d \n", options->pid, appid);
    if (options->pid == 0)
    {
        return;
    }
    if (kill(options->pid, 0) < 0)
        if (errno == ESRCH) {
            printf ("Process %d does not exist \n", options->pid);
            return;
        }
        else
            printf ("Hmmm what the hell happened??? \n");
    else
       printf(" Process exists so just continuing \n");
    printCounters();
    return;
}

PerfData::~PerfData()
{
    /* remove the mapped shared memory segment from the address space of the process */
    if (munmap(membase, memsize) == -1) {
        printf("PID %d Unmap failed: %s\n", pid, strerror(errno));
        exit(1);
    }

    /* close the shared memory segment as if it was a file */
    if (close(memfd) == -1) {
        printf("TID %d Close failed: %s\n", pid, strerror(errno));
        exit(1);
    }

    /* remove the shared memory segment from the file system
    if (shm_unlink(memname) == -1) {
        printf("TID %s Error removing %s\n", memname, strerror(errno));
        exit(1);
    } */
}

std::map<int, int> appmap;
std::map<int, PerfData*> perfdata;
int main(int argc, char *argv[])
{
	if (init_thresholds == 0) {
    	init_thresholds = 1;
        thresh_pt[0] = 1000000; // cycles
        thresh_pt[1] = 70;        // instructions scaled to 100
        thresh_pt[2] = SHAR_MEM_THRESH/SHAR_PROCESSORS_CORE; // Mem
        thresh_pt[3] = SHAR_COHERENCE_THRESH; 
        thresh_pt[4] = SHAR_COHERENCE_THRESH;        
        thresh_pt[5] = SHAR_REMOTE_THRESH;  
		
        ordernum = 0;
        counter_order[ordernum++] = METRIC_INTER; // LLC_MISSES
        counter_order[ordernum++] = METRIC_INTRA;
        counter_order[ordernum++] = METRIC_MEM;
        counter_order[ordernum++] = METRIC_AVGIPC;
		num_counter_orders = ordernum;
		std::cout << " Ordering of counters by piority: " << ordernum;
	}
RESUME: 
    std::map<int, int> files; 
	int appno = 1;
	appmap.clear();
    files.clear();
	resetApps();
	struct dirent *de; 
    DIR *dr = opendir("/u/srikanth/app_proc");
 
    if (dr == NULL)     {
        printf("Could not open current directory" );
        return 0;
    }
 
    while ((de = readdir(dr)) != NULL) {
		if (!((strcmp(de->d_name, ".") == 0) || (strcmp(de->d_name, "..") == 0))) {
    		printf("%s\n", de->d_name);
    		
			string appname(de->d_name); 
			string filename = "/proc/" + appname + "/task";
			std::cout << " Container launcher PID " << atoi(de->d_name) << " " << filename << "\n";
			SharGetDescendants(filename, appname, files, appno);
			appmap.insert(std::pair<int, int>(appno-1, atoi(de->d_name)));
			appno++; 
		}
	}
	std::map<int, int>::iterator i;
    std::map<int, PerfData*>::iterator perfiter;
    int appiter = 0;

    std::cout << "\n PIDs tracked: ";
    for (i = files.begin(); i != files.end(); ++i) {

        int pid = i->first;

        std::cout << " " << i->first;
        perfiter = perfdata.find(pid);
        if (perfiter == perfdata.end()) {
			std::cout << pid << " is not found in perfdata. Adding it under " << i->second << "\n";

            PerfData *perfcreate = new PerfData;
            perfcreate->initialize(pid);
            perfcreate->appid = i->second - 1;
            perfdata.insert(std::pair<int, PerfData*>(pid, perfcreate));
            perfiter = perfdata.find(pid);
            perfcreate = NULL;
        }
        PerfData *temp;

        temp = perfiter->second;
        temp->appid = i->second - 1;
        std::cout << "PID: " << pid << "App ID: " << temp->appid << '\n';
        temp->readCounters();
        temp->touch = 1;
        appiter++;
    }
    for (perfiter = perfdata.begin(); perfiter != perfdata.end();) {

        int flag_touch = 0;
        if (perfiter->second->touch == 0) {
				std::cout << perfiter->first << " has not been touched this round. Deleting it.\n";
            if (kill(perfiter->first, 0) < 0) {
                if (errno == ESRCH)
                    printf ("Process %d  %d does not exist \n", perfiter->first, perfiter->second->options->pid);
                else
                    printf ("Hmmm what the hell happened??? \n");
            }
            else
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
	sleep(1);
	if (stoprun == false)
		goto RESUME;
	else {
		std::cout << "Stopping .. \n";
		std::cout << "\n";
		// Clean up
	}

}
