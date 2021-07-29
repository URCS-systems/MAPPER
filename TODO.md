### Notes

1. The max CPUs an application should get, should be restricted to its active threads, which can be found in bottleneck[METRIC_ACTIVE]
	The colocate must take advantage of this to colocate threads even when its just one application running with less than 20 threads. 
2. Introduce the concept of fair share of CPUs for every application, to ensure an incoming application has the appropriate allocation.
3. This share will be modified after the first introduction of an application based on the MAP logic. Refer to the paper for 
	the outline. This is the bulk of the work to be done here. Must start work on this ASAP. 
4. Only for Sayak, Me: Working on replacing libpfm entirely so that counters can be used without multiple threads doing their stuff.
	The library still has sufficient inaccuracy to motivate this change. Also, the current method of collection counter information
	tends itself well to the Sayak perf based method better. To do that from the launcher would be significantly harder.
	So lets fix this for me. 
5. Introduce notion of Instructions per second alongside of the current IPC. 
6. Lot of garbage collection to be added at the end of the code. 

### Cleanup

- [ ] instructions in README
- [x] split schedulers into different files/executables
- [x] clean up code
