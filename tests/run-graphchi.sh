#!/bin/sh
source ./include.sh

cd $GRAPHCHI_DIR
exec env GRAPHCHI_DIR=$GRAPHCHI_DIR $@
