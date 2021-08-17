#!/bin/bash

# setup environment for measuring scheduler performance

if (( $UID != 0 )); then
    echo "I need to be root"
    exit 1
fi

source ./include.sh
nprocs=$(nproc)
proc_seq=$(seq 2 2 $nprocs)
project_dir=..
N_GRAPHCHI=1

for i in $(seq 1 $N_GRAPHCHI); do
    mkdir -p ~/netflix$i/
    # ln -s $graphchi_dir/smallnetflix_mm ~/netflix$i/
    # ln -s $graphchi_dir/smallnetflix_mme ~/netflix$i/
    ln -sf $GRAPHCHI_DIR/netflix_mm ~/netflix$i/
    ln -sf $GRAPHCHI_DIR/netflix_mme ~/netflix$i/
done

# create variants of the BSGD workload with threads from 2 to `nproc`
if [ ! -d perf ]; then
    mkdir perf
fi
for i in $proc_seq; do
    sed "/run-graphchi.sh/ s/--execthreads=[[:digit:]]\\+\\? /--execthreads=$i /g" workloads/BSGD.txt > perf/BSGD-n$i.txt
done

trap 'echo "signal caught, exiting..."; exit 1' SIGTERM SIGINT

# run these variants
for i in $proc_seq; do
    $project_dir/samd > samd-n$i.out &
    samd_pid=$!
    ./jobtest -n 1 -f perf/BSGD-n$i.txt
    sleep 2         # wait for scheduler to unmanage application
    kill $samd_pid  # send SIGTERM for scheduler graceful termination
    wait            # wait for scheduler to finish
    if (( $? != 0 )); then
        echo "failed to start scheduler"
        exit 1
    fi
done
