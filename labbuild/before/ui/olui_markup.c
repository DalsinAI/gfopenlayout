/* OpenLayout UI's markup: HTML's syntax, with the tags a window needs
 * (OpenLayout-UI.md). Text runs collapse their spaces as HTML's do;
 * <style> blocks are kept for the stylesheet.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "olui_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int olui_set_str(char **dst, const char *s)
{
    char *p = NULL;
    if (s) {
        size_t n = strlen(s) + 1;
        if (!(p = (char *)malloc(n))) return 0;
        memcpy(p, s, n);
    }
    free(*dst);
    *dst = p;
    return 1;
}

olui_el *olui_el_new(int kind, const char *tag)
{
    olui_el *el = (olui_el *)calloc(1, sizeof(olui_el));
    if (!el) return NULL;
    el->kind = kind;
    if (tag) {
        strncpy(el->tag, tag, sizeof(el->tag) - 1);
        el->tag[sizeof(el->tag) - 1] = 0;
    }
    el->imax = 100;
    return el;
}

void olui_el_free(olui_el *el)
{
    olui_el *c, *next;
    if (!el) return;
    for (c = el->first; c; c = next) {
        next = c->next;
        olui_el_free(c);
    }
    free(el->id); free(el->cls); free(el->name); free(el->value);
    free(el->text); free(el->style); free(el->placeholder); free(el->title);
    free(el);
}

void olui_el_append(olui_el *parent, olui_el *child)
{
    child->parent = parent;
    child->next = NULL;
    if (parent->last) parent->last->next = child;
    else parent->first = child;
    parent->last = child;
}

void olui_el_unlink(olui_el *el)
{
    olui_el *p = el->parent, **link;
    if (!p) return;
    for (link = &p->first; *link; link = &(*link)->next) {
        if (*link == el) {
            *link = el->next;
            break;
        }
    }
    if (p->last == el) {
        olui_el *c;
        p->last = NULL;
        for (c = p->first; c; c = c->next) p->last = c;
    }
    el->parent = el->next = NULL;
}

/* ---- Parsing ------------------------------------------------------------------ */

typedef struct {
    const char *p;
    char *err;
    size_t errlen;
    int line;
    char *css;                  /* <style> blocks, together */
    size_t css_len;
} parser;

static int fail(parser *ps, const char *why)
{
    if (ps->err && ps->errlen) snprintf(ps->err, ps->errlen, "line %d: %s", ps->line, why);
    return 0;
}

static int kind_of(const char *tag, const char *type)
{
    if (!strcmp(tag, "button")) return K_BUTTON;
    if (!strcmp(tag, "checkbox")) return K_CHECK;
    if (!strcmp(tag, "radio")) return K_RADIO;
    if (!strcmp(tag, "input")) {
        if (type && !strcmp(type, "checkbox")) return K_CHECK;
        if (type && !strcmp(type, "radio")) return K_RADIO;
        if (type && (!strcmp(type, "button") || !strcmp(type, "submit"))) return K_BUTTON;
        return K_INPUT;
    }
    if (!strcmp(tag, "list") || !strcmp(tag, "tree")) return K_LIST;
    if (!strcmp(tag, "item") || !strcmp(tag, "li")) return K_ITEM;
    if (!strcmp(tag, "tabs")) return K_TABS;
    if (!strcmp(tag, "tab")) return K_TAB;
    if (!strcmp(tag, "progress")) return K_PROGRESS;
    if (!strcmp(tag, "img")) return K_IMAGE;
    if (!strcmp(tag, "slider")) return K_SLIDER;
    return K_BOX;
}

static int is_void(const char *tag)
{
    return !strcmp(tag, "input") || !strcmp(tag, "br") || !strcmp(tag, "hr") || !strcmp(tag, "img") ||
           !strcmp(tag, "slider");
}

/* Appends UTF-8 for code point c */
static size_t put_utf8(char *d, unsigned long c)
{
    if (c < 0x80) { d[0] = (char)c; return 1; }
    if (c < 0x800) { d[0] = (char)(0xc0 | c >> 6); d[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) {
        d[0] = (char)(0xe0 | c >> 12); d[1] = (char)(0x80 | (c >> 6 & 63)); d[2] = (char)(0x80 | (c & 63));
        return 3;
    }
    d[0] = (char)(0xf0 | c >> 18); d[1] = (char)(0x80 | (c >> 12 & 63));
    d[2] = (char)(0x80 | (c >> 6 & 63)); d[3] = (char)(0x80 | (c & 63));
    return 4;
}

/* The text from s to e with its entities decoded; spaces collapsed when collapse. */
static char *decode(const char *s, const char *e, int collapse)
{
    char *out = (char *)malloc((size_t)(e - s) + 1), *d = out;
    int space = 0;
    if (!out) return NULL;
    while (s < e) {
        if (collapse && isspace((unsigned char)*s)) {
            space = 1;
            ++s;
            continue;
        }
        if (space) {
            *d++ = ' ';
            space = 0;
        }
        if (*s == '&') {
            const char *semi = memchr(s, ';', (size_t)(e - s) < 10 ? (size_t)(e - s) : 10);
            if (semi) {
                size_t n = (size_t)(semi - s - 1);
                unsigned long c = 0;
                if (n == 3 && !strncmp(s + 1, "amp", 3)) c = '&';
                else if (n == 2 && !strncmp(s + 1, "lt", 2)) c = '<';
                else if (n == 2 && !strncmp(s + 1, "gt", 2)) c = '>';
                else if (n == 4 && !strncmp(s + 1, "quot", 4)) c = '"';
                else if (n == 4 && !strncmp(s + 1, "apos", 4)) c = '\'';
                else if (n == 4 && !strncmp(s + 1, "nbsp", 4)) c = 0xa0;
                else if (n > 1 && s[1] == '#')
                    c = s[2] == 'x' ? strtoul(s + 3, NULL, 16) : strtoul(s + 2, NULL, 10);
                if (c && c < 0x110000) {
                    d += put_utf8(d, c);
                    s = semi + 1;
                    continue;
                }
            }
        }
        *d++ = *s++;
    }
    *d = 0;
    return out;
}

static int add_text(olui_el *parent, const char *s, const char *e)
{
    olui_el *t;
    char *text = decode(s, e, 1);
    if (!text) return 0;
    if (!text[0] || (text[0] == ' ' && !text[1])) {
        /* spaces alone between tags count only between two inline runs; drop them */
        free(text);
        return 1;
    }
    if (!(t = olui_el_new(K_TEXT, ""))) {
        free(text);
        return 0;
    }
    t->text = text;
    olui_el_append(parent, t);
    return 1;
}

static void skip_ws(parser *ps)
{
    while (isspace((unsigned char)*ps->p)) {
        if (*ps->p == '\n') ++ps->line;
        ++ps->p;
    }
}

static int set_attr(olui_el *el, const char *name, const char *value, char *type, size_t typelen)
{
    int ok = 1;
    if (!strcmp(name, "id")) ok = olui_set_str(&el->id, value);
    else if (!strcmp(name, "class")) ok = olui_set_str(&el->cls, value);
    else if (!strcmp(name, "name")) ok = olui_set_str(&el->name, value);
    else if (!strcmp(name, "value")) ok = olui_set_str(&el->value, value);
    else if (!strcmp(name, "style")) ok = olui_set_str(&el->style, value);
    else if (!strcmp(name, "placeholder")) ok = olui_set_str(&el->placeholder, value);
    else if (!strcmp(name, "title") || !strcmp(name, "label")) ok = olui_set_str(&el->title, value);
    else if (!strcmp(name, "type")) { strncpy(type, value, typelen - 1); type[typelen - 1] = 0; }
    else if (!strcmp(name, "checked")) el->state |= S_CHECKED;
    else if (!strcmp(name, "disabled")) el->state |= S_DISABLED;
    else if (!strcmp(name, "hidden")) el->state |= S_HIDDEN;
    else if (!strcmp(name, "selected")) el->state |= S_SELECTED;
    else if (!strcmp(name, "open")) el->state |= S_OPEN;
    else if (!strcmp(name, "max")) el->imax = atoi(value);
    else if (!strcmp(name, "min")) el->imin = atoi(value);
    else if (!strcmp(name, "image")) el->ivalue = atoi(value);
    else if (!strcmp(name, "width")) el->img_w = atoi(value);
    else if (!strcmp(name, "height")) el->img_h = atoi(value);
    return ok;
}

/* One element's content, up to its end tag (or the end, for the root) */
static int parse_content(parser *ps, olui_el *parent, int depth)
{
    if (depth > 64) return fail(ps, "elements nested too deep");
    for (;;) {
        const char *start = ps->p;
        while (*ps->p && *ps->p != '<') {
            if (*ps->p == '\n') ++ps->line;
            ++ps->p;
        }
        if (ps->p > start && !add_text(parent, start, ps->p)) return fail(ps, "out of memory");
        if (!*ps->p) return parent->parent ? fail(ps, "an element is not closed") : 1;

        if (!strncmp(ps->p, "<!--", 4)) {
            const char *e = strstr(ps->p + 4, "-->");
            if (!e) return fail(ps, "a comment is not closed");
            for (; ps->p < e; ++ps->p) if (*ps->p == '\n') ++ps->line;
            ps->p = e + 3;
            continue;
        }
        if (ps->p[1] == '!' || ps->p[1] == '?') {       /* <!doctype>, <?xml?> */
            const char *e = strchr(ps->p, '>');
            ps->p = e ? e + 1 : ps->p + strlen(ps->p);
            continue;
        }
        if (ps->p[1] == '/') {
            char tag[16];
            size_t n = 0;
            ps->p += 2;
            while (*ps->p && *ps->p != '>' && !isspace((unsigned char)*ps->p)) {
                if (n + 1 < sizeof(tag)) tag[n++] = (char)tolower((unsigned char)*ps->p);
                ++ps->p;
            }
            tag[n] = 0;
            while (*ps->p && *ps->p != '>') ++ps->p;
            if (*ps->p) ++ps->p;
            if (!parent->parent) return fail(ps, "an end tag with nothing open");
            if (strcmp(tag, parent->tag)) {
                char why[64];
                snprintf(why, sizeof(why), "</%s> where </%s> was due", tag, parent->tag);
                return fail(ps, why);
            }
            return 1;
        }

        /* a start tag */
        {
            char tag[16], type[16] = "";
            size_t n = 0;
            olui_el *el;
            int self_closed = 0;
            ++ps->p;
            while (*ps->p && (isalnum((unsigned char)*ps->p) || *ps->p == '-')) {
                if (n + 1 < sizeof(tag)) tag[n++] = (char)tolower((unsigned char)*ps->p);
                ++ps->p;
            }
            tag[n] = 0;
            if (!n) return fail(ps, "a '<' that starts no tag (write &lt;)");
            if (!(el = olui_el_new(K_BOX, tag))) return fail(ps, "out of memory");
            for (;;) {
                char name[24], *value = NULL;
                size_t k = 0;
                skip_ws(ps);
                if (*ps->p == '>') { ++ps->p; break; }
                if (ps->p[0] == '/' && ps->p[1] == '>') { ps->p += 2; self_closed = 1; break; }
                if (!*ps->p) { olui_el_free(el); return fail(ps, "a tag is not closed"); }
                while (*ps->p && !isspace((unsigned char)*ps->p) && *ps->p != '=' && *ps->p != '>' &&
                       *ps->p != '/') {
                    if (k + 1 < sizeof(name)) name[k++] = (char)tolower((unsigned char)*ps->p);
                    ++ps->p;
                }
                name[k] = 0;
                if (!k) { ++ps->p; continue; }
                skip_ws(ps);
                if (*ps->p == '=') {
                    const char *vs, *ve;
                    ++ps->p;
                    skip_ws(ps);
                    if (*ps->p == '"' || *ps->p == '\'') {
                        char q = *ps->p++;
                        vs = ps->p;
                        while (*ps->p && *ps->p != q) { if (*ps->p == '\n') ++ps->line; ++ps->p; }
                        ve = ps->p;
                        if (*ps->p) ++ps->p;
                    } else {
                        vs = ps->p;
                        while (*ps->p && !isspace((unsigned char)*ps->p) && *ps->p != '>') ++ps->p;
                        ve = ps->p;
                    }
                    if (!(value = decode(vs, ve, 0))) { olui_el_free(el); return fail(ps, "out of memory"); }
                }
                if (!set_attr(el, name, value ? value : "", type, sizeof(type))) {
                    free(value);
                    olui_el_free(el);
                    return fail(ps, "out of memory");
                }
                free(value);
            }
            el->kind = kind_of(tag, type[0] ? type : NULL);
            if (el->kind == K_SLIDER) {
                el->ivalue = el->value ? atoi(el->value) : el->imin;
                if (el->ivalue < el->imin) el->ivalue = el->imin;
                if (el->ivalue > el->imax) el->ivalue = el->imax;
            }
            if (!strcmp(tag, "style")) {
                /* its text goes to the stylesheet */
                const char *e = strstr(ps->p, "</style");
                size_t len;
                char *css;
                olui_el_free(el);
                if (!e) return fail(ps, "<style> is not closed");
                len = (size_t)(e - ps->p);
                css = (char *)realloc(ps->css, ps->css_len + len + 2);
                if (!css) return fail(ps, "out of memory");
                memcpy(css + ps->css_len, ps->p, len);
                ps->css_len += len;
                css[ps->css_len++] = '\n';
                css[ps->css_len] = 0;
                ps->css = css;
                for (; ps->p < e; ++ps->p) if (*ps->p == '\n') ++ps->line;
                ps->p = strchr(e, '>');
                ps->p = ps->p ? ps->p + 1 : e + strlen(e);
                continue;
            }
            if (!strcmp(tag, "br")) {
                olui_el_free(el);
                if (!(el = olui_el_new(K_TEXT, ""))) return fail(ps, "out of memory");
                if (!olui_set_str(&el->text, "\n")) { olui_el_free(el); return fail(ps, "out of memory"); }
                olui_el_append(parent, el);
                continue;
            }
            olui_el_append(parent, el);
            if (!self_closed && !is_void(tag) && !parse_content(ps, el, depth + 1)) return 0;
            /* one run of text alone is the element's own text */
            if (el->first && el->first == el->last && el->first->kind == K_TEXT && !el->text) {
                olui_el *t = el->first;
                el->text = t->text;
                t->text = NULL;
                el->first = el->last = NULL;
                olui_el_free(t);
                /* without the spaces at its ends */
                {
                    size_t len = strlen(el->text);
                    char *s = el->text;
                    while (len && s[len - 1] == ' ') s[--len] = 0;
                    if (*s == ' ') memmove(s, s + 1, len);
                }
            }
        }
    }
}

olui_el *olui_markup_parse(const char *markup, char *err, size_t errlen)
{
    parser ps;
    olui_el *root = olui_el_new(K_BOX, "window");
    if (!root) return NULL;
    memset(&ps, 0, sizeof(ps));
    ps.p = markup ? markup : "";
    ps.err = err;
    ps.errlen = errlen;
    ps.line = 1;
    if (!parse_content(&ps, root, 0)) {
        free(ps.css);
        olui_el_free(root);
        return NULL;
    }
    root->style = NULL;
    /* the <style> blocks ride on the root's value until olui_load takes them */
    root->value = ps.css;
    return root;
}
