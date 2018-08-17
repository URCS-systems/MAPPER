#!/bin/sh
export NAS_DIR=/localdisk/srikanth/gcc8.1-install/apps/mas/NPB3.3.1/NPB3.3-OMP/
cd $NAS_DIR
exec env NAS_DIR=$NAS_DIR $@
