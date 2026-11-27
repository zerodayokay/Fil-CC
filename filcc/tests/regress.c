#include "kernels.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __PIZLONATOR_WAS_HERE__
#include <pthread.h>
#include <stdatomic.h>
#include <stdfil.h>
#endif

/* A byte-for-byte comparison would miss a stale or prematurely updated PHI. */
__attribute__((noinline))
uint64_t recurrence(const uint16_t *p, size_t n)
{
    uint64_t a = 17;
    uint64_t b = 29;
    for (size_t i = 0; i != n; ++i) {
        uint64_t next = (a * 33) ^ (p[i] + b);
        b = a;
        a = next;
    }
    return a ^ (b << 1);
}

__attribute__((noinline))
uint64_t sum_u64(const uint64_t *p, size_t n)
{
    uint64_t sum = 0;
    for (size_t i = 0; i != n; ++i)
        sum += p[i];
    return sum;
}

static uint64_t reference(const volatile uint16_t *p, size_t n)
{
    uint64_t a = 17;
    uint64_t b = 29;
    for (size_t i = 0; i != n; ++i) {
        uint64_t next = (a * 33) ^ (p[i] + b);
        b = a;
        a = next;
    }
    return a ^ (b << 1);
}

#ifdef __PIZLONATOR_WAS_HERE__
static atomic_int stop_gc;
static atomic_uint completed_cycles;

static void *collect(void *ignored)
{
    (void)ignored;
    do {
        zgc_request_and_wait();
        atomic_fetch_add(&completed_cycles, 1);
    } while (!atomic_load(&stop_gc));
    return NULL;
}

static uint32_t *make_scan_input(size_t n)
{
    uint32_t *p = malloc(n * sizeof(*p));
    if (!p)
        exit(2);
    for (size_t i = 0; i != n; ++i)
        p[i] = 1;
    return p;
}

static int gc_stress(void)
{
    pthread_t gc;
    if (pthread_create(&gc, NULL, collect, NULL))
        return 1;

    const size_t n = 4 * 1024 * 1024 + 1;
    int failed = 0;
    for (unsigned i = 0; i != 12; ++i)
        failed |= sum_u32(make_scan_input(n), n) != n;

    unsigned during_scans = atomic_load(&completed_cycles);
    atomic_store(&stop_gc, 1);
    if (pthread_join(gc, NULL) || failed || !during_scans)
        return 1;
    printf("GC cycles completed during scans: %u\n", during_scans);
    return 0;
}
#endif

int main(void)
{
    uint16_t halves[33];
    uint64_t words[33];
    for (size_t i = 0; i != 33; ++i) {
        halves[i] = (uint16_t)(i * i * 373 + 13);
        words[i] = UINT64_MAX - i * i;
    }

    for (size_t offset = 0; offset != 8; ++offset) {
        uint64_t expected = 0;
        for (size_t n = 0; n <= 33 - offset; ++n) {
            if (recurrence(halves + offset, n) != reference(halves + offset, n) ||
                sum_u64(words + offset, n) != expected)
                return 1;
            if (n != 33 - offset)
                expected += words[offset + n];
        }
    }

#ifdef __PIZLONATOR_WAS_HERE__
    if (gc_stress())
        return 1;
#endif
    puts("16-bit recurrence and 64-bit sums match");
    return 0;
}
