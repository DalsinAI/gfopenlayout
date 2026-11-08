/* GFOpenGTK's glib: memory, strings, messages and lists (glib.h). The main
 * loop's half is in gtk.c, where gtk_main runs it.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "glib.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__AMIGA__) || defined(__amigaos__)
#define GTKC_AMIGA 1
#endif

/* ---- Memory -------------------------------------------------------------------- */

static void out_of_memory(gsize n)
{
    fprintf(stderr, "GFOpenGTK: out of memory (%lu bytes)\n", (unsigned long)n);
    exit(20);
}

gpointer g_malloc(gsize n)
{
    void *p;
    if (!n) return NULL;
    if (!(p = malloc(n))) out_of_memory(n);
    return p;
}

gpointer g_malloc0(gsize n)
{
    void *p;
    if (!n) return NULL;
    if (!(p = calloc(1, n))) out_of_memory(n);
    return p;
}

gpointer g_realloc(gpointer p, gsize n)
{
    if (!n) {
        free(p);
        return NULL;
    }
    if (!(p = realloc(p, n))) out_of_memory(n);
    return p;
}

gpointer g_try_malloc(gsize n) { return n ? malloc(n) : NULL; }

void g_free(gpointer p) { free(p); }

/* ---- Strings ------------------------------------------------------------------- */

gchar *g_strdup(const gchar *s)
{
    return s ? g_strndup(s, strlen(s)) : NULL;
}

gchar *g_strndup(const gchar *s, gsize n)
{
    gchar *d;
    gsize len = 0;
    if (!s) return NULL;
    while (len < n && s[len]) ++len;
    d = g_malloc(len + 1);
    memcpy(d, s, len);
    d[len] = 0;
    return d;
}

gchar *g_strdup_vprintf(const gchar *fmt, va_list ap)
{
    va_list again;
    int n;
    gchar *s;
    va_copy(again, ap);
    n = vsnprintf(NULL, 0, fmt, again);
    va_end(again);
    if (n < 0) return g_strdup("");
    s = g_malloc((gsize)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    return s;
}

gchar *g_strdup_printf(const gchar *fmt, ...)
{
    va_list ap;
    gchar *s;
    va_start(ap, fmt);
    s = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    return s;
}

gchar *g_strconcat(const gchar *s, ...)
{
    va_list ap;
    const gchar *p;
    gsize len = 0;
    gchar *d, *q;
    if (!s) return NULL;
    va_start(ap, s);
    for (p = s; p; p = va_arg(ap, const gchar *)) len += strlen(p);
    va_end(ap);
    q = d = g_malloc(len + 1);
    va_start(ap, s);
    for (p = s; p; p = va_arg(ap, const gchar *)) {
        size_t n = strlen(p);
        memcpy(q, p, n);
        q += n;
    }
    va_end(ap);
    *q = 0;
    return d;
}

gint g_snprintf(gchar *buf, gulong n, const gchar *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(buf, (size_t)n, fmt, ap);
    va_end(ap);
    return r;
}

gint g_strncasecmp(const gchar *a, const gchar *b, guint n)
{
    for (; n; --n, ++a, ++b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
    return 0;
}

gint g_strcasecmp(const gchar *a, const gchar *b) { return g_strncasecmp(a, b, UINT_MAX); }

gchar *g_strstrip(gchar *s)
{
    gchar *p = s, *e;
    if (!s) return s;
    while (isspace((unsigned char)*p)) ++p;
    memmove(s, p, strlen(p) + 1);
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) --e;
    *e = 0;
    return s;
}

void g_strfreev(gchar **v)
{
    gchar **p;
    if (!v) return;
    for (p = v; *p; ++p) free(*p);
    free(v);
}

/* ---- Messages ------------------------------------------------------------------ */

static void say(FILE *f, const char *what, const gchar *fmt, va_list ap)
{
    if (what) fprintf(f, "%s: ", what);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
}

void g_print(const gchar *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void g_message(const gchar *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    say(stdout, "Message", fmt, ap);
    va_end(ap);
}

void g_warning(const gchar *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    say(stderr, "Warning", fmt, ap);
    va_end(ap);
}

void g_error(const gchar *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    say(stderr, "Error", fmt, ap);
    va_end(ap);
    exit(20);
}

/* ---- GList ------------------------------------------------------------------------- */

GList *g_list_last(GList *l)
{
    if (l) while (l->next) l = l->next;
    return l;
}

GList *g_list_first(GList *l)
{
    if (l) while (l->prev) l = l->prev;
    return l;
}

GList *g_list_append(GList *l, gpointer data)
{
    GList *n = g_new0(GList, 1), *last = g_list_last(l);
    n->data = data;
    if (!last) return n;
    last->next = n;
    n->prev = last;
    return l;
}

GList *g_list_prepend(GList *l, gpointer data)
{
    GList *n = g_new0(GList, 1);
    n->data = data;
    n->next = l;
    if (l) {
        n->prev = l->prev;
        if (l->prev) l->prev->next = n;
        l->prev = n;
    }
    return n;
}

GList *g_list_remove(GList *l, gconstpointer data)
{
    GList *p;
    for (p = l; p; p = p->next)
        if (p->data == data) {
            if (p->prev) p->prev->next = p->next;
            if (p->next) p->next->prev = p->prev;
            if (p == l) l = p->next;
            free(p);
            break;
        }
    return l;
}

GList *g_list_reverse(GList *l)
{
    GList *last = NULL;
    while (l) {
        last = l;
        l = l->next;
        last->next = last->prev;
        last->prev = l;
    }
    return last;
}

GList *g_list_find(GList *l, gconstpointer data)
{
    for (; l; l = l->next)
        if (l->data == data) return l;
    return NULL;
}

GList *g_list_nth(GList *l, guint n)
{
    while (l && n--) l = l->next;
    return l;
}

gpointer g_list_nth_data(GList *l, guint n)
{
    l = g_list_nth(l, n);
    return l ? l->data : NULL;
}

guint g_list_length(GList *l)
{
    guint n = 0;
    for (; l; l = l->next) ++n;
    return n;
}

void g_list_foreach(GList *l, GFunc fn, gpointer user_data)
{
    while (l) {
        GList *next = l->next;
        fn(l->data, user_data);
        l = next;
    }
}

void g_list_free(GList *l)
{
    while (l) {
        GList *next = l->next;
        free(l);
        l = next;
    }
}

void g_list_free_1(GList *l) { free(l); }

/* ---- GSList ------------------------------------------------------------------------ */

GSList *g_slist_last(GSList *l)
{
    if (l) while (l->next) l = l->next;
    return l;
}

GSList *g_slist_append(GSList *l, gpointer data)
{
    GSList *n = g_new0(GSList, 1), *last = g_slist_last(l);
    n->data = data;
    if (!last) return n;
    last->next = n;
    return l;
}

GSList *g_slist_prepend(GSList *l, gpointer data)
{
    GSList *n = g_new(GSList, 1);
    n->data = data;
    n->next = l;
    return n;
}

GSList *g_slist_remove(GSList *l, gconstpointer data)
{
    GSList **pp;
    for (pp = &l; *pp; pp = &(*pp)->next)
        if ((*pp)->data == data) {
            GSList *dead = *pp;
            *pp = dead->next;
            free(dead);
            break;
        }
    return l;
}

GSList *g_slist_reverse(GSList *l)
{
    GSList *prev = NULL;
    while (l) {
        GSList *next = l->next;
        l->next = prev;
        prev = l;
        l = next;
    }
    return prev;
}

GSList *g_slist_find(GSList *l, gconstpointer data)
{
    for (; l; l = l->next)
        if (l->data == data) return l;
    return NULL;
}

GSList *g_slist_nth(GSList *l, guint n)
{
    while (l && n--) l = l->next;
    return l;
}

gpointer g_slist_nth_data(GSList *l, guint n)
{
    l = g_slist_nth(l, n);
    return l ? l->data : NULL;
}

gint g_slist_index(GSList *l, gconstpointer data)
{
    gint i;
    for (i = 0; l; l = l->next, ++i)
        if (l->data == data) return i;
    return -1;
}

guint g_slist_length(GSList *l)
{
    guint n = 0;
    for (; l; l = l->next) ++n;
    return n;
}

void g_slist_foreach(GSList *l, GFunc fn, gpointer user_data)
{
    while (l) {
        GSList *next = l->next;
        fn(l->data, user_data);
        l = next;
    }
}

void g_slist_free(GSList *l)
{
    while (l) {
        GSList *next = l->next;
        free(l);
        l = next;
    }
}

void g_slist_free_1(GSList *l) { free(l); }

/* ---- Odds and ends ----------------------------------------------------------------- */

const gchar *g_get_home_dir(void)
{
    const char *h = getenv("HOME");
#ifdef GTKC_AMIGA
    if (!h || !*h) h = "PROGDIR:";
#else
    if (!h || !*h) h = "/";
#endif
    return h;
}

const gchar *g_get_tmp_dir(void)
{
#ifdef GTKC_AMIGA
    return "T:";
#else
    const char *t = getenv("TMPDIR");
    return t && *t ? t : "/tmp";
#endif
}

gchar *g_get_current_dir(void)
{
    char buf[512];
    if (!getcwd(buf, sizeof(buf))) buf[0] = 0;
    return g_strdup(buf);
}
