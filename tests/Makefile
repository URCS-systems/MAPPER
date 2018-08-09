CFLAGS=-std=gnu11 -g3 -ggdb -Wall -Wextra -Werror

.PHONY: clean

all: jobtest
	sudo setcap cap_sys_resource+ep jobtest
	setcap -v cap_sys_resource+ep jobtest

jobtest: jobtest.c

clean:
	$(RM) jobtest
