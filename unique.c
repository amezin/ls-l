#include "unique.h"

#include <string.h>

size_t unique(void *buf, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    const void *end = (char *)buf + n * size;

    void *out = buf;
    const void *prev_out = NULL;
    size_t out_count = 0;

    for (const void *in = buf; in < end; in += size) {
        if (prev_out != NULL) {
            if (cmp(prev_out, in) == 0) {
                continue;
            }
        }

        if (out != in) {
            memcpy(out, in, size);
        }

        prev_out = out;
        out += size;
        out_count += 1;
    }

    return out_count;
}
