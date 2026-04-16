#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int is_prime(uint64_t n)
{
    if (n < 2) {
        return 0;
    }
    for (uint64_t i = 2; i * i <= n; ++i) {
        if (n % i == 0) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    time_t duration = argc > 1 ? atoi(argv[1]) : 15;
    time_t end_time = time(NULL) + duration;
    uint64_t value = 2;
    uint64_t primes = 0;

    while (time(NULL) < end_time) {
        if (is_prime(value)) {
            ++primes;
        }
        ++value;
    }

    printf("cpu_hog finished after %ld seconds, primes=%llu\n",
           (long)duration, (unsigned long long)primes);
    return 0;
}
