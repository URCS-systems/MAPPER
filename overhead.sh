#!/bin/sh
# feed daemon output into this script

grep -A4 'Elapsed time' | sed 's/Elapsed time (seconds)://' | sed 's/[A-Za-z]/ /g' | awk -f overhead.awk
