#!/bin/bash

# setup testing environment

source ./include.sh

read -p "Reset $joblists_dir? (y/N)" remove_workloads

if [[ $remove_workloads == "y" ]]; then
    rm -rf $joblists_dir
    git checkout $joblists_dir
fi

if [ ! -d "$GRAPHCHI_DIR" ]; then
    if ! which git 2>/dev/null; then
        echo "Cannot download anything. Please install git"
        exit 1
    fi
    git clone https://github.com/GraphChi/graphchi-cpp "$GRAPHCHI_DIR"
fi

for i in $(seq 1 $N_GRAPHCHI); do
    mkdir -p "$GRAPHCHI_DIR/netflix$i/"
    ln -sf "$GRAPHCHI_DIR/smallnetflix_mm" "$GRAPHCHI_DIR/netflix$i/"
    ln -sf "$GRAPHCHI_DIR/smallnetflix_mme" "$GRAPHCHI_DIR/netflix$i/"
    ln -sf "$GRAPHCHI_DIR/netflix_mm" "$GRAPHCHI_DIR/netflix$i/"
    ln -sf "$GRAPHCHI_DIR/netflix_mme" "$GRAPHCHI_DIR/netflix$i/"
done

# setup for number of threads
sed -i "/run-parsec.sh/ s/-n [[:digit:]]\\+\\?/-n $nprocs /g; /run-graphchi.sh/ s/--execthreads=[[:digit:]]\\+\\? /--execthreads=$nprocs /g" $joblists_dir/*.txt

# generate Linux variants
for f in $joblists_dir/*.txt; do
    sed 's/\.\.\/sam-launch //g' $f > ${f%.txt}-Linux.txt
done

echo "Done."
