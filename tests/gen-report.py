#!/bin/env python3

import sys
# import numpy as np
# import pandas as pd
import os
import re
import csv
from pathlib import Path
from math import log, exp
from functools import reduce

standalone_dir = 'one'
results = {}

def get_standalone(appname):
    for workload in results[standalone_dir]['linux']:
        if workload['apps'] == [appname]:
            return workload
    return None

# read in all results
for f in Path('results').glob('*/*/*.csv'):
    workload_size, strategy = re.match(r'results/(\w+)/(\w+)/.*', str(f)).group(1,2)

    if not workload_size in results:
        results[workload_size] = {}

    if not strategy in results[workload_size]:
        results[workload_size][strategy] = []

    workload = { 'f': f, 'apps': [], 'data': [] }

    with open(str(f), mode='r') as csv_file:
        csv_reader = csv.DictReader(csv_file)
        lineno = 0
        for row in csv_reader:
            row = {k.replace(' ', ''):float(v.replace(' ','')) for (k,v) in row.items()}
            workload['data'].append(row)
            # print(f'{f}:{lineno+1}: {row}\n')
            lineno = lineno + 1

    # check number of apps
    for k in workload['data'][0].keys():
        if not re.match(r'^[A-Za-z0-9\-]+$', k):
            break
        workload['apps'].append(k)

    if not workload['apps']:
        raise Exception(f'{f}: has no apps!')

    # check for other things
    for app in workload['apps']:
        standalone = Path('results') / standalone_dir / 'linux' / f'{app}-Linux.txt.csv'
        #if not (Path('workloads') / f'{app}.txt').exists():
        #    raise Exception(f'{f}: app {app} does not exist')
        #if not standalone.exists():
        #    raise Exception(f'{f}: app {app} does not have a standalone to compare to ({standalone} does not exist)')

    results[workload_size][strategy].append(workload)

# verify our loaded data
for (workload_size, strategy_list) in results.items():
    for (strategy, workload_list) in strategy_list.items():
        for workload in workload_list:
            for app in workload['apps']:
                if not app in reduce(lambda a,b: a+b, map(lambda a: a['apps'], results[standalone_dir]['linux'])):
                    raise Exception(f"{workload['f']}: app {app} does not have a standalone to compare to ({app} is not a column in any of the CSV files in results/{standalone_dir}/linux)")

# generate pages
for (workload_size, strategy_list) in results.items():
    with open(f'results-page-{workload_size}.csv', mode='a', newline='') as page_file:
        csv_writer = csv.writer(page_file, delimiter=' ')
        for (strategy, workload_list) in strategy_list.items():
            for workload in workload_list:
                # get average runtimes for all apps
                apps_avg_runtime = {appname: 0 for appname in workload['apps']}
                lineno = 0

                # write the header
                csv_writer.writerow([strategy])
                csv_writer.writerow(workload['data'][0].keys())

                for row in workload['data']:
                    for appname in workload['apps']:
                        apps_avg_runtime[appname] += row[appname]
                        if not row[appname]:
                            raise Exception(f"{workload['f']}:{lineno+1}: app {appname} has a zero entry")
                    # also write data
                    csv_writer.writerow(row.values())
                    lineno = lineno + 1

                apps_avg_runtime = {appname: v/len(workload['data']) for appname,v in apps_avg_runtime.items()}
                workload['avg-runtimes'] = apps_avg_runtime
                csv_writer.writerow(['AMean:'] + list(apps_avg_runtime.values()))

                # if this is not a standalone item
                if workload_size != standalone_dir:
                    # compute average speedups relative to standalone
                    workload['avg-speedups'] = {}
                    for (app, avg_runtime) in workload['avg-runtimes'].items():
                        standalone_workload = get_standalone(app)
                        workload['avg-speedups'][app] = standalone_workload['avg-runtimes'][app] / workload['avg-runtimes'][app]
                    avg_speedup_vals = list(workload['avg-speedups'].values())
                    # write the average speedups
                    csv_writer.writerow(['Speedups:'] + avg_speedup_vals)
                    workload['gmean-speedup'] = exp(reduce(lambda a,b: a+b, map(lambda a: log(a), avg_speedup_vals))/len(avg_speedup_vals))
                    # calculate geomean
                    csv_writer.writerow(['Speedup GMean:'] + [workload['gmean-speedup']])

# generate summary page
with open(f'results-page-summary.csv', mode='a') as page_file:
    strategies = ['linux', 'sam_map', 'hill_climbing', 'fair_share'] # list(results[standalone_dir].keys())
    csv_writer = csv.DictWriter(page_file, fieldnames=[' ', 'workload'] + strategies)
    for (workload_size, strategy_list) in results.items():
        # don't display standalone items on the summary page
        if workload_size == standalone_dir:
            continue
        csv_writer.writerow(dict([(' ', f'{workload_size} apps'), ('workload','')] + [(s,'') for s in strategies]))
        csv_writer.writeheader()
        workload_rows = {}
        for (strategy, workload_list) in strategy_list.items():
            for workload in workload_list:
                workload_name = reduce(lambda a,b: f'{a}-{b}', workload['apps'])
                if not workload_name in workload_rows:
                    workload_rows[workload_name] = {s:0 for s in strategies}
                workload_rows[workload_name][strategy] = workload['gmean-speedup']

        for (workload_name, strategy_speedups) in workload_rows.items():
            csv_writer.writerow(dict([(' ',''), ('workload', workload_name)] + list(strategy_speedups.items())))

print ("Done.")
