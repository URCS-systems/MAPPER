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
#include "cgroup.h"
using namespace std;
using std::string;
using std::vector;


const char *cgroup_root = "/sys/fs/cgroup";
const char *controller = "cpuset";
char cg_name[512];

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
        // TO DO: Have to avoid reitration of parents
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
    // std::cout << '\n';
    return 0;
}
int main(int argc, char *argv[])
{
    std::map<int, int> files; 
	int execno = 1;
    files.clear();

	string command_exec;
    for (int iteri = 1; iteri < argc; iteri++) {
 	   	if (iteri != 1)
    		command_exec = command_exec + ' ';
	     command_exec = command_exec + argv[iteri];
    }

	std::cout << "Command to be executed: " << command_exec << '\n';
  
	// boost::filesystem::create_directories("/u/srikanth/a/b/c"); 
	pid_t initial_pid = fork();	

	if (initial_pid == 0) {
		std::cout << " Child PID: " << getpid();
		/* Now launch the command from this process */
		system(command_exec.c_str());
		return 0;
	}
	else if (initial_pid > 0) {

		string filename = "/proc/" + std::to_string(initial_pid) + "/task";
		std::cout << " Container launcher PID " << initial_pid << " " << filename;
	    SharGetDescendants(filename, std::to_string(initial_pid), files, execno); 

		string path_to_application = "/u/srikanth/app_proc/" + std::to_string(initial_pid);
		boost::filesystem::create_directories(path_to_application.c_str());
		/* Maintain active list of tasks for which performance counters have been
		 * setup. Set up the counters for the other tasks. Read and analyze the counters
		 * periodically to determine the parallelism needed. Do not move tasks here. */

		/* setup cgroups 

		char group[64];
		int ret;

		snprintf(group, sizeof group, "sam/pid-%d", initial_pid);
		if ((ret = cg_create_cgroup("/sys/fs/cgroup", "cpuset", group)) != 0) {
			fprintf(stderr, "Failed to create cgroup '%s': %s\n", group, strerror(ret));
		} else {
			if ((ret = cg_write_string("/sys/fs/cgroup", "cpuset", group, "cpuset.mems", "%d", 0)) != 0
			 ||
				(ret = cg_write_string("/sys/fs/cgroup", "cpuset", group, "cpuset.cpus", "%d", 0)) != 0
			 || (ret = cg_write_string("/sys/fs/cgroup", "cpuset", group, "tasks", "%d", initial_pid)) != 0) {
				fprintf(stderr, "Failed to move process to its own cgroup: %s\n", strerror(ret));
			} else {
				printf("Moved process to its own cgroup: %s .\n",group);
			}
		}*/

		snprintf(cg_name, sizeof cg_name, "sam/app-%d", initial_pid);
		char *mems_string = NULL;
		char *cpus_string = NULL;

		if (cg_create_cgroup(cgroup_root, controller, cg_name) != 0
			 || cg_read_string(cgroup_root, controller, "sam", "cpuset.mems", &mems_string) != 0
			 || cg_write_string(cgroup_root, controller, cg_name, "cpuset.mems", "%s", mems_string) != 0
			 || cg_read_string(cgroup_root, controller, "sam", "cpuset.cpus", &cpus_string) != 0
			 || cg_write_string(cgroup_root, controller, cg_name, "cpuset.cpus", "%s", cpus_string) != 0) {
			fprintf(stderr, "Failed to create/establish cgroup for process %d: %s\n", initial_pid, strerror(errno));
			kill(initial_pid, SIGKILL);
			waitpid(initial_pid, NULL, 0);
			return 1;
		}

		free(mems_string);
		free(cpus_string);

		/* Wait and exit */
		int timeouttokill = 3600;
		int waited = 0;
    	int status, wpid;
	    do {
       		wpid = waitpid(initial_pid, &status, WNOHANG);
	        if (wpid == 0) {
	            if (waited < timeouttokill) {
    	            printf("Waiting %d second(s).\n", waited);
        	        sleep(1);
            	    waited++;
	            }
    	        else {
        	        printf("Killing process %d \n", initial_pid);
            	    kill(initial_pid, SIGKILL); 
	            }
    	    }
    	} while (wpid == 0 && waited < timeouttokill);
	 
	    if (WIFEXITED(status)) {
    	    printf("Child exited, status=%d\n", WEXITSTATUS(status));
	    }
    	else if (WIFSIGNALED(status)) {
        	printf("Child %d was terminated with a status of: %d \n", initial_pid, WTERMSIG(status));
	    }
	
		string rem_path = "rmdir " + path_to_application;
		system(rem_path.c_str());
		if (cg_remove_cgroup(cgroup_root, controller, cg_name) != 0)
        	perror("Failed to remove cgroup");
	}
}
