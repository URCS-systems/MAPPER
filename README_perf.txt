Directory perfMulti added which contains perf multiplexing functionality
Changes made only to SAM.cpp
1) Includes file perThread_perf.c
2) Argument added to PerfData:readCounters and PerfData:printCounters for copying performance counter values into pre-existing data structure
3) system call to use libpfm disbaled in PerfData:initialize() line 405 as now we are dependent on perf_event_open for performance counters
4) Code added to copy into data structure in PerfData:printCounters
5) options->pid has been changed to pid in some places
6) In main, initialize_events being called, and then in loop(GOTO) count_event_perfMultiplex and displayEvents being called with a list of TIDs 


To Do:
-----
1) Remote DRAM is being monitored and gathered, but not sure whether it is being used, hence haven't populated that. (Is a simple one line assignment) 

FIX 1 for crashing
------------------

For some TIDs perf_event_open is unable to return a valid file descriptor. These TIDs are short-lived and I generally encounter them at near the termination of an application and are not the application TIDs. Maybe they are too short lived to make the per_event_open system call and that is the issue. Anyways I have fixed the crashing of samd by error checking some of the fds. For the time being this will hopefully work. I have tested samd and it doesn’t crash anymore. For the TIds unmonitored, the event counts has been explicitly set to 0.


