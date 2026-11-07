/* GFOpenGTK's glib: the part of glib that GTK programs of the 1.2 and 2.x
 * days lean on, for AmigaOS 3.x and the host: the types, memory (g_malloc
 * stops the program when memory runs out, as glib does), strings, GList and
 * GSList, and the main loop's timeouts and idle functions, which run in
 * gtk_main (gtk/gtk.c is glib's main loop here: one per program, no
 * GMainContext of its own).
 *
 * Not here: GHashTable, GString, GArray, threads (g_thread_init is a no-op),
 * GModule, GIOChannel, GObject's types beyond the macros gtk.h gives.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFOPENGTK_GLIB_H
#define GFOPENGTK_GLIB_H

#include <stddef.h>
#include <stdarg.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char gchar;
typedef unsigned char guchar;
typedef short gshort;
typedef unsigned short gushort;
typedef int gint;
typedef unsigned int guint;
typedef long glong;
typedef unsigned long gulong;
typedef signed char gint8;
typedef unsigned char guint8;
typedef short gint16;
typedef unsigned short guint16;
typedef int gint32;
typedef unsigned int guint32;
typedef float gfloat;
typedef double gdouble;
typedef size_t gsize;
typedef long gssize;
typedef gint gboolean;
typedef void *gpointer;
typedef const void *gconstpointer;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE (!FALSE)
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

#undef MIN
#undef MAX
#undef ABS
#undef CLAMP
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ABS(a) (((a) < 0) ? -(a) : (a))
#define CLAMP(x, lo, hi) (((x) > (hi)) ? (hi) : (((x) < (lo)) ? (lo) : (x)))
#define G_N_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

#define GINT_TO_POINTER(i) ((gpointer)(glong)(i))
#define GPOINTER_TO_INT(p) ((gint)(glong)(p))
#define GUINT_TO_POINTER(u) ((gpointer)(gulong)(u))
#define GPOINTER_TO_UINT(p) ((guint)(gulong)(p))

#define G_MAXINT INT_MAX
#define G_MININT INT_MIN
#define G_MAXUINT UINT_MAX

#ifdef __GNUC__
#define G_GNUC_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define G_GNUC_PRINTF(f, a)
#endif

#define G_STMT_START do
#define G_STMT_END while (0)
#define g_return_if_fail(e) G_STMT_START { if (!(e)) { g_warning("%s: assertion `%s' failed", __func__, #e); return; } } G_STMT_END
#define g_return_val_if_fail(e, v) G_STMT_START { if (!(e)) { g_warning("%s: assertion `%s' failed", __func__, #e); return (v); } } G_STMT_END
#define g_assert(e) G_STMT_START { if (!(e)) g_error("%s:%d: assertion `%s' failed", __FILE__, __LINE__, #e); } G_STMT_END

/* ---- Memory -------------------------------------------------------------------- */

gpointer g_malloc(gsize n);
gpointer g_malloc0(gsize n);
gpointer g_realloc(gpointer p, gsize n);
gpointer g_try_malloc(gsize n);
void g_free(gpointer p);
#define g_new(type, n) ((type *)g_malloc(sizeof(type) * (gsize)(n)))
#define g_new0(type, n) ((type *)g_malloc0(sizeof(type) * (gsize)(n)))
#define g_renew(type, p, n) ((type *)g_realloc((p), sizeof(type) * (gsize)(n)))

/* ---- Strings ------------------------------------------------------------------- */

gchar *g_strdup(const gchar *s);
gchar *g_strndup(const gchar *s, gsize n);
gchar *g_strdup_printf(const gchar *fmt, ...) G_GNUC_PRINTF(1, 2);
gchar *g_strdup_vprintf(const gchar *fmt, va_list ap);
gchar *g_strconcat(const gchar *s, ...);   /* ends with NULL */
gint g_snprintf(gchar *buf, gulong n, const gchar *fmt, ...) G_GNUC_PRINTF(3, 4);
gint g_strcasecmp(const gchar *a, const gchar *b);
gint g_strncasecmp(const gchar *a, const gchar *b, guint n);
#define g_ascii_strcasecmp g_strcasecmp
#define g_ascii_strncasecmp g_strncasecmp
gchar *g_strstrip(gchar *s);
void g_strfreev(gchar **v);

/* ---- Messages ------------------------------------------------------------------ */

void g_print(const gchar *fmt, ...) G_GNUC_PRINTF(1, 2);
void g_message(const gchar *fmt, ...) G_GNUC_PRINTF(1, 2);
void g_warning(const gchar *fmt, ...) G_GNUC_PRINTF(1, 2);
void g_error(const gchar *fmt, ...) G_GNUC_PRINTF(1, 2);   /* stops the program */

/* ---- Lists ----------------------------------------------------------------------- */

typedef struct _GList GList;
struct _GList {
    gpointer data;
    GList *next;
    GList *prev;
};
typedef struct _GSList GSList;
struct _GSList {
    gpointer data;
    GSList *next;
};
typedef void (*GFunc)(gpointer data, gpointer user_data);
typedef void (*GDestroyNotify)(gpointer data);

GList *g_list_append(GList *l, gpointer data);
GList *g_list_prepend(GList *l, gpointer data);
GList *g_list_remove(GList *l, gconstpointer data);
GList *g_list_reverse(GList *l);
GList *g_list_find(GList *l, gconstpointer data);
GList *g_list_nth(GList *l, guint n);
gpointer g_list_nth_data(GList *l, guint n);
GList *g_list_first(GList *l);
GList *g_list_last(GList *l);
guint g_list_length(GList *l);
void g_list_foreach(GList *l, GFunc fn, gpointer user_data);
void g_list_free(GList *l);
void g_list_free_1(GList *l);
#define g_list_next(l) ((l) ? (l)->next : NULL)
#define g_list_previous(l) ((l) ? (l)->prev : NULL)

GSList *g_slist_append(GSList *l, gpointer data);
GSList *g_slist_prepend(GSList *l, gpointer data);
GSList *g_slist_remove(GSList *l, gconstpointer data);
GSList *g_slist_reverse(GSList *l);
GSList *g_slist_find(GSList *l, gconstpointer data);
GSList *g_slist_nth(GSList *l, guint n);
gpointer g_slist_nth_data(GSList *l, guint n);
GSList *g_slist_last(GSList *l);
gint g_slist_index(GSList *l, gconstpointer data);
guint g_slist_length(GSList *l);
void g_slist_foreach(GSList *l, GFunc fn, gpointer user_data);
void g_slist_free(GSList *l);
void g_slist_free_1(GSList *l);
#define g_slist_next(l) ((l) ? (l)->next : NULL)

/* ---- The main loop (run by gtk_main: gtk/gtk.c) ----------------------------------- */

typedef gboolean (*GSourceFunc)(gpointer data);
/* A function called every interval ms while it returns TRUE. An id for
 * g_source_remove (never 0). */
guint g_timeout_add(guint interval, GSourceFunc fn, gpointer data);
/* Called whenever the loop has nothing else to do, while it returns TRUE. */
guint g_idle_add(GSourceFunc fn, gpointer data);
gboolean g_source_remove(guint id);
/* One round of the loop: timeouts due, idle functions, windows' input and
 * drawing; waits for something when may_block and nothing was ready. TRUE
 * when something was done. */
gboolean g_main_iteration(gboolean may_block);
gboolean g_main_pending(void);
typedef struct _GMainContext GMainContext;
#define g_main_context_iteration(ctx, may_block) ((void)(ctx), g_main_iteration(may_block))
#define g_main_context_pending(ctx) ((void)(ctx), g_main_pending())

/* ---- Odds and ends ----------------------------------------------------------------- */

/* The home directory ($HOME; PROGDIR: on the Amiga when there is none), the
 * place for temporary files (T: on the Amiga) and the current directory (a
 * g_malloc'd copy). */
const gchar *g_get_home_dir(void);
const gchar *g_get_tmp_dir(void);
gchar *g_get_current_dir(void);
#define g_thread_init(vtable) ((void)(vtable))
#define g_thread_supported() (FALSE)

#ifdef __cplusplus
}
#endif
#endif
