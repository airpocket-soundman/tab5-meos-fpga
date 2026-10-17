/* Minimal runtime for M5EX apps.  Compiled with -fno-tree-loop-distribute-patterns
 * so GCC does not turn these loops back into calls to themselves. */
#include "m5ex_sdk.h"

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dd = d; const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dd = d; const uint8_t *ss = s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    uint8_t *dd = d;
    while (n--) *dd++ = (uint8_t)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
