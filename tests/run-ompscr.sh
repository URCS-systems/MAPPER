#!/bin/sh
export OMPSCR_DIR=/localdisk/srikanth/gcc8.1-install/apps/OmpSCRv2.0/OmpSCR_v2.0/bin/
cd $OMPSCR_DIR
exec env OMPSCR_DIR=$OMPSCR_DIR $@
