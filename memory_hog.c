#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int mib = argc > 1 ? atoi(argv[1]) : 96;
    size_t bytes = (size_t)mib * 1024 * 1024;
    char *buffer = malloc(bytes);
    if (!buffer) {
        perror("malloc");
        return 1;
    }

    for (size_t offset = 0; offset < bytes; offset += 4096) {
        buffer[offset] = (char)(offset % 251);
    }

    printf("memory_hog touched %d MiB\n", mib);
    fflush(stdout);
    sleep(argc > 2 ? atoi(argv[2]) : 30);

    free(buffer);
    return 0;
}
