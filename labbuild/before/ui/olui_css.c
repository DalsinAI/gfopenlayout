/* OpenLayout UI's CSS: rules with type, class, id and state selectors,
 * descendant and child combinators, and the properties a program's window
 * needs (OpenLayout-UI.md lists them). Written for OpenLayout UI; the
 * selector parser follows OpenBrowser Lite's (ob_css_lite.c).
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "olui_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CLASSES 4
#define MAX_PARTS 6

typedef struct {
    char tag[16];
    char id[40];
    char cls[MAX_CLASSES][32];
    int ncls;
    unsigned states;
    char comb;                  /* how it joins the part on its right: ' ' or '>' */
} css_part;

typedef struct {
    css_part part[MAX_PARTS];
    int nparts;
    unsigned spec;
    unsigned order;
    char *decls;
} css_rule;

struct olui_sheet {
    css_rule *rules;
    size_t count, capacity;
    unsigned order;
};

olui_sheet *olui_sheet_new(void)
{
    return (olui_sheet *)calloc(1, sizeof(olui_sheet));
}

void olui_sheet_free(olui_sheet *sheet)
{
    size_t i;
    if (!sheet) return;
    for (i = 0; i < sheet->count; ++i) free(sheet->rules[i].decls);
    free(sheet->rules);
    free(sheet);
}

static int ident(int c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

static const char *copy_ident(const char *p, char *dst, size_t size, int lower)
{
    size_t n = 0;
    while (*p && ident(*p)) {
        if (n + 1 < size) dst[n++] = (char)(lower ? tolower((unsigned char)*p) : *p);
        ++p;
    }
    dst[n] = 0;
    return p;
}

static unsigned state_of(const char *name)
{
    static const struct { const char *name; unsigned bit; } map[] = {
        { "hover", S_HOVER }, { "active", S_ACTIVE }, { "focus", S_FOCUS },
        { "checked", S_CHECKED }, { "disabled", S_DISABLED }, { "selected", S_SELECTED },
        { "open", S_OPEN }
    };
    size_t i;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
        if (!strcmp(map[i].name, name)) return map[i].bit;
    return 0;
}

/* One selector (no commas): 0 when it has what this CSS doesn't do. */
static int parse_selector(const char *p, css_rule *r)
{
    char comb = 0;
    memset(r->part, 0, sizeof(r->part));
    r->nparts = 0;
    r->spec = 0;
    while (*p) {
        css_part *part;
        while (isspace((unsigned char)*p)) {
            ++p;
            if (!comb) comb = ' ';
        }
        if (*p == '>') {
            comb = '>';
            ++p;
            continue;
        }
        if (!*p) break;
        if (r->nparts == MAX_PARTS) return 0;
        part = &r->part[r->nparts++];
        part->comb = r->nparts > 1 ? (comb ? comb : ' ') : 0;
        comb = 0;
        if (*p == '*') {
            ++p;
        } else if (isalpha((unsigned char)*p)) {
            p = copy_ident(p, part->tag, sizeof(part->tag), 1);
            r->spec += 1;
        }
        while (*p && !isspace((unsigned char)*p) && *p != '>') {
            char name[32];
            if (*p == '#') {
                p = copy_ident(p + 1, part->id, sizeof(part->id), 0);
                if (!part->id[0]) return 0;
                r->spec += 100;
            } else if (*p == '.') {
                p = copy_ident(p + 1, name, sizeof(name), 0);
                if (!name[0]) return 0;
                if (part->ncls < MAX_CLASSES) strcpy(part->cls[part->ncls++], name);
                r->spec += 10;
            } else if (*p == ':') {
                unsigned s;
                p = copy_ident(p + 1, name, sizeof(name), 1);
                if (!(s = state_of(name))) return 0;
                part->states |= s;
                r->spec += 10;
            } else {
                return 0;
            }
        }
    }
    return r->nparts > 0;
}

static char *dup_range(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static int add_rule(olui_sheet *sheet, const char *sel, size_t sel_len, const char *decls, size_t decl_len)
{
    css_rule r;
    char *s = dup_range(sel, sel_len);
    if (!s) return 0;
    if (!parse_selector(s, &r)) {
        free(s);
        return 1;               /* a selector we don't do is left out, as a browser does */
    }
    free(s);
    if (sheet->count == sheet->capacity) {
        size_t cap = sheet->capacity ? sheet->capacity * 2 : 32;
        css_rule *p = (css_rule *)realloc(sheet->rules, cap * sizeof(*p));
        if (!p) return 0;
        sheet->rules = p;
        sheet->capacity = cap;
    }
    if (!(r.decls = dup_range(decls, decl_len))) return 0;
    r.order = sheet->order++;
    sheet->rules[sheet->count++] = r;
    return 1;
}

int olui_sheet_add(olui_sheet *sheet, const char *css)
{
    const char *p = css;
    if (!sheet) return 0;
    if (!css) return 1;
    while (*p) {
        const char *sel, *open, *close;
        /* comments */
        if (p[0] == '/' && p[1] == '*') {
            const char *e = strstr(p + 2, "*/");
            p = e ? e + 2 : p + strlen(p);
            continue;
        }
        if (isspace((unsigned char)*p)) {
            ++p;
            continue;
        }
        sel = p;
        open = strchr(p, '{');
        if (!open) break;
        close = strchr(open, '}');
        if (!close) close = open + strlen(open);
        if (*sel == '@') {      /* @-rules are not ours: skip to the end of their block */
            int depth = 0;
            for (p = open; *p; ++p) {
                if (*p == '{') ++depth;
                else if (*p == '}' && --depth == 0) { ++p; break; }
            }
            continue;
        }
        /* each selector of a list, the same declarations */
        {
            const char *s = sel;
            while (s < open) {
                const char *e = s;
                while (e < open && *e != ',') ++e;
                if (!add_rule(sheet, s, (size_t)(e - s), open + 1, (size_t)(close - open - 1))) return 0;
                s = e < open ? e + 1 : e;
            }
        }
        p = *close ? close + 1 : close;
    }
    return 1;
}

/* ---- Matching ---------------------------------------------------------------- */

static int has_class(const olui_el *el, const char *c)
{
    const char *p = el->cls;
    size_t n = strlen(c);
    if (!p) return 0;
    while (*p) {
        while (*p == ' ') ++p;
        if (!strncmp(p, c, n) && (p[n] == 0 || p[n] == ' ')) return 1;
        while (*p && *p != ' ') ++p;
    }
    return 0;
}

static int part_matches(const css_part *part, const olui_el *el)
{
    int i;
    if (part->tag[0] && strcmp(part->tag, el->tag)) return 0;
    if (part->id[0] && (!el->id || strcmp(part->id, el->id))) return 0;
    for (i = 0; i < part->ncls; ++i)
        if (!has_class(el, part->cls[i])) return 0;
    if ((el->state & part->states) != part->states) return 0;
    return 1;
}

static int matches_from(const css_rule *r, int i, const olui_el *el)
{
    if (!part_matches(&r->part[i], el)) return 0;
    if (i == 0) return 1;
    if (r->part[i].comb == '>')
        return el->parent && matches_from(r, i - 1, el->parent);
    for (el = el->parent; el; el = el->parent)
        if (matches_from(r, i - 1, el)) return 1;
    return 0;
}

/* ---- Values ---------------------------------------------------------------------- */

static const char *skip_ws(const char *p)
{
    while (isspace((unsigned char)*p)) ++p;
    return p;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* A colour at p: #rgb, #rrggbb, #rrggbbaa, rgb(), rgba() or a name. *end after it. */
static int colour_at(const char *p, uint32_t *argb, const char **end)
{
    static const struct { const char *name; uint32_t argb; } names[] = {
        { "transparent", 0x00000000u }, { "black", 0xff000000u }, { "white", 0xffffffffu },
        { "red", 0xffff0000u }, { "green", 0xff008000u }, { "blue", 0xff0000ffu },
        { "gray", 0xff808080u }, { "grey", 0xff808080u }, { "silver", 0xffc0c0c0u },
        { "yellow", 0xffffff00u }, { "orange", 0xffffa500u }, { "navy", 0xff000080u }
    };
    size_t i;
    p = skip_ws(p);
    if (*p == '#') {
        int d[8], n = 0;
        ++p;
        while (n < 8 && hexval(p[n]) >= 0) { d[n] = hexval(p[n]); ++n; }
        if (ident(p[n])) return 0;
        if (n == 3) *argb = 0xff000000u | (uint32_t)(d[0] * 17) << 16 | (uint32_t)(d[1] * 17) << 8 | (uint32_t)(d[2] * 17);
        else if (n == 6 || n == 8) {
            uint32_t v = 0;
            for (i = 0; i < 6; ++i) v = v << 4 | (uint32_t)d[i];
            *argb = (n == 8 ? (uint32_t)(d[6] << 4 | d[7]) << 24 : 0xff000000u) | v;
        } else return 0;
        if (end) *end = p + n;
        return 1;
    }
    if (!strncmp(p, "rgb", 3)) {
        long c[4] = { 0, 0, 0, 255 };
        int k;
        char *e;
        const char *q = strchr(p, '(');
        if (!q) return 0;
        ++q;
        for (k = 0; k < 4; ++k) {
            q = skip_ws(q);
            if (*q == ')') break;
            if (k == 3) {               /* alpha, 0 to 1 */
                long whole = strtol(q, &e, 10), frac = 0;
                if (*e == '.') { frac = (e[1] >= '0' && e[1] <= '9') ? (e[1] - '0') : 0; while (isdigit((unsigned char)*++e)); }
                c[3] = whole >= 1 ? 255 : frac * 255 / 9;
            } else {
                c[k] = strtol(q, &e, 10);
            }
            if (e == q) return 0;
            q = skip_ws(e);
            if (*q == ',') ++q;
        }
        q = strchr(q, ')');
        if (!q) return 0;
        for (k = 0; k < 4; ++k) if (c[k] < 0) c[k] = 0; else if (c[k] > 255) c[k] = 255;
        *argb = (uint32_t)c[3] << 24 | (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
        if (end) *end = q + 1;
        return 1;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        size_t n = strlen(names[i].name);
        if (!strncmp(p, names[i].name, n) && !ident(p[n])) {
            *argb = names[i].argb;
            if (end) *end = p + n;
            return 1;
        }
    }
    return 0;
}

int olui_parse_colour(const char *s, uint32_t *argb)
{
    return colour_at(s, argb, NULL);
}

/* A length at p in units: px (and a bare number), pt, em (of em_size). */
static int length_at(const char *p, ol_unit em_size, ol_unit *out, const char **end)
{
    long whole = 0, frac = 0, scale = 1;
    int neg = 0, digits = 0;
    int64_t v;
    p = skip_ws(p);
    if (*p == '-') { neg = 1; ++p; }
    while (isdigit((unsigned char)*p)) { whole = whole * 10 + (*p++ - '0'); ++digits; }
    if (*p == '.') {
        ++p;
        while (isdigit((unsigned char)*p)) {
            if (scale < 1000) { frac = frac * 10 + (*p - '0'); scale *= 10; }
            ++p;
            ++digits;
        }
    }
    if (!digits) return 0;
    /* in thousandths of the unit */
    v = (int64_t)whole * 1000 + frac * (1000 / scale);
    if (!strncmp(p, "px", 2)) { v = v * OL_CSSPX(1) / 1000; p += 2; }
    else if (!strncmp(p, "pt", 2)) { v = v * OL_POINT(1) / 1000; p += 2; }
    else if (!strncmp(p, "em", 2)) { v = v * em_size / 1000; p += 2; }
    else v = v * OL_CSSPX(1) / 1000;
    *out = (ol_unit)(neg ? -v : v);
    if (end) *end = p;
    return 1;
}

static int number_at(const char *p, long *out)
{
    char *e;
    p = skip_ws(p);
    *out = strtol(p, &e, 10);
    return e != p;
}

/* "a b c d" lengths as CSS's sides: top, right, bottom, left */
static int sides(const char *p, ol_unit em, ol_unit out[4])
{
    ol_unit v[4];
    int n = 0;
    while (n < 4 && length_at(p, em, &v[n], &p)) ++n;
    if (n == 0) return 0;
    out[0] = v[0];
    out[1] = n > 1 ? v[1] : v[0];
    out[2] = n > 2 ? v[2] : v[0];
    out[3] = n > 3 ? v[3] : out[1];
    return 1;
}

/* linear-gradient(to bottom?, c1 [pos%], c2 [pos%], ...): down the box only */
static int gradient(const char *p, olui_cs *cs)
{
    const char *q = strchr(p, '(');
    int n = 0, k;
    if (!q) return 0;
    ++q;
    q = skip_ws(q);
    if (!strncmp(q, "to ", 3) || !strncmp(q, "180deg", 6)) {
        q = strchr(q, ',');
        if (!q) return 0;
        ++q;
    }
    while (n < OLUI_MAX_STOPS) {
        const char *e;
        if (!colour_at(q, &cs->stop_argb[n], &e)) break;
        q = skip_ws(e);
        cs->stop_pos[n] = -1;
        if (isdigit((unsigned char)*q)) {
            long pc = strtol(q, (char **)&e, 10);
            cs->stop_pos[n] = (int)(pc * 10);
            q = e;
            if (*q == '%') ++q;
        }
        ++n;
        q = skip_ws(q);
        if (*q != ',') break;
        ++q;
    }
    if (n == 0) return 0;
    for (k = 0; k < n; ++k)
        if (cs->stop_pos[k] < 0) cs->stop_pos[k] = n == 1 ? 0 : k * 1000 / (n - 1);
    cs->stops = n;
    return 1;
}

static int is(const char *v, const char *word)
{
    size_t n = strlen(word);
    return !strncmp(v, word, n) && !ident(v[n]);
}

static void set_font_size(olui_cs *cs, ol_unit fs, int lh_ratio)
{
    cs->ol.font_size = fs;
    if (lh_ratio > 0) cs->ol.line_height = (ol_unit)((int64_t)fs * lh_ratio / 100);
}

/* One property */
static void apply(olui_cs *cs, const char *prop, const char *v, const olui_cs *parent)
{
    ol_unit len, four[4];
    uint32_t c;
    long n;
    ol_unit em = cs->ol.font_size;

    if (!strcmp(prop, "display")) {
        if (is(v, "none")) cs->ol.display = OL_DISPLAY_NONE;
        else if (is(v, "inline") || is(v, "inline-block")) cs->ol.display = OL_DISPLAY_INLINE;
        else if (is(v, "flex") || is(v, "inline-flex")) {
            cs->ol.display = OL_DISPLAY_BLOCK;
            if (!cs->column) cs->box.flags |= OL_BOX_ROW;
        } else cs->ol.display = OL_DISPLAY_BLOCK;
    } else if (!strcmp(prop, "flex-direction")) {
        cs->column = is(v, "column");
        if (cs->column) cs->box.flags &= ~(uint32_t)OL_BOX_ROW;
        else cs->box.flags |= OL_BOX_ROW;
    } else if (!strcmp(prop, "flex") || !strcmp(prop, "flex-grow")) {
        if (is(v, "none")) cs->box.grow = 0;
        else if (is(v, "auto")) cs->box.grow = 1;
        else if (number_at(v, &n)) cs->box.grow = (int32_t)n;
    } else if (!strcmp(prop, "gap")) {
        if (length_at(v, em, &len, NULL)) cs->box.gap = len;
    } else if (!strcmp(prop, "align-items")) {
        if (is(v, "center")) cs->box.align = OL_CROSS_CENTRE;
        else if (is(v, "start") || is(v, "flex-start")) cs->box.align = OL_CROSS_START;
        else if (is(v, "end") || is(v, "flex-end")) cs->box.align = OL_CROSS_END;
        else cs->box.align = OL_CROSS_STRETCH;
    } else if (!strcmp(prop, "justify-content")) {
        /* a row's children with room to spare: OpenLayout puts them at the row's text_align */
        if (is(v, "center")) cs->ol.text_align = OL_ALIGN_CENTER;
        else if (is(v, "end") || is(v, "flex-end") || is(v, "right")) cs->ol.text_align = OL_ALIGN_END;
        else cs->ol.text_align = OL_ALIGN_START;
    } else if (!strcmp(prop, "width")) {
        if (is(v, "auto")) cs->box.width = 0;
        else if (length_at(v, em, &len, NULL)) cs->box.width = len;
    } else if (!strcmp(prop, "height")) {
        if (is(v, "auto")) cs->box.height = 0;
        else if (length_at(v, em, &len, NULL)) cs->box.height = len;
    } else if (!strcmp(prop, "margin")) {
        if (sides(v, em, four)) {
            cs->ol.margin_top = four[0]; cs->ol.margin_right = four[1];
            cs->ol.margin_bottom = four[2]; cs->ol.margin_left = four[3];
        }
    } else if (!strcmp(prop, "padding")) {
        if (sides(v, em, four)) {
            cs->ol.padding_top = four[0]; cs->ol.padding_right = four[1];
            cs->ol.padding_bottom = four[2]; cs->ol.padding_left = four[3];
        }
    } else if (!strncmp(prop, "margin-", 7) || !strncmp(prop, "padding-", 8)) {
        int pad = prop[0] == 'p';
        const char *side = prop + (pad ? 8 : 7);
        if (!length_at(v, em, &len, NULL)) return;
        if (!strcmp(side, "top")) *(pad ? &cs->ol.padding_top : &cs->ol.margin_top) = len;
        else if (!strcmp(side, "right")) *(pad ? &cs->ol.padding_right : &cs->ol.margin_right) = len;
        else if (!strcmp(side, "bottom")) *(pad ? &cs->ol.padding_bottom : &cs->ol.margin_bottom) = len;
        else if (!strcmp(side, "left")) *(pad ? &cs->ol.padding_left : &cs->ol.margin_left) = len;
    } else if (!strcmp(prop, "color")) {
        if (colour_at(v, &c, NULL)) cs->ol.foreground = c;
    } else if (!strcmp(prop, "accent-color")) {
        if (colour_at(v, &c, NULL)) cs->accent = c;
    } else if (!strcmp(prop, "mark-background")) {
        if (colour_at(v, &c, NULL)) cs->mark_bg = c;
    } else if (!strcmp(prop, "mark-border-color")) {
        if (colour_at(v, &c, NULL)) cs->mark_border = c;
    } else if (!strcmp(prop, "placeholder-color")) {
        if (colour_at(v, &c, NULL)) cs->muted = c;
    } else if (!strcmp(prop, "background") || !strcmp(prop, "background-color") ||
               !strcmp(prop, "background-image")) {
        if (strstr(v, "gradient(")) {
            gradient(v, cs);
        } else if (is(v, "none")) {
            cs->stops = 0;
        } else if (colour_at(v, &c, NULL)) {
            cs->stops = (c >> 24) ? 1 : 0;
            cs->stop_argb[0] = c;
            cs->stop_pos[0] = 0;
        }
    } else if (!strcmp(prop, "border")) {
        /* width, style and colour in any order; "none" takes it away */
        const char *p = v;
        if (is(skip_ws(v), "none") || is(skip_ws(v), "0")) { cs->border = 0; return; }
        while (*(p = skip_ws(p))) {
            const char *e;
            if (length_at(p, em, &len, &e)) { cs->border = len; p = e; }
            else if (colour_at(p, &c, &e)) { cs->border_argb = c; p = e; }
            else { while (*p && !isspace((unsigned char)*p)) ++p; }
        }
        if (!cs->border) cs->border = OL_CSSPX(1);
    } else if (!strcmp(prop, "border-width")) {
        if (length_at(v, em, &len, NULL)) cs->border = len;
    } else if (!strcmp(prop, "border-color")) {
        if (colour_at(v, &c, NULL)) cs->border_argb = c;
    } else if (!strcmp(prop, "border-radius")) {
        if (length_at(v, em, &len, NULL)) cs->radius = len;
    } else if (!strcmp(prop, "font-size")) {
        ol_unit base = parent ? parent->ol.font_size : OL_CSSPX(13);
        if (length_at(v, base, &len, NULL) && len > 0) {
            int ratio100 = cs->ol.font_size > 0
                ? (int)((int64_t)cs->ol.line_height * 100 / cs->ol.font_size) : 125;
            set_font_size(cs, len, ratio100);
        }
    } else if (!strcmp(prop, "line-height")) {
        const char *e;
        if (length_at(v, em, &len, &e)) {
            /* a bare number is times the font size */
            if (e[-1] == 'x' || e[-1] == 't' || e[-1] == 'm') cs->ol.line_height = len;
            else cs->ol.line_height = (ol_unit)((int64_t)len * cs->ol.font_size / OL_CSSPX(1));
        }
    } else if (!strcmp(prop, "font-weight")) {
        if (is(v, "bold") || (number_at(v, &n) && n >= 600)) cs->ol.text_flags |= OL_TEXT_BOLD;
        else cs->ol.text_flags &= ~(uint32_t)OL_TEXT_BOLD;
    } else if (!strcmp(prop, "font-style")) {
        if (is(v, "italic") || is(v, "oblique")) cs->ol.text_flags |= OL_TEXT_ITALIC;
        else cs->ol.text_flags &= ~(uint32_t)OL_TEXT_ITALIC;
    } else if (!strcmp(prop, "font-family")) {
        if (strstr(v, "monospace")) cs->ol.text_flags |= OL_TEXT_MONO;
        else cs->ol.text_flags &= ~(uint32_t)OL_TEXT_MONO;
    } else if (!strcmp(prop, "text-decoration")) {
        if (strstr(v, "underline")) cs->ol.text_flags |= OL_TEXT_UNDERLINE;
        else cs->ol.text_flags &= ~(uint32_t)OL_TEXT_UNDERLINE;
    } else if (!strcmp(prop, "text-align")) {
        if (is(v, "center")) cs->ol.text_align = OL_ALIGN_CENTER;
        else if (is(v, "right") || is(v, "end")) cs->ol.text_align = OL_ALIGN_END;
        else if (is(v, "justify")) cs->ol.text_align = OL_ALIGN_JUSTIFY;
        else cs->ol.text_align = OL_ALIGN_START;
    }
}

void olui_style_apply_decls(olui_cs *cs, const char *decls, const olui_cs *parent)
{
    const char *p = decls;
    while (p && *p) {
        char prop[32], value[160];
        size_t n = 0;
        int depth = 0;
        p = skip_ws(p);
        while (*p && *p != ':' && *p != ';') {
            if (n + 1 < sizeof(prop) && !isspace((unsigned char)*p))
                prop[n++] = (char)tolower((unsigned char)*p);
            ++p;
        }
        prop[n] = 0;
        if (*p != ':') {
            if (*p) ++p;
            continue;
        }
        ++p;
        p = skip_ws(p);
        n = 0;
        while (*p && (*p != ';' || depth > 0)) {
            if (*p == '(') ++depth;
            else if (*p == ')') --depth;
            if (n + 1 < sizeof(value)) value[n++] = *p;
            ++p;
        }
        while (n > 0 && isspace((unsigned char)value[n - 1])) --n;
        value[n] = 0;
        /* !important is taken as written: the rules are applied in order anyway */
        {
            char *imp = strstr(value, "!important");
            if (imp) { *imp = 0; while (imp > value && isspace((unsigned char)imp[-1])) *--imp = 0; }
        }
        if (prop[0]) apply(cs, prop, value, parent);
        if (*p == ';') ++p;
    }
}

static int rule_cmp(const void *a, const void *b)
{
    const css_rule *x = *(const css_rule *const *)a, *y = *(const css_rule *const *)b;
    if (x->spec != y->spec) return x->spec < y->spec ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

void olui_style_compute(const olui_sheet *sheet, olui_el *el, const olui_cs *parent)
{
    olui_cs *cs = &el->cs;
    const css_rule *hits[64];
    size_t i, nhits = 0;

    /* what is inherited, and what isn't */
    memset(cs, 0, sizeof(*cs));
    ol_style_init(&cs->ol);
    ol_box_style_init(&cs->box);
    cs->box.border_sides = 0;
    cs->border_argb = 0xff000000u;
    cs->accent = 0xff3366ccu;
    cs->mark_bg = 0xffffffffu;
    cs->mark_border = 0xff808080u;
    cs->muted = 0xff909090u;
    if (parent) {
        cs->ol.font_size = parent->ol.font_size;
        cs->ol.line_height = parent->ol.line_height;
        cs->ol.foreground = parent->ol.foreground;
        cs->ol.text_flags = parent->ol.text_flags;
        cs->accent = parent->accent;
        cs->mark_bg = parent->mark_bg;
        cs->mark_border = parent->mark_border;
        cs->muted = parent->muted;
        if (!(parent->box.flags & OL_BOX_ROW)) cs->ol.text_align = parent->ol.text_align;
    } else {
        cs->ol.font_size = OL_CSSPX(13);
        cs->ol.line_height = OL_CSSPX(16);
    }
    if (sheet) {
        for (i = 0; i < sheet->count; ++i) {
            const css_rule *r = &sheet->rules[i];
            if (matches_from(r, r->nparts - 1, el)) {
                if (nhits == sizeof(hits) / sizeof(hits[0])) break;
                hits[nhits++] = r;
            }
        }
        qsort(hits, nhits, sizeof(hits[0]), rule_cmp);
        for (i = 0; i < nhits; ++i) olui_style_apply_decls(cs, hits[i]->decls, parent);
    }
    if (el->style) olui_style_apply_decls(cs, el->style, parent);
    if (el->state & S_HIDDEN) cs->ol.display = OL_DISPLAY_NONE;
}
