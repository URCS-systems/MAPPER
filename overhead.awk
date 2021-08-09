# compute the geomean of each row
# should be called from overhead.sh

BEGIN {
    RS=""
    FS="\n"
}

$2 > 0 {                            # only process records where the scheduler was running
    sums["perf"     ]+=log($1)
    sums["scheduler"]+=log($2)
    sums["cgroups"  ]+=log($3)
    sums["total"    ]+=log($4)
    processed++
}

END {
    if (processed > 0) {
        print "Elapsed time (geomean seconds):"
        print "  perf      " exp(sums["perf"]/processed)
        print "  scheduler " exp(sums["scheduler"]/processed)
        print "  cgroups   " exp(sums["cgroups"]/processed)
        print "  total     " exp(sums["total"]/processed)
    } else if (NR > 0) {
        print "Scheduler was not running at any time."
    }
}
