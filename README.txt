Experiments have been ran using the perf_event_open interface on Intel IvyBridge (Intel(R) Xeon(R) CPU E5-2660 v2) two socket and Intel Haswell (Intel(R) Xeon(R) CPU E7-4820 v3) four socket  machines.

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
1) Run monitor program SAM-MAP (samd) with root privilege : "sudo ./samd"
2) Run applications that need to be monitored with sam-launch (application launching hook): ./sam-launch app

Performance events: (taken from Intel's Software development manual, specific to IvyBridge and Haswell)
--------------------
SNOOP_HIT and SNOOP_HITM (Local snoop, approximately measures intra-socket coherence): 0x06d2
Instruction: 0xc0 (used for IPC)
Remote_HITM (approximately measure inter-socket coherence): 0x10d3
Unhalted_Cycles: 0x3c (used for IPC)
LLC_Misses ( approximately measures memory contention): 0x412e

Additional notes
----------------
SAM-MAP uses the architectural information available using the lscpu comman to understand cores and sockets in the underlying system automatically.
Can use commands like "lscpu" and "htop" to debug any abnormalities observed.

