#!/bin/sh 

#Need to change line 73-74 to CPU_SET(tid*2, &mask)
#maps all threads to same socket if (two sockets exists)

#Intra-socket coherence:
#---------------------
gcc LuBench.c -lpthread -o newLuBench_2_intra -w


#Need to change line 73-74 to CPU_SET(tid, &mask)
#maps all threads to different sockets if (two sockets exists)

#Inter-socket coherence:
#---------------------
gcc LuBench.c -lpthread -o newLuBench_2_inter -w
