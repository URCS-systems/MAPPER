#!/bin/bash

source include.sh

if (( $# < 2 )); then
    echo "Usage: $0 scheduler-name num-apps-in-workload [num-runs]"
    exit 1
fi

results_dir=results
scheduler_name=$1
num_apps=$2
num_runs=${3:-3}
regex="^"
anti_regex="^\$"    # don't filter out anything

workloads=()

if [ ! -d $results_dir/$num_apps/$scheduler_name ]; then
    echo "ERROR: $results_dir/$num_apps/$scheduler_name doesn't exist"
    exit 1
fi

for i in $(seq 1 $num_apps); do
    if (( $i == 1 )); then
        regex="^([[:alnum:]]+)"
    else
        regex="$regex-([[:alnum:]]+)"
    fi
done

if [[ $scheduler_name == "linux" ]]; then
    regex="$regex-Linux"
else
    anti_regex="Linux"
fi

regex="$regex.txt\$"

csvs=($(ls $joblists_dir | grep -E $regex | grep -v $anti_regex))

for fname in ${csvs[@]}; do
    if [ ! -e $results_dir/$num_apps/$scheduler_name/$fname.csv ]; then
        echo "-f \"$joblists_dir/$fname\""
    fi
done | xargs -t ./jobtest -n $num_runs


csvs=($(printf '%s\n' ${csvs[@]} | while read fname; do if [ -e "$fname.csv" ]; then echo "$fname.csv"; fi; done))

if (( ${#csvs[@]} > 0 )); then
    mv -t $results_dir/$num_apps/$scheduler_name/ ${csvs[@]}
fi

echo "Done. Check the CSVs in $results_dir/$num_apps/$scheduler_name/"
