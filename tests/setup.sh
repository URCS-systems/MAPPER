#!/bin/bash

# setup testing environment

source ./include.sh

joblists_dir=./workloads # change this
N_GRAPHCHI=6        # number of different graphchi applications
nprocs=$(nproc)

read -p "Remove ./workloads? (y/n)" remove_workloads

if [[ $remove_workloads == "y" ]]; then
    rm -rf $joblists_dir
    git checkout $joblists_dir
fi

for i in $(seq 1 $N_GRAPHCHI); do
    mkdir -p ~/netflix$i/
    # ln -s $graphchi_dir/smallnetflix_mm ~/netflix$i/
    # ln -s $graphchi_dir/smallnetflix_mme ~/netflix$i/
    ln -sf $GRAPHCHI_DIR/netflix_mm ~/netflix$i/
    ln -sf $GRAPHCHI_DIR/netflix_mme ~/netflix$i/
done

# setup for number of threads
sed -i "/run-parsec.sh/ s/-n \\(\\d\\+\\)\\?/-n $nprocs /g; /run-graphchi.sh/ s/--execthreads=\\(\\d\\+\\)\\? /--execthreads=$nprocs /g" $joblists_dir/*.txt

# generate Linux variants
for f in $joblists_dir/*.txt; do
    sed 's/\.\.\/sam-launch //g' $f > ${f%.txt}-Linux.txt
done

echo "Done."
