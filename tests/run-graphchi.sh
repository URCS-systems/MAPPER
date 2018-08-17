#!/bin/sh
export GRAPHCHI_DIR=/u/srikanth/graphchi/graphchi-cpp-master
cd $GRAPHCHI_DIR
exec env GRAPHCHI_DIR=$GRAPHCHI_DIR $@
