# compile and link flags
PROJECT_ROOT = .
CFLAGS=-std=gnu11 -Wall -Wextra -Wformat=2
CCFLAGS = -Wall -g --static
LDFLAGS = -lpthread -Wall -g

.IGNORE:

# make rules
TARGETS = mb_main

all: $(TARGETS)

%.o: %.c
	$(CC) -c  $(CFLAGS) $^ -o $@

.cc.o:
	g++ $(CCFLAGS) -c -o $@ $<

mb_main: mb_main.o mb_util.o 
	g++ $(LDFLAGS) -o $@ $^

mb_util.o: mb_util.h mb_platform.h
mb_main.o: mb_util.h mb_platform.h hw_timer.h

clean:
	-rm -f *~ *.o $(TARGETS)
