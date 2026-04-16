#ifndef CONTAINER_MONITOR_IOCTL_H
#define CONTAINER_MONITOR_IOCTL_H

#include <linux/ioctl.h>
#include <stdint.h>

#define CONTAINER_MONITOR_DEVICE "/dev/container_monitor"

struct monitor_registration {
    int32_t pid;
    uint32_t soft_limit_mib;
    uint32_t hard_limit_mib;
    char container_id[64];
};

#define CONTAINER_MONITOR_IOCTL_MAGIC 'J'
#define CONTAINER_MONITOR_REGISTER \
    _IOW(CONTAINER_MONITOR_IOCTL_MAGIC, 1, struct monitor_registration)
#define CONTAINER_MONITOR_UNREGISTER \
    _IOW(CONTAINER_MONITOR_IOCTL_MAGIC, 2, int32_t)

#endif
