#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> // For random().
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "config.h"
#include "cgroup.h"

const char *cgroup_root = "/sys/fs/cgroup";
const char *controller = "cpuset";
char cg_name[512];
pid_t initial_pid;

void handle_quit(int sig) {
    printf("Received: %s\n", strsignal(sig));
    kill(initial_pid, SIGTERM);
    sleep(2);
    kill(initial_pid, SIGKILL);
}

int main(int argc, char *argv[])
{
    char cmdbuf[1024];
    int p = 0;
    int childret = 0;
    int childsig = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s program [arguments]\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        p += snprintf(&cmdbuf[p], sizeof cmdbuf - p, "%s ", argv[i]);
    }

    printf("Command to be executed: %s\n", cmdbuf);

    initial_pid = fork();

    if (initial_pid == (pid_t) -1) {
        perror("fork()");
        return 1;
    }

    if (initial_pid == 0) {
        /* Now launch the command from this process */
        if (execvp(argv[1], &argv[1]) < 0) {
            fprintf(stderr, "Could not run %s: %s\n", argv[1], strerror(errno));
            return 1;
        }
        return 0;
    } else if (initial_pid > 0) {
        char app_path[1024];
        snprintf(app_path, sizeof app_path, SAM_RUN_DIR "/%d", initial_pid);

        if (mkdir(app_path, 0) < 0 && errno != EEXIST) {
            fprintf(stderr, "Failed to create %s: %s\n", app_path, strerror(errno));
            exit(EXIT_FAILURE);
        }

        snprintf(cg_name, sizeof cg_name, SAM_CGROUP_NAME "/app-%d", initial_pid);
        char *mems_string = NULL;
        char *cpus_string = NULL;

        if (cg_create_cgroup(cgroup_root, controller, cg_name) != 0 
         || cg_read_string(cgroup_root, controller, SAM_CGROUP_NAME, "cpuset.mems", &mems_string) != 0
         || cg_write_string(cgroup_root, controller, cg_name, "cpuset.mems", "%s", mems_string) != 0 
         || cg_read_string(cgroup_root, controller, SAM_CGROUP_NAME, "cpuset.cpus", &cpus_string) != 0 
         || cg_write_string(cgroup_root, controller, cg_name, "cpuset.cpus", "%s", cpus_string) != 0
         || cg_write_string(cgroup_root, controller, cg_name, "tasks", "%d", initial_pid) != 0) {
            fprintf(stderr, "Failed to create/establish cgroup for process %d: %s\n", 
                    initial_pid,
                    strerror(errno));
            kill(initial_pid, SIGKILL);
            waitpid(initial_pid, NULL, 0);
            return 1;
        }

        free(mems_string);
        free(cpus_string);

        signal(SIGTERM, &handle_quit);
        signal(SIGQUIT, &handle_quit);
        signal(SIGINT, &handle_quit);

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
                } else {
                    printf("Killing process %d \n", initial_pid);
                    kill(initial_pid, SIGKILL);
                }
            }
        } while (wpid == 0 && waited < timeouttokill);

        if (WIFEXITED(status)) {
            childret = WEXITSTATUS(status);
            printf("Child exited, status=%d\n", childret);
        } else if (WIFSIGNALED(status)) {
            childsig = WTERMSIG(status);
            printf("Child %d was terminated with a status of: %d \n", 
                    initial_pid, childsig);
        }

        if (rmdir(app_path) != 0) {
            fprintf(stderr, "Failed to remove %s: %s\n", app_path, strerror(errno));
        }
        if (cg_remove_cgroup(cgroup_root, controller, cg_name) != 0)
            perror("Failed to remove cgroup");
    }

    if (childsig)
        raise(childsig);

    return childret;
}
