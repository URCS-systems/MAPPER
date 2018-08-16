#!/bin/sh

# feed samd output into this

grep "Elapsed time.*total.*s" | awk '{print $8-$5;sum_amt+=log($5-1) - log($8-1);sum_perf+=log($5-1);sum_total+=log($8-1)}END{print "geomean perf time: " exp(sum_perf/NR); print "geomean iter time: " exp(sum_total/NR); print "geomean perf overhead: " (exp(sum_amt/NR))}'
