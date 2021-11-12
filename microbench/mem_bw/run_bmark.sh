#!/bin/bash

declare -a LEVELS=("lo" "hi")
MAX_TASKS=10

mkdir -p mem_output

for level in "${LEVELS[@]}"
do
    for ((i=1;i<=${MAX_TASKS};i++));
    do
        out_file=${level}_10_${i}
        echo ${out_file}
        time perf stat -o mem_output/mb_mem${out_file}_stats.txt -e r3c,rc0,r412e ./mb_main ${level} 10 ${i} &> mem_output/mb_mem_${out_file}.txt
    done
done

