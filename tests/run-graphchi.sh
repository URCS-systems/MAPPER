#!/bin/sh
GRAPHCHI_DIR=~/Documents/summer2018-research/graphchi-cpp   # change this

cd $GRAPHCHI_DIR
exec env GRAPHCHI_DIR=$GRAPHCHI_DIR $@
