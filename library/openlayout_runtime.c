/* Minimal process-free runtime for resident gfopenlayout.library.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stddef.h>

extern struct ExecBase *SysBase;

struct OLAllocHeader {
    ULONG size;
};

void *malloc(size_t n)
{
    struct OLAllocHeader *h;
    ULONG bytes;
    if (n == 0) n = 1;
    if (n > 0x7ffffff0UL) return NULL;
    bytes = (ULONG)n + sizeof(*h);
    h = (struct OLAllocHeader *)AllocVec(bytes, MEMF_ANY);
    if (!h) return NULL;
    h->size = (ULONG)n;
    return (void *)(h + 1);
}

void free(void *p)
{
    struct OLAllocHeader *h;
    if (!p) return;
    h = ((struct OLAllocHeader *)p) - 1;
    FreeVec(h);
}

void *calloc(size_t n, size_t size)
{
    UBYTE *p;
    size_t total, i;
    if (n && size > ((size_t)-1) / n) return NULL;
    total = n * size;
    p = (UBYTE *)malloc(total);
    if (!p) return NULL;
    for (i = 0; i < total; ++i) p[i] = 0;
    return p;
}

void *realloc(void *p, size_t n)
{
    struct OLAllocHeader *h;
    UBYTE *q, *src;
    size_t oldn, copy, i;
    if (!p) return malloc(n);
    if (n == 0) { free(p); return NULL; }
    h = ((struct OLAllocHeader *)p) - 1;
    oldn = h->size;
    q = (UBYTE *)malloc(n);
    if (!q) return NULL;
    src = (UBYTE *)p;
    copy = oldn < n ? oldn : n;
    for (i = 0; i < copy; ++i) q[i] = src[i];
    free(p);
    return q;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    UBYTE *d = (UBYTE *)dst;
    const UBYTE *s = (const UBYTE *)src;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    UBYTE *d = (UBYTE *)dst;
    size_t i;
    for (i = 0; i < n; ++i) d[i] = (UBYTE)c;
    return dst;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    unsigned char ac, bc;
    do {
        ac = (unsigned char)*a++;
        bc = (unsigned char)*b++;
        if (ac != bc) return (int)ac - (int)bc;
    } while (ac);
    return 0;
}
