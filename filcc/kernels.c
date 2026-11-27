#include "kernels.h"

uint64_t sum_u32(const uint32_t *p, size_t n)
{
    uint64_t total = 0;
    for (size_t i = 0; i != n; ++i)
        total += p[i];
    return total;
}

uint64_t sum_u8(const uint8_t *p, size_t n)
{
    uint64_t total = 0;
    for (size_t i = 0; i != n; ++i)
        total += p[i];
    return total;
}

uint64_t sum_transformed(const uint32_t *p, size_t n, transform_fn fn)
{
    uint64_t total = 0;
    for (size_t i = 0; i != n; ++i)
        total += fn(p[i]);
    return total;
}

uint64_t sum_clamped(const uint32_t *p, size_t n)
{
    uint64_t total = 0;
    for (size_t i = 0; i != n; ++i)
        total += p[i] & UINT32_C(255);
    return total;
}

uint64_t chase(const struct node *p, size_t n)
{
    uint64_t total = 0;
    for (size_t i = 0; i != n; ++i) {
        total += p->value;
        p = p->next;
    }
    return total;
}
