CFLAGS=-std=gnu11 -g3 -ggdb -Wall -Wextra -Werror -Wno-unused-parameter -pthread

.PHONY: clean

all: jobtest
	sudo setcap cap_sys_resource+ep jobtest
	setcap -v cap_sys_resource+ep jobtest

jobtest: jobtest.c ../util.c ../cpuinfo.c

clean:
	$(RM) jobtest
