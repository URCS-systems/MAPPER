#!/bin/sh

# feed samd output into this

grep "Elapsed time.*total.*s" | head -n-1 | awk '{print "iter-perf: " $8-$5;sum_perf+=log($5-1);sum_total+=log($8-1)}END{print "geomean perf time (without sleep): " exp(sum_perf/NR); print "geomean iter time (without sleep): " exp(sum_total/NR); print "geomean perf overhead: " (exp((sum_perf - sum_total)/NR))}'
