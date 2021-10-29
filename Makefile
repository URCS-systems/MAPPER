CFLAGS=-Wall -Werror -Wformat=2 -Wcast-qual -Wextra -g3 -ggdb3
OBJDIR=obj

all: samd sam-faird sam-hillclimbd perfmon sam-launch

$(OBJDIR):
	mkdir $@

$(OBJDIR)/schedulers: | $(OBJDIR)
	mkdir $@

$(OBJDIR)/schedulers/sam: | $(OBJDIR)/schedulers
	mkdir $@

$(OBJDIR)/schedulers/sam-fair.o: schedulers/sam.c | $(OBJDIR) $(OBJDIR)/schedulers $(OBJDIR)/schedulers/sam
	$(CC) -c $(CFLAGS) -std=gnu11 -DFAIR $< -o $@

$(OBJDIR)/schedulers/sam/fair.o: schedulers/sam/fair.c | $(OBJDIR) $(OBJDIR)/schedulers $(OBJDIR)/schedulers/sam
	$(CC) -c $(CFLAGS) -std=gnu11 -DFAIR $< -o $@

$(OBJDIR)/schedulers/sam-hillclimb.o: schedulers/sam.c | $(OBJDIR) $(OBJDIR)/schedulers $(OBJDIR)/schedulers/sam
	$(CC) -c $(CFLAGS) -std=gnu11 -DHILL_CLIMBING $< -o $@

$(OBJDIR)/schedulers/sam/hillclimb.o: schedulers/sam/hillclimb.c | $(OBJDIR) $(OBJDIR)/schedulers $(OBJDIR)/schedulers/sam
	$(CC) -c $(CFLAGS) -std=gnu11 -DHILL_CLIMBING $< -o $@

$(OBJDIR)/%.o: %.c | $(OBJDIR) $(OBJDIR)/schedulers $(OBJDIR)/schedulers/sam
	$(CC) -c $(CFLAGS) -std=gnu11 $< -o $@

samd: mapper.cpp $(OBJDIR)/cpuinfo.o $(OBJDIR)/util.o $(OBJDIR)/budgets.o $(OBJDIR)/cgroup.o $(OBJDIR)/perfio.o $(OBJDIR)/schedulers/sam.o $(OBJDIR)/schedulers/sam/default.o
	$(CXX) $(CFLAGS) -std=c++11 $^ -o $@ -lrt

sam-faird: mapper.cpp $(OBJDIR)/cpuinfo.o $(OBJDIR)/util.o $(OBJDIR)/budgets.o $(OBJDIR)/cgroup.o $(OBJDIR)/perfio.o $(OBJDIR)/schedulers/sam-fair.o $(OBJDIR)/schedulers/sam/fair.o
	$(CXX) $(CFLAGS) -std=c++11 -DFAIR $^ -o $@ -lrt

sam-hillclimbd: mapper.cpp $(OBJDIR)/cpuinfo.o $(OBJDIR)/util.o $(OBJDIR)/budgets.o $(OBJDIR)/cgroup.o $(OBJDIR)/perfio.o $(OBJDIR)/schedulers/sam-hillclimb.o $(OBJDIR)/schedulers/sam/hillclimb.o
	$(CXX) $(CFLAGS) -std=c++11 -DHILL_CLIMBING $^ -o $@ -lrt

perfmon: mapper.cpp $(OBJDIR)/cpuinfo.o $(OBJDIR)/util.o $(OBJDIR)/budgets.o $(OBJDIR)/cgroup.o $(OBJDIR)/perfio.o
	$(CXX) $(CFLAGS) -std=c++11 -DJUST_PERFMON $^ -o $@ -lrt

sam-launch: $(OBJDIR)/launcher.o $(OBJDIR)/cgroup.o $(OBJDIR)/util.o
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean

clean: $(OBJDIR)
	$(RM) samd sam-faird sam-hillclimbd nupocod perfmon sam-launch $(OBJDIR)/*.o $(OBJDIR)/schedulers/*.o $(OBJDIR)/schedulers/*/*.o
	rmdir $(OBJDIR)/schedulers/sam
	rmdir $(OBJDIR)/schedulers
	rmdir $(OBJDIR)
