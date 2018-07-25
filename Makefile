LDFLAGS=-lrt -lboost_system -lboost_filesystem -lm -lstdc++ -lpfm
CFLAGS=-Wall -Werror -Wformat=2 -Wno-unused-parameter -Wcast-qual -Wextra

all: samd sam-launch PerTask

%.o: %.c
	$(CC) -c  $(CFLAGS) -std=gnu11 $^ -o $@ $(LDFLAGS)

samd: SAM_equi.cpp cpuinfo.o util.o budgets.o cgroup.o perfMulti/perThread_perf.o
	$(CC) $(CFLAGS) -std=gnu++17 $^ -o $@ $(LDFLAGS)
	sudo chown root samd
	sudo chmod u+s samd

sam-launch: launch_wrapper.cpp cgroup.o util.o
	$(CC) $(CFLAGS) -std=gnu++17 $^ -o $@ $(LDFLAGS)

PerTask: PerTask.c perf_util.o
	$(CC) $(CFLAGS) -I/usr/include/perfmon $^ -o $@ $(LDFLAGS)

.PHONY: clean

clean:
	$(RM) samd sam-launch PerTask *.o
