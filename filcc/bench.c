#define _POSIX_C_SOURCE 200809L
#include "kernels.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(2);
}

static size_t positive_size(const char *text)
{
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || text[0] < '0' || text[0] > '9' || *end ||
        !value || value > SIZE_MAX)
        fail("Expected a positive size");
    return (size_t)value;
}

static void *checked_malloc(size_t size)
{
    void *p = malloc(size);
    if (!p)
        fail("Allocation failed");
    return p;
}

static uint64_t ns(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time))
        fail("clock_gettime failed");
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static uint32_t clamp(uint32_t x)
{
    return x & UINT32_C(255);
}

static uint32_t alternate(uint32_t x)
{
    return (x >> 3) ^ UINT32_C(97);
}

/* Read one decimal PPM header token, including its terminating whitespace. */
static size_t ppm_number(FILE *file)
{
    int c;
    do {
        c = fgetc(file);
        if (c == '#') {
            do {
                c = fgetc(file);
            } while (c != EOF && c != '\n');
        }
    } while (c != EOF && isspace(c));

    size_t n = 0;
    while (c >= '0' && c <= '9') {
        size_t digit = (size_t)(c - '0');
        if (n > (SIZE_MAX - digit) / 10)
            fail("PPM header number overflows");
        n = 10 * n + digit;
        c = fgetc(file);
    }
    if (!n || c == EOF || !isspace(c))
        fail("Malformed PPM header");
    return n;
}

static uint64_t ppm(const char *path, size_t *bytes)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        fail("Cannot open PPM");
    if (fgetc(file) != 'P' || fgetc(file) != '6' || !isspace(fgetc(file)))
        fail("Expected a binary P6 PPM");
    size_t width = ppm_number(file);
    size_t height = ppm_number(file);
    if (ppm_number(file) != 255 || width > SIZE_MAX / 3 / height)
        fail("Expected an 8-bit PPM with valid dimensions");
    *bytes = width * height * 3;
    uint8_t *pixels = checked_malloc(*bytes);
    if (fread(pixels, 1, *bytes, file) != *bytes)
        fail("Truncated PPM");
    fclose(file);
    uint64_t result = sum_u8(pixels, *bytes);
    free(pixels);
    return result;
}

static struct node **nodes(size_t n)
{
    struct node **p = checked_malloc(n * sizeof(*p));
    for (size_t i = 0; i != n; ++i) {
        p[i] = checked_malloc(sizeof(*p[i]));
        p[i]->value = (uint32_t)i;
    }
    uint32_t seed = UINT32_C(20261127);
    for (size_t i = n - 1; i != 0; --i) {
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        size_t j = seed % (i + 1);
        struct node *swap = p[i];
        p[i] = p[j];
        p[j] = swap;
    }
    for (size_t i = 1; i != n; ++i)
        p[i - 1]->next = p[i];
    p[n - 1]->next = p[0];
    return p;
}

int main(int argc, char **argv)
{
    if (argc != 4)
        fail("Usage: bench WORKLOAD COUNT_OR_PPM REPEATS");

    const char *kind = argv[1];
    int image = !strcmp(kind, "ppm");
    size_t n = image ? 0 : positive_size(argv[2]);
    size_t reps = positive_size(argv[3]);
    if (n > SIZE_MAX / sizeof(struct node *))
        fail("Input exceeds supported size");

    uint32_t *values = NULL;
    struct node **list = NULL;
    if (!strcmp(kind, "chase")) {
        list = nodes(n);
    } else if (!image) {
        values = checked_malloc(n * sizeof(*values));
        for (size_t i = 0; i != n; ++i)
            values[i] = (uint32_t)i * UINT32_C(2654435761);
    }

    int mixed = !strcmp(kind, "callback-mixed");
    int aot = !strcmp(kind, "callback-aot-mixed") ||
              !strcmp(kind, "callback-aot-mono");
    mixed |= !strcmp(kind, "callback-aot-mixed");

    uint64_t total = 0;
    uint64_t start = ns();
    if (!strcmp(kind, "scan")) {
        for (size_t r = 0; r != reps; ++r)
            total += sum_u32(values, n);
    } else if (list) {
        for (size_t r = 0; r != reps; ++r)
            total += chase(list[0], n);
    } else if (image) {
        for (size_t r = 0; r != reps; ++r)
            total += ppm(argv[2], &n);
    } else if (aot || mixed || !strcmp(kind, "callback-mono")) {
        for (size_t r = 0; r != reps; ++r) {
            const int common = !mixed || r % 10 != 9;
            if (aot && common)
                total += sum_clamped(values, n);
            else
                total += sum_transformed(values, n, common ? clamp : alternate);
        }
    } else {
        fail("Unknown workload");
    }
    uint64_t elapsed = ns() - start;

    if (list) {
        for (size_t i = 0; i != n; ++i)
            free(list[i]);
        free(list);
    }
    free(values);
    if (reps > SIZE_MAX / n)
        fail("Work count overflows");
    printf("%" PRIu64 " %zu %" PRIu64 "\n", elapsed, n * reps, total);
    return 0;
}
