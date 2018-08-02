#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define MAX_JOBS    10
#define ARG_MAX     20

struct job {
    struct timespec start, end;
    char *argbuf;
    int argc;
    char *argv[ARG_MAX];
    pid_t pid;
    int status;
};

int num_jobs;
struct job jobs[MAX_JOBS];

void handle_quit(int sig) {
    printf("Received signal: %s\n", strsignal(sig));
    for (int i = 0; i < num_jobs; ++i)
        if (jobs[i].pid > 0)
            kill(jobs[i].pid, SIGTERM);
    sleep(2);
    for (int i = 0; i < num_jobs; ++i)
        if (jobs[i].pid > 0)
            kill(jobs[i].pid, SIGKILL);
}

static int find_job_id_by_pid(pid_t pid) {
    for (int i = 0; i < num_jobs; ++i)
        if (jobs[i].pid == pid)
            return i;
    return -1;
}

static pid_t run_job(struct job *jb) {
    void (*old)(int);

    old = signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGINT, SIG_DFL);

    jb->pid = fork();

    if (jb->pid == (pid_t) -1) {
        perror("fork()");
        exit(EXIT_FAILURE);
    }

    if (jb->pid == 0) {
        /* child */
        pid_t mypid = getpid();

        printf("[PID %6d] running %s ...\n", mypid, jb->argbuf);
        if (execvp(jb->argv[0], jb->argv) < 0) {
            fprintf(stderr, "failed to run %s: %s\n", jb->argbuf, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    signal(SIGTERM, old);
    signal(SIGQUIT, old);
    signal(SIGINT, old);

    return jb->pid;
}

int main(int argc, char *argv[]) {
    FILE *input = NULL;

    if (argc < 2) {
        fprintf(stderr, "usage: %s joblist\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-") == 0)
        input = stdin;
    else {
        if (!(input = fopen(argv[1], "r"))) {
            perror("Could not open job list");
            return 1;
        }
    }

    /* read jobs */
    char *line = NULL;
    size_t sz = 0;

    for (num_jobs = 0; getline(&line, &sz, input) >= 0; ) {
        if (num_jobs >= MAX_JOBS) {
            printf("Too many jobs (%d).\n", num_jobs);
            break;
        }

        char *nlptr = strchr(line, '\n');
        if (nlptr) *nlptr = '\0';

        /* skip empty lines or comments */
        if (*line == '\0' || *line == '\n' || *line == '#')
            continue;

        jobs[num_jobs].argbuf = strdup(line);
        char *token = line;
        char *save = NULL;

        for ( ; (token = strtok_r(token, " ", &save)) != NULL; token = NULL) {
            if (jobs[num_jobs].argc >= ARG_MAX - 1) {
                printf("Too many args (%d).\n", jobs[num_jobs].argc);
                break;
            }
            jobs[num_jobs].argv[jobs[num_jobs].argc++] = strdup(token);
        }
        jobs[num_jobs].argv[jobs[num_jobs].argc++] = NULL;
        num_jobs++;
    }

    free(line);

    fclose(input);

    signal(SIGTERM, &handle_quit);
    signal(SIGQUIT, &handle_quit);
    signal(SIGINT, &handle_quit);

    /* run all jobs */
    printf("Running %d jobs...\n", num_jobs);
    for (int i = 0; i < num_jobs; ++i) {
        if (run_job(&jobs[i]) != (pid_t) -1)
            clock_gettime(CLOCK_MONOTONIC_RAW, &jobs[i].start);
    }

    pid_t pid;
    int wstatus;
    while (!((pid = waitpid(-1, &wstatus, 0)) == -1 && errno == ECHILD)) {
        int i = find_job_id_by_pid(pid);
        if (i == -1)
            continue;
        clock_gettime(CLOCK_MONOTONIC_RAW, &jobs[i].end);
        jobs[i].status = wstatus;
    }

    printf("Summary:\n");
    for (int i = 0; i < num_jobs; ++i) {
        struct timespec diff_ts = jobs[i].end;
        if (diff_ts.tv_nsec < jobs[i].start.tv_nsec) {
            diff_ts.tv_sec = diff_ts.tv_sec - jobs[i].start.tv_sec - 1;
            diff_ts.tv_nsec = 1000000000 - (jobs[i].start.tv_nsec - diff_ts.tv_nsec);
        } else {
            diff_ts.tv_sec -= jobs[i].start.tv_sec;
            diff_ts.tv_nsec -= jobs[i].start.tv_nsec;
        }

        int n = 0;
        printf(" %.55s: %n", jobs[i].argbuf, &n);
        printf("%*lf s", 69 - n - 2,
                diff_ts.tv_sec + (double) diff_ts.tv_nsec / 1e+9);
        if (WIFEXITED(jobs[i].status) && WEXITSTATUS(jobs[i].status) != 0) {
            printf(" (ret %3d)\n", WEXITSTATUS(jobs[i].status));
        } else if (WIFSIGNALED(jobs[i].status)) {
            printf(" (%7s)\n", strsignal(WTERMSIG(jobs[i].status)));
        } else
            printf("\n");
    }

    return 0;
}
