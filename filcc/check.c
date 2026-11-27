#include "kernels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc == 2) {
        if (!strcmp(argv[1], "null"))
            return (int)sum_u32(NULL, 9);
        uint32_t *p = calloc(4, sizeof(*p));
        if (!p)
            return 2;
        if (!strcmp(argv[1], "short"))
            return (int)sum_u32(p + 1, 9);
        if (!strcmp(argv[1], "freed")) {
            free(p);
            return (int)sum_u32(p, 9);
        }
        return 2;
    }

    if (sum_u32(NULL, 0) || sum_u8(NULL, 0))
        return 1;

    uint32_t words[33];
    uint8_t bytes[33];
    for (size_t i = 0; i != 33; ++i) {
        words[i] = (uint32_t)(i * i + 3);
        bytes[i] = (uint8_t)(i * i + 3);
    }
    for (size_t offset = 0; offset != 8; ++offset) {
        uint64_t expected_words = 0;
        uint64_t expected_bytes = 0;
        for (size_t n = 0; n <= 33 - offset; ++n) {
            if (sum_u32(words + offset, n) != expected_words ||
                sum_u8(bytes + offset, n) != expected_bytes)
                return 1;
            if (n != 33 - offset) {
                expected_words += words[offset + n];
                expected_bytes += bytes[offset + n];
            }
        }
    }

    puts("valid sums match");
    return 0;
}
