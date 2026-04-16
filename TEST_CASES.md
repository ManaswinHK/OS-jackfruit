# Test Cases

## Build

1. `make -C boilerplate ci`
Expected: user-space binaries compile without requiring module loading.

2. `make`
Expected: `engine`, workloads, and `monitor.ko` are built.

## Supervisor

3. `sudo ./engine supervisor ./rootfs-base`
Expected: supervisor stays alive and creates `/tmp/osjackfruit_supervisor.sock`.

4. `sudo ./engine start alpha ./rootfs-alpha /bin/sh`
Expected: request returns quickly and `alpha` appears in `sudo ./engine ps`.

5. Start `alpha` and `beta` with different rootfs directories.
Expected: both appear in `ps` simultaneously under one supervisor.

6. `sudo ./engine run cpuA ./rootfs-alpha "/cpu_hog 10"`
Expected: client blocks until exit and prints final status.

7. `sudo ./engine logs alpha`
Expected: log file contents are returned.

8. `sudo ./engine stop alpha`
Expected: target is terminated and later shows state `stopped`.

## Logging

9. Run a command that writes to stdout.
Expected: output appears in `logs/<id>.log`.

10. Run `/bin/sh -c "echo error >&2"`.
Expected: stderr also appears in the same log file.

11. Run two noisy containers together.
Expected: no deadlock and the supervisor remains responsive.

## Kernel Monitor

12. `sudo ./engine run memSoft ./rootfs-alpha "/memory_hog 72 20" --soft-mib 48 --hard-mib 128`
Expected: soft-limit warning appears in `dmesg`, process survives.

13. `sudo ./engine run memHard ./rootfs-alpha "/memory_hog 96 20" --soft-mib 48 --hard-mib 64`
Expected: hard-limit event appears in `dmesg` and the container is killed.

## Scheduler

14. Launch two `cpu_hog` containers with different `--nice` values.
Expected: lower nice workload tends to finish sooner or receive more CPU share.

15. Launch `cpu_hog` and `io_pulse` together.
Expected: I/O task remains responsive while CPU work consumes sustained compute time.

## Cleanup

16. Inspect `ps aux | grep defunct`
Expected: no zombie container children remain.

17. `sudo rmmod monitor`
Expected: module unloads cleanly and frees tracked state.
