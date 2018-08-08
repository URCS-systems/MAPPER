#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MIN(a,b)    ((a) < (b) ? (a) : (b))
#define ARG_MAX     20
#define MAX_FAILED  20

struct job {
    char name[40];
    struct timespec start, end;
    char *argbuf;
    int argc;
    char *argv[ARG_MAX];
    pid_t pid;
    double avg_time;
    int successful_runs;
    int failed_runs;
    struct job *prev, *next;
};

int num_jobs;
struct job *job_list;

unsigned test_length = 60 * 4;   /* in seconds */
time_t start;

bool wants_to_quit;

void handle_quit(int sig) {
    if (!wants_to_quit) {
        wants_to_quit = true;
        printf("Received signal: %s\n", strsignal(sig));
        for (struct job *jb = job_list; jb; jb = jb->next) {
            if (jb->pid > 0)
                kill(-jb->pid, SIGTERM);
        }
        sleep(2);
        for (struct job *jb = job_list; jb; jb = jb->next) {
            if (jb->pid > 0)
                kill(-jb->pid, SIGKILL);
        }
    }
}

static struct job *find_job_id_by_pid(pid_t pid) {
    for (struct job *jb = job_list; jb; jb = jb->next)
        if (jb->pid == pid)
            return jb;
    return NULL;
}

static pid_t run_job(struct job *jb) {
    void (*old)(int);

    old = signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGALRM, SIG_DFL);

    jb->pid = fork();

    if (jb->pid == (pid_t) -1) {
        perror("fork()");
        exit(EXIT_FAILURE);
    }

    if (jb->pid == 0) {
        /* child */
        pid_t mypid;
        char fname[512];
        int log_fd;
        
        /* set this process as the session leader,
         * which creates a new process group that
         * we can send signals to */
        if ((mypid = setsid()) == (pid_t) -1)
            mypid = getpid();

        snprintf(fname, sizeof fname, "%d.out", mypid);
        printf("[PID %6d] running %s ...\n", mypid, jb->argbuf);

        if ((log_fd = open(fname, O_CREAT | O_RDWR, 0644)) != -1) {
            if (dup2(log_fd, STDOUT_FILENO) == -1)
                fprintf(stderr, "[PID %6d] failed to associate stdout with %s: %m\n", 
                        mypid, fname);
            if (dup2(log_fd, STDERR_FILENO) == -1)
                fprintf(stderr, "[PID %6d] failed to associate stderr with %s: %m\n",
                        mypid, fname);
        } else
            fprintf(stderr, "[PID %6d] failed to open log file %s: %m\n", mypid, fname);

        if (execvp(jb->argv[0], jb->argv) < 0) {
            fprintf(stderr, "failed to run %s: %m\n", jb->argbuf);
            exit(EXIT_FAILURE);
        }
    }

    signal(SIGTERM, old);
    signal(SIGQUIT, old);
    signal(SIGINT, old);
    signal(SIGALRM, old);

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
            fprintf(stderr, "Could not open %s: %m\n", argv[1]);
            return 1;
        }
    }

    /* read jobs */
    char *line = NULL;
    size_t sz = 0;
    int lineno = 1;

    for (num_jobs = 0; getline(&line, &sz, input) >= 0; ++lineno) {
        char *nlptr = strchr(line, '\n');
        struct job *jb = NULL;
        char *token = NULL;
        char *save = NULL;
        unsigned seconds = 0;
        char *sp_ptr = NULL;

        if (nlptr) *nlptr = '\0';

        /* skip empty lines or comments */
        if (*line == '\0' || *line == '\n' || *line == '#')
            continue;

        /* get test length */
        if (sscanf(line, "time: %u s", &seconds) == 1) {
            test_length = seconds;
            continue;
        }

        if (!(sp_ptr = strchr(line, ' '))) {
            fprintf(stderr, "%s:%d: expected \"<jobname> <args>\"\n",
                    argv[1], lineno);
            continue;
        }

        if (!(jb = calloc(1, sizeof *jb))) {
            perror("calloc");
            return 1;
        }

        jb->argbuf = strdup(sp_ptr + 1);
        strncpy(jb->name, line, MIN((long)(sizeof jb->name - 1), sp_ptr - line));
        token = sp_ptr + 1;

        for ( ; (token = strtok_r(token, " ", &save)) != NULL; token = NULL) {
            if (jb->argc >= ARG_MAX - 1) {
                printf("Too many args (%d).\n", jb->argc);
                break;
            }
            jb->argv[jb->argc++] = strdup(token);
        }
        jb->argv[jb->argc++] = NULL;

        jb->next = job_list;
        if (job_list)
            job_list->prev = jb;
        job_list = jb;
        num_jobs++;
    }

    free(line);

    fclose(input);

    signal(SIGTERM, &handle_quit);
    signal(SIGQUIT, &handle_quit);
    signal(SIGINT, &handle_quit);
    signal(SIGALRM, &handle_quit);

    /* run all jobs */
    printf("Running %d jobs for %u seconds...\n", num_jobs, test_length);
    time(&start);
    alarm(test_length);
    for (struct job *jb = job_list; jb; jb = jb->next) {
        if (run_job(jb) != (pid_t) -1)
            clock_gettime(CLOCK_MONOTONIC_RAW, &jb->start);
    }

    pid_t pid;
    int wstatus;
    while (!((pid = waitpid(-1, &wstatus, 0)) == -1 && errno == ECHILD)) {
        struct job *jb = find_job_id_by_pid(pid);
        double elapsed = difftime(time(NULL), start);

        clock_gettime(CLOCK_MONOTONIC_RAW, &jb->end);

        if (elapsed > test_length || wants_to_quit)
            /* don't count this test result */
            continue;

        if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
            struct timespec diff_ts = jb->end;
            if (diff_ts.tv_nsec < jb->start.tv_nsec) {
                diff_ts.tv_sec = diff_ts.tv_sec - jb->start.tv_sec - 1;
                diff_ts.tv_nsec = 1000000000 - (jb->start.tv_nsec - diff_ts.tv_nsec);
            } else {
                diff_ts.tv_sec -= jb->start.tv_sec;
                diff_ts.tv_nsec -= jb->start.tv_nsec;
            }

            jb->avg_time = (jb->avg_time * jb->successful_runs + (diff_ts.tv_sec + (double) diff_ts.tv_nsec / 1e+9))
                / (jb->successful_runs + 1);
            jb->successful_runs++;
        } else {
            fprintf(stderr, "WARNING: Ignoring result for %s since it failed\n", jb->name);
            jb->failed_runs++;
        }

        /* run the job again */
        if (jb->failed_runs >= MAX_FAILED)
            fprintf(stderr, "WARNING: %s has failed too many times (%d).\n", jb->name,
                    jb->failed_runs);
        else if (run_job(jb) != (pid_t) -1)
            clock_gettime(CLOCK_MONOTONIC_RAW, &jb->start);
    }

    printf("Summary:\n");
    for (struct job *jb = job_list; jb; jb = jb->next) {
        int n = 0;
        printf(" %.15s (%.50s): %n", jb->name, jb->argbuf, &n);
        printf("%*lf s (%2d)\n", 75 - n - 2, jb->avg_time, jb->successful_runs);
    }

    return 0;
}
