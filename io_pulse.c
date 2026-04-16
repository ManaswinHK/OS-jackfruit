#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/io_pulse.log";
    int rounds = argc > 2 ? atoi(argv[2]) : 20;
    int fd = open(path, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    for (int i = 0; i < rounds; ++i) {
        char line[128];
        int len = snprintf(line, sizeof(line), "io_pulse round=%d\n", i);
        if (write(fd, line, len) != len) {
            perror("write");
            close(fd);
            return 1;
        }
        fsync(fd);
        usleep(100000);
    }

    close(fd);
    printf("io_pulse wrote %d rounds to %s\n", rounds, path);
    return 0;
}
