#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

int foo(const char *whoami)
{
        printf("I am a %s.  My pid is:%d  my ppid is %d\n",
                        whoami, getpid(), getppid() );
        return 1;
}

int main(void)
{
        int n= 10;
        int i=0;
        int status=0;

        int x = 5;

        printf("Creating %d children\n", n);
        foo("parent");
        // for(i=0;i<n;i++)
        {
                pid_t pid=fork();

                if (pid==0) /* only execute this if child */
                {
                        foo("child 1");
                        pid_t pid1=fork();

                        if (pid1==0) /* only execute this if child */
                        {
                            foo("child 2");
                            exit(0);
                        }
                        {
                                x++;
                                printf("x value in for: %d ", x);
                for (pid=0;pid<100000;pid++)
                    for(i = 0; i < 100000; i++)
                        for(n = 0; n < 1000000; n++);
                
                        }

                        //wait(&status);
                        exit(0);

                }
                
                sleep(200);  /* only the parent waits */
        }

        x = 100 * x;
        printf("x value in main: %d ", x);


        return 0;
}
