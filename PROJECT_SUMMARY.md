# Project Summary

## Title

OS Jackfruit: Multi-Container Runtime with Kernel Memory Monitoring

## Overview

This project builds a lightweight Linux container runtime in C. It includes a long-running supervisor process that can launch and manage multiple containers at the same time, capture their output through a bounded-buffer logging pipeline, and expose a CLI for starting, stopping, listing, and inspecting containers.

The project also includes a Linux kernel module that monitors container processes by host PID, tracks their resident memory usage, logs a warning when a soft limit is crossed, and kills the process when a hard limit is exceeded.

## Main Components

### 1. User-Space Runtime (`engine.c`)

- Runs as a persistent supervisor daemon
- Accepts CLI commands through a UNIX domain socket
- Launches containers with Linux namespaces
- Maintains metadata for each container
- Reaps child processes correctly
- Supports:
  - `supervisor`
  - `start`
  - `run`
  - `ps`
  - `logs`
  - `stop`

### 2. Logging System

- Redirects container `stdout` and `stderr` through pipes
- Uses producer threads to read container output
- Uses a bounded shared buffer to avoid unbounded memory growth
- Uses a consumer thread to flush logs into per-container log files

### 3. Kernel Monitor (`monitor.c`)

- Exposes `/dev/container_monitor`
- Receives PID registrations through `ioctl`
- Tracks monitored processes in a lock-protected linked list
- Periodically checks RSS memory usage
- Implements:
  - Soft limit: warning only
  - Hard limit: process termination with `SIGKILL`

### 4. Workloads

- `cpu_hog.c`: CPU-bound workload
- `io_pulse.c`: I/O-bound workload
- `memory_hog.c`: memory stress workload

These workloads are used for testing scheduling behavior, memory monitoring, and logging correctness.

## OS Concepts Demonstrated

- Process creation and parent-child lifecycle management
- Linux namespaces for isolation
- `chroot()` and mount isolation
- IPC using UNIX domain sockets and pipes
- Producer-consumer synchronization with threads
- Kernel-user communication through `ioctl`
- RSS-based memory monitoring in kernel space
- Linux scheduling behavior under different workload types and priorities

## Expected Demonstrations

- Multiple containers running under one supervisor
- Correct container metadata with `ps`
- Persistent per-container logs
- CLI communication with the supervisor
- Soft-limit warning in kernel logs
- Hard-limit enforcement and kill behavior
- Scheduling experiments using CPU-bound and I/O-bound workloads
- Clean teardown with no zombie processes or stale monitor entries

## Environment Requirement

The final project must be built and demonstrated in an Ubuntu 22.04/24.04 VM with Secure Boot OFF. Windows can be used for preparing source files and documentation, but Linux is required for actual namespace operations, kernel module loading, and final testing.

## Deliverables

- Fully functional source code
- Test case documentation
- Demo walkthrough with screenshots
- Engineering analysis and design tradeoffs
