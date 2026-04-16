#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "monitor_ioctl.h"

#define SOCKET_PATH "/tmp/osjackfruit_supervisor.sock"
#define LOG_DIR "./logs"
#define MAX_ID 64
#define MAX_PATH 512
#define MAX_CMD 512
#define QUEUE_CAP 256
#define STACK_SIZE (1024 * 1024)
#define SOFT_DEFAULT 40
#define HARD_DEFAULT 64

enum container_state {
    STATE_STARTING,
    STATE_RUNNING,
    STATE_EXITED,
    STATE_STOPPED,
    STATE_HARD_KILLED,
    STATE_FAILED
};

struct container;

struct log_entry {
    struct container *container;
    size_t len;
    char data[256];
};

struct log_queue {
    struct log_entry items[QUEUE_CAP];
    size_t head, tail, count;
    bool closing;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

struct container {
    char id[MAX_ID];
    char rootfs[MAX_PATH];
    char command[MAX_CMD];
    char log_path[MAX_PATH];
    pid_t pid;
    time_t started_at;
    int soft_mib;
    int hard_mib;
    int nice_value;
    int exit_status;
    int term_signal;
    bool stop_requested;
    enum container_state state;
    int out_pipe[2];
    int err_pipe[2];
    FILE *log_fp;
    pthread_t out_thread;
    pthread_t err_thread;
    pthread_mutex_t mutex;
    pthread_cond_t done;
    struct container *next;
};

struct producer_arg {
    struct container *container;
    int fd;
};

struct child_config {
    char rootfs[MAX_PATH];
    char command[MAX_CMD];
    char id[MAX_ID];
    int nice_value;
    int out_fd;
    int err_fd;
};

struct supervisor {
    int server_fd;
    int monitor_fd;
    bool shutting_down;
    char base_rootfs[MAX_PATH];
    struct container *containers;
    pthread_mutex_t containers_mutex;
    struct log_queue queue;
    pthread_t consumer_thread;
    pthread_t reaper_thread;
};

struct parsed_start {
    bool wait_for_exit;
    char id[MAX_ID];
    char rootfs[MAX_PATH];
    char command[MAX_CMD];
    int soft_mib;
    int hard_mib;
    int nice_value;
};

static struct supervisor *g_sup;

static void usage(FILE *stream)
{
    fprintf(stream,
            "Usage:\n"
            "  engine supervisor <base-rootfs>\n"
            "  engine start <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n"
            "  engine run   <id> <container-rootfs> <command> [--soft-mib N] [--hard-mib N] [--nice N]\n"
            "  engine ps\n"
            "  engine logs <id>\n"
            "  engine stop <id>\n");
}

static const char *state_name(enum container_state state)
{
    switch (state) {
    case STATE_STARTING: return "starting";
    case STATE_RUNNING: return "running";
    case STATE_EXITED: return "exited";
    case STATE_STOPPED: return "stopped";
    case STATE_HARD_KILLED: return "hard_limit_killed";
    default: return "failed";
    }
}

static void close_fd(int *fd)
{
    if (*fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void queue_init(struct log_queue *q)
{
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

static void queue_shutdown(struct log_queue *q)
{
    pthread_mutex_lock(&q->mutex);
    q->closing = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

static void queue_push(struct supervisor *sup, const struct log_entry *entry)
{
    struct log_queue *q = &sup->queue;
    pthread_mutex_lock(&q->mutex);
    while (!q->closing && q->count == QUEUE_CAP) {
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    if (!q->closing) {
        q->items[q->tail] = *entry;
        q->tail = (q->tail + 1) % QUEUE_CAP;
        q->count++;
        pthread_cond_signal(&q->not_empty);
    }
    pthread_mutex_unlock(&q->mutex);
}

static bool queue_pop(struct supervisor *sup, struct log_entry *entry)
{
    struct log_queue *q = &sup->queue;
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->closing) {
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    if (q->count == 0 && q->closing) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }
    *entry = q->items[q->head];
    q->head = (q->head + 1) % QUEUE_CAP;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
    return true;
}

static void ensure_log_dir(void)
{
    struct stat st;
    if (stat(LOG_DIR, &st) != 0) {
        mkdir(LOG_DIR, 0755);
    }
}

static struct container *find_container_locked(struct supervisor *sup, const char *id)
{
    for (struct container *c = sup->containers; c; c = c->next) {
        if (strcmp(c->id, id) == 0) {
            return c;
        }
    }
    return NULL;
}

static bool rootfs_busy_locked(struct supervisor *sup, const char *rootfs)
{
    for (struct container *c = sup->containers; c; c = c->next) {
        if (strcmp(c->rootfs, rootfs) == 0 &&
            (c->state == STATE_STARTING || c->state == STATE_RUNNING)) {
            return true;
        }
    }
    return false;
}

static void time_string(time_t ts, char *buf, size_t len)
{
    struct tm tmv;
    if (!ts) {
        snprintf(buf, len, "-");
        return;
    }
    localtime_r(&ts, &tmv);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tmv);
}

static int register_monitor(struct supervisor *sup, struct container *c)
{
    struct monitor_registration reg;
    if (sup->monitor_fd < 0) {
        return -1;
    }
    memset(&reg, 0, sizeof(reg));
    reg.pid = c->pid;
    reg.soft_limit_mib = (uint32_t)c->soft_mib;
    reg.hard_limit_mib = (uint32_t)c->hard_mib;
    snprintf(reg.container_id, sizeof(reg.container_id), "%s", c->id);
    return ioctl(sup->monitor_fd, CONTAINER_MONITOR_REGISTER, &reg);
}

static void unregister_monitor(struct supervisor *sup, pid_t pid)
{
    if (sup->monitor_fd >= 0) {
        int32_t target = pid;
        ioctl(sup->monitor_fd, CONTAINER_MONITOR_UNREGISTER, &target);
    }
}

static int child_main(void *arg)
{
    struct child_config *cfg = arg;

    sethostname(cfg->id, strlen(cfg->id));
    setpriority(PRIO_PROCESS, 0, cfg->nice_value);
    mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);

    if (chdir(cfg->rootfs) != 0 || chroot(".") != 0 || chdir("/") != 0) {
        perror("rootfs setup");
        return 111;
    }
    mkdir("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount /proc");
        return 112;
    }

    dup2(cfg->out_fd, STDOUT_FILENO);
    dup2(cfg->err_fd, STDERR_FILENO);
    close(cfg->out_fd);
    close(cfg->err_fd);

    execl("/bin/sh", "/bin/sh", "-c", cfg->command, (char *)NULL);
    perror("exec");
    return 127;
}

static void *producer_main(void *arg)
{
    struct producer_arg *pa = arg;
    struct log_entry entry;
    ssize_t n;

    while ((n = read(pa->fd, entry.data, sizeof(entry.data))) > 0) {
        entry.container = pa->container;
        entry.len = (size_t)n;
        queue_push(g_sup, &entry);
    }

    close(pa->fd);
    free(pa);
    return NULL;
}

static void *consumer_main(void *arg)
{
    struct supervisor *sup = arg;
    struct log_entry entry;

    while (queue_pop(sup, &entry)) {
        if (!entry.container || !entry.container->log_fp) {
            continue;
        }
        pthread_mutex_lock(&entry.container->mutex);
        fwrite(entry.data, 1, entry.len, entry.container->log_fp);
        fflush(entry.container->log_fp);
        pthread_mutex_unlock(&entry.container->mutex);
    }
    return NULL;
}

static void finalize_container(struct container *c, int status)
{
    pthread_mutex_lock(&c->mutex);
    if (WIFEXITED(status)) {
        c->exit_status = WEXITSTATUS(status);
        c->state = c->stop_requested ? STATE_STOPPED : STATE_EXITED;
    } else if (WIFSIGNALED(status)) {
        c->term_signal = WTERMSIG(status);
        c->exit_status = 128 + c->term_signal;
        if (c->stop_requested) {
            c->state = STATE_STOPPED;
        } else if (c->term_signal == SIGKILL) {
            c->state = STATE_HARD_KILLED;
        } else {
            c->state = STATE_FAILED;
        }
    } else {
        c->state = STATE_FAILED;
    }
    pthread_cond_broadcast(&c->done);
    pthread_mutex_unlock(&c->mutex);
}

static void *reaper_main(void *arg)
{
    struct supervisor *sup = arg;
    while (!sup->shutting_down) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            pthread_mutex_lock(&sup->containers_mutex);
            for (struct container *c = sup->containers; c; c = c->next) {
                if (c->pid == pid) {
                    finalize_container(c, status);
                    unregister_monitor(sup, pid);
                    break;
                }
            }
            pthread_mutex_unlock(&sup->containers_mutex);
        } else {
            usleep(100000);
        }
    }
    return NULL;
}

static struct container *make_container(const struct parsed_start *ps)
{
    struct container *c = calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    snprintf(c->id, sizeof(c->id), "%s", ps->id);
    snprintf(c->rootfs, sizeof(c->rootfs), "%s", ps->rootfs);
    snprintf(c->command, sizeof(c->command), "%s", ps->command);
    snprintf(c->log_path, sizeof(c->log_path), "%s/%s.log", LOG_DIR, ps->id);
    c->soft_mib = ps->soft_mib;
    c->hard_mib = ps->hard_mib;
    c->nice_value = ps->nice_value;
    c->exit_status = -1;
    c->out_pipe[0] = c->out_pipe[1] = -1;
    c->err_pipe[0] = c->err_pipe[1] = -1;
    pthread_mutex_init(&c->mutex, NULL);
    pthread_cond_init(&c->done, NULL);
    c->log_fp = fopen(c->log_path, "a");
    if (!c->log_fp) {
        free(c);
        return NULL;
    }
    return c;
}

static void destroy_container(struct container *c)
{
    if (!c) {
        return;
    }
    if (c->log_fp) {
        fclose(c->log_fp);
    }
    close_fd(&c->out_pipe[0]);
    close_fd(&c->out_pipe[1]);
    close_fd(&c->err_pipe[0]);
    close_fd(&c->err_pipe[1]);
    pthread_mutex_destroy(&c->mutex);
    pthread_cond_destroy(&c->done);
    free(c);
}

static int send_text(int fd, const char *text)
{
    size_t left = strlen(text);
    while (left > 0) {
        ssize_t n = send(fd, text, left, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        text += n;
        left -= (size_t)n;
    }
    return 0;
}

static int start_container(struct supervisor *sup, const struct parsed_start *ps,
                           struct container **out, char *err, size_t err_len)
{
    struct container *c = NULL;
    struct producer_arg *out_arg = NULL;
    struct producer_arg *err_arg = NULL;
    struct child_config *cfg = NULL;
    char *stack = NULL;

    pthread_mutex_lock(&sup->containers_mutex);
    if (find_container_locked(sup, ps->id)) {
        pthread_mutex_unlock(&sup->containers_mutex);
        snprintf(err, err_len, "container '%s' already exists", ps->id);
        return -1;
    }
    if (rootfs_busy_locked(sup, ps->rootfs)) {
        pthread_mutex_unlock(&sup->containers_mutex);
        snprintf(err, err_len, "rootfs '%s' already belongs to a live container", ps->rootfs);
        return -1;
    }
    pthread_mutex_unlock(&sup->containers_mutex);

    c = make_container(ps);
    if (!c) {
        snprintf(err, err_len, "failed to allocate container");
        return -1;
    }
    if (pipe(c->out_pipe) != 0 || pipe(c->err_pipe) != 0) {
        snprintf(err, err_len, "failed to create logging pipes: %s", strerror(errno));
        destroy_container(c);
        return -1;
    }

    cfg = calloc(1, sizeof(*cfg));
    stack = malloc(STACK_SIZE);
    out_arg = calloc(1, sizeof(*out_arg));
    err_arg = calloc(1, sizeof(*err_arg));
    if (!cfg || !stack || !out_arg || !err_arg) {
        snprintf(err, err_len, "failed to allocate launch resources");
        free(cfg);
        free(stack);
        free(out_arg);
        free(err_arg);
        destroy_container(c);
        return -1;
    }

    snprintf(cfg->rootfs, sizeof(cfg->rootfs), "%s", ps->rootfs);
    snprintf(cfg->command, sizeof(cfg->command), "%s", ps->command);
    snprintf(cfg->id, sizeof(cfg->id), "%s", ps->id);
    cfg->nice_value = ps->nice_value;
    cfg->out_fd = c->out_pipe[1];
    cfg->err_fd = c->err_pipe[1];

    c->started_at = time(NULL);
    c->state = STATE_RUNNING;
    c->pid = clone(child_main, stack + STACK_SIZE,
                   CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, cfg);
    if (c->pid < 0) {
        snprintf(err, err_len, "clone failed: %s", strerror(errno));
        free(cfg);
        free(stack);
        free(out_arg);
        free(err_arg);
        destroy_container(c);
        return -1;
    }

    close_fd(&c->out_pipe[1]);
    close_fd(&c->err_pipe[1]);
    out_arg->container = c;
    out_arg->fd = c->out_pipe[0];
    err_arg->container = c;
    err_arg->fd = c->err_pipe[0];

    pthread_create(&c->out_thread, NULL, producer_main, out_arg);
    pthread_create(&c->err_thread, NULL, producer_main, err_arg);

    pthread_mutex_lock(&sup->containers_mutex);
    c->next = sup->containers;
    sup->containers = c;
    pthread_mutex_unlock(&sup->containers_mutex);

    register_monitor(sup, c);
    free(cfg);
    free(stack);
    *out = c;
    return 0;
}

static void wait_container(struct container *c, int *status)
{
    pthread_mutex_lock(&c->mutex);
    while (c->state == STATE_STARTING || c->state == STATE_RUNNING) {
        pthread_cond_wait(&c->done, &c->mutex);
    }
    *status = c->exit_status;
    pthread_mutex_unlock(&c->mutex);
}

static int stop_container(struct supervisor *sup, const char *id, char *err, size_t err_len)
{
    struct container *c;
    pthread_mutex_lock(&sup->containers_mutex);
    c = find_container_locked(sup, id);
    if (!c) {
        pthread_mutex_unlock(&sup->containers_mutex);
        snprintf(err, err_len, "unknown container '%s'", id);
        return -1;
    }
    pthread_mutex_lock(&c->mutex);
    c->stop_requested = true;
    pthread_mutex_unlock(&c->mutex);
    pthread_mutex_unlock(&sup->containers_mutex);

    if (kill(c->pid, SIGTERM) != 0) {
        snprintf(err, err_len, "failed to stop '%s': %s", id, strerror(errno));
        return -1;
    }
    return 0;
}

static void reap_finished_threads(struct supervisor *sup)
{
    pthread_mutex_lock(&sup->containers_mutex);
    struct container **cursor = &sup->containers;
    while (*cursor) {
        struct container *c = *cursor;
        bool finished = c->state != STATE_STARTING && c->state != STATE_RUNNING;
        if (!finished) {
            cursor = &c->next;
            continue;
        }
        *cursor = c->next;
        pthread_mutex_unlock(&sup->containers_mutex);
        pthread_join(c->out_thread, NULL);
        pthread_join(c->err_thread, NULL);
        destroy_container(c);
        pthread_mutex_lock(&sup->containers_mutex);
    }
    pthread_mutex_unlock(&sup->containers_mutex);
}

static int parse_start_tokens(char **tok, int count, bool wait_for_exit,
                              struct parsed_start *ps, char *err, size_t err_len)
{
    if (count < 4) {
        snprintf(err, err_len, "not enough arguments");
        return -1;
    }
    memset(ps, 0, sizeof(*ps));
    ps->wait_for_exit = wait_for_exit;
    ps->soft_mib = SOFT_DEFAULT;
    ps->hard_mib = HARD_DEFAULT;
    snprintf(ps->id, sizeof(ps->id), "%s", tok[1]);
    snprintf(ps->rootfs, sizeof(ps->rootfs), "%s", tok[2]);
    snprintf(ps->command, sizeof(ps->command), "%s", tok[3]);

    for (int i = 4; i < count; ++i) {
        if (strcmp(tok[i], "--soft-mib") == 0 && i + 1 < count) {
            ps->soft_mib = atoi(tok[++i]);
        } else if (strcmp(tok[i], "--hard-mib") == 0 && i + 1 < count) {
            ps->hard_mib = atoi(tok[++i]);
        } else if (strcmp(tok[i], "--nice") == 0 && i + 1 < count) {
            ps->nice_value = atoi(tok[++i]);
        } else {
            size_t used = strlen(ps->command);
            snprintf(ps->command + used, sizeof(ps->command) - used, " %s", tok[i]);
        }
    }
    if (ps->soft_mib <= 0 || ps->hard_mib <= 0 || ps->soft_mib > ps->hard_mib) {
        snprintf(err, err_len, "invalid memory limits");
        return -1;
    }
    return 0;
}

static void append_ps(struct supervisor *sup, char *buf, size_t len)
{
    char timebuf[64];
    snprintf(buf, len, "ID\tPID\tSTATE\tSOFT\tHARD\tNICE\tSTARTED\tEXIT\tLOG\n");
    pthread_mutex_lock(&sup->containers_mutex);
    for (struct container *c = sup->containers; c; c = c->next) {
        time_string(c->started_at, timebuf, sizeof(timebuf));
        size_t used = strlen(buf);
        snprintf(buf + used, len - used,
                 "%s\t%d\t%s\t%d\t%d\t%d\t%s\t%d\t%s\n",
                 c->id, c->pid, state_name(c->state), c->soft_mib,
                 c->hard_mib, c->nice_value, timebuf, c->exit_status, c->log_path);
    }
    pthread_mutex_unlock(&sup->containers_mutex);
}

static void handle_client(struct supervisor *sup, int fd, char *line)
{
    char *tok[32];
    int count = 0;
    char *save = NULL;
    char *part = strtok_r(line, " \t\r\n", &save);
    char response[16384];
    char err[256];

    while (part && count < 32) {
        tok[count++] = part;
        part = strtok_r(NULL, " \t\r\n", &save);
    }
    if (count == 0) {
        send_text(fd, "ERR empty command\n");
        return;
    }

    if (strcmp(tok[0], "start") == 0 || strcmp(tok[0], "run") == 0) {
        struct parsed_start ps;
        struct container *c = NULL;
        int status = 0;
        if (parse_start_tokens(tok, count, strcmp(tok[0], "run") == 0, &ps, err, sizeof(err)) != 0) {
            snprintf(response, sizeof(response), "ERR %s\n", err);
            send_text(fd, response);
            return;
        }
        if (start_container(sup, &ps, &c, err, sizeof(err)) != 0) {
            snprintf(response, sizeof(response), "ERR %s\n", err);
            send_text(fd, response);
            return;
        }
        if (!ps.wait_for_exit) {
            snprintf(response, sizeof(response), "OK started %s pid=%d log=%s\n", c->id, c->pid, c->log_path);
            send_text(fd, response);
            return;
        }
        wait_container(c, &status);
        snprintf(response, sizeof(response), "OK exited %s status=%d state=%s\n",
                 c->id, status, state_name(c->state));
        send_text(fd, response);
        return;
    }

    if (strcmp(tok[0], "ps") == 0) {
        append_ps(sup, response, sizeof(response));
        send_text(fd, "OK\n");
        send_text(fd, response);
        return;
    }

    if (strcmp(tok[0], "logs") == 0 && count == 2) {
        FILE *fp;
        size_t n;
        pthread_mutex_lock(&sup->containers_mutex);
        struct container *c = find_container_locked(sup, tok[1]);
        if (!c) {
            pthread_mutex_unlock(&sup->containers_mutex);
            send_text(fd, "ERR unknown container\n");
            return;
        }
        snprintf(response, sizeof(response), "%s", c->log_path);
        pthread_mutex_unlock(&sup->containers_mutex);
        fp = fopen(response, "r");
        if (!fp) {
            send_text(fd, "ERR failed to open log\n");
            return;
        }
        send_text(fd, "OK\n");
        while ((n = fread(response, 1, sizeof(response) - 1, fp)) > 0) {
            response[n] = '\0';
            send_text(fd, response);
        }
        fclose(fp);
        return;
    }

    if (strcmp(tok[0], "stop") == 0 && count == 2) {
        if (stop_container(sup, tok[1], err, sizeof(err)) != 0) {
            snprintf(response, sizeof(response), "ERR %s\n", err);
            send_text(fd, response);
            return;
        }
        snprintf(response, sizeof(response), "OK stop requested for %s\n", tok[1]);
        send_text(fd, response);
        return;
    }

    snprintf(response, sizeof(response), "ERR invalid command\n");
    send_text(fd, response);
}

static void signal_handler(int signo)
{
    (void)signo;
    if (!g_sup) {
        return;
    }
    g_sup->shutting_down = true;
    if (g_sup->server_fd >= 0) {
        close(g_sup->server_fd);
        g_sup->server_fd = -1;
    }
}

static int run_supervisor(const char *base_rootfs)
{
    struct supervisor sup;
    struct sockaddr_un addr;

    memset(&sup, 0, sizeof(sup));
    sup.server_fd = -1;
    sup.monitor_fd = open(CONTAINER_MONITOR_DEVICE, O_RDWR);
    snprintf(sup.base_rootfs, sizeof(sup.base_rootfs), "%s", base_rootfs);
    pthread_mutex_init(&sup.containers_mutex, NULL);
    queue_init(&sup.queue);
    ensure_log_dir();

    g_sup = &sup;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    unlink(SOCKET_PATH);
    sup.server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sup.server_fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);
    if (bind(sup.server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }
    if (listen(sup.server_fd, 16) != 0) {
        perror("listen");
        return 1;
    }

    pthread_create(&sup.consumer_thread, NULL, consumer_main, &sup);
    pthread_create(&sup.reaper_thread, NULL, reaper_main, &sup);
    printf("Supervisor listening on %s\n", SOCKET_PATH);
    fflush(stdout);

    while (!sup.shutting_down) {
        char line[2048];
        int client = accept(sup.server_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR || sup.shutting_down) {
                continue;
            }
            perror("accept");
            break;
        }
        ssize_t n = recv(client, line, sizeof(line) - 1, 0);
        if (n > 0) {
            line[n] = '\0';
            handle_client(&sup, client, line);
        }
        close(client);
    }

    pthread_mutex_lock(&sup.containers_mutex);
    for (struct container *c = sup.containers; c; c = c->next) {
        pthread_mutex_lock(&c->mutex);
        c->stop_requested = true;
        pthread_mutex_unlock(&c->mutex);
        kill(c->pid, SIGTERM);
    }
    pthread_mutex_unlock(&sup.containers_mutex);

    sleep(1);
    sup.shutting_down = true;
    pthread_join(sup.reaper_thread, NULL);
    queue_shutdown(&sup.queue);
    pthread_join(sup.consumer_thread, NULL);
    reap_finished_threads(&sup);

    if (sup.monitor_fd >= 0) {
        close(sup.monitor_fd);
    }
    if (sup.server_fd >= 0) {
        close(sup.server_fd);
    }
    unlink(SOCKET_PATH);
    pthread_mutex_destroy(&sup.containers_mutex);
    return 0;
}

static int send_command(int argc, char **argv)
{
    struct sockaddr_un addr;
    char cmd[2048] = {0};
    char reply[16384];
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        perror("socket");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "failed to connect to supervisor: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        size_t used = strlen(cmd);
        snprintf(cmd + used, sizeof(cmd) - used, "%s%s", i == 1 ? "" : " ", argv[i]);
    }
    send_text(fd, cmd);

    ssize_t n;
    while ((n = recv(fd, reply, sizeof(reply) - 1, 0)) > 0) {
        reply[n] = '\0';
        fputs(reply, stdout);
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "supervisor") == 0) {
        if (argc != 3) {
            usage(stderr);
            return 1;
        }
        return run_supervisor(argv[2]);
    }

    if (strcmp(argv[1], "start") == 0 || strcmp(argv[1], "run") == 0 ||
        strcmp(argv[1], "ps") == 0 || strcmp(argv[1], "logs") == 0 ||
        strcmp(argv[1], "stop") == 0) {
        return send_command(argc, argv);
    }

    usage(stderr);
    return 1;
}
