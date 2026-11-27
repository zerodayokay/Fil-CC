#include <stddef.h>
#include <stdint.h>

typedef uint32_t (*transform_fn)(uint32_t);

struct node {
    struct node *next;
    uint32_t value;
};

uint64_t sum_u32(const uint32_t *p, size_t n);
uint64_t sum_u8(const uint8_t *p, size_t n);
uint64_t sum_transformed(const uint32_t *p, size_t n, transform_fn fn);
uint64_t sum_clamped(const uint32_t *p, size_t n);
uint64_t chase(const struct node *p, size_t n);
