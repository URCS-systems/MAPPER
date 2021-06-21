Experiments have been ran using the perf_event_open interface on Intel IvyBridge and Intel Haswell machines.

Dependencies:-
------------
perf version 4.17.7
Linux 4.17.11
gcc 8.1.1

Compilation:-
-----------
run Makefile

Running:-
---------
1) Run monitor program SAM-MAP (samd) with root privilege : "sudo samd"
2) Run applications that need to be monitored with sam-launch (application launching hook): ./sam-launch app 

Performance events: (taken from Intel's Software development manual, specific to IvyBridge and Haswell)
--------------------
SNOOP_HIT and SNOOP_HITM (Local snoop, approximately measures intra-socket coherence): 0x06d2
Instruction: 0xc0 (used for IPC)
Remote_HITM (approximately measure inter-socket coherence): 0x10d3
Unhalted_Cycles: 0x3c (used for IPC)
LLC_Misses ( approximately measures memory contention): 0x412e



perfio.c added which contains perf multiplexing functionality
Changes made only to mapper.cpp
1) Argument added to PerfData:readCounters and PerfData:printCounters for copying performance counter values into pre-existing data structure
2) system call to use libpfm disbaled in PerfData:initialize() line 405 as now we are dependent on perf_event_open for performance counters
3) Code added to copy into data structure in PerfData:printCounters
4) options->pid has been changed to pid in some places
5) In main, initialize_events being called, and then in loop(GOTO) count_event_perfMultiplex and displayEvents being called with a list of TIDs 


To Do:
-----
1) Remote DRAM is being monitored and gathered, but not sure whether it is being used, hence haven't populated that. (Is a simple one line assignment) 

FIX 1 for crashing
------------------

For some TIDs perf_event_open is unable to return a valid file descriptor. These TIDs are short-lived and I generally encounter them at near the termination of an application and are not the application TIDs. Maybe they are too short lived to make the per_event_open system call and that is the issue. Anyways I have fixed the crashing of samd by error checking some of the fds. For the time being this will hopefully work. I have tested samd and it doesn’t crash anymore. For the TIds unmonitored, the event counts has been explicitly set to 0.


