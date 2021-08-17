# compute the geomean of each row
# should be called from overhead.sh

BEGIN {
    RS=""
    FS="\n"
}

$5 > 0 {                            # only process records when the schduler was running
    sums["sleep"     ]+=log($1)
    sums["perf"      ]+=log($2)
    sums["perf setup"]+=log($3)
    sums["perf read" ]+=log($4)
    sums["scheduler" ]+=log($5)
    sums["cgroups"   ]+=log($6)
    sums["total"     ]+=log($7)
    processed++
}

END {
    if (processed > 0) {
        print "Elapsed time (geomean seconds):"
        print "  sleep     " exp(sums["sleep"]/processed)
        print "  perf      " exp(sums["perf"]/processed)
        print "    setup   " exp(sums["perf setup"]/processed)
        print "    read    " exp(sums["perf read"]/processed)
        print "  scheduler " exp(sums["scheduler"]/processed)
        print "  cgroups   " exp(sums["cgroups"]/processed)
        print "  total     " exp(sums["total"]/processed)
    } else if (NR > 0) {
        print "Scheduler was not running at any time."
    }
}
