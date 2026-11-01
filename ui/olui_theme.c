/* GFOpenLayout UI's theme: a GFOpenGadTools theme file (THEME_SPEC.md,
 * version 1) as CSS, read with GFOpenGadTools' own reader (lib/ogt_theme.c,
 * built in from OGT_ROOT), so a window described in HTML and CSS takes the
 * same colours, gradients and corners as every GadTools window. The
 * program's own CSS, added after, can still change any of it.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olui_internal.h"

#include "ogt_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *s;
    size_t len, cap;
    int failed;
} buf;

static void put(buf *b, const char *text)
{
    size_t n = strlen(text);
    if (b->failed) return;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 2048;
        char *p;
        while (cap < b->len + n + 1) cap *= 2;
        if (!(p = (char *)realloc(b->s, cap))) {
            b->failed = 1;
            return;
        }
        b->s = p;
        b->cap = cap;
    }
    memcpy(b->s + b->len, text, n + 1);
    b->len += n;
}

static void hex(char *out, ogt_rgb c)
{
    sprintf(out, "#%02x%02x%02x", c.r, c.g, c.b);
}

/* "prop:#rrggbb;" when the key has a colour */
static void colour(buf *b, const ogt_theme *t, int mode, const char *prop, const char *key)
{
    ogt_rgb c;
    char line[64], h[8];
    if (!ogt_theme_colour(t, mode, key, &c)) return;
    hex(h, c);
    sprintf(line, "%s:%s;", prop, h);
    put(b, line);
}

/* "background:linear-gradient(...);" from a gradient key (down the box; a
 * horizontal or radial one is drawn down it too), reversed for a pressed
 * button */
static void gradient(buf *b, const ogt_theme *t, int mode, const char *key, int reversed)
{
    ogt_grad g;
    int i, n;
    char part[32], h[8];
    if (!ogt_theme_grad(t, mode, key, &g)) return;
    n = g.n < OLUI_MAX_STOPS ? g.n : OLUI_MAX_STOPS;
    if (n < 2) {
        hex(h, g.c[0]);
        sprintf(part, "background:%s;", h);
        put(b, part);
        return;
    }
    put(b, "background:linear-gradient(");
    for (i = 0; i < n; ++i) {
        /* with more stops than ours: the first, some between, and the last;
         * reversed, from the bottom up */
        int j = reversed ? n - 1 - i : i;
        int k = g.n <= OLUI_MAX_STOPS ? j : (j * (g.n - 1)) / (n - 1);
        int pos = reversed ? 1000 - g.pos[k] : g.pos[k];
        hex(h, g.c[k]);
        sprintf(part, "%s%s %d%%", i ? "," : "", h, pos / 10);
        put(b, part);
    }
    put(b, ");");
}

static void radius(buf *b, const ogt_theme *t, int mode, const char *key)
{
    char part[40];
    sprintf(part, "border-radius:%dpx;", ogt_theme_number(t, mode, key, 0));
    put(b, part);
}

static void border(buf *b, const ogt_theme *t, int mode, const char *key)
{
    ogt_rgb c;
    char part[48], h[8];
    if (!ogt_theme_colour(t, mode, key, &c)) return;
    hex(h, c);
    sprintf(part, "border:1px solid %s;", h);
    put(b, part);
}

char *olui_theme_css(const char *theme_text, int dark, char *err, size_t errlen)
{
    ogt_theme t;
    buf b;
    int mode;
    char tmp[16];

    memset(&b, 0, sizeof(b));
    if (!theme_text || !ogt_theme_parse(&t, theme_text, err ? err : tmp, err ? errlen : sizeof(tmp)))
        return NULL;
    if (t.passthrough || !t.has_light) {
        /* Classic draws what the OS draws: the built-in look stands */
        ogt_theme_free(&t);
        put(&b, "/* Classic */\n");
        return b.s;
    }
    mode = dark && t.has_dark ? OGT_DARK : OGT_LIGHT;

    put(&b, "/* from the theme */\nwindow{");
    gradient(&b, &t, mode, "window", 0);
    colour(&b, &t, mode, "color", "text");
    colour(&b, &t, mode, "accent-color", "accent.mark");
    colour(&b, &t, mode, "placeholder-color", "muted");
    colour(&b, &t, mode, "mark-background", "string");
    colour(&b, &t, mode, "mark-border-color", "string.shadow");
    if (t.font_size > 0) {
        char part[32];
        sprintf(part, "font-size:%dpx;", t.font_size);
        put(&b, part);
    }
    put(&b, "}\nlabel,h1,h2,h3{");
    colour(&b, &t, mode, "color", "label");
    put(&b, "}\nbutton{");
    gradient(&b, &t, mode, "button", 0);
    colour(&b, &t, mode, "color", "button.text");
    border(&b, &t, mode, "button.border");
    radius(&b, &t, mode, "button.radius");
    put(&b, "}\nslider{");
    gradient(&b, &t, mode, "button", 0);
    border(&b, &t, mode, "button.border");
    radius(&b, &t, mode, "button.radius");
    colour(&b, &t, mode, "accent-color", "accent");
    colour(&b, &t, mode, "mark-background", "track");
    colour(&b, &t, mode, "mark-border-color", "string.shadow");
    put(&b, "}\nslider:focus{");
    colour(&b, &t, mode, "mark-border-color", "accent");
    put(&b, "}\nbutton:active{");
    gradient(&b, &t, mode, "button", 1);
    put(&b, "}\nbutton:focus,checkbox:focus,radio:focus,list:focus,tabbutton:focus,input:focus{");
    colour(&b, &t, mode, "border-color", "accent");
    put(&b, "}\ninput{");
    colour(&b, &t, mode, "background", "string");
    colour(&b, &t, mode, "color", "text");
    border(&b, &t, mode, "string.shadow");
    radius(&b, &t, mode, "string.radius");
    put(&b, "}\ninput:focus{");
    colour(&b, &t, mode, "background", "string.active");
    put(&b, "}\nlist,tree{");
    colour(&b, &t, mode, "background", "list");
    colour(&b, &t, mode, "color", "text");
    border(&b, &t, mode, "string.shadow");
    put(&b, "}\nitem:selected,li:selected{");
    colour(&b, &t, mode, "background", "accent");
    colour(&b, &t, mode, "color", "accent.text");
    put(&b, "}\nprogress{");
    colour(&b, &t, mode, "background", "track");
    border(&b, &t, mode, "string.shadow");
    colour(&b, &t, mode, "accent-color", "fill");
    put(&b, "}\ntabbutton{");
    gradient(&b, &t, mode, "tab", 0);
    colour(&b, &t, mode, "color", "tab.text");
    border(&b, &t, mode, "group.line");
    radius(&b, &t, mode, "tab.radius");
    put(&b, "}\ntabbutton:selected{");
    gradient(&b, &t, mode, "window", 0);
    colour(&b, &t, mode, "color", "text");
    put(&b, "}\ntab{");
    border(&b, &t, mode, "group.line");
    radius(&b, &t, mode, "group.radius");
    put(&b, "}\nhr{");
    colour(&b, &t, mode, "background", "group.line");
    put(&b, "}\n");
    ogt_theme_free(&t);
    if (b.failed) {
        free(b.s);
        if (err && errlen) snprintf(err, errlen, "out of memory");
        return NULL;
    }
    return b.s;
}
