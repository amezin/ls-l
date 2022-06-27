#pragma once

#include <stddef.h>

size_t unique(void *buf, size_t n, size_t size, int (*cmp)(const void *, const void *));
