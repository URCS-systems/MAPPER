#!/bin/sh
export GAUSS_DIR=/localdisk/srikanth/gcc8.1-install/apps/
cd $GAUSS_DIR
exec env GAUSS_DIR=$GAUSS_DIR $@
