# Submission Checklist

This checklist is based on the assignment guide in the repository:

- Guide: [OS-Jackfruit project guide](https://github.com/shivangjhalani/OS-Jackfruit/blob/main/project-guide.md)

## 1. Repository Contents Required

The final GitHub repository must contain all of the following:

- [ ] `engine.c`
- [ ] `monitor.c`
- [ ] `monitor_ioctl.h`
- [ ] At least two workload/test programs
- [ ] `Makefile` that builds everything with a single `make`
- [ ] `README.md`
- [ ] `boilerplate/` folder with a CI-safe build path
- [ ] `make -C boilerplate ci` works in Ubuntu

## 2. Team Information Required

Fill these in before submission:

- [ ] Team member 1 name
- [ ] Team member 1 SRN
- [ ] Team member 2 name
- [ ] Team member 2 SRN

Current status:

- `README.md` still contains placeholders for team info

## 3. Build / Load / Run Instructions Required in README

Your `README.md` must clearly explain how to reproduce the project on a fresh Ubuntu VM.

Make sure it includes:

- [ ] Dependency installation
- [ ] Rootfs setup
- [ ] Full build with `make`
- [ ] Kernel module load with `insmod`
- [ ] Verification of `/dev/container_monitor`
- [ ] Supervisor start command
- [ ] CLI commands for `start`, `run`, `ps`, `logs`, `stop`
- [ ] Workload execution steps
- [ ] Cleanup steps including `rmmod`

Current status:

- `README.md` already contains most of this
- You still need to verify all commands on Ubuntu and correct anything that fails there

## 4. Required Demonstration Evidence

The guide requires annotated screenshots with brief captions for each item below.

- [ ] Two or more containers running under one supervisor
- [ ] `ps` output showing tracked container metadata
- [ ] Logging pipeline result and evidence of logging behavior
- [ ] A CLI command and supervisor response showing control IPC
- [ ] Soft-limit warning in `dmesg` or log output
- [ ] Hard-limit enforcement in `dmesg` or log output plus supervisor metadata
- [ ] At least one scheduling experiment with observable differences
- [ ] Clean teardown with no zombie processes and proper exit behavior

Use `DEMO_EVIDENCE_TEMPLATE.md` to fill these in.

## 5. Engineering Analysis Required in README

The guide requires these five topics:

- [ ] Isolation mechanisms
- [ ] Supervisor and process lifecycle
- [ ] IPC, threads, and synchronization
- [ ] Memory management and enforcement
- [ ] Scheduling behavior

Current status:

- `README.md` already has draft analysis sections
- You still need to replace general statements with actual observations from your Ubuntu run where needed

## 6. Runtime Behavior Required

Before submission, verify that the implementation actually demonstrates:

- [ ] Multiple concurrent containers
- [ ] Isolated PID, UTS, and mount namespaces
- [ ] Separate writable rootfs copies per live container
- [ ] `/proc` working inside containers
- [ ] No zombie children after exit
- [ ] Soft-limit warning behavior
- [ ] Hard-limit kill behavior
- [ ] Clean thread/process/module teardown

## 7. Experiments Required

At least one experiment must compare:

- [ ] Two CPU-bound containers with different priorities, or
- [ ] One CPU-bound and one I/O-bound container running concurrently

Recommended minimum submission:

- [ ] CPU vs CPU experiment with `nice 0` and `nice 10`
- [ ] CPU vs IO experiment with `cpu_hog` and `io_pulse`
- [ ] Record start/end times or completion times
- [ ] Record observations about fairness, responsiveness, and throughput

## 8. What Is Already Prepared In This Folder

Already present:

- `engine.c`
- `monitor.c`
- `monitor_ioctl.h`
- `Makefile`
- `cpu_hog.c`
- `io_pulse.c`
- `memory_hog.c`
- `README.md`
- `TEST_CASES.md`
- `PROJECT_SUMMARY.md`
- `boilerplate/` with CI-safe wrappers and environment check

Still to complete:

- [ ] Run and debug everything on Ubuntu VM
- [ ] Replace README placeholders with your team info
- [ ] Capture all required screenshots
- [ ] Fill in measured experiment results
- [ ] Update `TEST_CASES.md` with actual outputs/observations
- [ ] Make sure `make -C boilerplate ci` and full `make` both succeed

## 9. Final Submission Packet You Should Have

Before you submit, your repo should effectively contain:

1. Source code
2. Build files
3. Test workloads
4. README with reproducible instructions
5. Engineering analysis
6. Screenshot-based demo evidence
7. Test case documentation
8. Verified Ubuntu VM run
