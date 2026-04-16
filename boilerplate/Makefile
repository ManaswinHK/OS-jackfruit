CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -O2 -pthread

.PHONY: ci clean

ci: engine cpu_hog io_pulse memory_hog

engine: engine.c monitor_ioctl.h
	$(CC) $(CFLAGS) -o $@ engine.c

cpu_hog: cpu_hog.c
	$(CC) $(CFLAGS) -o $@ cpu_hog.c

io_pulse: io_pulse.c
	$(CC) $(CFLAGS) -o $@ io_pulse.c

memory_hog: memory_hog.c
	$(CC) $(CFLAGS) -o $@ memory_hog.c

clean:
	rm -f engine cpu_hog io_pulse memory_hog
