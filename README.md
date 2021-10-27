# MAPPER
MAPPER is a performance-aware scheduler designed to run as a user daemon. MAPPER attempts to maximize system throughput by assigning system resources to applications based on their efficiency, in contrast to the "fair share" policy of the default Linux scheduler.

MAPPER also uses sharing behavior to determine the optimal topology of resources to assign to applications. This makes MAPPER especially suited for NUMA architectures.

MAPPER uses the following primitives:

- `perf_event_open(2)` for reading performance counters of processes
- `cgroups(7)` for dividing system resources and assigning them to applications

Experiments have been conducted on Intel IvyBridge (Intel(R) Xeon(R) CPU E5-2660 v2) two-socket and Intel Haswell (Intel(R) Xeon(R) CPU E7-4820 v3) four-socket machines. MAPPER relies on Intel-specific performance counters, although it should be possible to adapt MAPPER to other platforms (like AMD).

## Dependencies
- perf version 4.17.7
- Linux kernel 4.17.11
- gcc 8.1.1 or later

## Compilation
`make -j`

## Running
1) Run monitor program SAM-MAP (samd) with root privileges : `sudo ./samd`
2) Run applications that need to be monitored with sam-launch (application launching hook): `./sam-launch APPLICATION`

## Details

### Performance counters
These are the performance counter events MAPPER uses. They are taken from Intel's Software development manual, specific to Ivy Bridge and Haswell.

- **SNOOP_HIT** and **SNOOP_HITM** (Local snoop, approximately measures intra-socket coherence): `0x06d2`
- **Instruction**: `0xc0` (used for IPC)
- **REMOTE_HITM** (approximately measure inter-socket coherence): 0x10d3
- Unhalted_Cycles: `0x3c` (used for IPC)
- **LLC_Misses** ( approximately measures memory contention): `0x412e`

These events may also be present on later Intel generations.

Manuals can be found at:https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html

Additional notes
----------------
We use thresholds based on microbenchmark based experiments discussed in [Data sharing or resource contention: toward performance transparency on multicore systems](https://dl.acm.org/citation.cfm?id=2813807). 
The thresholds are defined as Macros in `mapper.cpp` 
SAM-MAP uses the architectural information available using the `lscpu` command to understand cores and sockets in the underlying system automatically.
Can use commands like "lscpu" and "htop" to debug any abnormalities observed.

Copyright Notice
----------------

Copyright &copy; 2017-2021 University of Rochester. All rights reserved.

Authors: Sharanyan Srikanthan, Sayak Chakraborti, Princeton Ferro, Sandhya Dwarkadas; Department of Computer Science

MAPPER is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

MAPPER is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with MAPPER.  If not, see <https://www.gnu.org/licenses/>.
