#include "cpuinfo.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h> /* for get_nprocs() */
#include <stdbool.h>

#define MAX_CPUS 1024


const struct cpu *get_cpu(int i) {
  static struct cpu info;
  FILE *fp = NULL;
  char path[1024];

  info.tnumber = i;
  snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/topology/core_id", i);

  if ((fp = fopen(path, "r"))) {
    fscanf(fp, "%d", &info.core_id);
    fclose(fp);
  } else {
    fprintf(stderr, "Could not open %s: %s\n", path, strerror(errno));
    return NULL;
  }

  snprintf(path, sizeof path,
           "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", i);
  if ((fp = fopen(path, "r"))) {
    fscanf(fp, "%d", &info.sock_id);
    fclose(fp);
  } else {
    fprintf(stderr, "Could not open %s: %s\n", path, strerror(errno));
    return NULL;
  }

  return &info;
}

struct cpuinfo *get_cpuinfo(void) {
  const struct cpu *ci;
  int num_cpus = 0;
  int nprocs = get_nprocs();
  struct cpu cpus[MAX_CPUS];
  struct cpu_socket sockets[MAX_CPUS];
  int num_sockets = 0;
  int num_cores = 0;

  if (nprocs > MAX_CPUS) {
    fprintf(stderr, "nprocs (%d) > MAX_CPUS (%d)\n", nprocs, MAX_CPUS);
    return NULL;
  }

  for (int i = 0; i < nprocs && (ci = get_cpu(i)); ++i) {
    cpus[i] = *ci;
    num_cpus++;
  }

  if (num_cpus != nprocs)
    return NULL;

  memset(sockets, 0, sizeof sockets);
  for (int i = 0; i < num_cpus; ++i) {
    // int sock_cpus = sockets[cpus[i].sock_id].num_cpus;
    sockets[cpus[i].sock_id].num_cpus++;
    num_sockets = cpus[i].sock_id > num_sockets ? cpus[i].sock_id : num_sockets;
    num_cores = cpus[i].core_id > num_cores ? cpus[i].core_id : num_cores;
  }

  num_sockets++;
  num_cores++;

  struct cpuinfo *cpuinfo = (struct cpuinfo*) malloc(sizeof *cpuinfo);

  cpuinfo->sockets = (struct cpu_socket*) calloc(num_sockets, sizeof cpuinfo->sockets[0]);
  cpuinfo->num_sockets = num_sockets;
  cpuinfo->total_cpus = num_cpus;
  cpuinfo->total_cores = num_cores;

  for (int i = 0; i < num_cpus; ++i) {
    if (cpuinfo->sockets[cpus[i].sock_id].cpus == NULL) {
      cpuinfo->sockets[cpus[i].sock_id].cpus =
          (struct cpu*) calloc(sockets[cpus[i].sock_id].num_cpus,
                 sizeof cpuinfo->sockets[cpus[i].sock_id].cpus[0]);
    }
    int j = cpuinfo->sockets[cpus[i].sock_id].num_cpus;
    cpuinfo->sockets[cpus[i].sock_id].cpus[j] = cpus[i];
    cpuinfo->sockets[cpus[i].sock_id].num_cpus++;
  }


  bool got_clock_rate = false;
  const char *cmds[] = {
      "lscpu | grep 'CPU max MHz:' | awk '{print $4}'",
      "lscpu | grep 'CPU MHz:' | awk '{print $3}'"
  };

  double clock_rate_mhz = 1000; /* 1GHz is a good estimate */
  for (size_t tries = 0; tries < sizeof(cmds) / sizeof(cmds[0]) && !got_clock_rate; ++tries) {
      FILE *lscpu_f = popen(cmds[tries], "r");
      if (lscpu_f) {
          char *line = NULL;
          size_t sz = 0;
          getline(&line, &sz, lscpu_f);
          if (!line || sscanf(line, "%lf", &clock_rate_mhz) != 1) {
              fprintf(stderr, "failed to get clock rate: expected double, got '%s'\n", line);
          } else
              got_clock_rate = true;
          free(line);
          pclose(lscpu_f);
      } else
          perror("popen");
  }
  cpuinfo->clock_rate = (unsigned long) (clock_rate_mhz * 1000000);

  return cpuinfo;
}
