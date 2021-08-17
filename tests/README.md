# Tests

## Running workloads

This is how you can test the workloads that appear in the MAPPER paper:

1. run `setup.sh`
2. `make jobtest`
3. run `jobtest` on any of the workloads in `workloads/`

## Measuring scheduler performance

This is how you can measure the overhead of the scheduler phases:

1. run `perf-setup.sh`
2. `make jobtest`
3. run `perf-test.sh`
