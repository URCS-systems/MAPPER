#!/bin/sh
export GRAPHCHI_DIR=~/Documents/summer2018-research/graphchi-cpp
cd $GRAPHCHI_DIR
exec env GRAPHCHI_DIR=$GRAPHCHI_DIR $@