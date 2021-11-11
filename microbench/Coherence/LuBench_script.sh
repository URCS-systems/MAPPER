#!/bin/sh 
## declare an array variable
# r3c=UNHALTED_CORE_CYCLE,rc0=INSTRUCTIONS_RETIRED,r06d2=SNOOP HIT+HITM,r10d3=REMOTE HITM,r412e=LLC MISSES
declare -a arraySize=("4" "8" "16" "32" "64" "128" "256" "512")

mkdir -p coherence_output
## now loop through the above array
for i in "${arraySize[@]}"
do
   for ((j=1;j<=2;j++));
   do	
       inter_file=lubench_inter_mem${i}_${j}
       intra_file=lubench_intra_mem${i}_${j}      
       echo ${intra_file}
       time perf stat -o coherence_output/LuBench_2_intra_mem${i}_${j}_stats.txt -e r3c,rc0,r06d2,r10d3,r412e ./newLuBench_2_intra ${i} &> coherence_output/LuBench_2_intra_mem${i}_${j}.txt
       sleep 10
       echo ${inter_file}
       time perf stat -o coherence_output/LuBench_2_inter_mem${i}_${j}_stats.txt -e r3c,rc0,r06d2,r10d3,r412e ./newLuBench_2_inter ${i} &> coherence_output/LuBench_2_inter_mem${i}_${j}.txt
       sleep 10
   done
done

