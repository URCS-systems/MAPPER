#!/bin/bash

parsec_dir=~/Documents/summer2018-research/parsec-3.0   # change this

cd $parsec_dir
source env.sh           # needed for parsecmgmt
exec $@
