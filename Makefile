CFLAGS=-Wall -Werror -Wformat=2 -Wno-unused-parameter -Wcast-qual -Wextra -g3 -ggdb3

all: samd sam-launch

%.o: %.c
	$(CC) -c  $(CFLAGS) -std=gnu11 $^ -o $@

samd: SAM_equi.cpp cpuinfo.o util.o budgets.o cgroup.o perfMulti/perThread_perf.o
	$(CC) $(CFLAGS) -std=gnu++11 $^ -o $@ -lstdc++ -lm -lrt

sam-launch: launcher.o cgroup.o util.o
	$(CC) $(CFLAGS) -std=gnu11 $^ -o $@

.PHONY: clean

clean:
	$(RM) samd sam-launch *.o
