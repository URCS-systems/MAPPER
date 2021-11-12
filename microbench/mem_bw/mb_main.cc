#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "mb_util.h"
#include <sys/syscall.h>

#include "hrtimer_x86.h"
#define CORE_MULT 1

#define MEASURE_TIME

// global shared data
long num_threads;
char *mode_str;
int mode;
double time_secs;
char *mbdata_buffer;

// synchronization objects
pthread_mutex_t start_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tid_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t tid_cond = PTHREAD_COND_INITIALIZER;
pid_t *tids;

void *thread_process(void *threadid)
{
    long tid = (long) threadid;
    long pid = syscall(SYS_gettid);

    /* set affinity */
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(tid*CORE_MULT, &mask);
    sched_setaffinity(pid, sizeof(mask), &mask);

    /* store this thread's PID */
    pthread_mutex_lock(&tid_mutex);
    tids[tid] = pid;
    pthread_cond_signal(&tid_cond);
    pthread_mutex_unlock(&tid_mutex);

    /* wait for main to signal execution */
    pthread_mutex_lock(&start_mutex);
    pthread_cond_wait(&start_cond, &start_mutex);
    pthread_mutex_unlock(&start_mutex);

#ifdef MEASURE_TIME
    double sttime = gethrtime();
#endif

    mbdata(mbdata_buffer, mode, time_secs, num_threads, tid);

#ifdef MEASURE_TIME
    double edtime = gethrtime();

    #if defined(__x86_64__)
        fprintf(stdout, "Elapsed time is %.6fsec\n", edtime-sttime);
    #elif defined(__powerpc64__)
        fprintf(stdout, "Elapsed time is %.6fsec\n", (edtime-sttime)/1000000000);
    #endif
#endif

    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    if (argc!=3 && argc!=4) {
        fprintf(stderr, "Usage: %s <lo|hi> <time in seconds> [num threads]\n", argv[0]);
        exit(-1);
    }

    /* initialize global shared data */
    num_threads = 1;
    mode_str = argv[1];
    time_secs = atof(argv[2]);

    if (strcmp(mode_str, "lo") == 0) {
        mode = MODE_LO;
    }
    else if (strcmp(mode_str, "hi") == 0) {
        mode = MODE_HI;
    }
    else {
        printf("ERROR: invalid mode \"%s\" provided.\n", mode_str);
        pthread_exit(NULL);
    }

    if (argc == 4) {
        num_threads = atoi(argv[3]);
    }

    /* initialize block and buffer */
    if (mbdata_init(&mbdata_buffer, mode, num_threads)) {
        printf("ERROR: unable to allocate memory\n");
        return -1;
    }

    pthread_t threads[num_threads];
    tids = (pid_t*) malloc(num_threads * sizeof(pid_t));

    /* spawn threads */
    for (long i = 0; i < num_threads; i++) {
        int err = pthread_create(&threads[i], NULL, thread_process, (void *)(i));
        if (err) {
            printf("ERROR: return code from pthread_create() is %d\n", err);
            return -1;
        }

        /* Wait for the thread to store its TID */
        pthread_mutex_lock(&tid_mutex);
        pthread_cond_wait(&tid_cond, &tid_mutex);
        pthread_mutex_unlock(&tid_mutex);
    }

    /* Signal the threads to start */
    pthread_mutex_lock(&start_mutex);
    pthread_cond_broadcast(&start_cond);
    pthread_mutex_unlock(&start_mutex);

    /* Sample the counters until one thread quits
     * or we run out of time
     */
    int do_sample = 1;
    for (int i = 0; (i < (int)time_secs) && do_sample; i++) {
        
        for (int j = 0; j < num_threads; j++) {
            if (!pthread_tryjoin_np(threads[j], NULL)) {
                do_sample = 0;
                break;
            }
        }
    }

    /* Now just wait for other threads to finish */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    mbdata_final(mbdata_buffer);
    free(tids);

    return 0;
}
