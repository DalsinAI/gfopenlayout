/* GFCSS: property values. Each declaration is read into longhands
 * (margin into its four sides, font into seven) as small integer records:
 * a keyword, a length in units or thousandths of an em, a colour, a
 * stretch of the sheet's text, a calc() sum. No floating point: decimals
 * are thousandths from the tokenizer on.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfcss_internal.h"

/* ---- Value tokens ------------------------------------------------------------- */

#define VT_RAWFUNC 100          /* a function we don't read inside: one token, all of it */
#define MAX_VT 48

typedef struct {
    uint8_t type;
    uint8_t ws;                 /* white space before it */
    uint8_t is_int;
    uint8_t esc;
    uint16_t close;             /* a function or '(': its ')' (MAX_VT: the end) */
    int32_t num;
    const char *s;
    uint32_t n;
    const char *unit;
    uint32_t unit_n;
    unsigned delim;
    const char *start, *stop;
} vtok;

typedef struct {
    gfcss_engine *e;
    gsink *sink;
    int important;
    vtok t[MAX_VT];
    int n;
    char scratch[192];
    size_t used;
} vctx;

static const char *keep(vctx *c, const char *s, size_t n)
{
    char *d;
    if (c->used + n + 1 > sizeof(c->scratch)) return NULL;
    d = c->scratch + c->used;
    memcpy(d, s, n);
    d[n] = 0;
    c->used += n + 1;
    return d;
}

static int is_inner_func(const char *s, size_t n)
{
    return g_ieq(s, n, "rgb") || g_ieq(s, n, "rgba") || g_ieq(s, n, "hsl") || g_ieq(s, n, "hsla") ||
           g_ieq(s, n, "calc") || g_ieq(s, n, "-webkit-calc") || g_ieq(s, n, "url");
}

static int tokenize(vctx *c, const char *p, const char *end)
{
    glex lx;
    gtok t;
    int open[MAX_VT], depth = 0, ws = 0, i;
    g_lex_init(&lx, p, end);
    c->n = 0;
    c->used = 0;
    for (;;) {
        vtok *v;
        g_next(&lx, &t);
        if (t.type == T_EOF) break;
        if (t.type == T_WS) { ws = 1; continue; }
        if (t.type == T_BADSTR || t.type == T_BADURL || t.type == T_LBRACE || t.type == T_RBRACE ||
            t.type == T_LBRACK || t.type == T_RBRACK || t.type == T_SEMI || t.type == T_CDO ||
            t.type == T_CDC || t.type == T_AT)
            return 0;
        if (c->n == MAX_VT) return 0;
        v = &c->t[c->n];
        memset(v, 0, sizeof(*v));
        v->type = (uint8_t)t.type;
        v->ws = (uint8_t)ws;
        ws = 0;
        v->num = t.num;
        v->is_int = (uint8_t)t.is_int;
        v->esc = (uint8_t)t.esc;
        v->s = t.s;
        v->n = (uint32_t)t.n;
        v->unit = t.unit;
        v->unit_n = (uint32_t)t.unit_n;
        v->delim = t.delim;
        v->start = t.start;
        v->stop = t.stop;
        v->close = MAX_VT;
        if (t.s == t.buf && t.n) {
            if (!(v->s = keep(c, t.s, t.n))) return 0;
        }
        if (t.unit == t.ubuf && t.unit_n) {
            if (!(v->unit = keep(c, t.unit, t.unit_n))) return 0;
        }
        if (t.type == T_FUNC && !is_inner_func(t.s, t.n)) {
            v->type = VT_RAWFUNC;
            lx.p = g_skip_block(lx.p, lx.end, T_RPAREN);
            v->stop = lx.p;
        } else if (t.type == T_FUNC || t.type == T_LPAREN) {
            if (depth == MAX_VT) return 0;
            open[depth++] = c->n;
        } else if (t.type == T_RPAREN) {
            if (!depth) return 0;
            c->t[open[--depth]].close = (uint16_t)c->n;
        }
        ++c->n;
    }
    /* functions left open close at the end, as CSS says */
    for (i = 0; i < depth; ++i) c->t[open[i]].close = (uint16_t)c->n;
    return 1;
}

static int is_ident(const vctx *c, int i, const char *kw)
{
    return i < c->n && c->t[i].type == T_IDENT && g_ieq(c->t[i].s, c->t[i].n, kw);
}

static int is_delim(const vctx *c, int i, unsigned d)
{
    return i < c->n && c->t[i].type == T_DELIM && c->t[i].delim == d;
}

static int after(const vctx *c, int i)
{
    return c->t[i].close >= c->n ? c->n : c->t[i].close + 1;
}

/* ---- Output --------------------------------------------------------------------- */

static void emit(vctx *c, int prop, int unit, int32_t v, int32_t w)
{
    gsink *s = c->sink;
    gdecl *d;
    if (s->n == s->cap) {
        if (s->fixed || !g_grow((void **)&s->d, &s->cap, s->n + 1, sizeof(gdecl))) {
            s->oom = 1;
            return;
        }
    }
    d = &s->d[s->n++];
    d->prop = (uint16_t)prop;
    d->flags = (uint8_t)(c->important ? D_IMPORTANT : 0);
    d->unit = (uint8_t)unit;
    d->v = v;
    d->w = w;
}

typedef struct {
    int unit;
    int32_t v, w;
} gval;

static void emit_val(vctx *c, int prop, const gval *g)
{
    emit(c, prop, g->unit, g->v, g->w);
}

static int32_t text_offset(vctx *c, const char *s)
{
    return (int32_t)(s - c->sink->base);
}

/* ---- Keywords ----------------------------------------------------------------- */

typedef struct {
    const char *name;
    int v;
} kwent;

#define KW(t) t, (int)(sizeof(t) / sizeof(t[0]))

static int kw_at(const vctx *c, int i, const kwent *tab, int n, int *out)
{
    int k;
    if (i >= c->n || c->t[i].type != T_IDENT) return 0;
    for (k = 0; k < n; ++k)
        if (g_ieq(c->t[i].s, c->t[i].n, tab[k].name)) {
            *out = tab[k].v;
            return 1;
        }
    return 0;
}

static const kwent kw_display[] = {
    { "inline", GFCSS_DISPLAY_INLINE }, { "block", GFCSS_DISPLAY_BLOCK }, { "flow-root", GFCSS_DISPLAY_BLOCK },
    { "inline-block", GFCSS_DISPLAY_INLINE_BLOCK }, { "none", GFCSS_DISPLAY_NONE },
    { "list-item", GFCSS_DISPLAY_LIST_ITEM }, { "flex", GFCSS_DISPLAY_FLEX },
    { "inline-flex", GFCSS_DISPLAY_INLINE_FLEX }, { "table", GFCSS_DISPLAY_TABLE },
    { "inline-table", GFCSS_DISPLAY_INLINE_TABLE }, { "table-row", GFCSS_DISPLAY_TABLE_ROW },
    { "table-cell", GFCSS_DISPLAY_TABLE_CELL }, { "table-header-group", GFCSS_DISPLAY_TABLE_HEADER_GROUP },
    { "table-row-group", GFCSS_DISPLAY_TABLE_ROW_GROUP },
    { "table-footer-group", GFCSS_DISPLAY_TABLE_FOOTER_GROUP },
    { "table-column", GFCSS_DISPLAY_TABLE_COLUMN }, { "table-column-group", GFCSS_DISPLAY_TABLE_COLUMN_GROUP },
    { "table-caption", GFCSS_DISPLAY_TABLE_CAPTION }, { "contents", GFCSS_DISPLAY_CONTENTS },
    { "grid", GFCSS_DISPLAY_BLOCK }, { "inline-grid", GFCSS_DISPLAY_INLINE_BLOCK }
};
static const kwent kw_position[] = {
    { "static", GFCSS_POSITION_STATIC }, { "relative", GFCSS_POSITION_RELATIVE },
    { "absolute", GFCSS_POSITION_ABSOLUTE }, { "fixed", GFCSS_POSITION_FIXED }, { "sticky", GFCSS_POSITION_STICKY },
    { "-webkit-sticky", GFCSS_POSITION_STICKY }
};
static const kwent kw_float[] = {
    { "none", GFCSS_FLOAT_NONE }, { "left", GFCSS_FLOAT_LEFT }, { "right", GFCSS_FLOAT_RIGHT },
    { "inline-start", GFCSS_FLOAT_LEFT }, { "inline-end", GFCSS_FLOAT_RIGHT }
};
static const kwent kw_clear[] = {
    { "none", GFCSS_CLEAR_NONE }, { "left", GFCSS_CLEAR_LEFT }, { "right", GFCSS_CLEAR_RIGHT },
    { "both", GFCSS_CLEAR_BOTH }
};
static const kwent kw_visibility[] = {
    { "visible", GFCSS_VISIBLE }, { "hidden", GFCSS_HIDDEN }, { "collapse", GFCSS_COLLAPSE }
};
static const kwent kw_overflow[] = {
    { "visible", GFCSS_OVERFLOW_VISIBLE }, { "hidden", GFCSS_OVERFLOW_HIDDEN },
    { "scroll", GFCSS_OVERFLOW_SCROLL }, { "auto", GFCSS_OVERFLOW_AUTO }, { "clip", GFCSS_OVERFLOW_CLIP },
    { "overlay", GFCSS_OVERFLOW_AUTO }
};
static const kwent kw_box_sizing[] = { { "content-box", GFCSS_CONTENT_BOX }, { "border-box", GFCSS_BORDER_BOX } };
static const kwent kw_border_style[] = {
    { "none", GFCSS_BORDER_NONE }, { "hidden", GFCSS_BORDER_HIDDEN }, { "solid", GFCSS_BORDER_SOLID },
    { "dotted", GFCSS_BORDER_DOTTED }, { "dashed", GFCSS_BORDER_DASHED }, { "double", GFCSS_BORDER_DOUBLE },
    { "groove", GFCSS_BORDER_GROOVE }, { "ridge", GFCSS_BORDER_RIDGE }, { "inset", GFCSS_BORDER_INSET },
    { "outset", GFCSS_BORDER_OUTSET }
};
static const kwent kw_outline_style[] = {
    { "auto", GFCSS_BORDER_SOLID }, { "none", GFCSS_BORDER_NONE }, { "solid", GFCSS_BORDER_SOLID },
    { "dotted", GFCSS_BORDER_DOTTED }, { "dashed", GFCSS_BORDER_DASHED }, { "double", GFCSS_BORDER_DOUBLE },
    { "groove", GFCSS_BORDER_GROOVE }, { "ridge", GFCSS_BORDER_RIDGE }, { "inset", GFCSS_BORDER_INSET },
    { "outset", GFCSS_BORDER_OUTSET }
};
static const kwent kw_border_width[] = {
    { "thin", OL_CSSPX(1) }, { "medium", OL_CSSPX(3) }, { "thick", OL_CSSPX(5) }
};
static const kwent kw_repeat[] = {
    { "repeat", GFCSS_REPEAT }, { "repeat-x", GFCSS_REPEAT_X }, { "repeat-y", GFCSS_REPEAT_Y },
    { "no-repeat", GFCSS_NO_REPEAT }, { "space", GFCSS_REPEAT }, { "round", GFCSS_REPEAT }
};
static const kwent kw_generic[] = {
    { "serif", GFCSS_FONT_SERIF }, { "sans-serif", GFCSS_FONT_SANS_SERIF }, { "monospace", GFCSS_FONT_MONOSPACE },
    { "cursive", GFCSS_FONT_CURSIVE }, { "fantasy", GFCSS_FONT_FANTASY }, { "system-ui", GFCSS_FONT_SANS_SERIF },
    { "ui-sans-serif", GFCSS_FONT_SANS_SERIF }, { "ui-serif", GFCSS_FONT_SERIF },
    { "ui-monospace", GFCSS_FONT_MONOSPACE }
};
/* well-known families, so that "Courier New" is monospace with no generic after it */
static const kwent kw_families[] = {
    { "courier", GFCSS_FONT_MONOSPACE }, { "courier new", GFCSS_FONT_MONOSPACE },
    { "consolas", GFCSS_FONT_MONOSPACE }, { "menlo", GFCSS_FONT_MONOSPACE }, { "monaco", GFCSS_FONT_MONOSPACE },
    { "lucida console", GFCSS_FONT_MONOSPACE }, { "dejavu sans mono", GFCSS_FONT_MONOSPACE },
    { "topaz", GFCSS_FONT_MONOSPACE }, { "times", GFCSS_FONT_SERIF }, { "times new roman", GFCSS_FONT_SERIF },
    { "georgia", GFCSS_FONT_SERIF }, { "garamond", GFCSS_FONT_SERIF }, { "arial", GFCSS_FONT_SANS_SERIF },
    { "helvetica", GFCSS_FONT_SANS_SERIF }, { "verdana", GFCSS_FONT_SANS_SERIF },
    { "tahoma", GFCSS_FONT_SANS_SERIF }, { "geneva", GFCSS_FONT_SANS_SERIF },
    { "comic sans ms", GFCSS_FONT_CURSIVE }, { "impact", GFCSS_FONT_FANTASY }
};
static const kwent kw_font_size[] = {
    { "xx-small", OL_CSSPX(9) }, { "x-small", OL_CSSPX(10) }, { "small", OL_CSSPX(13) },
    { "medium", OL_CSSPX(16) }, { "large", OL_CSSPX(18) }, { "x-large", OL_CSSPX(24) },
    { "xx-large", OL_CSSPX(32) }, { "xxx-large", OL_CSSPX(48) }
};
#define FS_SMALLER (-1)
#define FS_LARGER (-2)
#define FW_BOLDER (-1)
#define FW_LIGHTER (-2)
static const kwent kw_font_style[] = {
    { "normal", GFCSS_FONT_NORMAL }, { "italic", GFCSS_FONT_ITALIC }, { "oblique", GFCSS_FONT_OBLIQUE }
};
static const kwent kw_font_variant[] = { { "normal", GFCSS_VARIANT_NORMAL }, { "small-caps", GFCSS_VARIANT_SMALL_CAPS } };
static const kwent kw_text_align[] = {
    { "start", GFCSS_ALIGN_START }, { "end", GFCSS_ALIGN_END }, { "left", GFCSS_ALIGN_LEFT },
    { "right", GFCSS_ALIGN_RIGHT }, { "center", GFCSS_ALIGN_CENTER }, { "justify", GFCSS_ALIGN_JUSTIFY },
    { "match-parent", GFCSS_ALIGN_START }, { "-webkit-center", GFCSS_ALIGN_CENTER },
    { "-moz-center", GFCSS_ALIGN_CENTER }, { "-webkit-left", GFCSS_ALIGN_LEFT }, { "-webkit-right", GFCSS_ALIGN_RIGHT }
};
static const kwent kw_decoration[] = {
    { "underline", GFCSS_UNDERLINE }, { "line-through", GFCSS_LINE_THROUGH }, { "overline", GFCSS_OVERLINE },
    { "blink", 0 }
};
static const kwent kw_decoration_other[] = {
    { "solid", 0 }, { "double", 0 }, { "dotted", 0 }, { "dashed", 0 }, { "wavy", 0 }, { "auto", 0 },
    { "from-font", 0 }
};
static const kwent kw_transform[] = {
    { "none", GFCSS_TRANSFORM_NONE }, { "capitalize", GFCSS_CAPITALIZE }, { "uppercase", GFCSS_UPPERCASE },
    { "lowercase", GFCSS_LOWERCASE }
};
static const kwent kw_white_space[] = {
    { "normal", GFCSS_WS_NORMAL }, { "pre", GFCSS_WS_PRE }, { "nowrap", GFCSS_WS_NOWRAP },
    { "pre-wrap", GFCSS_WS_PRE_WRAP }, { "pre-line", GFCSS_WS_PRE_LINE }, { "break-spaces", GFCSS_WS_PRE_WRAP }
};
static const kwent kw_valign[] = {
    { "baseline", GFCSS_VALIGN_BASELINE }, { "top", GFCSS_VALIGN_TOP }, { "middle", GFCSS_VALIGN_MIDDLE },
    { "bottom", GFCSS_VALIGN_BOTTOM }, { "text-top", GFCSS_VALIGN_TEXT_TOP },
    { "text-bottom", GFCSS_VALIGN_TEXT_BOTTOM }, { "sub", GFCSS_VALIGN_SUB }, { "super", GFCSS_VALIGN_SUPER }
};
static const kwent kw_list_type[] = {
    { "disc", GFCSS_LIST_DISC }, { "circle", GFCSS_LIST_CIRCLE }, { "square", GFCSS_LIST_SQUARE },
    { "decimal", GFCSS_LIST_DECIMAL }, { "decimal-leading-zero", GFCSS_LIST_DECIMAL_LEADING_ZERO },
    { "lower-roman", GFCSS_LIST_LOWER_ROMAN }, { "upper-roman", GFCSS_LIST_UPPER_ROMAN },
    { "lower-alpha", GFCSS_LIST_LOWER_ALPHA }, { "lower-latin", GFCSS_LIST_LOWER_ALPHA },
    { "upper-alpha", GFCSS_LIST_UPPER_ALPHA }, { "upper-latin", GFCSS_LIST_UPPER_ALPHA },
    { "lower-greek", GFCSS_LIST_LOWER_GREEK }, { "none", GFCSS_LIST_NONE }
};
static const kwent kw_list_position[] = { { "outside", GFCSS_LIST_OUTSIDE }, { "inside", GFCSS_LIST_INSIDE } };
static const kwent kw_cursor[] = {
    { "auto", GFCSS_CURSOR_AUTO }, { "default", GFCSS_CURSOR_DEFAULT }, { "pointer", GFCSS_CURSOR_POINTER },
    { "text", GFCSS_CURSOR_TEXT }, { "move", GFCSS_CURSOR_MOVE }, { "wait", GFCSS_CURSOR_WAIT },
    { "help", GFCSS_CURSOR_HELP }, { "crosshair", GFCSS_CURSOR_CROSSHAIR },
    { "not-allowed", GFCSS_CURSOR_NOT_ALLOWED }, { "progress", GFCSS_CURSOR_PROGRESS },
    { "grab", GFCSS_CURSOR_GRAB }, { "none", GFCSS_CURSOR_NONE }, { "context-menu", GFCSS_CURSOR_OTHER },
    { "cell", GFCSS_CURSOR_OTHER }, { "vertical-text", GFCSS_CURSOR_TEXT }, { "alias", GFCSS_CURSOR_OTHER },
    { "copy", GFCSS_CURSOR_OTHER }, { "no-drop", GFCSS_CURSOR_NOT_ALLOWED }, { "grabbing", GFCSS_CURSOR_GRAB },
    { "all-scroll", GFCSS_CURSOR_MOVE }, { "col-resize", GFCSS_CURSOR_OTHER }, { "row-resize", GFCSS_CURSOR_OTHER },
    { "n-resize", GFCSS_CURSOR_OTHER }, { "e-resize", GFCSS_CURSOR_OTHER }, { "s-resize", GFCSS_CURSOR_OTHER },
    { "w-resize", GFCSS_CURSOR_OTHER }, { "ne-resize", GFCSS_CURSOR_OTHER }, { "nw-resize", GFCSS_CURSOR_OTHER },
    { "se-resize", GFCSS_CURSOR_OTHER }, { "sw-resize", GFCSS_CURSOR_OTHER }, { "ew-resize", GFCSS_CURSOR_OTHER },
    { "ns-resize", GFCSS_CURSOR_OTHER }, { "nesw-resize", GFCSS_CURSOR_OTHER },
    { "nwse-resize", GFCSS_CURSOR_OTHER }, { "zoom-in", GFCSS_CURSOR_OTHER }, { "zoom-out", GFCSS_CURSOR_OTHER }
};
static const kwent kw_direction[] = {
    { "row", GFCSS_ROW }, { "row-reverse", GFCSS_ROW_REVERSE }, { "column", GFCSS_COLUMN },
    { "column-reverse", GFCSS_COLUMN_REVERSE }
};
static const kwent kw_wrap[] = { { "nowrap", GFCSS_NOWRAP }, { "wrap", GFCSS_WRAP }, { "wrap-reverse", GFCSS_WRAP_REVERSE } };
static const kwent kw_flex_align[] = {
    { "normal", GFCSS_FLEX_NORMAL }, { "stretch", GFCSS_FLEX_STRETCH }, { "start", GFCSS_FLEX_START },
    { "flex-start", GFCSS_FLEX_START }, { "self-start", GFCSS_FLEX_START }, { "end", GFCSS_FLEX_END },
    { "flex-end", GFCSS_FLEX_END }, { "self-end", GFCSS_FLEX_END }, { "center", GFCSS_FLEX_CENTER },
    { "baseline", GFCSS_FLEX_BASELINE }, { "space-between", GFCSS_FLEX_SPACE_BETWEEN },
    { "space-around", GFCSS_FLEX_SPACE_AROUND }, { "space-evenly", GFCSS_FLEX_SPACE_EVENLY },
    { "left", GFCSS_FLEX_LEFT }, { "right", GFCSS_FLEX_RIGHT }
};
static const kwent kw_collapse[] = { { "separate", GFCSS_SEPARATE }, { "collapse", GFCSS_COLLAPSE_BORDERS } };
static const kwent kw_caption[] = { { "top", GFCSS_CAPTION_TOP }, { "bottom", GFCSS_CAPTION_BOTTOM } };

/* ---- Colours -------------------------------------------------------------------- */

/* CSS Color 4's named colours, in order for a binary search */
static const struct { const char *name; uint32_t rgb; } named[] = {
    { "aliceblue", 0xf0f8ff }, { "antiquewhite", 0xfaebd7 }, { "aqua", 0x00ffff }, { "aquamarine", 0x7fffd4 },
    { "azure", 0xf0ffff }, { "beige", 0xf5f5dc }, { "bisque", 0xffe4c4 }, { "black", 0x000000 },
    { "blanchedalmond", 0xffebcd }, { "blue", 0x0000ff }, { "blueviolet", 0x8a2be2 }, { "brown", 0xa52a2a },
    { "burlywood", 0xdeb887 }, { "cadetblue", 0x5f9ea0 }, { "chartreuse", 0x7fff00 }, { "chocolate", 0xd2691e },
    { "coral", 0xff7f50 }, { "cornflowerblue", 0x6495ed }, { "cornsilk", 0xfff8dc }, { "crimson", 0xdc143c },
    { "cyan", 0x00ffff }, { "darkblue", 0x00008b }, { "darkcyan", 0x008b8b }, { "darkgoldenrod", 0xb8860b },
    { "darkgray", 0xa9a9a9 }, { "darkgreen", 0x006400 }, { "darkgrey", 0xa9a9a9 }, { "darkkhaki", 0xbdb76b },
    { "darkmagenta", 0x8b008b }, { "darkolivegreen", 0x556b2f }, { "darkorange", 0xff8c00 },
    { "darkorchid", 0x9932cc }, { "darkred", 0x8b0000 }, { "darksalmon", 0xe9967a }, { "darkseagreen", 0x8fbc8f },
    { "darkslateblue", 0x483d8b }, { "darkslategray", 0x2f4f4f }, { "darkslategrey", 0x2f4f4f },
    { "darkturquoise", 0x00ced1 }, { "darkviolet", 0x9400d3 }, { "deeppink", 0xff1493 },
    { "deepskyblue", 0x00bfff }, { "dimgray", 0x696969 }, { "dimgrey", 0x696969 }, { "dodgerblue", 0x1e90ff },
    { "firebrick", 0xb22222 }, { "floralwhite", 0xfffaf0 }, { "forestgreen", 0x228b22 }, { "fuchsia", 0xff00ff },
    { "gainsboro", 0xdcdcdc }, { "ghostwhite", 0xf8f8ff }, { "gold", 0xffd700 }, { "goldenrod", 0xdaa520 },
    { "gray", 0x808080 }, { "green", 0x008000 }, { "greenyellow", 0xadff2f }, { "grey", 0x808080 },
    { "honeydew", 0xf0fff0 }, { "hotpink", 0xff69b4 }, { "indianred", 0xcd5c5c }, { "indigo", 0x4b0082 },
    { "ivory", 0xfffff0 }, { "khaki", 0xf0e68c }, { "lavender", 0xe6e6fa }, { "lavenderblush", 0xfff0f5 },
    { "lawngreen", 0x7cfc00 }, { "lemonchiffon", 0xfffacd }, { "lightblue", 0xadd8e6 }, { "lightcoral", 0xf08080 },
    { "lightcyan", 0xe0ffff }, { "lightgoldenrodyellow", 0xfafad2 }, { "lightgray", 0xd3d3d3 },
    { "lightgreen", 0x90ee90 }, { "lightgrey", 0xd3d3d3 }, { "lightpink", 0xffb6c1 }, { "lightsalmon", 0xffa07a },
    { "lightseagreen", 0x20b2aa }, { "lightskyblue", 0x87cefa }, { "lightslategray", 0x778899 },
    { "lightslategrey", 0x778899 }, { "lightsteelblue", 0xb0c4de }, { "lightyellow", 0xffffe0 },
    { "lime", 0x00ff00 }, { "limegreen", 0x32cd32 }, { "linen", 0xfaf0e6 }, { "magenta", 0xff00ff },
    { "maroon", 0x800000 }, { "mediumaquamarine", 0x66cdaa }, { "mediumblue", 0x0000cd },
    { "mediumorchid", 0xba55d3 }, { "mediumpurple", 0x9370db }, { "mediumseagreen", 0x3cb371 },
    { "mediumslateblue", 0x7b68ee }, { "mediumspringgreen", 0x00fa9a }, { "mediumturquoise", 0x48d1cc },
    { "mediumvioletred", 0xc71585 }, { "midnightblue", 0x191970 }, { "mintcream", 0xf5fffa },
    { "mistyrose", 0xffe4e1 }, { "moccasin", 0xffe4b5 }, { "navajowhite", 0xffdead }, { "navy", 0x000080 },
    { "oldlace", 0xfdf5e6 }, { "olive", 0x808000 }, { "olivedrab", 0x6b8e23 }, { "orange", 0xffa500 },
    { "orangered", 0xff4500 }, { "orchid", 0xda70d6 }, { "palegoldenrod", 0xeee8aa }, { "palegreen", 0x98fb98 },
    { "paleturquoise", 0xafeeee }, { "palevioletred", 0xdb7093 }, { "papayawhip", 0xffefd5 },
    { "peachpuff", 0xffdab9 }, { "peru", 0xcd853f }, { "pink", 0xffc0cb }, { "plum", 0xdda0dd },
    { "powderblue", 0xb0e0e6 }, { "purple", 0x800080 }, { "rebeccapurple", 0x663399 }, { "red", 0xff0000 },
    { "rosybrown", 0xbc8f8f }, { "royalblue", 0x4169e1 }, { "saddlebrown", 0x8b4513 }, { "salmon", 0xfa8072 },
    { "sandybrown", 0xf4a460 }, { "seagreen", 0x2e8b57 }, { "seashell", 0xfff5ee }, { "sienna", 0xa0522d },
    { "silver", 0xc0c0c0 }, { "skyblue", 0x87ceeb }, { "slateblue", 0x6a5acd }, { "slategray", 0x708090 },
    { "slategrey", 0x708090 }, { "snow", 0xfffafa }, { "springgreen", 0x00ff7f }, { "steelblue", 0x4682b4 },
    { "tan", 0xd2b48c }, { "teal", 0x008080 }, { "thistle", 0xd8bfd8 }, { "tomato", 0xff6347 },
    { "turquoise", 0x40e0d0 }, { "violet", 0xee82ee }, { "wheat", 0xf5deb3 }, { "white", 0xffffff },
    { "whitesmoke", 0xf5f5f5 }, { "yellow", 0xffff00 }, { "yellowgreen", 0x9acd32 }
};

int gfcss_named_colours(void);
int gfcss_named_colours(void)
{
    return (int)(sizeof(named) / sizeof(named[0]));
}

static int named_colour(const char *s, size_t n, uint32_t *argb)
{
    int lo = 0, hi = (int)(sizeof(named) / sizeof(named[0])) - 1;
    char low[24];
    size_t i;
    if (n >= sizeof(low)) return 0;
    for (i = 0; i < n; ++i) low[i] = (char)g_lower((unsigned char)s[i]);
    low[n] = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = strcmp(low, named[mid].name);
        if (!c) {
            *argb = 0xff000000u | named[mid].rgb;
            return 1;
        }
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return 0;
}

static int hexd(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = g_lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int hex_colour(const char *s, size_t n, uint32_t *argb)
{
    int d[8];
    size_t i;
    if (n != 3 && n != 4 && n != 6 && n != 8) return 0;
    for (i = 0; i < n; ++i)
        if ((d[i] = hexd((unsigned char)s[i])) < 0) return 0;
    if (n <= 4) {
        uint32_t a = n == 4 ? (uint32_t)d[3] * 17 : 255;
        *argb = a << 24 | (uint32_t)d[0] * 17 << 16 | (uint32_t)d[1] * 17 << 8 | (uint32_t)d[2] * 17;
    } else {
        uint32_t a = n == 8 ? (uint32_t)(d[6] << 4 | d[7]) : 255;
        *argb = a << 24 | (uint32_t)(d[0] << 4 | d[1]) << 16 | (uint32_t)(d[2] << 4 | d[3]) << 8 |
                (uint32_t)(d[4] << 4 | d[5]);
    }
    return 1;
}

static int clamp255(int64_t v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (int)v;
}

/* thousandths of a channel (0 to 255000) or a percentage, to 0..255 */
static int channel(const vtok *t, int *out)
{
    if (t->type == T_NUMBER) *out = clamp255(((int64_t)t->num + 500) / 1000);
    else if (t->type == T_PERCENT) *out = clamp255(((int64_t)t->num * 255 + 50000) / 100000);
    else if (t->type == T_IDENT && g_ieq(t->s, t->n, "none")) *out = 0;
    else return 0;
    return 1;
}

static int alpha_of(const vtok *t, int *out)
{
    if (t->type == T_NUMBER) *out = clamp255(((int64_t)t->num * 255 + 500) / 1000);
    else if (t->type == T_PERCENT) *out = clamp255(((int64_t)t->num * 255 + 50000) / 100000);
    else if (t->type == T_IDENT && g_ieq(t->s, t->n, "none")) *out = 0;
    else return 0;
    return 1;
}

/* the numbers of a colour function: up to 3 and an alpha (after '/' or a fourth comma) */
static int colour_args(const vctx *c, int i, int end, const vtok **v, int *nv, const vtok **alpha)
{
    int commas = 0;
    *nv = 0;
    *alpha = NULL;
    for (; i < end; ++i) {
        const vtok *t = &c->t[i];
        if (t->type == T_COMMA) { ++commas; continue; }
        if (t->type == T_DELIM && t->delim == '/') {
            if (*alpha || *nv != 3 || commas) return 0;
            if (i + 1 >= end) return 0;
            *alpha = &c->t[++i];
            continue;
        }
        if (*alpha) return 0;
        if (*nv == 3) {
            if (commas != 3) return 0;
            *alpha = t;
            continue;
        }
        v[(*nv)++] = t;
    }
    if (*nv != 3) return 0;
    if (commas && commas != (*alpha ? 3 : 2)) return 0;
    return 1;
}

/* hue in thousandths of a degree from a number or an angle */
static int hue_of(const vtok *t, int64_t *h)
{
    if (t->type == T_NUMBER) *h = t->num;
    else if (t->type == T_DIM && g_ieq(t->unit, t->unit_n, "deg")) *h = t->num;
    else if (t->type == T_DIM && g_ieq(t->unit, t->unit_n, "turn")) *h = (int64_t)t->num * 360;
    else if (t->type == T_DIM && g_ieq(t->unit, t->unit_n, "grad")) *h = (int64_t)t->num * 9 / 10;
    else if (t->type == T_DIM && g_ieq(t->unit, t->unit_n, "rad")) *h = (int64_t)t->num * 180000 / 3142;
    else if (t->type == T_IDENT && g_ieq(t->s, t->n, "none")) *h = 0;
    else return 0;
    *h %= 360000;
    if (*h < 0) *h += 360000;
    return 1;
}

/* p, q, t in hundred-thousandths and thousandths of a degree */
static int64_t hue_part(int64_t p, int64_t q, int64_t t)
{
    t %= 360000;
    if (t < 0) t += 360000;
    if (t < 60000) return p + (q - p) * t / 60000;
    if (t < 180000) return q;
    if (t < 240000) return p + (q - p) * (240000 - t) / 60000;
    return p;
}

static int pct_of(const vtok *t, int64_t *v)
{
    /* hundred-thousandths: 100% is 100000 */
    if (t->type == T_PERCENT || t->type == T_NUMBER) *v = t->num;
    else if (t->type == T_IDENT && g_ieq(t->s, t->n, "none")) *v = 0;
    else return 0;
    if (*v < 0) *v = 0;
    if (*v > 100000) *v = 100000;
    return 1;
}

static int colour_func(const vctx *c, int i, uint32_t *argb)
{
    const vtok *f = &c->t[i], *v[3], *alpha;
    int nv, a = 255, end = f->close;
    if (!colour_args(c, i + 1, end, v, &nv, &alpha)) return 0;
    if (alpha && !alpha_of(alpha, &a)) return 0;
    if (g_ieq(f->s, f->n, "rgb") || g_ieq(f->s, f->n, "rgba")) {
        int r, g, b;
        if (!channel(v[0], &r) || !channel(v[1], &g) || !channel(v[2], &b)) return 0;
        *argb = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        return 1;
    }
    if (g_ieq(f->s, f->n, "hsl") || g_ieq(f->s, f->n, "hsla")) {
        int64_t h, s, l, q, p;
        int r, g, b;
        if (!hue_of(v[0], &h) || !pct_of(v[1], &s) || !pct_of(v[2], &l)) return 0;
        q = l < 50000 ? l * (100000 + s) / 100000 : l + s - l * s / 100000;
        p = 2 * l - q;
        r = clamp255((hue_part(p, q, h + 120000) * 255 + 50000) / 100000);
        g = clamp255((hue_part(p, q, h) * 255 + 50000) / 100000);
        b = clamp255((hue_part(p, q, h - 120000) * 255 + 50000) / 100000);
        *argb = (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        return 1;
    }
    return 0;
}

/* A colour at c->t[*i]: 1 with argb (or *current for currentColor) */
static int colour_at(vctx *c, int *i, uint32_t *argb, int *current)
{
    const vtok *t;
    if (*i >= c->n) return 0;
    t = &c->t[*i];
    *current = 0;
    if (t->type == T_HASH) {
        if (!hex_colour(t->s, t->n, argb)) return 0;
    } else if (t->type == T_IDENT) {
        if (g_ieq(t->s, t->n, "transparent")) *argb = 0;
        else if (g_ieq(t->s, t->n, "currentcolor")) *current = 1;
        else if (!named_colour(t->s, t->n, argb)) return 0;
    } else if (t->type == T_FUNC) {
        if (!colour_func(c, *i, argb)) return 0;
        *i = after(c, *i);
        return 1;
    } else {
        return 0;
    }
    ++*i;
    return 1;
}

int gfcss_parse_colour(const char *s, size_t length, uint32_t *argb)
{
    vctx *c;
    gsink sink;
    int i = 0, cur, ok;
    uint32_t v = 0;
    if (!s || !argb) return 0;
    c = (vctx *)malloc(sizeof(vctx));
    if (!c) return 0;
    memset(&sink, 0, sizeof(sink));
    c->sink = &sink;
    ok = tokenize(c, s, s + length) && colour_at(c, &i, &v, &cur) && !cur && i == c->n;
    free(c);
    if (ok) *argb = v;
    return ok;
}

/* ---- Lengths ------------------------------------------------------------------- */

enum {
    L_PCT = 1u << 0,            /* percentages */
    L_NEG = 1u << 1,            /* below 0 */
    L_AUTO = 1u << 2,
    L_NONE = 1u << 3,
    L_NORMAL = 1u << 4,
    L_CONTENT = 1u << 5,
    L_NUMBER = 1u << 6          /* a bare number: V_NUM */
};

/* keywords stand for themselves in a length: V_KW with these */
enum { LK_AUTO = 1, LK_NONE, LK_NORMAL, LK_CONTENT };

static int abs_unit(const char *u, size_t n, int32_t *num, int32_t *den)
{
    static const struct { const char *u; int32_t num, den; } units[] = {
        { "px", 60, 1 }, { "pt", 80, 1 }, { "pc", 960, 1 }, { "in", 5760, 1 }, { "cm", 576000, 254 },
        { "mm", 57600, 254 }, { "q", 14400, 254 }
    };
    size_t i;
    for (i = 0; i < sizeof(units) / sizeof(units[0]); ++i)
        if (g_ieq(u, n, units[i].u)) {
            *num = units[i].num;
            *den = units[i].den;
            return 1;
        }
    return 0;
}

static int rel_unit(const char *u, size_t n)
{
    if (g_ieq(u, n, "em")) return V_EM;
    if (g_ieq(u, n, "rem")) return V_REM;
    if (g_ieq(u, n, "ex")) return V_EX;
    if (g_ieq(u, n, "ch")) return V_CH;
    if (g_ieq(u, n, "vw")) return V_VW;
    if (g_ieq(u, n, "vh")) return V_VH;
    if (g_ieq(u, n, "vmin")) return V_VMIN;
    if (g_ieq(u, n, "vmax")) return V_VMAX;
    return 0;
}

/* calc(): a sum of products; each term a gcalc or a bare number */
typedef struct {
    gcalc c;
    int is_num;
    int32_t num;
} cterm;

static int calc_sum(const vctx *c, int *i, int end, cterm *out, int depth);

static int calc_add_unit(cterm *t, int unit, int32_t v)
{
    memset(t, 0, sizeof(*t));
    switch (unit) {
    case V_ABS: t->c.abs = v; break;
    case V_EM: t->c.em = v; break;
    case V_EX: case V_CH: t->c.em = v / 2; break;
    case V_REM: t->c.rem = v; break;
    case V_PCT: t->c.pct = v; break;
    case V_VW: t->c.vw = v; break;
    case V_VH: t->c.vh = v; break;
    case V_VMIN: t->c.vmin = v; break;
    case V_VMAX: t->c.vmax = v; break;
    default: return 0;
    }
    return 1;
}

static int calc_value(const vctx *c, int *i, int end, cterm *out, int depth)
{
    const vtok *t;
    if (*i >= end || depth > 8) return 0;
    t = &c->t[*i];
    if (t->type == T_NUMBER) {
        memset(out, 0, sizeof(*out));
        out->is_num = 1;
        out->num = t->num;
        ++*i;
        return 1;
    }
    if (t->type == T_PERCENT) {
        ++*i;
        return calc_add_unit(out, V_PCT, t->num);
    }
    if (t->type == T_DIM) {
        int32_t num, den;
        int u;
        ++*i;
        if (abs_unit(t->unit, t->unit_n, &num, &den)) return calc_add_unit(out, V_ABS, g_mul_div(t->num, num, den * 1000));
        if ((u = rel_unit(t->unit, t->unit_n)) != 0) return calc_add_unit(out, u, t->num);
        return 0;
    }
    if (t->type == T_LPAREN || (t->type == T_FUNC && (g_ieq(t->s, t->n, "calc") || g_ieq(t->s, t->n, "-webkit-calc")))) {
        int j = *i + 1, close = t->close;
        if (close > end) return 0;
        if (!calc_sum(c, &j, close, out, depth + 1) || j != close) return 0;
        *i = close < c->n ? close + 1 : close;
        return 1;
    }
    return 0;
}

static void calc_scale(cterm *t, int32_t num, int32_t den)
{
    t->c.abs = g_mul_div(t->c.abs, num, den);
    t->c.em = g_mul_div(t->c.em, num, den);
    t->c.rem = g_mul_div(t->c.rem, num, den);
    t->c.pct = g_mul_div(t->c.pct, num, den);
    t->c.vw = g_mul_div(t->c.vw, num, den);
    t->c.vh = g_mul_div(t->c.vh, num, den);
    t->c.vmin = g_mul_div(t->c.vmin, num, den);
    t->c.vmax = g_mul_div(t->c.vmax, num, den);
}

static int calc_product(const vctx *c, int *i, int end, cterm *out, int depth)
{
    if (!calc_value(c, i, end, out, depth)) return 0;
    while (*i < end && (is_delim(c, *i, '*') || is_delim(c, *i, '/'))) {
        int div = is_delim(c, *i, '/');
        cterm r;
        ++*i;
        if (!calc_value(c, i, end, &r, depth)) return 0;
        if (div) {
            if (!r.is_num || r.num == 0) return 0;
            if (out->is_num) out->num = g_mul_div(out->num, 1000, r.num);
            else calc_scale(out, 1000, r.num);
        } else if (r.is_num) {
            if (out->is_num) out->num = g_mul_div(out->num, r.num, 1000);
            else calc_scale(out, r.num, 1000);
        } else if (out->is_num) {
            int32_t n = out->num;
            *out = r;
            calc_scale(out, n, 1000);
        } else {
            return 0;               /* a length times a length */
        }
    }
    return 1;
}

static int32_t sat_add(int32_t a, int32_t b)
{
    int64_t v = (int64_t)a + b;
    return v > G_SAT ? G_SAT : v < -G_SAT ? -G_SAT : (int32_t)v;
}

static int calc_sum(const vctx *c, int *i, int end, cterm *out, int depth)
{
    if (!calc_product(c, i, end, out, depth)) return 0;
    while (*i < end) {
        const vtok *op = &c->t[*i];
        cterm r;
        int neg;
        if (op->type != T_DELIM || (op->delim != '+' && op->delim != '-')) return 0;
        /* CSS wants white space round + and - */
        if (!op->ws || *i + 1 >= end || !c->t[*i + 1].ws) return 0;
        neg = op->delim == '-';
        ++*i;
        if (!calc_product(c, i, end, &r, depth)) return 0;
        if (r.is_num != out->is_num) return 0;
        if (neg) calc_scale(&r, -1, 1), r.num = -r.num;
        out->num = sat_add(out->num, r.num);
        out->c.abs = sat_add(out->c.abs, r.c.abs);
        out->c.em = sat_add(out->c.em, r.c.em);
        out->c.rem = sat_add(out->c.rem, r.c.rem);
        out->c.pct = sat_add(out->c.pct, r.c.pct);
        out->c.vw = sat_add(out->c.vw, r.c.vw);
        out->c.vh = sat_add(out->c.vh, r.c.vh);
        out->c.vmin = sat_add(out->c.vmin, r.c.vmin);
        out->c.vmax = sat_add(out->c.vmax, r.c.vmax);
    }
    return 1;
}

static int add_calc(vctx *c, const gcalc *g, int32_t *index)
{
    gsink *s = c->sink;
    if (s->ncalc == s->calc_cap) {
        if (s->fixed || !g_grow((void **)&s->calc, &s->calc_cap, s->ncalc + 1, sizeof(gcalc))) {
            s->oom = 1;
            return 0;
        }
    }
    s->calc[s->ncalc] = *g;
    *index = (int32_t)s->ncalc++;
    return 1;
}

/* A length (or what flags allow) at c->t[*i] */
static int length_at(vctx *c, int *i, unsigned flags, gval *out)
{
    const vtok *t;
    if (*i >= c->n) return 0;
    t = &c->t[*i];
    out->w = 0;
    if (t->type == T_IDENT) {
        int k = 0;
        if ((flags & L_AUTO) && g_ieq(t->s, t->n, "auto")) k = LK_AUTO;
        else if ((flags & L_NONE) && g_ieq(t->s, t->n, "none")) k = LK_NONE;
        else if ((flags & L_NORMAL) && g_ieq(t->s, t->n, "normal")) k = LK_NORMAL;
        else if ((flags & L_CONTENT) && g_ieq(t->s, t->n, "content")) k = LK_CONTENT;
        if (!k) return 0;
        out->unit = V_KW;
        out->v = k;
        ++*i;
        return 1;
    }
    if (t->type == T_NUMBER) {
        if (flags & L_NUMBER) {
            if (t->num < 0 && !(flags & L_NEG)) return 0;
            out->unit = V_NUM;
            out->v = t->num;
            ++*i;
            return 1;
        }
        if (t->num != 0) return 0;      /* only 0 needs no unit */
        out->unit = V_ABS;
        out->v = 0;
        ++*i;
        return 1;
    }
    if (t->num < 0 && !(flags & L_NEG) && (t->type == T_DIM || t->type == T_PERCENT)) return 0;
    if (t->type == T_PERCENT) {
        if (!(flags & L_PCT)) return 0;
        out->unit = V_PCT;
        out->v = t->num;
        ++*i;
        return 1;
    }
    if (t->type == T_DIM) {
        int32_t num, den;
        int u;
        if (abs_unit(t->unit, t->unit_n, &num, &den)) {
            out->unit = V_ABS;
            out->v = g_mul_div(t->num, num, den * 1000);
        } else if ((u = rel_unit(t->unit, t->unit_n)) != 0) {
            out->unit = u;
            out->v = t->num;
        } else {
            return 0;
        }
        ++*i;
        return 1;
    }
    if (t->type == T_FUNC && (g_ieq(t->s, t->n, "calc") || g_ieq(t->s, t->n, "-webkit-calc"))) {
        cterm r;
        int j = *i + 1;
        if (!calc_sum(c, &j, t->close, &r, 0) || j != t->close) return 0;
        if (r.is_num) {
            if (flags & L_NUMBER) {
                out->unit = V_NUM;
                out->v = r.num;
            } else if (r.num == 0) {
                out->unit = V_ABS;
                out->v = 0;
            } else {
                return 0;
            }
        } else {
            if (r.c.pct && !(flags & L_PCT)) return 0;
            if (!add_calc(c, &r.c, &out->v)) return 0;
            out->unit = V_CALC;
        }
        *i = after(c, *i);
        return 1;
    }
    return 0;
}

int32_t g_calc_resolve(const gcalc *c, ol_unit fs, ol_unit rem, ol_unit vw, ol_unit vh, int32_t *pct)
{
    int64_t v = c->abs;
    ol_unit vmin = vw < vh ? vw : vh, vmax = vw > vh ? vw : vh;
    v += (int64_t)c->em * fs / 1000;
    v += (int64_t)c->rem * rem / 1000;
    v += (int64_t)c->vw * vw / 100000;
    v += (int64_t)c->vh * vh / 100000;
    v += (int64_t)c->vmin * vmin / 100000;
    v += (int64_t)c->vmax * vmax / 100000;
    if (pct) *pct = c->pct;
    if (v > G_SAT) v = G_SAT;
    if (v < -G_SAT) v = -G_SAT;
    return (int32_t)v;
}

/* ---- Properties ----------------------------------------------------------------- */

static const struct { const char *name; uint8_t inherited; } props[GFCSS_P_COUNT] = {
    { "display", 0 }, { "position", 0 }, { "float", 0 }, { "clear", 0 }, { "z-index", 0 },
    { "visibility", 1 }, { "overflow-x", 0 }, { "overflow-y", 0 }, { "box-sizing", 0 },
    { "width", 0 }, { "height", 0 }, { "min-width", 0 }, { "min-height", 0 }, { "max-width", 0 },
    { "max-height", 0 },
    { "margin-top", 0 }, { "margin-right", 0 }, { "margin-bottom", 0 }, { "margin-left", 0 },
    { "padding-top", 0 }, { "padding-right", 0 }, { "padding-bottom", 0 }, { "padding-left", 0 },
    { "border-top-width", 0 }, { "border-right-width", 0 }, { "border-bottom-width", 0 },
    { "border-left-width", 0 },
    { "border-top-style", 0 }, { "border-right-style", 0 }, { "border-bottom-style", 0 },
    { "border-left-style", 0 },
    { "border-top-color", 0 }, { "border-right-color", 0 }, { "border-bottom-color", 0 },
    { "border-left-color", 0 },
    { "border-top-left-radius", 0 }, { "border-top-right-radius", 0 }, { "border-bottom-right-radius", 0 },
    { "border-bottom-left-radius", 0 },
    { "outline-width", 0 }, { "outline-style", 0 }, { "outline-color", 0 },
    { "color", 1 }, { "background-color", 0 }, { "background-image", 0 }, { "background-repeat", 0 },
    { "background-position-x", 0 }, { "background-position-y", 0 }, { "opacity", 0 },
    { "font-family", 1 }, { "", 1 }, { "font-size", 1 }, { "font-weight", 1 },
    { "font-style", 1 }, { "font-variant", 1 }, { "line-height", 1 },
    { "text-align", 1 }, { "text-indent", 1 }, { "text-decoration-line", 0 }, { "text-transform", 1 },
    { "white-space", 1 }, { "vertical-align", 0 },
    { "list-style-type", 1 }, { "list-style-position", 1 }, { "list-style-image", 1 },
    { "letter-spacing", 1 }, { "word-spacing", 1 }, { "cursor", 1 },
    { "flex-direction", 0 }, { "flex-wrap", 0 }, { "flex-grow", 0 }, { "flex-shrink", 0 },
    { "flex-basis", 0 }, { "row-gap", 0 }, { "column-gap", 0 }, { "align-items", 0 },
    { "justify-content", 0 },
    { "border-collapse", 1 }, { "", 1 }, { "", 1 }, { "caption-side", 1 },
    { "content", 0 }
};

int g_prop_inherited(int p)
{
    return p >= 0 && p < GFCSS_P_COUNT && props[p].inherited;
}

/* Shorthands: what they set */
enum {
    SH_MARGIN = 1, SH_PADDING, SH_BORDER, SH_BORDER_TOP, SH_BORDER_RIGHT, SH_BORDER_BOTTOM, SH_BORDER_LEFT,
    SH_BORDER_WIDTH, SH_BORDER_STYLE, SH_BORDER_COLOR, SH_BORDER_RADIUS, SH_OUTLINE, SH_BACKGROUND,
    SH_BACKGROUND_POSITION, SH_FONT, SH_LIST_STYLE, SH_FLEX, SH_FLEX_FLOW, SH_GAP, SH_OVERFLOW,
    SH_TEXT_DECORATION, SH_BORDER_SPACING
};

static const struct { const char *name; int sh; } shorthands[] = {
    { "margin", SH_MARGIN }, { "padding", SH_PADDING }, { "border", SH_BORDER }, { "border-top", SH_BORDER_TOP },
    { "border-right", SH_BORDER_RIGHT }, { "border-bottom", SH_BORDER_BOTTOM }, { "border-left", SH_BORDER_LEFT },
    { "border-width", SH_BORDER_WIDTH }, { "border-style", SH_BORDER_STYLE }, { "border-color", SH_BORDER_COLOR },
    { "border-radius", SH_BORDER_RADIUS }, { "outline", SH_OUTLINE }, { "background", SH_BACKGROUND },
    { "background-position", SH_BACKGROUND_POSITION }, { "font", SH_FONT }, { "list-style", SH_LIST_STYLE },
    { "flex", SH_FLEX }, { "flex-flow", SH_FLEX_FLOW }, { "gap", SH_GAP }, { "grid-gap", SH_GAP },
    { "overflow", SH_OVERFLOW }, { "text-decoration", SH_TEXT_DECORATION },
    { "border-spacing", SH_BORDER_SPACING }
};

static const struct { const char *name; int p; } aliases[] = {
    { "grid-row-gap", GFCSS_P_ROW_GAP }, { "grid-column-gap", GFCSS_P_COLUMN_GAP },
    { "word-wrap", -1 }
};

static int sh_longhands(int sh, int *out)
{
    int n = 0, k;
    switch (sh) {
    case SH_MARGIN: for (k = 0; k < 4; ++k) out[n++] = GFCSS_P_MARGIN_TOP + k; break;
    case SH_PADDING: for (k = 0; k < 4; ++k) out[n++] = GFCSS_P_PADDING_TOP + k; break;
    case SH_BORDER:
        for (k = 0; k < 4; ++k) {
            out[n++] = GFCSS_P_BORDER_TOP_WIDTH + k;
            out[n++] = GFCSS_P_BORDER_TOP_STYLE + k;
            out[n++] = GFCSS_P_BORDER_TOP_COLOR + k;
        }
        break;
    case SH_BORDER_TOP: case SH_BORDER_RIGHT: case SH_BORDER_BOTTOM: case SH_BORDER_LEFT:
        k = sh - SH_BORDER_TOP;
        out[n++] = GFCSS_P_BORDER_TOP_WIDTH + k;
        out[n++] = GFCSS_P_BORDER_TOP_STYLE + k;
        out[n++] = GFCSS_P_BORDER_TOP_COLOR + k;
        break;
    case SH_BORDER_WIDTH: for (k = 0; k < 4; ++k) out[n++] = GFCSS_P_BORDER_TOP_WIDTH + k; break;
    case SH_BORDER_STYLE: for (k = 0; k < 4; ++k) out[n++] = GFCSS_P_BORDER_TOP_STYLE + k; break;
    case SH_BORDER_COLOR: for (k = 0; k < 4; ++k) out[n++] = GFCSS_P_BORDER_TOP_COLOR + k; break;
    case SH_BORDER_RADIUS: for (k = 0; k < 4; ++k) out[n++] = GFCSS_P_BORDER_TOP_LEFT_RADIUS + k; break;
    case SH_OUTLINE:
        out[n++] = GFCSS_P_OUTLINE_WIDTH; out[n++] = GFCSS_P_OUTLINE_STYLE; out[n++] = GFCSS_P_OUTLINE_COLOR;
        break;
    case SH_BACKGROUND:
        out[n++] = GFCSS_P_BACKGROUND_COLOR; out[n++] = GFCSS_P_BACKGROUND_IMAGE;
        out[n++] = GFCSS_P_BACKGROUND_REPEAT; out[n++] = GFCSS_P_BACKGROUND_POSITION_X;
        out[n++] = GFCSS_P_BACKGROUND_POSITION_Y;
        break;
    case SH_BACKGROUND_POSITION:
        out[n++] = GFCSS_P_BACKGROUND_POSITION_X; out[n++] = GFCSS_P_BACKGROUND_POSITION_Y;
        break;
    case SH_FONT:
        out[n++] = GFCSS_P_FONT_STYLE; out[n++] = GFCSS_P_FONT_VARIANT; out[n++] = GFCSS_P_FONT_WEIGHT;
        out[n++] = GFCSS_P_FONT_SIZE; out[n++] = GFCSS_P_LINE_HEIGHT; out[n++] = GFCSS_P_FONT_FAMILY;
        out[n++] = GFCSS_P_FONT_GENERIC;
        break;
    case SH_LIST_STYLE:
        out[n++] = GFCSS_P_LIST_STYLE_TYPE; out[n++] = GFCSS_P_LIST_STYLE_POSITION;
        out[n++] = GFCSS_P_LIST_STYLE_IMAGE;
        break;
    case SH_FLEX:
        out[n++] = GFCSS_P_FLEX_GROW; out[n++] = GFCSS_P_FLEX_SHRINK; out[n++] = GFCSS_P_FLEX_BASIS;
        break;
    case SH_FLEX_FLOW: out[n++] = GFCSS_P_FLEX_DIRECTION; out[n++] = GFCSS_P_FLEX_WRAP; break;
    case SH_GAP: out[n++] = GFCSS_P_ROW_GAP; out[n++] = GFCSS_P_COLUMN_GAP; break;
    case SH_OVERFLOW: out[n++] = GFCSS_P_OVERFLOW_X; out[n++] = GFCSS_P_OVERFLOW_Y; break;
    case SH_TEXT_DECORATION: out[n++] = GFCSS_P_TEXT_DECORATION_LINE; break;
    case SH_BORDER_SPACING: out[n++] = GFCSS_P_BORDER_SPACING_H; out[n++] = GFCSS_P_BORDER_SPACING_V; break;
    default: break;
    }
    return n;
}

/* ---- One longhand's value ------------------------------------------------------ */

static int kw_value(vctx *c, int *i, int p, const kwent *tab, int n)
{
    int v;
    if (!kw_at(c, *i, tab, n, &v)) return 0;
    ++*i;
    emit(c, p, V_KW, v, 0);
    return 1;
}

static int len_value(vctx *c, int *i, int p, unsigned flags)
{
    gval g;
    if (!length_at(c, i, flags, &g)) return 0;
    emit_val(c, p, &g);
    return 1;
}

static int colour_value(vctx *c, int *i, int p)
{
    uint32_t argb = 0;
    int cur;
    if (!colour_at(c, i, &argb, &cur)) return 0;
    if (cur) emit(c, p, V_CURRENT, 0, 0);
    else emit(c, p, V_COLOUR, (int32_t)argb, 0);
    return 1;
}

static int border_width_at(vctx *c, int *i, gval *g)
{
    int v;
    if (kw_at(c, *i, KW(kw_border_width), &v)) {
        ++*i;
        g->unit = V_ABS;
        g->v = v;
        g->w = 0;
        return 1;
    }
    return length_at(c, i, 0, g);
}

/* an image: none, url(), or a function we keep as written */
static int image_at(vctx *c, int *i, gval *g)
{
    const vtok *t;
    if (*i >= c->n) return 0;
    t = &c->t[*i];
    if (t->type == T_IDENT && g_ieq(t->s, t->n, "none")) {
        g->unit = V_KW;
        g->v = 0;
        g->w = 0;
        ++*i;
        return 1;
    }
    if (t->type == T_URL) {
        g->unit = V_URL;
        g->v = text_offset(c, t->s);
        g->w = (int32_t)t->n;
        ++*i;
        return 1;
    }
    if (t->type == T_FUNC && g_ieq(t->s, t->n, "url")) {
        const vtok *s = *i + 1 < c->n ? &c->t[*i + 1] : NULL;
        if (!s || s->type != T_STRING || t->close != *i + 2) return 0;
        g->unit = V_URL;
        g->v = text_offset(c, s->s);
        g->w = (int32_t)s->n;
        *i = after(c, *i);
        return 1;
    }
    if (t->type == VT_RAWFUNC) {
        size_t n = t->n;
        /* gradients and the like, kept whole for the caller */
        if (!(g_ieq(t->s, n, "linear-gradient") || g_ieq(t->s, n, "radial-gradient") ||
              g_ieq(t->s, n, "repeating-linear-gradient") || g_ieq(t->s, n, "repeating-radial-gradient") ||
              g_ieq(t->s, n, "conic-gradient") || g_ieq(t->s, n, "image") || g_ieq(t->s, n, "image-set") ||
              g_ieq(t->s, n, "-webkit-linear-gradient") || g_ieq(t->s, n, "-webkit-gradient") ||
              g_ieq(t->s, n, "-moz-linear-gradient") || g_ieq(t->s, n, "cross-fade")))
            return 0;
        g->unit = V_FUNC;
        g->v = text_offset(c, t->start);
        g->w = (int32_t)(t->stop - t->start);
        ++*i;
        return 1;
    }
    return 0;
}

/* background-position's one or two values: across then down (or keywords either way round) */
static int position_at(vctx *c, int *i, gval *x, gval *y)
{
    gval v[2];
    int axis[2], n = 0;     /* axis: 0 either, 1 across, 2 down */
    while (n < 2 && *i < c->n) {
        const vtok *t = &c->t[*i];
        if (t->type == T_IDENT) {
            int32_t pc;
            int ax;
            if (g_ieq(t->s, t->n, "left")) { pc = 0; ax = 1; }
            else if (g_ieq(t->s, t->n, "right")) { pc = 100000; ax = 1; }
            else if (g_ieq(t->s, t->n, "top")) { pc = 0; ax = 2; }
            else if (g_ieq(t->s, t->n, "bottom")) { pc = 100000; ax = 2; }
            else if (g_ieq(t->s, t->n, "center")) { pc = 50000; ax = 0; }
            else break;
            v[n].unit = V_PCT;
            v[n].v = pc;
            v[n].w = 0;
            axis[n++] = ax;
            ++*i;
        } else if (length_at(c, i, L_PCT | L_NEG, &v[n])) {
            axis[n++] = 0;
        } else {
            break;
        }
    }
    if (n == 0) return 0;
    if (n == 1) {
        v[1].unit = V_PCT;
        v[1].v = 50000;
        v[1].w = 0;
        axis[1] = 0;
        if (axis[0] == 2) {
            *x = v[1];
            *y = v[0];
            return 1;
        }
    } else if (axis[0] == 2 || axis[1] == 1) {
        if (axis[0] == axis[1] && axis[0]) return 0;
        *x = v[1];
        *y = v[0];
        return 1;
    } else if (axis[0] == axis[1] && axis[0]) {
        return 0;
    }
    *x = v[0];
    *y = v[1];
    return 1;
}

static int family_generic(const char *s, size_t n)
{
    size_t k;
    for (k = 0; k < sizeof(kw_families) / sizeof(kw_families[0]); ++k)
        if (g_ieq(s, n, kw_families[k].name)) return kw_families[k].v;
    return GFCSS_FONT_DEFAULT;
}

/* font-family: the first family's name and the generic the list comes to */
static int family_at(vctx *c, int *i)
{
    int generic = GFCSS_FONT_DEFAULT, first = 1, have_first = 0;
    const char *fs = NULL;
    size_t fn = 0;
    while (*i < c->n) {
        const vtok *t = &c->t[*i];
        if (t->type == T_STRING) {
            if (first) { fs = t->s; fn = t->n; have_first = 1; }
            if (generic == GFCSS_FONT_DEFAULT && first) {
                char nm[32];
                gfcss_unescape(t->s, t->n, nm, sizeof(nm));
                generic = family_generic(nm, strlen(nm));
            }
            ++*i;
        } else if (t->type == T_IDENT) {
            /* a name of idents: up to the comma */
            const char *start = t->start, *stop = t->stop;
            int g, words = 0, gen = 0;
            char nm[40];
            size_t len = 0;
            while (*i < c->n && c->t[*i].type == T_IDENT) {
                const vtok *w = &c->t[*i];
                if (words == 0 && kw_at(c, *i, KW(kw_generic), &g)) gen = g;
                if (len + w->n + 2 < sizeof(nm)) {
                    if (len) nm[len++] = ' ';
                    memcpy(nm + len, w->s, w->n);
                    len += w->n;
                }
                stop = w->stop;
                ++words;
                ++*i;
            }
            nm[len] = 0;
            if (words == 1 && gen) {
                if (generic == GFCSS_FONT_DEFAULT) generic = gen;
                if (first) { fs = start; fn = (size_t)(stop - start); have_first = 1; }
            } else {
                if (first) {
                    fs = start;
                    fn = (size_t)(stop - start);
                    have_first = 1;
                    if (generic == GFCSS_FONT_DEFAULT) generic = family_generic(nm, len);
                }
            }
        } else {
            return 0;
        }
        first = 0;
        if (*i < c->n && c->t[*i].type == T_COMMA) {
            ++*i;
            if (*i >= c->n) return 0;
            continue;
        }
        break;
    }
    if (!have_first) return 0;
    emit(c, GFCSS_P_FONT_FAMILY, V_TEXT, text_offset(c, fs), (int32_t)fn);
    emit(c, GFCSS_P_FONT_GENERIC, V_KW, generic, 0);
    return 1;
}

static int font_size_at(vctx *c, int *i, gval *g)
{
    int v;
    if (kw_at(c, *i, KW(kw_font_size), &v)) {
        g->unit = V_ABS;
        g->v = v;
        g->w = 0;
        ++*i;
        return 1;
    }
    if (is_ident(c, *i, "smaller") || is_ident(c, *i, "larger")) {
        g->unit = V_KW;
        g->v = is_ident(c, *i, "smaller") ? FS_SMALLER : FS_LARGER;
        g->w = 0;
        ++*i;
        return 1;
    }
    return length_at(c, i, L_PCT, g);
}

static int font_weight_at(vctx *c, int *i, gval *g)
{
    const vtok *t;
    if (*i >= c->n) return 0;
    t = &c->t[*i];
    g->w = 0;
    if (t->type == T_NUMBER) {
        if (t->num < 1000 || t->num > 1000000) return 0;
        g->unit = V_INT;
        g->v = t->num / 1000;
    } else if (is_ident(c, *i, "normal")) {
        g->unit = V_INT;
        g->v = 400;
    } else if (is_ident(c, *i, "bold")) {
        g->unit = V_INT;
        g->v = 700;
    } else if (is_ident(c, *i, "bolder") || is_ident(c, *i, "lighter")) {
        g->unit = V_KW;
        g->v = is_ident(c, *i, "bolder") ? FW_BOLDER : FW_LIGHTER;
    } else {
        return 0;
    }
    ++*i;
    return 1;
}

static int line_height_at(vctx *c, int *i, gval *g)
{
    return length_at(c, i, L_PCT | L_NORMAL | L_NUMBER, g);
}

/* Fills four sides from one to four values; *count is how many there were */
static int sides(vctx *c, int *i, int (*one)(vctx *, int *, unsigned, gval *), unsigned flags, gval out[4])
{
    int n = 0;
    while (n < 4 && *i < c->n && one(c, i, flags, &out[n])) ++n;
    if (!n) return 0;
    if (n < 2) out[1] = out[0];
    if (n < 3) out[2] = out[0];
    if (n < 4) out[3] = out[1];
    return 1;
}

static int len_one(vctx *c, int *i, unsigned flags, gval *g)
{
    return length_at(c, i, flags, g);
}

static int bw_one(vctx *c, int *i, unsigned flags, gval *g)
{
    (void)flags;
    return border_width_at(c, i, g);
}

static int bs_one(vctx *c, int *i, unsigned flags, gval *g)
{
    int v;
    (void)flags;
    if (!kw_at(c, *i, KW(kw_border_style), &v)) return 0;
    ++*i;
    g->unit = V_KW;
    g->v = v;
    g->w = 0;
    return 1;
}

static int bc_one(vctx *c, int *i, unsigned flags, gval *g)
{
    uint32_t argb = 0;
    int cur;
    (void)flags;
    if (!colour_at(c, i, &argb, &cur)) return 0;
    g->unit = cur ? V_CURRENT : V_COLOUR;
    g->v = (int32_t)argb;
    g->w = 0;
    return 1;
}

/* width style colour in any order (border, border-top, outline) */
static int border_parts(vctx *c, int *i, gval *w, gval *s, gval *col, const kwent *styles, int nstyles)
{
    int hw = 0, hs = 0, hc = 0;
    w->unit = V_ABS; w->v = OL_CSSPX(3); w->w = 0;
    s->unit = V_KW; s->v = GFCSS_BORDER_NONE; s->w = 0;
    col->unit = V_CURRENT; col->v = 0; col->w = 0;
    while (*i < c->n) {
        int v;
        if (!hw && border_width_at(c, i, w)) { hw = 1; continue; }
        if (!hs && kw_at(c, *i, styles, nstyles, &v)) {
            s->v = v;
            ++*i;
            hs = 1;
            continue;
        }
        if (!hc && bc_one(c, i, 0, col)) { hc = 1; continue; }
        return 0;
    }
    return hw || hs || hc;
}

/* a longhand's value: 1 when it is one, emitted */
static int longhand(vctx *c, int *i, int p)
{
    gval g;
    switch (p) {
    case GFCSS_P_DISPLAY: return kw_value(c, i, p, KW(kw_display));
    case GFCSS_P_POSITION: return kw_value(c, i, p, KW(kw_position));
    case GFCSS_P_FLOAT: return kw_value(c, i, p, KW(kw_float));
    case GFCSS_P_CLEAR: return kw_value(c, i, p, KW(kw_clear));
    case GFCSS_P_Z_INDEX:
        if (is_ident(c, *i, "auto")) {
            ++*i;
            emit(c, p, V_KW, LK_AUTO, 0);
            return 1;
        }
        if (*i < c->n && c->t[*i].type == T_NUMBER && c->t[*i].is_int) {
            emit(c, p, V_INT, c->t[*i].num / 1000, 0);
            ++*i;
            return 1;
        }
        return 0;
    case GFCSS_P_VISIBILITY: return kw_value(c, i, p, KW(kw_visibility));
    case GFCSS_P_OVERFLOW_X: case GFCSS_P_OVERFLOW_Y: return kw_value(c, i, p, KW(kw_overflow));
    case GFCSS_P_BOX_SIZING: return kw_value(c, i, p, KW(kw_box_sizing));
    case GFCSS_P_WIDTH: case GFCSS_P_HEIGHT: case GFCSS_P_MIN_WIDTH: case GFCSS_P_MIN_HEIGHT:
        return len_value(c, i, p, L_PCT | L_AUTO);
    case GFCSS_P_MAX_WIDTH: case GFCSS_P_MAX_HEIGHT: return len_value(c, i, p, L_PCT | L_NONE);
    case GFCSS_P_MARGIN_TOP: case GFCSS_P_MARGIN_RIGHT: case GFCSS_P_MARGIN_BOTTOM: case GFCSS_P_MARGIN_LEFT:
        return len_value(c, i, p, L_PCT | L_AUTO | L_NEG);
    case GFCSS_P_PADDING_TOP: case GFCSS_P_PADDING_RIGHT: case GFCSS_P_PADDING_BOTTOM: case GFCSS_P_PADDING_LEFT:
        return len_value(c, i, p, L_PCT);
    case GFCSS_P_BORDER_TOP_WIDTH: case GFCSS_P_BORDER_RIGHT_WIDTH: case GFCSS_P_BORDER_BOTTOM_WIDTH:
    case GFCSS_P_BORDER_LEFT_WIDTH: case GFCSS_P_OUTLINE_WIDTH:
        if (!border_width_at(c, i, &g)) return 0;
        emit_val(c, p, &g);
        return 1;
    case GFCSS_P_BORDER_TOP_STYLE: case GFCSS_P_BORDER_RIGHT_STYLE: case GFCSS_P_BORDER_BOTTOM_STYLE:
    case GFCSS_P_BORDER_LEFT_STYLE:
        return kw_value(c, i, p, KW(kw_border_style));
    case GFCSS_P_OUTLINE_STYLE: return kw_value(c, i, p, KW(kw_outline_style));
    case GFCSS_P_BORDER_TOP_COLOR: case GFCSS_P_BORDER_RIGHT_COLOR: case GFCSS_P_BORDER_BOTTOM_COLOR:
    case GFCSS_P_BORDER_LEFT_COLOR: case GFCSS_P_COLOR: case GFCSS_P_BACKGROUND_COLOR:
        return colour_value(c, i, p);
    case GFCSS_P_OUTLINE_COLOR:
        if (is_ident(c, *i, "invert")) {
            ++*i;
            emit(c, p, V_CURRENT, 0, 0);
            return 1;
        }
        return colour_value(c, i, p);
    case GFCSS_P_BORDER_TOP_LEFT_RADIUS: case GFCSS_P_BORDER_TOP_RIGHT_RADIUS:
    case GFCSS_P_BORDER_BOTTOM_RIGHT_RADIUS: case GFCSS_P_BORDER_BOTTOM_LEFT_RADIUS:
        if (!length_at(c, i, L_PCT, &g)) return 0;
        length_at(c, i, L_PCT, &(gval){ 0, 0, 0 });     /* an ellipse's second radius: the first is kept */
        emit_val(c, p, &g);
        return 1;
    case GFCSS_P_BACKGROUND_IMAGE: case GFCSS_P_LIST_STYLE_IMAGE:
        if (!image_at(c, i, &g)) return 0;
        emit_val(c, p, &g);
        return 1;
    case GFCSS_P_BACKGROUND_REPEAT: {
        int a, b;
        if (!kw_at(c, *i, KW(kw_repeat), &a)) return 0;
        ++*i;
        if (kw_at(c, *i, KW(kw_repeat), &b)) {
            /* two values: across and down */
            ++*i;
            if (a == GFCSS_REPEAT && b == GFCSS_NO_REPEAT) a = GFCSS_REPEAT_X;
            else if (a == GFCSS_NO_REPEAT && b == GFCSS_REPEAT) a = GFCSS_REPEAT_Y;
            else if (a == GFCSS_NO_REPEAT || b == GFCSS_NO_REPEAT) a = GFCSS_NO_REPEAT;
        }
        emit(c, p, V_KW, a, 0);
        return 1;
    }
    case GFCSS_P_BACKGROUND_POSITION_X: case GFCSS_P_BACKGROUND_POSITION_Y:
        return len_value(c, i, p, L_PCT | L_NEG);
    case GFCSS_P_OPACITY: {
        const vtok *t = *i < c->n ? &c->t[*i] : NULL;
        int32_t v;
        if (!t) return 0;
        if (t->type == T_NUMBER) v = t->num;
        else if (t->type == T_PERCENT) v = t->num / 100;
        else return 0;
        if (v < 0) v = 0;
        if (v > 1000) v = 1000;
        emit(c, p, V_NUM, v, 0);
        ++*i;
        return 1;
    }
    case GFCSS_P_FONT_FAMILY: return family_at(c, i);
    case GFCSS_P_FONT_SIZE:
        if (!font_size_at(c, i, &g)) return 0;
        emit_val(c, p, &g);
        return 1;
    case GFCSS_P_FONT_WEIGHT:
        if (!font_weight_at(c, i, &g)) return 0;
        emit_val(c, p, &g);
        return 1;
    case GFCSS_P_FONT_STYLE:
        if (!kw_value(c, i, p, KW(kw_font_style))) return 0;
        if (*i < c->n && c->t[*i].type == T_DIM) ++*i;     /* oblique 10deg */
        return 1;
    case GFCSS_P_FONT_VARIANT: return kw_value(c, i, p, KW(kw_font_variant));
    case GFCSS_P_LINE_HEIGHT:
        if (!line_height_at(c, i, &g)) return 0;
        emit_val(c, p, &g);
        return 1;
    case GFCSS_P_TEXT_ALIGN: return kw_value(c, i, p, KW(kw_text_align));
    case GFCSS_P_TEXT_INDENT: return len_value(c, i, p, L_PCT | L_NEG);
    case GFCSS_P_TEXT_DECORATION_LINE: {
        int v, bits = 0, any = 0;
        if (is_ident(c, *i, "none")) {
            ++*i;
            emit(c, p, V_KW, 0, 0);
            return 1;
        }
        while (kw_at(c, *i, KW(kw_decoration), &v)) {
            if (bits & v) return 0;
            bits |= v;
            any = 1;
            ++*i;
        }
        if (!any) return 0;
        emit(c, p, V_KW, bits, 0);
        return 1;
    }
    case GFCSS_P_TEXT_TRANSFORM: return kw_value(c, i, p, KW(kw_transform));
    case GFCSS_P_WHITE_SPACE: return kw_value(c, i, p, KW(kw_white_space));
    case GFCSS_P_VERTICAL_ALIGN:
        if (kw_value(c, i, p, KW(kw_valign))) return 1;
        if (!length_at(c, i, L_PCT | L_NEG, &g)) return 0;
        emit_val(c, p, &g);
        return 1;
    case GFCSS_P_LIST_STYLE_TYPE:
        if (kw_value(c, i, p, KW(kw_list_type))) return 1;
        if (*i < c->n && (c->t[*i].type == T_IDENT || c->t[*i].type == T_STRING)) {
            /* another counter style: numbers */
            ++*i;
            emit(c, p, V_KW, GFCSS_LIST_DECIMAL, 0);
            return 1;
        }
        return 0;
    case GFCSS_P_LIST_STYLE_POSITION: return kw_value(c, i, p, KW(kw_list_position));
    case GFCSS_P_LETTER_SPACING: case GFCSS_P_WORD_SPACING:
        return len_value(c, i, p, L_NEG | L_NORMAL | (p == GFCSS_P_WORD_SPACING ? L_PCT : 0));
    case GFCSS_P_CURSOR:
        /* url(...) x y, ... then a keyword: the keyword is kept */
        while (*i < c->n && (c->t[*i].type == T_URL || (c->t[*i].type == T_FUNC && is_ident(c, *i, "url")) ||
                             c->t[*i].type == T_FUNC)) {
            *i = c->t[*i].type == T_URL ? *i + 1 : after(c, *i);
            while (*i < c->n && c->t[*i].type == T_NUMBER) ++*i;
            if (*i >= c->n || c->t[*i].type != T_COMMA) return 0;
            ++*i;
        }
        return kw_value(c, i, p, KW(kw_cursor));
    case GFCSS_P_FLEX_DIRECTION: return kw_value(c, i, p, KW(kw_direction));
    case GFCSS_P_FLEX_WRAP: return kw_value(c, i, p, KW(kw_wrap));
    case GFCSS_P_FLEX_GROW: case GFCSS_P_FLEX_SHRINK:
        if (*i < c->n && c->t[*i].type == T_NUMBER && c->t[*i].num >= 0) {
            emit(c, p, V_NUM, c->t[*i].num, 0);
            ++*i;
            return 1;
        }
        return 0;
    case GFCSS_P_FLEX_BASIS: return len_value(c, i, p, L_PCT | L_AUTO | L_CONTENT);
    case GFCSS_P_ROW_GAP: case GFCSS_P_COLUMN_GAP: return len_value(c, i, p, L_PCT | L_NORMAL);
    case GFCSS_P_ALIGN_ITEMS: case GFCSS_P_JUSTIFY_CONTENT: {
        int v;
        if (is_ident(c, *i, "safe") || is_ident(c, *i, "unsafe")) ++*i;
        if (is_ident(c, *i, "first") || is_ident(c, *i, "last")) ++*i;
        if (!kw_at(c, *i, KW(kw_flex_align), &v)) return 0;
        if (p == GFCSS_P_JUSTIFY_CONTENT && (v == GFCSS_FLEX_BASELINE)) return 0;
        if (p == GFCSS_P_ALIGN_ITEMS && (v == GFCSS_FLEX_SPACE_BETWEEN || v == GFCSS_FLEX_SPACE_AROUND ||
                                         v == GFCSS_FLEX_SPACE_EVENLY || v == GFCSS_FLEX_LEFT ||
                                         v == GFCSS_FLEX_RIGHT))
            return 0;
        ++*i;
        emit(c, p, V_KW, v, 0);
        return 1;
    }
    case GFCSS_P_BORDER_COLLAPSE: return kw_value(c, i, p, KW(kw_collapse));
    case GFCSS_P_CAPTION_SIDE: return kw_value(c, i, p, KW(kw_caption));
    case GFCSS_P_CONTENT:
        if (is_ident(c, *i, "normal") || is_ident(c, *i, "none")) {
            emit(c, p, V_KW, is_ident(c, *i, "none") ? GFCSS_TEXT_NONE : GFCSS_TEXT_NORMAL, 0);
            ++*i;
            return 1;
        }
        if (*i < c->n && c->t[*i].type == T_STRING) {
            const vtok *t = &c->t[*i];
            emit(c, p, V_TEXT, text_offset(c, t->s), (int32_t)t->n);
            /* strings after the first are read and left out */
            while (*i < c->n && c->t[*i].type == T_STRING) ++*i;
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static int shorthand(vctx *c, int *i, int sh)
{
    gval v[4];
    int k;
    switch (sh) {
    case SH_MARGIN:
        if (!sides(c, i, len_one, L_PCT | L_AUTO | L_NEG, v)) return 0;
        for (k = 0; k < 4; ++k) emit_val(c, GFCSS_P_MARGIN_TOP + k, &v[k]);
        return 1;
    case SH_PADDING:
        if (!sides(c, i, len_one, L_PCT, v)) return 0;
        for (k = 0; k < 4; ++k) emit_val(c, GFCSS_P_PADDING_TOP + k, &v[k]);
        return 1;
    case SH_BORDER_WIDTH:
        if (!sides(c, i, bw_one, 0, v)) return 0;
        for (k = 0; k < 4; ++k) emit_val(c, GFCSS_P_BORDER_TOP_WIDTH + k, &v[k]);
        return 1;
    case SH_BORDER_STYLE:
        if (!sides(c, i, bs_one, 0, v)) return 0;
        for (k = 0; k < 4; ++k) emit_val(c, GFCSS_P_BORDER_TOP_STYLE + k, &v[k]);
        return 1;
    case SH_BORDER_COLOR:
        if (!sides(c, i, bc_one, 0, v)) return 0;
        for (k = 0; k < 4; ++k) emit_val(c, GFCSS_P_BORDER_TOP_COLOR + k, &v[k]);
        return 1;
    case SH_BORDER_RADIUS:
        if (!sides(c, i, len_one, L_PCT, v)) return 0;
        if (is_delim(c, *i, '/')) {     /* the vertical radii: read, the horizontal kept */
            gval w[4];
            ++*i;
            if (!sides(c, i, len_one, L_PCT, w)) return 0;
        }
        for (k = 0; k < 4; ++k) emit_val(c, GFCSS_P_BORDER_TOP_LEFT_RADIUS + k, &v[k]);
        return 1;
    case SH_BORDER: case SH_BORDER_TOP: case SH_BORDER_RIGHT: case SH_BORDER_BOTTOM: case SH_BORDER_LEFT: {
        gval w, s, col;
        int from = sh == SH_BORDER ? 0 : sh - SH_BORDER_TOP, to = sh == SH_BORDER ? 4 : from + 1;
        if (!border_parts(c, i, &w, &s, &col, KW(kw_border_style))) return 0;
        for (k = from; k < to; ++k) {
            emit_val(c, GFCSS_P_BORDER_TOP_WIDTH + k, &w);
            emit_val(c, GFCSS_P_BORDER_TOP_STYLE + k, &s);
            emit_val(c, GFCSS_P_BORDER_TOP_COLOR + k, &col);
        }
        return 1;
    }
    case SH_OUTLINE: {
        gval w, s, col;
        if (!border_parts(c, i, &w, &s, &col, KW(kw_outline_style))) return 0;
        if (col.unit == V_CURRENT && is_ident(c, *i, "invert")) ++*i;
        emit_val(c, GFCSS_P_OUTLINE_WIDTH, &w);
        emit_val(c, GFCSS_P_OUTLINE_STYLE, &s);
        emit_val(c, GFCSS_P_OUTLINE_COLOR, &col);
        return 1;
    }
    case SH_BACKGROUND: {
        /* layers by commas: the first's image, repeat and position; the last's colour */
        gval col, img, rep, px, py;
        int layer = 0;
        col.unit = V_COLOUR; col.v = 0; col.w = 0;
        img.unit = V_KW; img.v = 0; img.w = 0;
        rep.unit = V_KW; rep.v = GFCSS_REPEAT; rep.w = 0;
        px.unit = V_PCT; px.v = 0; px.w = 0;
        py = px;
        for (;;) {
            int hc = 0, hi = 0, hr = 0, hp = 0, any = 0;
            while (*i < c->n && c->t[*i].type != T_COMMA) {
                gval g, gx, gy;
                int r;
                any = 1;
                if (!hi && image_at(c, i, &g)) { if (!layer) img = g; hi = 1; continue; }
                if (!hr && kw_at(c, *i, KW(kw_repeat), &r)) {
                    ++*i;
                    if (kw_at(c, *i, KW(kw_repeat), &r)) ++*i;
                    if (!layer) { rep.unit = V_KW; rep.v = r; rep.w = 0; }
                    hr = 1;
                    continue;
                }
                if (!hp && position_at(c, i, &gx, &gy)) {
                    if (!layer) { px = gx; py = gy; }
                    hp = 1;
                    if (is_delim(c, *i, '/')) {
                        /* background-size: read and left out */
                        ++*i;
                        if (!(is_ident(c, *i, "cover") || is_ident(c, *i, "contain"))) {
                            gval s1;
                            if (!length_at(c, i, L_PCT | L_AUTO, &s1)) return 0;
                            length_at(c, i, L_PCT | L_AUTO, &s1);
                        } else {
                            ++*i;
                        }
                    }
                    continue;
                }
                if (!hc && bc_one(c, i, 0, &g)) { col = g; hc = 1; continue; }
                if (is_ident(c, *i, "scroll") || is_ident(c, *i, "fixed") || is_ident(c, *i, "local") ||
                    is_ident(c, *i, "border-box") || is_ident(c, *i, "padding-box") ||
                    is_ident(c, *i, "content-box") || is_ident(c, *i, "text")) {
                    ++*i;
                    continue;
                }
                return 0;
            }
            if (!any) return 0;
            if (*i >= c->n) break;
            if (hc) return 0;           /* a colour only in the last layer */
            ++*i;
            ++layer;
            col.unit = V_COLOUR;
            col.v = 0;
        }
        emit_val(c, GFCSS_P_BACKGROUND_COLOR, &col);
        emit_val(c, GFCSS_P_BACKGROUND_IMAGE, &img);
        emit_val(c, GFCSS_P_BACKGROUND_REPEAT, &rep);
        emit_val(c, GFCSS_P_BACKGROUND_POSITION_X, &px);
        emit_val(c, GFCSS_P_BACKGROUND_POSITION_Y, &py);
        return 1;
    }
    case SH_BACKGROUND_POSITION: {
        gval x, y;
        if (!position_at(c, i, &x, &y)) return 0;
        while (*i < c->n && c->t[*i].type == T_COMMA) {     /* further layers: read, left out */
            gval a, b;
            ++*i;
            if (!position_at(c, i, &a, &b)) return 0;
        }
        emit_val(c, GFCSS_P_BACKGROUND_POSITION_X, &x);
        emit_val(c, GFCSS_P_BACKGROUND_POSITION_Y, &y);
        return 1;
    }
    case SH_FONT: {
        gval style, variant, weight, size, lh;
        int hs = 0, hv = 0, hw = 0, n = 0, kv;
        style.unit = V_KW; style.v = GFCSS_FONT_NORMAL; style.w = 0;
        variant.unit = V_KW; variant.v = GFCSS_VARIANT_NORMAL; variant.w = 0;
        weight.unit = V_INT; weight.v = 400; weight.w = 0;
        lh.unit = V_KW; lh.v = LK_NORMAL; lh.w = 0;
        /* style, variant and weight in any order (normal fits any), then the size */
        while (n < 4 && *i < c->n) {
            if (is_ident(c, *i, "normal")) { ++*i; ++n; continue; }
            if (!hs && kw_at(c, *i, KW(kw_font_style), &kv)) { style.v = kv; ++*i; hs = 1; ++n; continue; }
            if (!hv && kw_at(c, *i, KW(kw_font_variant), &kv)) { variant.v = kv; ++*i; hv = 1; ++n; continue; }
            if (!hw && (c->t[*i].type != T_NUMBER || c->t[*i].num % 1000 == 0) && font_weight_at(c, i, &weight)) {
                hw = 1;
                ++n;
                continue;
            }
            if (is_ident(c, *i, "ultra-condensed") || is_ident(c, *i, "extra-condensed") ||
                is_ident(c, *i, "condensed") || is_ident(c, *i, "semi-condensed") ||
                is_ident(c, *i, "semi-expanded") || is_ident(c, *i, "expanded") ||
                is_ident(c, *i, "extra-expanded") || is_ident(c, *i, "ultra-expanded")) {
                ++*i;
                ++n;
                continue;
            }
            break;
        }
        if (!font_size_at(c, i, &size)) return 0;
        if (is_delim(c, *i, '/')) {
            ++*i;
            if (!line_height_at(c, i, &lh)) return 0;
        }
        emit_val(c, GFCSS_P_FONT_STYLE, &style);
        emit_val(c, GFCSS_P_FONT_VARIANT, &variant);
        emit_val(c, GFCSS_P_FONT_WEIGHT, &weight);
        emit_val(c, GFCSS_P_FONT_SIZE, &size);
        emit_val(c, GFCSS_P_LINE_HEIGHT, &lh);
        return family_at(c, i);
    }
    case SH_LIST_STYLE: {
        int ht = 0, hp = 0, hi = 0, nones = 0, v;
        gval type, pos, img;
        type.unit = V_KW; type.v = GFCSS_LIST_DISC; type.w = 0;
        pos.unit = V_KW; pos.v = GFCSS_LIST_OUTSIDE; pos.w = 0;
        img.unit = V_KW; img.v = 0; img.w = 0;
        while (*i < c->n) {
            gval g;
            if (is_ident(c, *i, "none")) { ++nones; ++*i; continue; }
            if (!hp && kw_at(c, *i, KW(kw_list_position), &v)) { pos.v = v; hp = 1; ++*i; continue; }
            if (!hi && image_at(c, i, &g)) { img = g; hi = 1; continue; }
            if (!ht && kw_at(c, *i, KW(kw_list_type), &v)) { type.v = v; ht = 1; ++*i; continue; }
            return 0;
        }
        if (nones > 2 || (nones == 2 && (ht || hi))) return 0;
        if (nones && !ht) type.v = GFCSS_LIST_NONE;
        emit_val(c, GFCSS_P_LIST_STYLE_TYPE, &type);
        emit_val(c, GFCSS_P_LIST_STYLE_POSITION, &pos);
        emit_val(c, GFCSS_P_LIST_STYLE_IMAGE, &img);
        return 1;
    }
    case SH_FLEX: {
        gval basis;
        int32_t grow = 1000, shrink = 1000;
        int nn = 0, hb = 0;
        basis.unit = V_ABS; basis.v = 0; basis.w = 0;
        if (is_ident(c, *i, "none") || is_ident(c, *i, "auto")) {
            int none = is_ident(c, *i, "none");
            ++*i;
            emit(c, GFCSS_P_FLEX_GROW, V_NUM, none ? 0 : 1000, 0);
            emit(c, GFCSS_P_FLEX_SHRINK, V_NUM, none ? 0 : 1000, 0);
            emit(c, GFCSS_P_FLEX_BASIS, V_KW, LK_AUTO, 0);
            return 1;
        }
        while (*i < c->n) {
            const vtok *t = &c->t[*i];
            if (t->type == T_NUMBER && t->num != 0 && nn < 2 && t->num > 0) {
                if (nn == 0) grow = t->num;
                else shrink = t->num;
                ++nn;
                ++*i;
                continue;
            }
            if (t->type == T_NUMBER && t->num == 0 && nn < 2 && !(hb == 0 && nn == 2)) {
                if (nn == 0) grow = 0;
                else shrink = 0;
                ++nn;
                ++*i;
                continue;
            }
            if (!hb && length_at(c, i, L_PCT | L_AUTO | L_CONTENT, &basis)) { hb = 1; continue; }
            return 0;
        }
        if (!nn && !hb) return 0;
        if (!nn) grow = 1000;
        emit(c, GFCSS_P_FLEX_GROW, V_NUM, grow, 0);
        emit(c, GFCSS_P_FLEX_SHRINK, V_NUM, shrink, 0);
        emit_val(c, GFCSS_P_FLEX_BASIS, &basis);
        return 1;
    }
    case SH_FLEX_FLOW: {
        int d = GFCSS_ROW, w = GFCSS_NOWRAP, hd = 0, hw = 0, v;
        while (*i < c->n) {
            if (!hd && kw_at(c, *i, KW(kw_direction), &v)) { d = v; hd = 1; ++*i; continue; }
            if (!hw && kw_at(c, *i, KW(kw_wrap), &v)) { w = v; hw = 1; ++*i; continue; }
            return 0;
        }
        if (!hd && !hw) return 0;
        emit(c, GFCSS_P_FLEX_DIRECTION, V_KW, d, 0);
        emit(c, GFCSS_P_FLEX_WRAP, V_KW, w, 0);
        return 1;
    }
    case SH_GAP:
        if (!length_at(c, i, L_PCT | L_NORMAL, &v[0])) return 0;
        if (!length_at(c, i, L_PCT | L_NORMAL, &v[1])) v[1] = v[0];
        emit_val(c, GFCSS_P_ROW_GAP, &v[0]);
        emit_val(c, GFCSS_P_COLUMN_GAP, &v[1]);
        return 1;
    case SH_OVERFLOW: {
        int a, b;
        if (!kw_at(c, *i, KW(kw_overflow), &a)) return 0;
        ++*i;
        if (kw_at(c, *i, KW(kw_overflow), &b)) ++*i;
        else b = a;
        emit(c, GFCSS_P_OVERFLOW_X, V_KW, a, 0);
        emit(c, GFCSS_P_OVERFLOW_Y, V_KW, b, 0);
        return 1;
    }
    case SH_TEXT_DECORATION: {
        int bits = 0, none = 0, hc = 0, v, any = 0;
        while (*i < c->n) {
            gval g;
            if (is_ident(c, *i, "none")) { none = 1; ++*i; any = 1; continue; }
            if (kw_at(c, *i, KW(kw_decoration), &v)) { bits |= v; ++*i; any = 1; continue; }
            if (kw_at(c, *i, KW(kw_decoration_other), &v)) { ++*i; any = 1; continue; }
            if (!hc && bc_one(c, i, 0, &g)) { hc = 1; any = 1; continue; }
            if (length_at(c, i, L_PCT, &g)) { any = 1; continue; }     /* a thickness */
            return 0;
        }
        if (!any || (none && bits)) return 0;
        emit(c, GFCSS_P_TEXT_DECORATION_LINE, V_KW, bits, 0);
        return 1;
    }
    case SH_BORDER_SPACING:
        if (!length_at(c, i, 0, &v[0])) return 0;
        if (!length_at(c, i, 0, &v[1])) v[1] = v[0];
        emit_val(c, GFCSS_P_BORDER_SPACING_H, &v[0]);
        emit_val(c, GFCSS_P_BORDER_SPACING_V, &v[1]);
        return 1;
    default:
        return 0;
    }
}

int g_parse_declaration(gfcss_engine *e, gsink *sink, const char *name, size_t name_n,
                        const char *value, const char *value_end, int important)
{
    vctx *c;
    int p = -1, sh = 0, k, i = 0, ok = 0, ext = -1;
    size_t n0 = sink->n, c0 = sink->ncalc;
    char lname[40];
    if (name_n == 0 || name_n >= sizeof(lname)) return 0;
    for (k = 0; k < (int)name_n; ++k) lname[k] = (char)g_lower((unsigned char)name[k]);
    lname[name_n] = 0;
    if (lname[0] == '-' && lname[1] == '-') return 0;       /* custom properties: not done */
    for (k = 0; k < GFCSS_P_COUNT; ++k)
        if (props[k].name[0] && !strcmp(props[k].name, lname)) { p = k; break; }
    if (p < 0)
        for (k = 0; k < (int)(sizeof(shorthands) / sizeof(shorthands[0])); ++k)
            if (!strcmp(shorthands[k].name, lname)) { sh = shorthands[k].sh; break; }
    if (p < 0 && !sh)
        for (k = 0; k < (int)(sizeof(aliases) / sizeof(aliases[0])); ++k)
            if (!strcmp(aliases[k].name, lname)) { p = aliases[k].p; break; }
    if (p < 0 && !sh && e)
        for (k = 0; k < e->next; ++k)
            if (!strcmp(e->ext[k].name, lname)) { ext = k; break; }
    if (p < 0 && !sh && ext < 0) return 0;
    /* the engine's scratch, made once: nothing is allocated for a declaration */
    if (e && !e->vscratch) e->vscratch = malloc(sizeof(vctx));
    c = e ? (vctx *)e->vscratch : (vctx *)malloc(sizeof(vctx));
    if (!c) {
        sink->oom = 1;
        return 0;
    }
    c->e = e;
    c->sink = sink;
    c->important = important;
    if (!tokenize(c, value, value_end) || c->n == 0) goto done;
    if (c->n == 1 && c->t[0].type == T_IDENT &&
        (g_ieq(c->t[0].s, c->t[0].n, "inherit") || g_ieq(c->t[0].s, c->t[0].n, "initial") ||
         g_ieq(c->t[0].s, c->t[0].n, "unset") || g_ieq(c->t[0].s, c->t[0].n, "revert"))) {
        int unit = g_ieq(c->t[0].s, c->t[0].n, "inherit") ? V_INHERIT
                 : g_ieq(c->t[0].s, c->t[0].n, "initial") ? V_INITIAL : V_UNSET;
        int list[12], n;
        if (ext >= 0) {
            emit(c, GFCSS_P_COUNT + ext, unit, 0, 0);
        } else if (sh) {
            n = sh_longhands(sh, list);
            for (k = 0; k < n; ++k) emit(c, list[k], unit, 0, 0);
        } else {
            emit(c, p, unit, 0, 0);
            if (p == GFCSS_P_FONT_FAMILY) emit(c, GFCSS_P_FONT_GENERIC, unit, 0, 0);
        }
        ok = 1;
        goto done;
    }
    if (ext >= 0) {
        uint32_t v = 0;
        /* the caller's property: its own parser, on the value as written */
        while (value < value_end && g_ws((unsigned char)*value)) ++value;
        while (value_end > value && g_ws((unsigned char)value_end[-1])) --value_end;
        if (e->ext[ext].fn(e->ext[ext].userdata, ext, value, (size_t)(value_end - value), &v)) {
            emit(c, GFCSS_P_COUNT + ext, V_EXT, (int32_t)v, 0);
            ok = 1;
        }
        goto done;
    }
    ok = sh ? shorthand(c, &i, sh) : p >= 0 ? longhand(c, &i, p) : 0;
    if (ok && i != c->n) ok = 0;    /* something left over: the whole declaration is bad */
done:
    if (!e) free(c);
    if (!ok || sink->oom) {
        sink->n = n0;
        sink->ncalc = c0;
        return 0;
    }
    return 1;
}
