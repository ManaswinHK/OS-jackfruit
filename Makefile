CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2 -pthread
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

USER_TARGETS := engine cpu_hog io_pulse memory_hog

.PHONY: all clean ci

all: $(USER_TARGETS) monitor.ko

engine: engine.c monitor_ioctl.h
	$(CC) $(CFLAGS) -o $@ engine.c

cpu_hog: cpu_hog.c
	$(CC) $(CFLAGS) -o $@ cpu_hog.c

io_pulse: io_pulse.c
	$(CC) $(CFLAGS) -o $@ io_pulse.c

memory_hog: memory_hog.c
	$(CC) $(CFLAGS) -o $@ memory_hog.c

monitor.ko: monitor.c monitor_ioctl.h
	$(MAKE) -C $(KDIR) M=$(PWD) modules

obj-m += monitor.o

ci: $(USER_TARGETS)

clean:
	rm -f $(USER_TARGETS) *.o *.ko *.mod.c *.symvers *.order
	rm -rf .tmp_versions
