/* The rest of the C library version 10 needs inside gfopenlayout.library:
 * GFOpenLayout UI's and GFOpenGadTools' theme reader's string calls, their
 * character classes (ctype.h's table, through __locale_ctype_ptr as the
 * stove's newlib headers read it) and an integer-only snprintf (%d %i %u %x
 * %X %c %s %%, with the flags - and 0, a width, a precision for %s and the
 * l and z sizes). No floating point, no stdio, no locale: a library has no
 * process of its own.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 */
#include <stddef.h>
#include <stdarg.h>
#include <ctype.h>   /* for its class bits (_U, _L ...) */

/* not string.h, stdlib.h or stdio.h: libnix's have some of these as inline
   functions of their own; these are what is called when one is not inlined */
size_t strlen(const char *s);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
size_t strcspn(const char *s, const char *reject);
int memcmp(const void *a, const void *b, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memchr(const void *p, int c, size_t n);
const char *__locale_ctype_ptr(void);
unsigned long strtoul(const char *s, char **end, int base);
long strtol(const char *s, char **end, int base);
int atoi(const char *s);
long atol(const char *s);
int vsnprintf(char *s, size_t size, const char *f, va_list ap);
int snprintf(char *s, size_t size, const char *f, ...);
int sprintf(char *s, const char *f, ...);
#ifndef NULL
#define NULL ((void *)0)
#endif

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; --n, ++a, ++b) {
        unsigned char ac = (unsigned char)*a, bc = (unsigned char)*b;
        if (ac != bc) return (int)ac - (int)bc;
        if (!ac) break;
    }
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    for (; n && *s; --n) *d++ = *s++;
    for (; n; --n) *d++ = 0;
    return r;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *e = d + strlen(d);
    for (; n && *s; --n) *e++ = *s++;
    *e = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; ++s) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (;; ++s) {
        if (*s == (char)c) last = s;
        if (!*s) return (char *)last;
    }
}

char *strstr(const char *h, const char *n)
{
    size_t len = strlen(n);
    if (!len) return (char *)h;
    for (; *h; ++h)
        if (*h == *n && !strncmp(h, n, len)) return (char *)h;
    return NULL;
}

size_t strcspn(const char *s, const char *reject)
{
    const char *p = s;
    for (; *p; ++p)
        if (strchr(reject, *p)) break;
    return (size_t)(p - s);
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; --n, ++x, ++y)
        if (*x != *y) return (int)*x - (int)*y;
    return 0;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) while (n--) *d++ = *s++;
    else if (d > s) { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void *memchr(const void *p, int c, size_t n)
{
    const unsigned char *s = p;
    for (; n; --n, ++s)
        if (*s == (unsigned char)c) return (void *)s;
    return NULL;
}

#ifdef _U   /* the stove's newlib ctype.h (the host's has no table to give) */
/* ctype.h's table: one entry before the characters (EOF), then 0 to 255;
   only ASCII has a class, as in the C locale */
static char ctype_table[1 + 256];
static int ctype_ready;

const char *__locale_ctype_ptr(void)
{
    if (!ctype_ready) {
        int c;
        for (c = 0; c < 128; ++c) {
            char k = 0;
            if (c >= 'A' && c <= 'Z') k |= _U;
            if (c >= 'a' && c <= 'z') k |= _L;
            if (c >= '0' && c <= '9') k |= _N;
            if (c == ' ' || (c >= 9 && c <= 13)) k |= _S;
            if (c < 32 || c == 127) k |= _C;
            if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) k |= _X;
            if (c == ' ') k |= _B;
            if (c > 32 && c < 127 && !(k & (_U | _L | _N))) k |= _P;
            ctype_table[1 + c] = k;
        }
        ctype_ready = 1;    /* the same table whoever makes it first */
    }
    return ctype_table;
}

#endif

int (tolower)(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int (toupper)(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    const char *p = s;
    int neg = 0, any = 0;
    while (*p == ' ' || (*p >= 9 && *p <= 13)) ++p;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
    else if (base == 0) base = p[0] == '0' ? 8 : 10;
    for (;; ++p) {
        int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'z' ? *p - 'a' + 10
              : *p >= 'A' && *p <= 'Z' ? *p - 'A' + 10 : 99;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
        any = 1;
    }
    if (end) *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

long strtol(const char *s, char **end, int base) { return (long)strtoul(s, end, base); }
int atoi(const char *s) { return (int)strtoul(s, NULL, 10); }
long atol(const char *s) { return (long)strtoul(s, NULL, 10); }

/* ---- snprintf ---- */

struct out { char *s; size_t size, n; };

static void put(struct out *o, char c)
{
    if (o->n + 1 < o->size) o->s[o->n] = c;
    ++o->n;
}

int vsnprintf(char *s, size_t size, const char *f, va_list ap)
{
    struct out o;
    o.s = s; o.size = size; o.n = 0;
    for (; *f; ++f) {
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0;
        char buf[24];
        const char *str;
        size_t len, i;
        if (*f != '%') { put(&o, *f); continue; }
        ++f;
        for (;; ++f) {
            if (*f == '-') left = 1;
            else if (*f == '0') zero = 1;
            else break;
        }
        if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } ++f; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
        if (*f == '.') {
            ++f; prec = 0;
            if (*f == '*') { prec = va_arg(ap, int); ++f; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + *f++ - '0';
        }
        while (*f == 'l' || *f == 'z' || *f == 'h') { if (*f != 'h') lng = 1; ++f; }
        str = buf;
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': {
            unsigned long v;
            int neg = 0, b = (*f == 'x' || *f == 'X') ? 16 : 10;
            const char *dig = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char *e = buf + sizeof(buf);
            if (*f == 'd' || *f == 'i') {
                long sv = lng ? va_arg(ap, long) : va_arg(ap, int);
                neg = sv < 0;
                v = neg ? 0UL - (unsigned long)sv : (unsigned long)sv;
            } else v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            *--e = 0;
            do { *--e = dig[v % (unsigned)b]; v /= (unsigned)b; } while (v);
            if (neg) {
                if (zero && !left) { put(&o, '-'); if (width) --width; }
                else *--e = '-';
            }
            str = e;
            len = strlen(str);
            break;
        }
        case 'c': buf[0] = (char)va_arg(ap, int); buf[1] = 0; len = 1; break;
        case 's':
            str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            for (len = 0; str[len] && (prec < 0 || (int)len < prec); ++len) ;
            zero = 0;
            break;
        case '%': buf[0] = '%'; len = 1; width = 0; break;
        default:
            if (!*f) --f;
            continue;
        }
        if (!left) for (i = len; (int)i < width; ++i) put(&o, zero ? '0' : ' ');
        for (i = 0; i < len; ++i) put(&o, str[i]);
        if (left) for (i = len; (int)i < width; ++i) put(&o, ' ');
    }
    if (o.size) o.s[o.n < o.size ? o.n : o.size - 1] = 0;
    return (int)o.n;
}

int snprintf(char *s, size_t size, const char *f, ...)
{
    va_list ap;
    int n;
    va_start(ap, f);
    n = vsnprintf(s, size, f, ap);
    va_end(ap);
    return n;
}

int sprintf(char *s, const char *f, ...)
{
    va_list ap;
    int n;
    va_start(ap, f);
    n = vsnprintf(s, (size_t)-1 / 2, f, ap);
    va_end(ap);
    return n;
}
