CFLAGS=-Wall -Werror -Wformat=2 -Wno-unused-parameter -Wcast-qual -Wextra -g3 -ggdb3
OBJDIR=obj

all: samd sam-launch

$(OBJDIR):
	mkdir $@
	mkdir $@/schedulers

$(OBJDIR)/%.o: %.c $(OBJDIR)
	$(CC) -c  $(CFLAGS) -std=gnu11 $< -o $@

SCHEDULERS=$(patsubst %.c,$(OBJDIR)/%.o,$(wildcard schedulers/*.c))

samd: mapper.cpp $(OBJDIR)/cpuinfo.o $(OBJDIR)/util.o $(OBJDIR)/budgets.o $(OBJDIR)/cgroup.o $(OBJDIR)/perfio.o $(SCHEDULERS)
	$(CC) $(CFLAGS) -std=gnu++11 $^ -o $@ -lstdc++ -lm -lrt

sam-launch: $(OBJDIR)/launcher.o $(OBJDIR)/cgroup.o $(OBJDIR)/util.o
	$(CC) $(CFLAGS) -std=gnu11 $^ -o $@

.PHONY: clean

clean:
	$(RM) samd sam-launch $(OBJDIR)/*.o $(OBJDIR)/schedulers/*.o
	rmdir $(OBJDIR)/schedulers
	rmdir $(OBJDIR)
