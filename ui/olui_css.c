/* GFOpenLayout UI's CSS, through GFCSS (gfopenlayout_css.h), the engine it
 * shares with GFOpenBrowser Lite: an olui_sheet is a GFCSS engine with one
 * sheet, every element is styled by it through the olui_el functions
 * below, and its computed style is turned into what the UI draws and lays
 * out (olui_cs). The UI's own properties (accent-color, mark-background,
 * mark-border-color, placeholder-color) are GFCSS's caller's properties;
 * :selected and :open are its caller's pseudo-classes; a
 * linear-gradient() background comes from GFCSS as the function's text.
 *
 * What the UI did before GFCSS, it still does: a box is a block unless its
 * CSS says otherwise, a row's children don't take its text-align (its
 * justify-content is that), a line height follows a new font size in
 * proportion, and a border is as wide as its top side.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olui_internal.h"
#include "gfopenlayout_css.h"

#include <stdlib.h>
#include <string.h>

/* The UI's own properties, and what they are with no rule */
enum { X_ACCENT = 0, X_MARK_BG, X_MARK_BORDER, X_MUTED, X_GRID, X_SAME, X_COUNT };

static int own_colour(void *userdata, int slot, const char *value, size_t length, uint32_t *out);
static int own_count(void *userdata, int slot, const char *value, size_t length, uint32_t *out);
static int own_same(void *userdata, int slot, const char *value, size_t length, uint32_t *out);

static const struct {
    const char *name;
    uint32_t initial;
    unsigned flags;
    gfcss_ext_fn parse;
} own[X_COUNT] = {
    { "accent-color", 0xff3366ccu, GFCSS_INHERITED, own_colour },
    { "mark-background", 0xffffffffu, GFCSS_INHERITED, own_colour },
    { "mark-border-color", 0xff808080u, GFCSS_INHERITED, own_colour },
    { "placeholder-color", 0xff909090u, GFCSS_INHERITED, own_colour },
    /* 0.5 (library 18): a grid of n columns (OL_BOX_GRID); 0 or none: not a grid */
    { "grid-columns", 0, 0, own_count },
    /* library 18: its children the same size (MUI's SameSize): none, width, height or both */
    { "same-size", 0, 0, own_same }
};

#define ST_SELECTED GFCSS_STATE_USER(0)
#define ST_OPEN GFCSS_STATE_USER(1)
#define ST_DRAGGING GFCSS_STATE_USER(2)
#define ST_DROP GFCSS_STATE_USER(3)

struct olui_sheet {
    gfcss_engine *e;
    gfcss_sheet *sheet;
    int slot[X_COUNT];
    gfcss_computed *parent, *cs;    /* olui_style_compute's, kept between calls */
    /* 0.4: background-image's url()s met, each with the program's number for it (-1: none given) */
    char **names;
    int *images;
    int nnames, names_cap;
    int refs;                       /* the windows sharing it */
    unsigned gen;                   /* moves on with each change */
};

static int own_colour(void *userdata, int slot, const char *value, size_t length, uint32_t *out)
{
    (void)userdata;
    (void)slot;
    return gfcss_parse_colour(value, length, out);
}

/* same-size: none 0, width 1, height 2, both 3 */
static int own_same(void *userdata, int slot, const char *value, size_t length, uint32_t *out)
{
    static const char *const words[4] = { "none", "width", "height", "both" };
    uint32_t i;
    (void)userdata;
    (void)slot;
    for (i = 0; i < 4; ++i)
        if (strlen(words[i]) == length && !memcmp(words[i], value, length)) {
            *out = i;
            return 1;
        }
    return 0;
}

/* a count: digits, 1 to 64 */
static int own_count(void *userdata, int slot, const char *value, size_t length, uint32_t *out)
{
    uint32_t n = 0;
    size_t i;
    (void)userdata;
    (void)slot;
    if (!length || length > 3) return 0;
    for (i = 0; i < length; ++i) {
        if (value[i] < '0' || value[i] > '9') return 0;
        n = n * 10 + (uint32_t)(value[i] - '0');
    }
    if (n < 1 || n > 64) return 0;
    *out = n;
    return 1;
}

/* an engine with the UI's pseudo-classes and properties */
static gfcss_engine *new_engine(int slot[X_COUNT])
{
    gfcss_engine *e = gfcss_new();
    int i;
    if (!e) return NULL;
    if (!gfcss_add_state(e, "selected", ST_SELECTED) || !gfcss_add_state(e, "open", ST_OPEN) ||
        !gfcss_add_state(e, "dragging", ST_DRAGGING) || !gfcss_add_state(e, "drop-target", ST_DROP)) {
        gfcss_free(e);
        return NULL;
    }
    for (i = 0; i < X_COUNT; ++i)
        if ((slot[i] = gfcss_add_property(e, own[i].name, own[i].flags, own[i].initial, own[i].parse, NULL)) < 0) {
            gfcss_free(e);
            return NULL;
        }
    return e;
}

olui_sheet *olui_sheet_new(void)
{
    olui_sheet *s = (olui_sheet *)calloc(1, sizeof(olui_sheet));
    if (!s) return NULL;
    s->parent = (gfcss_computed *)malloc(2 * sizeof(gfcss_computed));
    if (!s->parent || !(s->e = new_engine(s->slot)) || !(s->sheet = gfcss_sheet_new(s->e, GFCSS_ORIGIN_AUTHOR))) {
        olui_sheet_free(s);
        return NULL;
    }
    s->cs = s->parent + 1;
    s->refs = 1;
    s->gen = 1;
    return s;
}

olui_sheet *olui_sheet_ref(olui_sheet *sheet)
{
    if (sheet) ++sheet->refs;
    return sheet;
}

unsigned olui_sheet_gen(const olui_sheet *sheet)
{
    return sheet ? sheet->gen : 0;
}

void olui_sheet_changed(olui_sheet *sheet)
{
    if (sheet) ++sheet->gen;
}

void olui_sheet_free(olui_sheet *sheet)
{
    int i;
    if (!sheet || --sheet->refs > 0) return;
    for (i = 0; i < sheet->nnames; ++i) free(sheet->names[i]);
    free(sheet->names);
    free(sheet->images);
    gfcss_free(sheet->e);
    free(sheet->parent);
    free(sheet);
}

int olui_sheet_add(olui_sheet *sheet, const char *css)
{
    if (!sheet) return 0;
    if (!css) return 1;
    return gfcss_sheet_parse(sheet->sheet, css, strlen(css));
}

unsigned olui_sheet_states(const olui_sheet *sheet)
{
    uint32_t g;
    unsigned s = 0;
    if (!sheet) return 0;
    g = gfcss_states(sheet->e);
    if (g & GFCSS_STATE_HOVER) s |= S_HOVER;
    if (g & GFCSS_STATE_ACTIVE) s |= S_ACTIVE;
    if (g & GFCSS_STATE_FOCUS) s |= S_FOCUS;
    if (g & GFCSS_STATE_CHECKED) s |= S_CHECKED;
    if (g & (GFCSS_STATE_DISABLED | GFCSS_STATE_ENABLED)) s |= S_DISABLED;
    if (g & ST_SELECTED) s |= S_SELECTED;
    if (g & ST_OPEN) s |= S_OPEN;
    if (g & ST_DRAGGING) s |= S_DRAGGING;
    if (g & ST_DROP) s |= S_DROP;
    return s;
}

/* A name's index, from 1 (made when it is new); 0 when memory ran out */
static int name_index(olui_sheet *sheet, const char *name, size_t n)
{
    int i;
    char *copy;
    for (i = 0; i < sheet->nnames; ++i)
        if (strlen(sheet->names[i]) == n && !memcmp(sheet->names[i], name, n)) return i + 1;
    if (sheet->nnames == sheet->names_cap) {
        int cap = sheet->names_cap ? sheet->names_cap * 2 : 8;
        char **nm = (char **)realloc(sheet->names, (size_t)cap * sizeof(*nm));
        int *im;
        if (!nm) return 0;
        sheet->names = nm;
        if (!(im = (int *)realloc(sheet->images, (size_t)cap * sizeof(*im)))) return 0;
        sheet->images = im;
        sheet->names_cap = cap;
    }
    if (!(copy = (char *)malloc(n + 1))) return 0;
    memcpy(copy, name, n);
    copy[n] = 0;
    sheet->names[sheet->nnames] = copy;
    sheet->images[sheet->nnames] = -1;
    return ++sheet->nnames;
}

int olui_sheet_name_image(olui_sheet *sheet, const char *name, int image)
{
    int i;
    if (!sheet || !name || !(i = name_index(sheet, name, strlen(name)))) return 0;
    sheet->images[i - 1] = image;
    return 1;
}

const char *olui_sheet_name(const olui_sheet *sheet, int index)
{
    return sheet && index > 0 && index <= sheet->nnames ? sheet->names[index - 1] : NULL;
}

int olui_parse_colour(const char *s, uint32_t *argb)
{
    return s && gfcss_parse_colour(s, strlen(s), argb);
}

/* ---- The elements, as GFCSS sees them ------------------------------------------- */

static void el_info(void *ud, const void *p, gfcss_info *in)
{
    const olui_el *el = (const olui_el *)p;
    uint32_t st = 0;
    (void)ud;
    in->tag = el->tag;
    in->id = el->id;
    in->classes = el->cls;
    in->style = el->style;
    if (el->state & S_HOVER) st |= GFCSS_STATE_HOVER;
    if (el->state & S_ACTIVE) st |= GFCSS_STATE_ACTIVE;
    if (el->state & S_FOCUS) st |= GFCSS_STATE_FOCUS;
    if (el->state & S_CHECKED) st |= GFCSS_STATE_CHECKED;
    st |= (el->state & S_DISABLED) ? GFCSS_STATE_DISABLED : GFCSS_STATE_ENABLED;
    if (el->state & S_SELECTED) st |= ST_SELECTED;
    if (el->state & S_OPEN) st |= ST_OPEN;
    if (el->state & S_DRAGGING) st |= ST_DRAGGING;
    if (el->state & S_DROP) st |= ST_DROP;
    in->state = st;
    in->index = 0;              /* el_index, only when a selector asks */
    in->count = 0;
    in->empty = !el->first && (!el->text || !el->text[0]);
}

static const void *el_parent(void *ud, const void *p)
{
    (void)ud;
    return ((const olui_el *)p)->parent;
}

static const void *el_prev(void *ud, const void *p)
{
    const olui_el *el = (const olui_el *)p, *c, *before = NULL;
    (void)ud;
    if (!el->parent) return NULL;
    for (c = el->parent->first; c && c != el; c = c->next) before = c;
    return before;
}

static const void *el_next(void *ud, const void *p)
{
    (void)ud;
    return ((const olui_el *)p)->next;
}

static const char *el_attr(void *ud, const void *p, const char *name)
{
    const olui_el *el = (const olui_el *)p;
    (void)ud;
    if (!strcmp(name, "id")) return el->id;
    if (!strcmp(name, "class")) return el->cls;
    if (!strcmp(name, "name")) return el->name;
    if (!strcmp(name, "value")) return el->value;
    if (!strcmp(name, "placeholder")) return el->placeholder;
    if (!strcmp(name, "title")) return el->title;
    if (!strcmp(name, "style")) return el->style;
    if (!strcmp(name, "checked")) return (el->state & S_CHECKED) ? "" : NULL;
    if (!strcmp(name, "disabled")) return (el->state & S_DISABLED) ? "" : NULL;
    if (!strcmp(name, "selected")) return (el->state & S_SELECTED) ? "" : NULL;
    if (!strcmp(name, "open")) return (el->state & S_OPEN) ? "" : NULL;
    if (!strcmp(name, "hidden")) return (el->state & S_HIDDEN) ? "" : NULL;
    if (!strcmp(name, "selectable")) return (el->flags & F_SELECTABLE) ? "" : NULL;
    if (!strcmp(name, "draggable")) return (el->flags & F_DRAGGABLE) ? "true" : NULL;
    if (!strcmp(name, "droppable")) return (el->flags & F_DROPPABLE) ? "" : NULL;
    if (!strcmp(name, "marquee")) return (el->flags & F_MARQUEE) ? "" : NULL;
    return NULL;
}

static int32_t el_index(void *ud, const void *p)
{
    const olui_el *el = (const olui_el *)p, *c;
    int32_t n = 1;
    (void)ud;
    if (!el->parent) return 1;
    for (c = el->parent->first; c && c != el; c = c->next) ++n;
    return n;
}

static const gfcss_element_ops el_ops = { el_info, el_parent, el_prev, el_next, el_attr, el_index };

/* ---- From the UI's style to GFCSS's, and back ------------------------------------- */

/* What an element inherits from its parent's olui_cs (NULL: the window's defaults) */
static void inherited_of(const olui_sheet *s, const olui_cs *p, gfcss_computed *pc)
{
    uint32_t flags = p ? p->ol.text_flags : 0;
    int i;
    gfcss_computed_init(pc);
    pc->font_size = p ? p->ol.font_size : OL_CSSPX(13);
    pc->line_height_kind = GFCSS_LINE_LENGTH;
    pc->line_height = p ? p->ol.line_height : OL_CSSPX(16);
    pc->color = p ? p->ol.foreground : 0xff000000u;
    pc->font_weight = (flags & OL_TEXT_BOLD) ? 700 : 400;
    pc->font_style = (flags & OL_TEXT_ITALIC) ? GFCSS_FONT_ITALIC : GFCSS_FONT_NORMAL;
    pc->font_generic = (flags & OL_TEXT_MONO) ? GFCSS_FONT_MONOSPACE : GFCSS_FONT_DEFAULT;
    /* a row's text-align is where its children go: they don't take it */
    pc->text_align = GFCSS_ALIGN_START;
    if (p && !(p->box.flags & OL_BOX_ROW)) {
        if (p->ol.text_align == OL_ALIGN_CENTER) pc->text_align = GFCSS_ALIGN_CENTER;
        else if (p->ol.text_align == OL_ALIGN_END) pc->text_align = GFCSS_ALIGN_END;
        else if (p->ol.text_align == OL_ALIGN_JUSTIFY) pc->text_align = GFCSS_ALIGN_JUSTIFY;
    }
    for (i = 0; i < X_COUNT; ++i) pc->ext[s->slot[i]] = own[i].initial;
    if (p) {
        /* 0.4: text-shadow is inherited */
        pc->text_shadows = p->shadows;
        for (i = 0; i < p->shadows; ++i) {
            pc->text_shadow[i].x = p->shadow_x[i];
            pc->text_shadow[i].y = p->shadow_y[i];
            pc->text_shadow[i].blur = 0;
            pc->text_shadow[i].color = p->shadow_argb[i];
        }
        pc->ext[s->slot[X_ACCENT]] = p->accent;
        pc->ext[s->slot[X_MARK_BG]] = p->mark_bg;
        pc->ext[s->slot[X_MARK_BORDER]] = p->mark_border;
        pc->ext[s->slot[X_MUTED]] = p->muted;
    }
}

/* the end of a colour in a gradient's text: #hex, a name, or a function */
static const char *colour_end(const char *p)
{
    if (*p == '#' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
        const char *q = p + 1;
        while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '-') ++q;
        if (*q == '(') {
            while (*q && *q != ')') ++q;
            if (*q) ++q;
        }
        return q;
    }
    return p;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return p;
}

/* linear-gradient(to bottom?, c1 [pos%], c2 [pos%], ...): down the box only */
static int gradient(const gfcss_text *t, olui_cs *cs)
{
    char text[512];
    const char *q;
    int n = 0, k;
    if (t->length >= sizeof(text)) return 0;
    memcpy(text, t->text, t->length);
    text[t->length] = 0;
    if (strncmp(text, "linear-gradient(", 16)) return 0;
    q = skip_ws(text + 16);
    if (!strncmp(q, "to ", 3) || !strncmp(q, "180deg", 6)) {
        q = strchr(q, ',');
        if (!q) return 0;
        ++q;
    }
    while (n < OLUI_MAX_STOPS) {
        const char *e;
        q = skip_ws(q);
        e = colour_end(q);
        if (e == q || !gfcss_parse_colour(q, (size_t)(e - q), &cs->stop_argb[n])) break;
        q = skip_ws(e);
        cs->stop_pos[n] = -1;
        if (*q >= '0' && *q <= '9') {
            long pc = 0;
            while (*q >= '0' && *q <= '9') pc = pc * 10 + (*q++ - '0');
            while ((*q >= '0' && *q <= '9') || *q == '.') ++q;
            cs->stop_pos[n] = (int)(pc * 10);
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

/* Named font families (font-family's first name, when it isn't a generic):
 * a number for each, 1 up, carried in text_flags' top 16 bits
 * (OL_TEXT_FONT), so the measure callback and the renderer open that face.
 * The numbers are for the program's life; 0 is the default face. */
#define OLUI_FAMILIES 64
static char olui_family_names[OLUI_FAMILIES][48];
static unsigned olui_nfamilies;

static int olui_name_ieq(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return *a == *b;
}

static unsigned olui_family_id(const gfcss_text *t)
{
    static const char *const generic[] = { "monospace", "sans-serif", "serif", "cursive", "fantasy",
                                           "system-ui", "ui-monospace", "ui-sans-serif", "ui-serif", "math" };
    char name[48];
    size_t n, k;
    if (!t->text || !t->length) return 0;
    n = gfcss_text_copy(t, name, sizeof(name));
    if (n == 0 || n >= sizeof(name)) return 0;
    for (k = 0; k < sizeof(generic) / sizeof(generic[0]); ++k)
        if (olui_name_ieq(name, generic[k])) return 0;
    for (k = 0; k < olui_nfamilies; ++k)
        if (olui_name_ieq(name, olui_family_names[k])) return (unsigned)k + 1;
    if (olui_nfamilies == OLUI_FAMILIES) return 0;
    memcpy(olui_family_names[olui_nfamilies], name, n + 1);
    return ++olui_nfamilies;
}

const char *olui_family_name(unsigned id)
{
    return id >= 1 && id <= olui_nfamilies ? olui_family_names[id - 1] : NULL;
}

static ol_unit units_of(const gfcss_length *l)
{
    return l->kind == GFCSS_LEN ? l->units : 0;     /* a percentage of what? the UI leaves it out */
}

/* c into cs. base: what a new font size's line height is in proportion to,
 * and the text flags CSS doesn't say (the parent's; NULL: the window's) */
static void from_computed(olui_sheet *s, const gfcss_computed *c, const olui_cs *base, olui_cs *cs)
{
    uint32_t flags = base ? base->ol.text_flags : 0;
    ol_unit base_fs = base ? base->ol.font_size : OL_CSSPX(13), base_lh = base ? base->ol.line_height : OL_CSSPX(16);
    int flex;
    memset(cs, 0, sizeof(*cs));
    ol_style_init(&cs->ol);
    ol_box_style_init(&cs->box);
    cs->box.border_sides = 0;

    /* a box is a block until its CSS says otherwise */
    flex = c->display == GFCSS_DISPLAY_FLEX || c->display == GFCSS_DISPLAY_INLINE_FLEX;
    cs->column = c->flex_direction == GFCSS_COLUMN || c->flex_direction == GFCSS_COLUMN_REVERSE;
    if (gfcss_is_set(c, GFCSS_P_DISPLAY)) {
        if (c->display == GFCSS_DISPLAY_NONE) cs->ol.display = OL_DISPLAY_NONE;
        else if (c->display == GFCSS_DISPLAY_INLINE || c->display == GFCSS_DISPLAY_INLINE_BLOCK)
            cs->ol.display = OL_DISPLAY_INLINE;
        else cs->ol.display = OL_DISPLAY_BLOCK;
        if (flex && !cs->column) cs->box.flags |= OL_BOX_ROW;
    }
    cs->box.grow = c->flex_grow / 1000;
    cs->box.gap = units_of((cs->box.flags & OL_BOX_ROW) ? &c->column_gap : &c->row_gap);
    if (c->ext[s->slot[X_GRID]] > 0 && cs->ol.display != OL_DISPLAY_NONE) {
        /* 0.5: a grid, gap apart both ways (row-gap's; gap sets both) */
        cs->box.flags = (cs->box.flags & ~(uint32_t)OL_BOX_ROW) | OL_BOX_GRID;
        cs->box.grid_columns = (int32_t)c->ext[s->slot[X_GRID]];
        cs->box.gap = units_of(&c->row_gap);
        cs->column = 0;
    }
    cs->box.flags &= ~(uint32_t)(OL_BOX_SAME_WIDTH | OL_BOX_SAME_HEIGHT);
    if (c->ext[s->slot[X_SAME]] & 1) cs->box.flags |= OL_BOX_SAME_WIDTH;
    if (c->ext[s->slot[X_SAME]] & 2) cs->box.flags |= OL_BOX_SAME_HEIGHT;
    switch (c->align_items) {
    case GFCSS_FLEX_CENTER: cs->box.align = OL_CROSS_CENTRE; break;
    case GFCSS_FLEX_START: cs->box.align = OL_CROSS_START; break;
    case GFCSS_FLEX_END: cs->box.align = OL_CROSS_END; break;
    default: cs->box.align = OL_CROSS_STRETCH; break;
    }
    /* a row's children with room to spare: GFOpenLayout puts them at the row's text_align */
    if (gfcss_is_set(c, GFCSS_P_JUSTIFY_CONTENT)) {
        if (c->justify_content == GFCSS_FLEX_CENTER) cs->ol.text_align = OL_ALIGN_CENTER;
        else if (c->justify_content == GFCSS_FLEX_END || c->justify_content == GFCSS_FLEX_RIGHT)
            cs->ol.text_align = OL_ALIGN_END;
        else cs->ol.text_align = OL_ALIGN_START;
    } else {
        switch (c->text_align) {
        case GFCSS_ALIGN_CENTER: cs->ol.text_align = OL_ALIGN_CENTER; break;
        case GFCSS_ALIGN_RIGHT: case GFCSS_ALIGN_END: cs->ol.text_align = OL_ALIGN_END; break;
        case GFCSS_ALIGN_JUSTIFY: cs->ol.text_align = OL_ALIGN_JUSTIFY; break;
        default: cs->ol.text_align = OL_ALIGN_START; break;
        }
    }
    cs->box.width = units_of(&c->width);
    cs->box.height = units_of(&c->height);
    cs->ol.margin_top = units_of(&c->margin[GFCSS_TOP]);
    cs->ol.margin_right = units_of(&c->margin[GFCSS_RIGHT]);
    cs->ol.margin_bottom = units_of(&c->margin[GFCSS_BOTTOM]);
    cs->ol.margin_left = units_of(&c->margin[GFCSS_LEFT]);
    cs->ol.padding_top = units_of(&c->padding[GFCSS_TOP]);
    cs->ol.padding_right = units_of(&c->padding[GFCSS_RIGHT]);
    cs->ol.padding_bottom = units_of(&c->padding[GFCSS_BOTTOM]);
    cs->ol.padding_left = units_of(&c->padding[GFCSS_LEFT]);
    cs->ol.foreground = c->color;

    /* the box's fill: a gradient down it, or a colour */
    if (c->background_image.kind == GFCSS_TEXT_FUNCTION) {
        if (!gradient(&c->background_image, cs)) cs->stops = 0;
    } else {
        cs->stops = (c->background_color >> 24) ? 1 : 0;
        cs->stop_argb[0] = c->background_color;
        cs->stop_pos[0] = 0;
    }
    /* one border, as wide as its top; black unless a colour was given */
    cs->border = c->border_width[GFCSS_TOP];
    cs->border_argb = 0xff000000u;
    if (gfcss_is_set(c, GFCSS_P_BORDER_TOP_COLOR)) cs->border_argb = c->border_color[GFCSS_TOP];
    cs->radius = units_of(&c->border_radius[0]);

    cs->ol.font_size = c->font_size;
    if (gfcss_is_set(c, GFCSS_P_LINE_HEIGHT) && c->line_height_kind != GFCSS_LINE_NORMAL) {
        if (c->line_height_kind == GFCSS_LINE_NUMBER)       /* times the font size (in CSS pixels' sixtieths, as ever) */
            cs->ol.line_height = (ol_unit)((int64_t)(c->line_height * OL_CSSPX(1) / 1000) * c->font_size / OL_CSSPX(1));
        else
            cs->ol.line_height = c->line_height;
    } else if (gfcss_is_set(c, GFCSS_P_FONT_SIZE)) {
        /* the line height keeps its proportion to the font size */
        int ratio100 = base_fs > 0 ? (int)((int64_t)base_lh * 100 / base_fs) : 125;
        cs->ol.line_height = (ol_unit)((int64_t)c->font_size * ratio100 / 100);
    } else {
        cs->ol.line_height = base_lh;
    }
    if (c->font_weight >= 600) flags |= OL_TEXT_BOLD;
    else flags &= ~(uint32_t)OL_TEXT_BOLD;
    if (c->font_style != GFCSS_FONT_NORMAL) flags |= OL_TEXT_ITALIC;
    else flags &= ~(uint32_t)OL_TEXT_ITALIC;
    if (c->font_generic == GFCSS_FONT_MONOSPACE) flags |= OL_TEXT_MONO;
    else flags &= ~(uint32_t)OL_TEXT_MONO;
    if (gfcss_is_set(c, GFCSS_P_FONT_FAMILY))
        flags = (flags & 0xffffu) | OL_TEXT_FONT(olui_family_id(&c->font_family));
    if (gfcss_is_set(c, GFCSS_P_TEXT_DECORATION_LINE)) {
        flags &= ~(uint32_t)(OL_TEXT_UNDERLINE | OL_TEXT_STRIKE);
        if (c->text_decoration & GFCSS_UNDERLINE) flags |= OL_TEXT_UNDERLINE;
        if (c->text_decoration & GFCSS_LINE_THROUGH) flags |= OL_TEXT_STRIKE;
    }
    cs->ol.text_flags = flags;
    switch (c->overflow_y) {
    case GFCSS_OVERFLOW_AUTO: cs->overflow = O_AUTO; break;
    case GFCSS_OVERFLOW_SCROLL: cs->overflow = O_SCROLL; break;
    case GFCSS_OVERFLOW_HIDDEN: case GFCSS_OVERFLOW_CLIP: cs->overflow = O_HIDDEN; break;
    default: cs->overflow = O_VISIBLE; break;
    }
    /* 0.4: out of the flow at left and top (right and bottom: of what, the styling doesn't know); a layer */
    if (c->position == GFCSS_POSITION_ABSOLUTE || c->position == GFCSS_POSITION_FIXED) {
        cs->positioned = P_OUT;
        cs->box.flags |= OL_BOX_ABSOLUTE;
        cs->box.frame.x = units_of(&c->inset[GFCSS_LEFT]);
        cs->box.frame.y = units_of(&c->inset[GFCSS_TOP]);
    } else if (c->position == GFCSS_POSITION_RELATIVE) {
        cs->positioned = P_RELATIVE;
    }
    cs->z = cs->positioned && !c->z_auto ? c->z_index : 0;
    /* 0.4: how much shows, the text's shadows, an outline, a picture */
    cs->opacity = c->opacity >= 1000 ? 255 : c->opacity <= 0 ? 0 : (int)((c->opacity * 255 + 500) / 1000);
    cs->shadows = c->text_shadows < OLUI_MAX_SHADOWS ? c->text_shadows : OLUI_MAX_SHADOWS;
    {
        int k;
        for (k = 0; k < cs->shadows; ++k) {
            cs->shadow_x[k] = c->text_shadow[k].x;
            cs->shadow_y[k] = c->text_shadow[k].y;
            cs->shadow_argb[k] = c->text_shadow[k].color;
        }
    }
    cs->outline = c->outline_style != GFCSS_BORDER_NONE && c->outline_style != GFCSS_BORDER_HIDDEN
                  ? c->outline_width : 0;
    cs->outline_argb = c->outline_color;
    cs->bg_image = -1;
    if (c->background_image.kind == GFCSS_TEXT_URL && s->e) {
        char name[128];
        size_t n = gfcss_text_copy(&c->background_image, name, sizeof(name));
        const char *d = name;
        if (n < sizeof(name) && n) {
            int number = 0;
            while (*d >= '0' && *d <= '9' && number < 100000000) number = number * 10 + (*d++ - '0');
            cs->bg_name = name_index(s, name, n);
            cs->bg_image = !*d ? number : cs->bg_name ? s->images[cs->bg_name - 1] : -1;
        }
    }
    switch (c->background_repeat) {
    case GFCSS_REPEAT_X: cs->bg_repeat = OLUI_REPEAT_X; break;
    case GFCSS_REPEAT_Y: cs->bg_repeat = OLUI_REPEAT_Y; break;
    case GFCSS_NO_REPEAT: cs->bg_repeat = OLUI_NO_REPEAT; break;
    default: cs->bg_repeat = OLUI_REPEAT; break;
    }
    cs->bg_align_x = c->background_position[0].kind == GFCSS_LEN ? c->background_position[0].pct / 100 : 0;
    cs->bg_align_y = c->background_position[1].kind == GFCSS_LEN ? c->background_position[1].pct / 100 : 0;
    cs->accent = c->ext[s->slot[X_ACCENT]];
    cs->mark_bg = c->ext[s->slot[X_MARK_BG]];
    cs->mark_border = c->ext[s->slot[X_MARK_BORDER]];
    cs->muted = c->ext[s->slot[X_MUTED]];
}

void olui_style_compute(olui_sheet *sheet, olui_el *el, const olui_cs *parent)
{
    olui_cs *cs = &el->cs;
    if (!sheet) {
        /* no sheet: the defaults, and what is inherited */
        gfcss_computed c;
        olui_sheet tmp;
        int i;
        memset(&tmp, 0, sizeof(tmp));
        for (i = 0; i < X_COUNT; ++i) tmp.slot[i] = i;
        inherited_of(&tmp, parent, &c);
        from_computed(&tmp, &c, parent, cs);
    } else {
        inherited_of(sheet, parent, sheet->parent);
        gfcss_computed_init(sheet->cs);
        if (gfcss_compute(sheet->e, &el_ops, NULL, el, GFCSS_PSEUDO_NONE, sheet->parent, sheet->cs))
            from_computed(sheet, sheet->cs, parent, cs);
        else
            from_computed(sheet, sheet->parent, parent, cs);
    }
    if (el->state & S_HIDDEN) cs->ol.display = OL_DISPLAY_NONE;
}

/* cs as GFCSS's computed style, everything it holds as though declared */
static void to_computed(const olui_sheet *s, const olui_cs *cs, gfcss_computed *c)
{
    int k;
    gfcss_computed_init(c);
    c->display = cs->ol.display == OL_DISPLAY_NONE ? GFCSS_DISPLAY_NONE
               : cs->ol.display == OL_DISPLAY_INLINE ? GFCSS_DISPLAY_INLINE
               : (cs->box.flags & OL_BOX_ROW) || cs->column ? GFCSS_DISPLAY_FLEX : GFCSS_DISPLAY_BLOCK;
    c->flex_direction = cs->column ? GFCSS_COLUMN : GFCSS_ROW;
    c->flex_grow = cs->box.grow * 1000;
    c->row_gap.units = c->column_gap.units = cs->box.gap;
    c->row_gap.kind = c->column_gap.kind = GFCSS_LEN;
    c->align_items = cs->box.align == OL_CROSS_CENTRE ? GFCSS_FLEX_CENTER : cs->box.align == OL_CROSS_START
                   ? GFCSS_FLEX_START : cs->box.align == OL_CROSS_END ? GFCSS_FLEX_END : GFCSS_FLEX_STRETCH;
    c->text_align = cs->ol.text_align == OL_ALIGN_CENTER ? GFCSS_ALIGN_CENTER : cs->ol.text_align == OL_ALIGN_END
                  ? GFCSS_ALIGN_END : cs->ol.text_align == OL_ALIGN_JUSTIFY ? GFCSS_ALIGN_JUSTIFY : GFCSS_ALIGN_START;
    if (cs->box.width > 0) { c->width.kind = GFCSS_LEN; c->width.units = cs->box.width; }
    if (cs->box.height > 0) { c->height.kind = GFCSS_LEN; c->height.units = cs->box.height; }
    c->margin[0].units = cs->ol.margin_top;
    c->margin[1].units = cs->ol.margin_right;
    c->margin[2].units = cs->ol.margin_bottom;
    c->margin[3].units = cs->ol.margin_left;
    c->padding[0].units = cs->ol.padding_top;
    c->padding[1].units = cs->ol.padding_right;
    c->padding[2].units = cs->ol.padding_bottom;
    c->padding[3].units = cs->ol.padding_left;
    c->color = cs->ol.foreground;
    for (k = 0; k < 4; ++k) {
        c->border_width[k] = cs->border;
        c->border_style[k] = cs->border > 0 ? GFCSS_BORDER_SOLID : GFCSS_BORDER_NONE;
        c->border_color[k] = cs->border_argb;
        c->border_radius[k].units = cs->radius;
    }
    c->font_size = cs->ol.font_size;
    c->line_height_kind = GFCSS_LINE_LENGTH;
    c->line_height = cs->ol.line_height;
    c->font_weight = (cs->ol.text_flags & OL_TEXT_BOLD) ? 700 : 400;
    c->font_style = (cs->ol.text_flags & OL_TEXT_ITALIC) ? GFCSS_FONT_ITALIC : GFCSS_FONT_NORMAL;
    c->font_generic = (cs->ol.text_flags & OL_TEXT_MONO) ? GFCSS_FONT_MONOSPACE : GFCSS_FONT_DEFAULT;
    c->text_decoration = ((cs->ol.text_flags & OL_TEXT_UNDERLINE) ? GFCSS_UNDERLINE : 0) |
                         ((cs->ol.text_flags & OL_TEXT_STRIKE) ? GFCSS_LINE_THROUGH : 0);
    c->overflow_x = c->overflow_y = cs->overflow == O_AUTO ? GFCSS_OVERFLOW_AUTO
                  : cs->overflow == O_SCROLL ? GFCSS_OVERFLOW_SCROLL
                  : cs->overflow == O_HIDDEN ? GFCSS_OVERFLOW_HIDDEN : GFCSS_OVERFLOW_VISIBLE;
    if (cs->positioned == P_OUT) {
        c->position = GFCSS_POSITION_ABSOLUTE;
        c->inset[GFCSS_LEFT].kind = c->inset[GFCSS_TOP].kind = GFCSS_LEN;
        c->inset[GFCSS_LEFT].units = cs->box.frame.x;
        c->inset[GFCSS_TOP].units = cs->box.frame.y;
    } else if (cs->positioned == P_RELATIVE) {
        c->position = GFCSS_POSITION_RELATIVE;
    }
    if (cs->z) {
        c->z_auto = 0;
        c->z_index = cs->z;
    }
    c->ext[s->slot[X_ACCENT]] = cs->accent;
    c->ext[s->slot[X_GRID]] = (cs->box.flags & OL_BOX_GRID) ? (uint32_t)cs->box.grid_columns : 0;
    c->ext[s->slot[X_SAME]] = ((cs->box.flags & OL_BOX_SAME_WIDTH) ? 1u : 0u) |
                              ((cs->box.flags & OL_BOX_SAME_HEIGHT) ? 2u : 0u);
    c->ext[s->slot[X_MARK_BG]] = cs->mark_bg;
    c->ext[s->slot[X_MARK_BORDER]] = cs->mark_border;
    c->ext[s->slot[X_MUTED]] = cs->muted;
}

void olui_style_apply_decls(olui_cs *cs, const char *decls, const olui_cs *parent)
{
    /* one declaration list on cs: GFCSS applies it to cs as GFCSS sees it, and what
       it says comes back (an engine of its own: this has no sheet) */
    olui_sheet *s;
    gfcss_computed *c, *pc;
    int p, bg;
    if (!cs || !decls || !decls[0] || !(s = olui_sheet_new())) return;
    c = s->cs;
    pc = s->parent;
    to_computed(s, cs, c);
    inherited_of(s, parent, pc);
    if (gfcss_apply_declarations(s->e, decls, strlen(decls), pc, c)) {
        olui_cs was = *cs;
        bg = gfcss_is_set(c, GFCSS_P_BACKGROUND_COLOR) || gfcss_is_set(c, GFCSS_P_BACKGROUND_IMAGE);
        /* all but the font size and line height as though declared: they come back as they are */
        for (p = 0; p < GFCSS_P_COUNT + GFCSS_MAX_EXT; ++p)
            if (p != GFCSS_P_FONT_SIZE && p != GFCSS_P_LINE_HEIGHT && p != GFCSS_P_JUSTIFY_CONTENT)
                c->set[p >> 5] |= 1u << (p & 31);
        from_computed(s, c, &was, cs);
        if (!bg) {
            cs->stops = was.stops;
            memcpy(cs->stop_argb, was.stop_argb, sizeof(cs->stop_argb));
            memcpy(cs->stop_pos, was.stop_pos, sizeof(cs->stop_pos));
        }
    }
    olui_sheet_free(s);
}
