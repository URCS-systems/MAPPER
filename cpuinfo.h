#ifndef CPUINFO_H
#define CPUINFO_H

struct cpu_socket {
  struct cpu *cpus;
  int num_cpus;
};

struct cpu {
  int core_id;
  int sock_id;
  int tnumber; // thread number
};

struct cpuinfo {
  struct cpu_socket *sockets;
  int num_sockets;
  int total_cpus;
  int total_cores;
  unsigned long clock_rate;
};

#if defined(__cplusplus)
extern "C" {
#endif

struct cpuinfo *get_cpuinfo(void);

#if defined(__cplusplus)
};
#endif

#endif
