#define _GNU_SOURCE
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
#include <sys/time.h>
#include <sys/resource.h>
#include "../util.h"
#include <pthread.h>
#include <sys/sysinfo.h>
#include <sched.h>

#define ARG_MAX     20
#define MAX_FAILED  20
#define SUMMARY_LEN 140 /* in columns */
#define MAX_JOBLIST 100

struct job {
    char name[40];
    char outfile[512];
    struct timespec start, end;
    char *argbuf;
    int argc;
    char *argv[ARG_MAX];
    pid_t pid;
    double avg_time;
    double avg_time2;
    int successful_runs;
    int failed_runs;
    int total_runs;
    char *filter_cmd;
    cpu_set_t *cpuset;
    int cpuset_changes; /* number of times the cpuset changed */
    int context_changes; /* total number of contexts added between cpuset changes */
    double avg_ctx_changes_per_second;
    double avg_cpuset_changes_per_second;
    pthread_mutex_t mtx;
    struct job *prev, *next;
};

int num_jobs;
struct job *job_list;
int nprocs;

unsigned test_length = 60 * 4;   /* in seconds */
struct timespec start, end;

bool wants_to_quit;
bool stop_thread;
bool killing_jobs;

pthread_t monitor_thread;

double total_runtime = 0;
int total_context_changes = 0;
int total_cpuset_changes = 0;

const char *warn_color = "\033[38;5;196m";
const char *reset = "\033[0m";

void kill_jobs(void) {
    int amt_killed = 0;
    for (struct job *jb = job_list; jb; jb = jb->next) {
        if (jb->pid > 0) {
            kill(-jb->pid, SIGTERM);
            amt_killed++;
        }
    }
    if (amt_killed > 0)
        sleep(2);
    for (struct job *jb = job_list; jb; jb = jb->next) {
        if (jb->pid > 0)
            kill(-jb->pid, SIGKILL);
    }
}


void handle_quit(int sig) {
    printf("Received signal: %s\n", strsignal(sig));
    if (sig != SIGALRM)
        wants_to_quit = true;
    if (!killing_jobs) {
        killing_jobs = true;
        kill_jobs();
        killing_jobs = false;
    }
}

void *monitor_cpuset_changes(void *arg) {
    /* read procfs every second and compare change in cpusets */
    while (!wants_to_quit && !stop_thread) {
        for (struct job *jb = job_list; jb && !wants_to_quit && !stop_thread; jb = jb->next) {
            char cmd[1024];
            char *buf;
            FILE *pf;
            int *intlist = NULL;
            size_t intlist_l = 0;
            cpu_set_t *set = NULL;
            size_t cpus_sz = CPU_ALLOC_SIZE(nprocs);

            pthread_mutex_lock(&jb->mtx);

            /* hack */
            if (strstr(jb->argv[0], "sam-launch") != NULL)
                snprintf(cmd, sizeof cmd - 1, "cat /proc/$(cat /proc/%d/task/%d/children | awk '{print $1}')/status | grep Cpus_allowed_list | awk '{print $2}'", jb->pid, jb->pid);
            else
                snprintf(cmd, sizeof cmd - 1, 
                        "cat /proc/%d/status | grep Cpus_allowed_list | awk '{print $2}'", jb->pid);

            if (!(pf = popen(cmd, "r"))) {
                fprintf(stderr, "%sWARNING: %10s: failed to read /proc/%d/status: %m%s\n", warn_color, jb->name, jb->pid, reset);
                pthread_mutex_unlock(&jb->mtx);
                continue;
            }

            fscanf(pf, "%ms", &buf);
            if (!buf || string_to_intlist(buf, &intlist, &intlist_l) != 0) {
                if (errno == 0)
                    fprintf(stderr, "%sWARNING: %10s: expected intlist, found '%s%s%s'%s\n", 
                            warn_color, jb->name, buf, reset, warn_color, reset);
                else
                    fprintf(stderr, "%sWARNING: %10s: could not read intlist: %m%s\n", 
                            warn_color, jb->name, reset);
            }
            free(buf);
            buf = NULL;
            pclose(pf);
            pf = NULL;

            if (!intlist) {
                pthread_mutex_unlock(&jb->mtx);
                continue;
            }

            /*
            printf("[PID %6d] before:", jb->pid);
            for (size_t k = 0; k < intlist_l; ++k) {
                printf("%d,", intlist[k]);
            }
            printf("\n");
            */

            intlist_to_cpuset(intlist, intlist_l, &set, nprocs);
            free(intlist);
            intlist = NULL;
            intlist_l = 0;
            /*
            cpuset_to_intlist(set, nprocs, &intlist, &intlist_l);

            printf("[PID %6d] after:", jb->pid);
            for (size_t k = 0; k < intlist_l; ++k) {
                printf("%d,", intlist[k]);
            }
            printf("\n");
            */

            if (!jb->cpuset) {
                /* first read */
                // jb->context_changes += CPU_COUNT_S(cpus_sz, set);
                // jb->cpuset_changes++;
                jb->cpuset = set;
            } else {
                /* subsequent reads */
                cpu_set_t *tmp1 = CPU_ALLOC(nprocs),
                          *tmp2 = CPU_ALLOC(nprocs);
                int diff = 0;
                CPU_ZERO_S(cpus_sz, tmp1);
                CPU_ZERO_S(cpus_sz, tmp2);
                CPU_AND_S(cpus_sz, tmp1, jb->cpuset, set);
                CPU_XOR_S(cpus_sz, tmp2, set, tmp1);

                /*
                cpuset_to_intlist(jb->cpuset, nprocs, &intlist, &intlist_l);

                printf("[PID %6d] old cpuset:", jb->pid);
                for (size_t k = 0; k < intlist_l; ++k) {
                    printf("%d,", intlist[k]);
                }
                printf("\n");
                free(intlist);
                intlist = NULL;
                intlist_l = 0;
                */

                if ((diff = CPU_COUNT_S(cpus_sz, tmp2)) > 0) {
                    jb->context_changes += diff;
                    jb->cpuset_changes++;
                }

                CPU_FREE(jb->cpuset);
                jb->cpuset = set;
                CPU_FREE(tmp1);
                CPU_FREE(tmp2);
            }

            pthread_mutex_unlock(&jb->mtx);
        }
        nanosleep(&(struct timespec) { 1, 0 }, NULL);
    }
    return NULL;
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
        int log_fd;
        struct rlimit lm;
        
        /* set this process as the session leader,
         * which creates a new process group that
         * we can send signals to */
        if ((mypid = setsid()) == (pid_t) -1)
            mypid = getpid();

        snprintf(jb->outfile, sizeof jb->outfile, "%s-v%d.%d.out", jb->name, jb->total_runs + 1, mypid);

        if (getrlimit(RLIMIT_NOFILE, &lm) != -1) {
            lm.rlim_max = 1 << 14;
            lm.rlim_cur = lm.rlim_max;

            if (setrlimit(RLIMIT_NOFILE, &lm) == -1)
                fprintf(stderr, "%sWARNING: failed to set file limit for %s to %lu: %m%s\n", 
                        warn_color, jb->name, lm.rlim_cur, reset);
        }

        printf("[PID %6d] running %s ...\n", mypid, jb->argbuf);

        if ((log_fd = open(jb->outfile, O_CREAT | O_RDWR, 0644)) != -1) {
            if (dup2(log_fd, STDOUT_FILENO) == -1)
                fprintf(stderr, "s[PID %6d] failed to associate stdout with %s: %m\n", 
                        mypid, jb->outfile);
            if (dup2(log_fd, STDERR_FILENO) == -1)
                fprintf(stderr, "[PID %6d] failed to associate stderr with %s: %m\n",
                        mypid, jb->outfile);
        } else
            fprintf(stderr, "[PID %6d] failed to open log file %s: %m\n", 
                    mypid, jb->outfile);

        if (execvp(jb->argv[0], jb->argv) < 0) {
            fprintf(stderr, "failed to run %s: %m\n", jb->argbuf);
            exit(EXIT_FAILURE);
        }
    }

    /* parent */
    snprintf(jb->outfile, sizeof jb->outfile, "%s-v%d.%d.out", jb->name, jb->total_runs + 1, jb->pid);

    signal(SIGTERM, old);
    signal(SIGQUIT, old);
    signal(SIGINT, old);
    signal(SIGALRM, old);

    return jb->pid;
}

static int parse_result(const struct job *jb, double *res)
{
    FILE *pf;
    int ret = 0;
    char *line = NULL;
    size_t sz = 0;

    if (jb->filter_cmd != NULL) {
        char cmdbuf[1024];
        snprintf(cmdbuf, sizeof cmdbuf, "cat %s | %s", jb->outfile, jb->filter_cmd);
        
        if (!(pf = popen(cmdbuf, "r"))) {
            fprintf(stderr, "%sWARNING: could not run filter '%s' for %s: %m%s\n", 
                    warn_color, cmdbuf, jb->name, reset);
            return -1;
        }
    } else {
        if (!(pf = fopen(jb->outfile, "r"))) {
            fprintf(stderr, "%sWARNING: could not open log file '%s' for %s: %m%s\n", 
                    warn_color, jb->outfile, jb->name, reset);
            return -1;
        }
    }

    /* we expect the result on a single line */
    if (getline(&line, &sz, pf) == -1) {
        ret = -1;
        fprintf(stderr, "%sWARNING: could not read line from output of %s: %m%s\n", 
                warn_color, jb->name, reset);
    } else if (sscanf(line, "%lf", res) != 1) {
        ret = -1;
        if (errno != 0)
            fprintf(stderr, "%sWARNING: could not parse result for %s: %m%s\n", 
                    warn_color, jb->name, reset);
        else {
            char *nl_ptr = strchr(line, '\n');
            if (nl_ptr) *nl_ptr = '\0';
            fprintf(stderr, "%sWARNING: could not parse result for %s: expected double, got '%s%s%s'%s\n", 
                    warn_color, jb->name, line, reset, warn_color, reset);
        }
    }

    free(line);
    if (jb->filter_cmd != NULL)
        pclose(pf);
    else
        fclose(pf);
    return ret;
}

int test_joblist(const char *filename, FILE *input, FILE *csv, bool write_header) {
    /* read jobs */
    char *line = NULL;
    size_t sz = 0;
    int lineno = 1;
    char home_dir[512];
    bool ended = false;

    snprintf(home_dir, sizeof home_dir - 1, "%s", getenv("HOME"));

    for (num_jobs = 0; getline(&line, &sz, input) >= 0; ++lineno) {
        char *nlptr = strchr(line, '\n');
        struct job *jb = NULL;
        char *token = NULL;
        char *save = NULL;
        unsigned seconds = 0;
        char *sp_ptr = NULL;
        char *pipe_ptr = NULL;

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
                    filename, lineno);
            continue;
        }

        if (!(jb = calloc(1, sizeof *jb))) {
            perror("calloc");
            return 1;
        }

        pthread_mutex_init(&jb->mtx, NULL);

        if ((pipe_ptr = strchr(line, '|'))) {
            jb->filter_cmd = strdup(pipe_ptr + 1);
            *pipe_ptr = '\0';
        }

        // jb->argbuf = strdup(sp_ptr + 1);
        strncpy(jb->name, line, MIN((long)(sizeof jb->name - 1), sp_ptr - line));
        token = sp_ptr + 1;

        for ( ; (token = strtok_r(token, " ", &save)) != NULL; token = NULL) {
            if (jb->argc >= ARG_MAX - 1) {
                fprintf(stderr, "Too many args (%d).\n", jb->argc);
                break;
            }
            if (strcmp(token, "|") == 0)
                break;

            /* replace '~' with $HOME */
            char *str = NULL;
            char *new_argbuf = NULL;
            char *tilde_ptr = NULL;
            if ((tilde_ptr = strchr(token, '~')) && home_dir[0]) {
                size_t sz = strlen(home_dir) + strlen(token) + 1;   /* excluding '~' for token */
                *tilde_ptr = '\0';
                str = malloc(sz);
                snprintf(str, sz - 1, "%s%s%s", token, home_dir, tilde_ptr + 1);
            } else {
                if (tilde_ptr && !home_dir[0]) {
                    fprintf(stderr, "%sWARNING: HOME is undefined but needed for %s%s\n",
                            warn_color, jb->name, reset);
                }
                str = strdup(token);
            }
            jb->argv[jb->argc++] = str;
            if (jb->argbuf)
                asprintf(&new_argbuf, "%s %s", jb->argbuf, str);
            else
                asprintf(&new_argbuf, "%s", str);
            free(jb->argbuf);
            jb->argbuf = new_argbuf;
        }
        jb->argv[jb->argc++] = NULL;

        jb->next = job_list;
        if (job_list)
            job_list->prev = jb;
        job_list = jb;
        num_jobs++;
    }

    free(line);

    signal(SIGTERM, &handle_quit);
    signal(SIGQUIT, &handle_quit);
    signal(SIGINT, &handle_quit);
    signal(SIGALRM, &handle_quit);

    /* run all jobs */
    printf("Running %d jobs for %u seconds...\n", num_jobs, test_length);
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    alarm(test_length);
    for (struct job *jb = job_list; jb; jb = jb->next) {
        if (run_job(jb) != (pid_t) -1)
            clock_gettime(CLOCK_MONOTONIC_RAW, &jb->start);
    }

    /* start monitoring thread */
    if (pthread_create(&monitor_thread, NULL, &monitor_cpuset_changes, NULL) != 0) {
        perror("Failed to start monitor thread");
        raise(SIGQUIT);
    }


    pid_t pid;
    int wstatus;
    while (!((pid = waitpid(-1, &wstatus, 0)) == -1 && errno == ECHILD)) {
        struct job *jb = find_job_id_by_pid(pid);
        struct timespec cur;
        double elapsed;

        clock_gettime(CLOCK_MONOTONIC_RAW, &cur);
        elapsed = timespec_diff(&start, &cur);

        if (!jb)
            /* this isn't a job */
            continue;

        clock_gettime(CLOCK_MONOTONIC_RAW, &jb->end);

        if (elapsed > test_length || wants_to_quit) {
            /* don't count this test result */
            if (!ended)
                clock_gettime(CLOCK_MONOTONIC_RAW, &end);
            continue;
        }

        pthread_mutex_lock(&jb->mtx);
        if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
            double parsed_time = 0;

            /* compute (running) average runtime */
            parsed_time = timespec_diff(&jb->start, &jb->end);
            jb->avg_time = (jb->avg_time * jb->successful_runs + parsed_time) / (jb->successful_runs + 1);

            /* compute parsed time */
            if (jb->filter_cmd && parse_result(jb, &parsed_time) != -1)
                jb->avg_time2 = (jb->avg_time2 * jb->successful_runs + parsed_time) / (jb->successful_runs + 1);

            /* compute average changes / second */
            jb->avg_ctx_changes_per_second = (jb->avg_ctx_changes_per_second * jb->successful_runs 
                    + (jb->context_changes / parsed_time)) / (jb->successful_runs + 1);
            jb->avg_cpuset_changes_per_second = (jb->avg_cpuset_changes_per_second * jb->successful_runs
                    + (jb->cpuset_changes / parsed_time)) / (jb->successful_runs + 1);
            total_runtime += parsed_time;
            total_context_changes += jb->context_changes;
            total_cpuset_changes += jb->cpuset_changes;

            jb->successful_runs++;
        } else {
            fprintf(stderr, "%sWARNING: Ignoring result for %s since it failed%s\n", 
                    warn_color, jb->name, reset);
            jb->failed_runs++;
        }
        jb->total_runs++;

        jb->context_changes = 0;
        jb->cpuset_changes = 0;
        CPU_FREE(jb->cpuset);
        jb->cpuset = NULL;

        /* run the job again */
        if (jb->failed_runs >= MAX_FAILED)
            fprintf(stderr, "%sWARNING: %s has failed too many times (%d).%s\n", 
                    warn_color, jb->name, jb->failed_runs, reset);
        else if (run_job(jb) != (pid_t) -1)
            clock_gettime(CLOCK_MONOTONIC_RAW, &jb->start);

        pthread_mutex_unlock(&jb->mtx);
    }

    stop_thread = true;

    if (pthread_join(monitor_thread, NULL) != 0)
        fprintf(stderr, "%sWARNING: pthread_join() on monitor_thread: %m%s", warn_color, reset);

    stop_thread = false;

    printf("Summary:\n");
    for (struct job *jb = job_list; jb; jb = jb->next) {
        int n = 0;
        int rem = 0;
        double val = jb->avg_time;
        const char *str = "";
        printf(" %.15s (%.50s): %n", jb->name, jb->argbuf, &n);
        /*
         * SUMMARY_LEN - (length of all other text)
         */
        rem = SUMMARY_LEN - (55 - 4 - 4 - 4  - 7) - n;
        if (jb->filter_cmd) {
            val = jb->avg_time2;
            /*
             * this means we're reporting the real time it took the algorithm to run,
             * according to when the child started its timer
             */
            str = "r";
            rem -= 1;
        }
        printf("%*lf s, %*lf changes/s, %*lf cpuset changes/s (%2d%.1s)\n", 
                rem / 3 + rem % 3, val, 
                rem / 3, jb->avg_ctx_changes_per_second,
                rem / 3, jb->avg_cpuset_changes_per_second,
                jb->successful_runs, str);
    }

    double duration = timespec_diff(&start, &end);

    printf("Total changes: %10d, Total cpuset changes: %10d, Total job time: %10lf, Test duration: %10lf\n",
            total_context_changes, total_cpuset_changes, total_runtime, duration);

    if (write_header) {
        for (struct job *jb = job_list; jb; jb = jb->next)
            fprintf(csv, "%s,", jb->name);
        for (struct job *jb = job_list; jb; jb = jb->next)
            fprintf(csv, "%1$s-C/s,%1$s-N/s", jb->name);
        fprintf(csv, "C,N,runtime,duration");
    }

    for (struct job *jb = job_list; jb; jb = jb->next)
        fprintf(csv, "%lf,", jb->avg_time2 ? jb->avg_time2 : jb->avg_time);
    for (struct job *jb = job_list; jb; jb = jb->next)
        fprintf(csv, "%lf,%lf,", jb->avg_ctx_changes_per_second, jb->avg_cpuset_changes_per_second);

    fprintf(csv, "%lf,%lf,%lf,%lf", (double) total_context_changes, (double) total_cpuset_changes, total_runtime, duration);

    for (struct job *jb = job_list; jb; ) {
        struct job *next = jb->next;
        pthread_mutex_destroy(&jb->mtx);
        free(jb);
        jb = next;
    }

    job_list = NULL;

    return 0;
}

void usage(const char *prog) {
    fprintf(stderr, "usage: %s [-n NTIMES] -f FILE ... [FILE...]\n", prog);
}

int main(int argc, char *argv[]) {
    int num_joblists = 0;
    char *jls[MAX_JOBLIST];
    int ntimes;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* get system info */
    nprocs = get_nprocs();

    /* setup colors */
    if (!isatty(fileno(stdout))) {
        warn_color = "";
        reset = "";
    }

    for (int i = 1; i < argc && num_joblists < MAX_JOBLIST;i++) {
        if (strcmp(argv[i], "-n") == 0) {
            ntimes = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-f") == 0) {
            jls[num_joblists++] = argv[++i];
        } else
            fprintf(stderr, "invalid argument '%s'\n", argv[i]);
    }

    if (num_joblists < 1) {
        usage(argv[0]);
        return 1;
    }

    for (int i = 0; i < num_joblists && !wants_to_quit; ++i) {
        FILE *input = NULL;
        char *log_name = strdup(jls[i]);
        const char *log_base = NULL;
        if (strcmp(argv[1], "-") == 0)
            input = stdin;
        else {
            if (!(input = fopen(jls[i], "r"))) {
                fprintf(stderr, "Could not open %s: %m\n", jls[i]);
                return 1;
            }
        }

        log_base = basename(log_name);
            
        for (int j = 0; j < ntimes && !wants_to_quit; ++j) {
            FILE *log = NULL;
            char *log_name2 = NULL;
            asprintf(&log_name2, "%s-%d.csv", log_base, j+1);

            if (!(log = fopen(log_name2, "w"))) {
                fprintf(stderr, "failed to open `%s':%m\n", log_name2);
                return 1;
            }
            if (test_joblist(jls[i], input, log, j == 0) != 0)
                return 1;
            free(log_name2);
            fclose(log);
            rewind(input);
        }
        if (input != stdin)
            fclose(input);
        free(log_name);
    }

    return 0;
}
