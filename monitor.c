#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pid.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "monitor_ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Codex");
MODULE_DESCRIPTION("Jackfruit memory monitor");

struct monitor_entry {
    pid_t pid;
    u32 soft_limit_mib;
    u32 hard_limit_mib;
    bool soft_warned;
    char container_id[64];
    struct list_head list;
};

static LIST_HEAD(entries);
static DEFINE_MUTEX(entries_lock);
static struct task_struct *monitor_thread;

static bool pid_exists(pid_t pid)
{
    bool exists;
    rcu_read_lock();
    exists = find_vpid(pid) != NULL;
    rcu_read_unlock();
    return exists;
}

static unsigned long rss_in_mib(pid_t pid)
{
    struct task_struct *task;
    struct mm_struct *mm;
    unsigned long pages;
    unsigned long mib = 0;

    rcu_read_lock();
    task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
    rcu_read_unlock();
    if (!task) {
        return 0;
    }

    mm = get_task_mm(task);
    put_task_struct(task);
    if (!mm) {
        return 0;
    }

    pages = get_mm_rss(mm);
    mib = (pages << PAGE_SHIFT) / (1024 * 1024);
    mmput(mm);
    return mib;
}

static void free_entry(struct monitor_entry *entry)
{
    list_del(&entry->list);
    kfree(entry);
}

static int monitor_main(void *unused)
{
    while (!kthread_should_stop()) {
        struct monitor_entry *entry;
        struct monitor_entry *tmp;

        mutex_lock(&entries_lock);
        list_for_each_entry_safe(entry, tmp, &entries, list) {
            unsigned long rss;
            struct pid *pidref;

            if (!pid_exists(entry->pid)) {
                pr_info("jackfruit_monitor: removing stale pid=%d id=%s\n",
                        entry->pid, entry->container_id);
                free_entry(entry);
                continue;
            }

            rss = rss_in_mib(entry->pid);
            if (!entry->soft_warned && rss > entry->soft_limit_mib) {
                entry->soft_warned = true;
                pr_warn("jackfruit_monitor: soft limit exceeded id=%s pid=%d rss=%luMiB soft=%uMiB\n",
                        entry->container_id, entry->pid, rss, entry->soft_limit_mib);
            }

            if (rss > entry->hard_limit_mib) {
                pidref = find_get_pid(entry->pid);
                if (pidref) {
                    pr_err("jackfruit_monitor: hard limit exceeded id=%s pid=%d rss=%luMiB hard=%uMiB\n",
                           entry->container_id, entry->pid, rss, entry->hard_limit_mib);
                    kill_pid(pidref, SIGKILL, 1);
                    put_pid(pidref);
                }
            }
        }
        mutex_unlock(&entries_lock);

        ssleep(1);
    }

    return 0;
}

static long monitor_ioctl_handler(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct monitor_registration reg;
    struct monitor_entry *entry;
    int32_t pid;

    (void)file;

    switch (cmd) {
    case CONTAINER_MONITOR_REGISTER:
        if (copy_from_user(&reg, (void __user *)arg, sizeof(reg))) {
            return -EFAULT;
        }
        entry = kzalloc(sizeof(*entry), GFP_KERNEL);
        if (!entry) {
            return -ENOMEM;
        }
        entry->pid = reg.pid;
        entry->soft_limit_mib = reg.soft_limit_mib;
        entry->hard_limit_mib = reg.hard_limit_mib;
        strscpy(entry->container_id, reg.container_id, sizeof(entry->container_id));

        mutex_lock(&entries_lock);
        list_add_tail(&entry->list, &entries);
        mutex_unlock(&entries_lock);

        pr_info("jackfruit_monitor: registered id=%s pid=%d soft=%uMiB hard=%uMiB\n",
                entry->container_id, entry->pid, entry->soft_limit_mib, entry->hard_limit_mib);
        return 0;

    case CONTAINER_MONITOR_UNREGISTER:
        if (copy_from_user(&pid, (void __user *)arg, sizeof(pid))) {
            return -EFAULT;
        }
        mutex_lock(&entries_lock);
        list_for_each_entry(entry, &entries, list) {
            if (entry->pid == pid) {
                pr_info("jackfruit_monitor: unregistered pid=%d id=%s\n",
                        entry->pid, entry->container_id);
                free_entry(entry);
                mutex_unlock(&entries_lock);
                return 0;
            }
        }
        mutex_unlock(&entries_lock);
        return -ENOENT;

    default:
        return -EINVAL;
    }
}

static const struct file_operations monitor_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = monitor_ioctl_handler,
#ifdef CONFIG_COMPAT
    .compat_ioctl = monitor_ioctl_handler,
#endif
};

static struct miscdevice monitor_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "container_monitor",
    .fops = &monitor_fops,
    .mode = 0666,
};

static int __init monitor_init(void)
{
    int rc = misc_register(&monitor_dev);
    if (rc) {
        pr_err("jackfruit_monitor: misc register failed: %d\n", rc);
        return rc;
    }

    monitor_thread = kthread_run(monitor_main, NULL, "jackfruit_monitor");
    if (IS_ERR(monitor_thread)) {
        rc = PTR_ERR(monitor_thread);
        misc_deregister(&monitor_dev);
        pr_err("jackfruit_monitor: failed to start thread: %d\n", rc);
        return rc;
    }

    pr_info("jackfruit_monitor: loaded\n");
    return 0;
}

static void __exit monitor_exit(void)
{
    struct monitor_entry *entry;
    struct monitor_entry *tmp;

    if (monitor_thread) {
        kthread_stop(monitor_thread);
    }

    mutex_lock(&entries_lock);
    list_for_each_entry_safe(entry, tmp, &entries, list) {
        free_entry(entry);
    }
    mutex_unlock(&entries_lock);

    misc_deregister(&monitor_dev);
    pr_info("jackfruit_monitor: unloaded\n");
}

module_init(monitor_init);
module_exit(monitor_exit);
