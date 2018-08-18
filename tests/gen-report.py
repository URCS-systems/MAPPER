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

results_dir = 'results'
standalone_dir = '1'
linux_schedname = 'linux'
# I should've called these 'schednames' but it's too late now and I'm too lazy
strategies = [linux_schedname]
results = {}

def get_standalone(appname):
    for workload in results[standalone_dir][linux_schedname]:
        if workload['apps'] == [appname]:
            return workload
    return None

# read in all results
for f in Path(results_dir).glob('*/*/*.csv'):
    workload_size, strategy = re.match(results_dir + r'/(\w+)/(\w+)/.*', str(f)).group(1,2)

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
    #for app in workload['apps']:
        #standalone = Path(results_dir) / standalone_dir / linux_schedname / f'{app}-Linux.txt.csv'
        #if not (Path('workloads') / f'{app}.txt').exists():
        #    raise Exception(f'{f}: app {app} does not exist')
        #if not standalone.exists():
        #    raise Exception(f'{f}: app {app} does not have a standalone to compare to ({standalone} does not exist)')

    if not strategy in strategies:
        strategies.append(strategy)

    results[workload_size][strategy].append(workload)

# check for some things
if not standalone_dir in results:
    raise Exception(f'No directory with standalone runs. ({results_dir}/{standalone_dir} is empty or does not exist)')

if not linux_schedname in results[standalone_dir]:
    raise Exception(f'No standalone Linux runs to compare anything to. ({results_dir}/{standalone_dir}/{linux_schedname} is empty or does not exist)')

# verify our loaded data
for (workload_size, strategy_list) in results.items():
    for (strategy, workload_list) in strategy_list.items():
        for workload in workload_list:
            for app in workload['apps']:
                if not app in reduce(lambda a,b: a+b, map(lambda a: a['apps'], results[standalone_dir][linux_schedname])):
                    raise Exception(f"{workload['f']}: app {app} does not have a standalone to compare to ({app} is not a column in any of the CSV files in {results_dir}/{standalone_dir}/{linux_schedname})")

# compute average runtimes
for (workload_size, strategy_list) in results.items():
    for (strategy, workload_list) in strategy_list.items():
        for workload in workload_list:
            # get average runtimes for all apps
            apps_avg_runtime = {appname: 0 for appname in workload['apps']}
            lineno = 1  # the first line is the header

            for row in workload['data']:
                for appname in workload['apps']:
                    if not row[appname]:
                        raise Exception(f"{workload['f']}:{lineno+1}: app {appname} has a zero entry")
                    apps_avg_runtime[appname] += row[appname]
                lineno = lineno + 1

            apps_avg_runtime = {appname: v/len(workload['data']) for appname,v in apps_avg_runtime.items()}
            workload['avg-runtimes'] = apps_avg_runtime

# compute speedups
for (workload_size, strategy_list) in results.items():
    for (strategy, workload_list) in strategy_list.items():
        for workload in workload_list:
            # if this is not a standalone item
            if workload_size != standalone_dir:
                # compute average speedups relative to standalone
                workload['avg-speedups'] = {}
                for (app, avg_runtime) in workload['avg-runtimes'].items():
                    standalone_workload = get_standalone(app)
                    workload['avg-speedups'][app] = standalone_workload['avg-runtimes'][app] / workload['avg-runtimes'][app]
                avg_speedup_vals = list(workload['avg-speedups'].values())
                # calculate geomean
                workload['gmean-speedup'] = exp(reduce(lambda a,b: a+b, map(lambda a: log(a), avg_speedup_vals))/len(avg_speedup_vals))


# generate pages
for (workload_size, strategy_list) in results.items():
    with open(f'results-page-{workload_size}.csv', mode='a', newline='') as page_file:
        csv_writer = csv.writer(page_file, delimiter=',')
        for (strategy, workload_list) in strategy_list.items():
            for workload in workload_list:
                # write the header
                csv_writer.writerow([strategy])
                # add an extra column on the left to align with AMean
                csv_writer.writerow([' '] + list(workload['data'][0].keys()))

                for row in workload['data']:
                    # add an extra column on the left to align with AMean
                    csv_writer.writerow([' '] + list(row.values()))

                csv_writer.writerow(['AMean:'] + list(workload['avg-runtimes'].values()))

                # if this is not a standalone item
                if workload_size != standalone_dir:
                    # write the average speedups
                    csv_writer.writerow(['Speedups:'] + list(workload['avg-speedups'].values()))
                    csv_writer.writerow(['Speedup GMean:'] + [workload['gmean-speedup']])

# generate summary page
with open(f'results-page-summary.csv', mode='a') as page_file:
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
