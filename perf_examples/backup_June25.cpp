#include <errno.h>
#include <signal.h>
#include <stdlib.h> // For random().
#include <inttypes.h>
#include <stdio.h>
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

#include <boost/filesystem.hpp>
using namespace std;
using std::string;
using std::vector;

#define MAX_GROUPS 10
#define TOT_CPUS 16
#define MAX_COUNTERS 50
#define MAX_APPS 50
#define TOT_COUNTERS 13
#define TOT_SOCKETS 2
#define TOT_PROCESSORS 10
#define TOT_CONTEXTS 20


int thresh_pt[MAX_COUNTERS], limits_soc[MAX_COUNTERS], thresh_soc[MAX_COUNTERS];
int counter_order[MAX_COUNTERS];
int ordernum = 0;
int init_thresholds = 0;

struct PerApp
{
    unsigned long value[MAX_COUNTERS];
    double valshare[MAX_COUNTERS];
    int bottleneck[MAX_COUNTERS];
    int interference[MAX_COUNTERS];
    int maxCPUS[MAX_COUNTERS];
};
struct PerApp apps[MAX_APPS];
struct PerApp sockets[TOT_SOCKETS];
struct PerApp system;

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
        // appagg[appid].val[i] += options->counters[i].delta;
    }
    /*
    for (i = 0; i < num; i++) {
        if ((i  == 0 || i == 5 || i == 9) && options->counters[i].delta > (uint64_t)thresh[i]) {
            // Cycles active spent
            active = 1;
            val[i] = options->counters[i].delta/thresh[i];
            bottleneck[i] = 1;
            std::cout << " PID :: " << pid << " Cycle bottleneck ";
            appagg[appid].bottleneck[i] += 1;
        }
        if ( (i == 1 || i == 6 || i == 10) && active == 1) {
            // IPC thresh scaled up by 100
            val[i] = ((100.0 * options->counters[i].delta)/options->counters[i-1].delta);
            if (val[i] >= thresh[i]) {
                bottleneck[i] = 1;
                appagg[appid].bottleneck[i] += 1;
                std::cout << " PID :: " << pid << " Instructions/Pipeline bottleneck ";
            }
        }
        if (i == 2 && options->counters[i].delta > (uint64_t)thresh[i] && active == 1) {
            val[i] = options->counters[i].delta;
            bottleneck[i] = 1;
            appagg[appid].bottleneck[i] += 1;
            std::cout << " PID :: " << pid << " HW ITLB bottleneck ";
        }
        if (i == 3 && options->counters[i].delta > (uint64_t)thresh[i] && active == 1) {
            val[i] = options->counters[i].delta;
            bottleneck[i] = 1;
            appagg[appid].bottleneck[i] += 1;
            std::cout << " PID :: " << pid << " L2 transactions bottleneck ";
        }
        if (i == 4 && options->counters[i].delta > (uint64_t)thresh[i] && active == 1) {
            val[i] = ((100.0 * options->counters[i].delta)/options->counters[0].delta);
            if (val[i] > thresh[i]) {
                bottleneck[i] = 1;
                appagg[appid].bottleneck[i] += 1;
                std::cout << " PID :: " << pid << " Resource stalls bottleneck ";
            }
        }
        if (i == 7 && active == 1) {
            val[i] = ((100.0 * options->counters[i].delta)/options->counters[i-2].delta);
            if (val[i] > thresh[i]) {
                bottleneck[i] = 1;
                appagg[appid].bottleneck[i] += 1;
                std::cout << " PID :: " << pid << " L1 Hit bottleneck ";
            }
        }
        if (i == 8 && active == 1) {
            val[i] = ((100.0 * options->counters[i].delta)/options->counters[i-3].delta);
            if (val[i] > thresh[i]) {
                bottleneck[i] = 1;
                appagg[appid].bottleneck[i] += 1;
                std::cout << " PID :: " << pid << " L2 Hit bottleneck ";
            }
        }
        if (i == 11 && options->counters[i].delta > (uint64_t)thresh[i] && active == 1) {
            val[i] = options->counters[i].delta;
            appagg[appid].bottleneck[i] += 1;
            bottleneck[i] = 1;
            std::cout << " PID :: " << pid << " L1 Miss bottleneck ";
        }
        if (i == 12 && options->counters[i].delta > (uint64_t)thresh[i] && active == 1) {
            val[i] = options->counters[i].delta;
            bottleneck[i] = 1;
            appagg[appid].bottleneck[i] += 1;
            std::cout << " PID :: " << pid << " LLC Misses bottleneck ";
       } 
       appagg[appid].val[i] += val[i];
    } */
}
void PerfData::readCounters()
{
    printf ("\n PID:: %d Exec ID %d \n", options->pid, appid);
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
RESUME: 
    std::map<int, int> files; 
	int appno = 1;
	appmap.clear();
    files.clear();

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

	sleep(1);
	goto RESUME;
}
