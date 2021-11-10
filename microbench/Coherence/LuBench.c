#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#define NUM_THREADS 2
//#define NUM_ITER 200000 // Original value
#define NUM_ITER 1000000 // For elongating the run
#define NUM_ITER_IDLE 10
#define INNER_ITER 10
#define SIZE_CRIT 32

int ARRAY_SIZE = 128;
int sum[NUM_THREADS];
int sum_ex = NUM_THREADS*NUM_ITER*INNER_ITER;
pthread_mutex_t mutexsum[NUM_THREADS];
int shared_array[NUM_THREADS][SIZE_CRIT];
//int state_one[NUM_THREADS];
//int state_sys[NUM_ITER*INNER_ITER][NUM_THREADS];

/* barrier block */
pthread_mutex_t mut[NUM_THREADS]; // = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond[NUM_THREADS]; // = PTHREAD_COND_INITIALIZER;
int barrier_arrived[NUM_THREADS][32];
/*
void barrier (int n, int index)
{
    int ind = index/2;
    pthread_mutex_lock (&mut[ind]);  

    barrier_arrived[ind]++;

    //fprintf(stderr,"Thread %d arrived \n",index);
    if (barrier_arrived[ind] < n)
        pthread_cond_wait (&cond[ind], &mut[ind]);
    else {
        barrier_arrived[ind] = 0;           
        pthread_cond_broadcast (&cond[ind]);
    }

    pthread_mutex_unlock (&mut[ind]);     
}
*/
void barrier_init ()
{
    int i;

    for (i=0; i<NUM_THREADS; i++)
    {
	pthread_mutex_init(&mut[i], NULL);
	pthread_cond_init(&cond[i], NULL);
	barrier_arrived[i][0] = 0;
    }
    return;
}


	
/* end of barrier */

void *PrintHello(void *threadid)
{
   long tid;
   int cnt = 0;
   int spin_time = 0;
   tid = (long)threadid;
   char flnm[10];
   cpu_set_t  mask;
   CPU_ZERO(&mask);

   int int_a, int_b, int_sum;
  
   //CPU_SET(tid*2, &mask);
   CPU_SET(tid, &mask);

   sched_setaffinity(0, sizeof(mask), &mask);


   printf("Hello World! It's me, thread #%ld!\n", tid);
   int i, j, k;
   for(i=0; i<NUM_ITER; i++)
   {
	for (j=0; j<INNER_ITER; j++)
	{
		/*
		if (tid%2 == 1)
		   	barrier(2, (int)tid);
		pthread_mutex_lock (&mutexsum[tid/2]);
		*/
		cnt++;
		if (tid%2 == 1)
		{
			while(barrier_arrived[tid-1][0] == cnt-1)
				spin_time++;
		}	
    		//fprintf(stderr,"Thread %d arrived \n",index);
		for (k=0; k<SIZE_CRIT; k=k+SIZE_CRIT)	
		{
			shared_array[tid][k] += 1;
			//sum[tid] += 1;
			if (tid%2 == 1)
			{
				shared_array[tid-1][k] += 1;
				//sum[tid-1] += 1;
			}
			else
			{
				//sum[tid+1] += 1;
				shared_array[tid+1][k] += 1;
			}
		}
		/*
		pthread_mutex_unlock(&mutexsum[tid/2]);
		if (tid%2 == 0)
			barrier(2, (int)tid);
		*/
		
		barrier_arrived[tid][0] += 1;
		 for (k=0;k<ARRAY_SIZE*NUM_ITER_IDLE;k++)
                {
                        int_a = k;
                        int_b = int_a*k;
                        int_sum = int_a * int_b;
                }

		if (tid%2 == 0)
		{
			while(barrier_arrived[tid+1][0] == cnt-1)
				spin_time++;
		}		
		//pthread_mutex_unlock (&mutexsum[0]);
	
	}
   }
   /*free(int_a);
   free(int_b);
   free(int_sum);*/
   sum[tid] = spin_time/(NUM_ITER*INNER_ITER);

   flnm[0] = (int)tid + '0';
   flnm[1] = '\0';
   fprintf(stderr, "\n Exitting thread : %ld Spun for %d \n",tid,spin_time);
   /*
   FILE *fp;
   fp = fopen(flnm,"w");

   for (i=0; i<NUM_ITER; i++)
   {
	for (j=0; j<INNER_ITER; j++)
	{
	     fprintf(fp,"\n");
	     for (k=0; k<NUM_THREADS; k++)
	     {
		 fprintf(fp," %d",state_sys[j+(INNER_ITER*i)][k]);	
	     }	     
	}
   }
   fclose(fp);
   */
   if (tid == 5)
       system("/u/srikanth/rpmbuild/./gather_data 0");
   
   printf("Thread %d done \n",tid);
   sleep(3);
   pthread_exit(NULL);
}

int main (int argc, char *argv[])
{
   pthread_t threads[NUM_THREADS];
   int rc;
   long t;
   void *status;
   pthread_mutex_init(&mutexsum[1], NULL);
   pthread_mutex_init(&mutexsum[0], NULL);

   barrier_init();
   if (argc > 1)
   ARRAY_SIZE = atoi(argv[1]);
   printf("\n Array Size %d \n", ARRAY_SIZE);
   for(t=0; t<NUM_THREADS; t++)
   {
      printf("In main: creating thread %ld\n", t);
      rc = pthread_create(&threads[t], NULL, PrintHello, (void *)(t+4));
      if (rc)
      {
         printf("ERROR; return code from pthread_create() is %d\n", rc);
         return -1;
      }
   }

   /* Last thing that main() should do */
   /* Wait on the other threads */
   for(t=0; t<NUM_THREADS; t++)
   {
	 pthread_join(threads[t], &status);
   }
   fprintf(stderr,"\n\n");
   for(t=0; t<NUM_THREADS; t++)
	 fprintf(stderr,"%d \t \n",sum[t]);
}
