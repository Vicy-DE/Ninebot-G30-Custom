/**
 * @file libc_min.c
 * @brief Minimal freestanding mem* for the bootloader (built with -nostdlib).
 *
 * The bootloader links without a C library, but the compiler emits calls to
 * memcpy/memset (struct copies/zeroing) and the .sfw code uses memcmp. These
 * tiny implementations satisfy those references. Build with
 * -fno-tree-loop-distribute-patterns so the compiler does not turn the loops
 * below into self-referential memcpy/memset calls.
 */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) { *d++ = *s++; }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) { *d++ = (unsigned char)c; }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y) { return (int)*x - (int)*y; }
        ++x; ++y;
    }
    return 0;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) { while (n--) { *d++ = *s++; } }
    else { d += n; s += n; while (n--) { *--d = *--s; } }
    return dst;
}
