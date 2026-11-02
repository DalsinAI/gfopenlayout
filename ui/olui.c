/* GFOpenLayout UI: the widgets, their layout through GFOpenLayout, the
 * pointer and the keyboard, and drawing.
 *
 * The program's markup is kept as a tree of elements. Each layout styles
 * them (olui_css.c), builds a fresh GFOpenLayout document from them (one
 * node an element; a list's items, open or not, flattened under it) and
 * lays it out at the window's size. Drawing walks the elements back to
 * front: each one's box, its marks, then GFOpenLayout's text ops for its
 * node, a list's items cut to the list and moved by its scroll.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olui_internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PX(n) OL_CSSPX(n)
#define MAX_HANDLERS 128
#define INDENT 16           /* a tree level, CSS px */
#define BAR 6               /* a scroll bar's room, pixels */
#define POPUP_ROWS 12       /* rows a select's pop-up shows at most */
#define BULLET "\xe2\x80\xa2"   /* a password's character: U+2022 */

static const char ua_css[] =
    "window{display:flex;flex-direction:column;padding:6px;gap:6px;background:#e8e8e8;color:#000000}"
    "row{display:flex;gap:6px}"
    "column{display:flex;flex-direction:column;gap:6px}"
    "h1{font-size:20px;font-weight:bold}h2{font-size:16px;font-weight:bold}h3{font-weight:bold}"
    "span,b,strong,i,em,u,code,a,img{display:inline}"
    "b,strong{font-weight:bold}i,em{font-style:italic}u,a{text-decoration:underline}"
    "code{font-family:monospace}"
    "hr{height:1px;background:#909090}"
    "spacer{flex:1}"
    "button,tabbutton{padding:3px 10px;border:1px solid #808080;border-radius:3px;"
    "background:linear-gradient(#ffffff,#d6d6d6);text-align:center}"
    "button:active{background:linear-gradient(#c4c4c4,#e2e2e2)}"
    "button:focus,checkbox:focus,radio:focus,list:focus,tabbutton:focus{border-color:#3366cc}"
    "button:disabled,checkbox:disabled,radio:disabled,input:disabled,item:disabled{color:#9a9a9a}"
    "checkbox,radio{padding:2px 0}"
    "input{padding:3px 4px;border:1px solid #808080;background:#ffffff;width:160px}"
    "input:focus{border-color:#3366cc}"
    "list,tree{height:120px;border:1px solid #808080;background:#ffffff;padding:1px}"
    "item,li{padding:1px 4px}"
    "item:selected,li:selected{background:#3366cc;color:#ffffff}"
    "progress{height:14px;border:1px solid #808080;background:#ffffff;padding:1px}"
    "slider{height:16px;width:120px;padding:0 7px;accent-color:#3366cc;mark-background:#ffffff;"
    "mark-border-color:#808080;border:1px solid #808080;background:linear-gradient(#ffffff,#d6d6d6);border-radius:3px}"
    "slider:focus{mark-border-color:#3366cc}"
    "tabs{display:flex;flex-direction:column}"
    "tabstrip{display:flex;gap:2px}"
    "tabbutton{border-radius:0;background:#d6d6d6}"
    "tabbutton:selected{background:#f2f2f2;font-weight:bold}"
    "tab{display:flex;flex-direction:column;gap:6px;padding:6px;border:1px solid #808080}"
    /* 0.3 */
    "spinner{padding:3px 4px;border:1px solid #808080;background:#ffffff;width:80px;"
    "mark-background:#e8e8e8;mark-border-color:#a0a0a0}"
    "input:focus,spinner:focus,textarea:focus,select:focus{border-color:#3366cc}"
    "spinner:disabled,textarea:disabled,select:disabled,option:disabled{color:#9a9a9a}"
    "textarea{padding:3px 4px;border:1px solid #808080;background:#ffffff;mark-background:#ffffff}"
    "select{padding:3px 6px;border:1px solid #808080;border-radius:3px;background:linear-gradient(#ffffff,#d6d6d6)}"
    "select.listbox,popup{padding:1px;border:1px solid #808080;border-radius:0;background:#ffffff}"
    "option{padding:1px 4px}optgroup{padding:1px 4px;font-weight:bold}optgroup option{padding-left:16px;font-weight:normal}"
    "option:selected{background:#3366cc;color:#ffffff}"
    "fieldset{display:flex;flex-direction:column;gap:6px;padding:0 8px 6px;border:1px solid #909090}"
    "legend{padding:0 3px}"
    "tooltip{padding:2px 5px;border:1px solid #606060;background:#ffffe1;color:#000000;font-size:12px;font-weight:normal}";

typedef struct {
    char *id;
    int type;
    olui_handler fn;
    void *userdata;
} handler;

struct olui {
    olui_sheet *sheet;
    olui_el *root;
    ol_document *doc;
    olui_el **map;              /* the element of each node, by id */
    size_t map_count;
    size_t *op_first, *op_list; /* each node's text ops: op_list[op_first[id]..op_first[id + 1]] */
    size_t ops_reserved;
    ol_measure_text_fn measure;
    void *measure_userdata;
    int width, height, zoom;
    ol_scale scale;
    int stale;                  /* must be laid out again */
    int damaged;                /* all of it must be drawn again */
    olui_rect part;             /* else this part (w 0: none): what a hover, press, focus, check or selection changed */
    handler handlers[MAX_HANDLERS];
    int nhandlers;
    olui_el *focus, *hover, *pressed;
    int busy;                   /* calling a handler: changes it makes wait for the next layout */
    unsigned style_gen;         /* moves on when the stylesheet changes: every element is styled again */
    /* 0.3 */
    olui_el *popup;             /* the select whose pop-up list is open */
    olui_el *popup_box;         /* made: what styles the pop-up (popup, under the select) */
    olui_rect popup_rect;       /* where it is */
    int popup_hot;              /* the option it points at, from 0 */
    int popup_top;              /* its first row shown */
    int popup_rows;             /* rows it shows */
    int32_t popup_row_h;
    int popup_fresh;            /* opened by this press: its release chooses nothing */
    olui_el *drag;              /* a textarea a selection is dragged in */
    olui_el *label_down;        /* a label pressed */
    olui_el *tip_for;           /* the element the pointer rests on, with a title */
    olui_el *tip_box;           /* made: what styles the tip (tooltip, under it) */
    int tip_rest;               /* ms it has rested (-1: no tip until the pointer goes to another) */
    int tip_on;
    int tip_x, tip_y;           /* the pointer */
    olui_rect tip_rect;
    olui_el *spin;              /* a number field's arrow button held */
    int spin_dir, spin_wait;
};

/* ---- Small things ---------------------------------------------------------- */

static int32_t px(const olui *ui, ol_unit u)
{
    return ol_scale_length(&ui->scale, u);
}

static void touch(olui *ui)
{
    ui->stale = 1;
    ui->damaged = 1;
}

static void grow_to(olui_rect *a, olui_rect r)
{
    int32_t x1, y1;
    if (r.w <= 0 || r.h <= 0) return;
    if (a->w <= 0 || a->h <= 0) {
        *a = r;
        return;
    }
    x1 = a->x + a->w > r.x + r.w ? a->x + a->w : r.x + r.w;
    y1 = a->y + a->h > r.y + r.h ? a->y + a->h : r.y + r.h;
    if (r.x < a->x) a->x = r.x;
    if (r.y < a->y) a->y = r.y;
    a->w = x1 - a->x;
    a->h = y1 - a->y;
}

static olui_rect cut(olui_rect r, olui_rect c)
{
    int32_t x1 = r.x + r.w < c.x + c.w ? r.x + r.w : c.x + c.w;
    int32_t y1 = r.y + r.h < c.y + c.h ? r.y + r.h : c.y + c.h;
    if (r.x < c.x) r.x = c.x;
    if (r.y < c.y) r.y = c.y;
    r.w = x1 > r.x ? x1 - r.x : 0;
    r.h = y1 > r.y ? y1 - r.y : 0;
    return r;
}

/* Part of the window to draw again, as a hover does */
static void damage(olui *ui, olui_rect r)
{
    olui_rect win;
    win.x = win.y = 0;
    win.w = ui->width;
    win.h = ui->height;
    grow_to(&ui->part, cut(r, win));
}

/* The states that change only how a widget looks, unless its style says
 * otherwise (restyle_paint). Open shows or hides items: laid out again. */
#define S_PAINT (S_HOVER | S_ACTIVE | S_FOCUS | S_CHECKED | S_SELECTED)

static int restyle_paint(olui *ui, olui_el *el, unsigned bit);

static void set_state(olui *ui, olui_el *el, unsigned bit, int on)
{
    unsigned was;
    if (!el) return;
    was = el->state;
    if (on) el->state |= bit;
    else el->state &= ~bit;
    if (el->state == was) return;
    if (!(bit & ~S_PAINT) && restyle_paint(ui, el, bit)) return;
    touch(ui);
}

static int visible(const olui_el *el)
{
    return el && el->node && ol_node_style(el->node)->display != OL_DISPLAY_NONE;
}

static int enabled(const olui_el *el)
{
    for (; el; el = el->parent)
        if (el->state & S_DISABLED) return 0;
    return 1;
}

static int has_items(const olui_el *el)
{
    const olui_el *c;
    for (c = el->first; c; c = c->next)
        if (c->kind == K_ITEM) return 1;
    return 0;
}

static olui_el *list_of(olui_el *el)
{
    for (; el; el = el->parent)
        if (el->kind == K_LIST) return el;
    return NULL;
}

static void emit(olui *ui, int type, olui_el *el, olui_el *item, const char *value, int index, int checked)
{
    olui_event ev;
    int i;
    ev.type = type;
    ev.el = el;
    ev.id = el && el->id ? el->id : "";
    ev.item = item;
    ev.value = value;
    ev.index = index;
    ev.checked = checked;
    ++ui->busy;
    for (i = ui->nhandlers - 1; i >= 0; --i) {
        const handler *h = &ui->handlers[i];
        if (h->type && h->type != type) continue;
        if (h->id && strcmp(h->id, ev.id)) continue;
        h->fn(ui, &ev, h->userdata);
    }
    --ui->busy;
}

/* ---- Making and freeing ------------------------------------------------------ */

olui *olui_new(void)
{
    olui *ui = (olui *)calloc(1, sizeof(olui));
    if (!ui) return NULL;
    ui->sheet = olui_sheet_new();
    if (!ui->sheet || !olui_sheet_add(ui->sheet, ua_css)) {
        olui_free(ui);
        return NULL;
    }
    ui->zoom = 100;
    ui->style_gen = 1;
    ol_scale_set(&ui->scale, 1, OL_CSSPX(1));
    ui->stale = 1;
    return ui;
}

void olui_free(olui *ui)
{
    int i;
    if (!ui) return;
    olui_sheet_free(ui->sheet);
    olui_el_free(ui->root);
    olui_el_free(ui->popup_box);
    olui_el_free(ui->tip_box);
    ol_document_free(ui->doc);
    free(ui->map);
    free(ui->op_first);
    free(ui->op_list);
    for (i = 0; i < ui->nhandlers; ++i) free(ui->handlers[i].id);
    free(ui);
}

void olui_set_measure(olui *ui, ol_measure_text_fn measure, void *userdata)
{
    if (!ui) return;
    ui->measure = measure;
    ui->measure_userdata = userdata;
    touch(ui);
}

int olui_add_css(olui *ui, const char *css)
{
    if (!ui || !olui_sheet_add(ui->sheet, css)) return 0;
    ++ui->style_gen;
    touch(ui);
    return 1;
}

void olui_set_zoom(olui *ui, int percent)
{
    if (!ui || percent < 10) return;
    ui->zoom = percent;
    ol_scale_set(&ui->scale, percent, OL_CSSPX(100));
    touch(ui);
}

static int number_setup(olui_el *el);
static olui_el *row_after(const olui_el *sel, const olui_el *r);
static void mark_rows(olui_el *sel, int index);
static int option_step(const olui_el *sel, int index, int dir);
static int option_count(const olui_el *sel);
static int listbox(const olui_el *sel);

/* The made parts: a tabs' strip of tab buttons, one for each of its tabs */
static int make_parts(olui_el *el)
{
    olui_el *c;
    if (el->kind == K_TABS) {
        olui_el *strip = olui_el_new(K_STRIP, "tabstrip");
        int n = 0, chosen = -1;
        if (!strip) return 0;
        for (c = el->first; c; c = c->next) {
            olui_el *b;
            if (c->kind != K_TAB) continue;
            if (!(b = olui_el_new(K_TABBTN, "tabbutton"))) {
                olui_el_free(strip);
                return 0;
            }
            if (!olui_set_str(&b->text, c->title ? c->title : (c->id ? c->id : "Tab"))) {
                olui_el_free(b);
                olui_el_free(strip);
                return 0;
            }
            b->tab = c;
            c->tab = b;
            olui_el_append(strip, b);
            if (chosen < 0 && (c->state & S_SELECTED)) chosen = n;
            ++n;
        }
        el->ivalue = chosen < 0 ? 0 : chosen;
        /* the strip first */
        strip->parent = el;
        strip->next = el->first;
        el->first = strip;
        if (!el->last) el->last = strip;
    }
    if ((el->kind == K_ITEM || el->kind == K_BUTTON || el->kind == K_CHECK || el->kind == K_RADIO ||
         el->kind == K_OPTION) && !el->text) {
        /* an item with items under it: its own text is the runs before them */
        olui_el *t, *next;
        size_t n = 0;
        for (t = el->first; t; t = t->next)
            if (t->kind == K_TEXT && t->text) n += strlen(t->text) + 1;
        if (n) {
            char *s = (char *)malloc(n), *d = s;
            if (!s) return 0;
            for (t = el->first; t; t = next) {
                next = t->next;
                if (t->kind != K_TEXT || !t->text) continue;
                if (d > s && d[-1] != ' ' && t->text[0] != ' ') *d++ = ' ';
                strcpy(d, t->text);
                d += strlen(d);
                olui_el_unlink(t);
                olui_el_free(t);
            }
            while (d > s && d[-1] == ' ') --d;
            *d = 0;
            el->text = s[0] == ' ' ? (memmove(s, s + 1, strlen(s)), s) : s;
        }
    }
    if (el->kind == K_INPUT) {
        if (!el->text && el->value) {
            el->text = el->value;
            el->value = NULL;
        }
        if ((el->flags & F_NUMBER) && !number_setup(el)) return 0;
        el->caret = el->anchor = el->text ? strlen(el->text) : 0;
    }
    if (el->kind == K_SELECT) {
        /* the option chosen (the last with selected, as HTML takes it), or the first that can be */
        olui_el *r;
        int i = 0, chosen = -1;
        for (r = row_after(el, NULL); r; r = row_after(el, r)) {
            if (r->kind != K_OPTION) continue;
            if (r->state & S_SELECTED) chosen = i;
            r->state &= ~(unsigned)S_SELECTED;
            ++i;
        }
        if (listbox(el)) {
            /* a list box: of class listbox too, for its look (select.listbox) */
            size_t n = el->cls ? strlen(el->cls) : 0;
            char *cls = (char *)malloc(n + 9);
            if (!cls) return 0;
            if (n) memcpy(cls, el->cls, n);
            strcpy(cls + n, n ? " listbox" : "listbox");
            free(el->cls);
            el->cls = cls;
            mark_rows(el, chosen);
            el->ivalue = chosen;
        } else {
            el->ivalue = chosen >= 0 ? chosen : option_step(el, 0, 0);
            if (el->ivalue >= option_count(el)) el->ivalue = -1;
        }
    }
    for (c = el->first; c; c = c->next)
        if (c->kind != K_STRIP && !make_parts(c)) return 0;
    return 1;
}

int olui_load(olui *ui, const char *markup, const char *css, char *err, size_t errlen)
{
    olui_el *root;
    if (err && errlen) err[0] = 0;
    if (!ui) return 0;
    root = olui_markup_parse(markup, err, errlen);
    if (!root) return 0;
    if ((root->value && !olui_sheet_add(ui->sheet, root->value)) ||
        (css && !olui_sheet_add(ui->sheet, css)) || !make_parts(root)) {
        olui_el_free(root);
        return 0;
    }
    free(root->value);
    root->value = NULL;
    olui_el_free(ui->root);
    ui->root = root;
    ui->focus = ui->hover = ui->pressed = NULL;
    ui->popup = ui->drag = ui->label_down = ui->tip_for = ui->spin = NULL;
    ui->tip_on = 0;
    ++ui->style_gen;
    touch(ui);
    return 1;
}

int olui_on(olui *ui, const char *id, int type, olui_handler fn, void *userdata)
{
    handler *h;
    if (!ui || !fn || ui->nhandlers == MAX_HANDLERS) return 0;
    h = &ui->handlers[ui->nhandlers];
    memset(h, 0, sizeof(*h));
    if (id && !olui_set_str(&h->id, id)) return 0;
    h->type = type;
    h->fn = fn;
    h->userdata = userdata;
    ++ui->nhandlers;
    return 1;
}

/* ---- Layout ------------------------------------------------------------------------ */

static ol_role role_of(const olui_el *el)
{
    switch (el->kind) {
    case K_TEXT: return OL_ROLE_TEXT;
    case K_BUTTON: case K_TABBTN: return OL_ROLE_BUTTON;
    case K_CHECK: case K_RADIO: case K_INPUT: case K_TEXTAREA: case K_SELECT: return OL_ROLE_INPUT;
    case K_LIST: return OL_ROLE_LIST;
    case K_ITEM: return OL_ROLE_LIST_ITEM;
    case K_IMAGE: return OL_ROLE_IMAGE;
    default: break;
    }
    if (el->tag[0] == 'h' && el->tag[1] >= '1' && el->tag[1] <= '6' && !el->tag[2]) return OL_ROLE_HEADING;
    if (!strcmp(el->tag, "p")) return OL_ROLE_PARAGRAPH;
    return OL_ROLE_BLOCK;
}

static int map_node(olui *ui, olui_el *el)
{
    ol_id id = ol_node_id(el->node);
    if (id >= ui->map_count) {
        size_t n = ui->map_count ? ui->map_count * 2 : 64;
        olui_el **m;
        while (n <= id) n *= 2;
        m = (olui_el **)realloc(ui->map, n * sizeof(*m));
        if (!m) return 0;
        memset(m + ui->map_count, 0, (n - ui->map_count) * sizeof(*m));
        ui->map = m;
        ui->map_count = n;
    }
    ui->map[id] = el;
    return 1;
}

static const char *row_text(const olui_el *r);
static void row_style(olui *ui, olui_el *r, const olui_el *sel);
static ol_unit text_width(olui *ui, const olui_el *el, const char *s, size_t n);

/* An element's node, styled; and its children's, unless it is a list */
static int build(olui *ui, olui_el *el, ol_node *parent, const olui_cs *pcs, int shown, int restyle);

static int build_items(olui *ui, olui_el *list, olui_el *from, ol_node *parent, int depth, int shown,
                       int restyle)
{
    olui_el *c;
    for (c = from->first; c; c = c->next) {
        /* an item styled again: so are the items under it (rules reach them through it) */
        int again;
        if (c->kind != K_ITEM) continue;
        c->depth = depth;
        again = restyle || c->styled_gen != ui->style_gen || c->styled_state != c->state;
        if (!build(ui, c, parent, &list->cs, shown, restyle)) return 0;
        if (!build_items(ui, list, c, parent, depth + 1, shown && (c->state & S_OPEN) && visible(c),
                         again))
            return 0;
    }
    return 1;
}

/* restyle: its parent was styled again, so it is too (what it inherits may
 * have changed); otherwise its style is kept while the stylesheet and its
 * states stay as they were */
static int build(olui *ui, olui_el *el, ol_node *parent, const olui_cs *pcs, int shown, int restyle)
{
    olui_cs *cs = &el->cs;
    ol_style st;
    ol_box_style box;
    const char *text = el->text;
    olui_el *c;
    ol_unit line;

    if (restyle || el->styled_gen != ui->style_gen || el->styled_state != el->state) {
        olui_style_compute(ui->sheet, el, pcs);
        el->styled_gen = ui->style_gen;
        el->styled_state = el->state;
        restyle = 1;
    }
    st = cs->ol;
    if (!shown) st.display = OL_DISPLAY_NONE;
    if (el->kind == K_TEXT) st.display = OL_DISPLAY_INLINE;
    box = cs->box;
    st.background = 0;          /* olui draws the boxes */
    if (st.display == OL_DISPLAY_INLINE && el->kind != K_TEXT && el->first == NULL &&
        el->kind != K_BOX && el->kind != K_IMAGE)
        st.display = OL_DISPLAY_BLOCK;  /* widgets are boxes; a picture stays in its line, where GFOpenLayout places it */
    line = st.line_height > st.font_size ? st.line_height : st.font_size;

    switch (el->kind) {
    case K_CHECK:
    case K_RADIO:
        /* room for the box at the start */
        st.padding_left += line + PX(6);
        if (!text && el->title) text = el->title;
        if (!text || !text[0]) text = " ";
        break;
    case K_INPUT:
        text = NULL;            /* drawn by olui, so that it scrolls under the caret */
        if (box.height <= 0) box.height = line + st.padding_top + st.padding_bottom;
        break;
    case K_TEXTAREA:
        /* rows lines high; cols characters wide (20 in a row when not given, else as wide as its column) */
        text = NULL;
        if (box.height <= 0)
            box.height = (el->rows > 0 ? el->rows : 2) * line + st.padding_top + st.padding_bottom + 2 * cs->border;
        if (box.width <= 0 && ((el->flags & F_COLS) || (pcs && (pcs->box.flags & OL_BOX_ROW))))
            box.width = (el->cols > 0 ? el->cols : 20) * text_width(ui, el, "0", 1) + st.padding_left +
                        st.padding_right + 2 * cs->border;
        break;
    case K_SELECT: {
        /* as wide as its widest option (and the mark); a list box size rows high */
        olui_el *r;
        ol_unit widest = 0, row = 0;
        for (r = row_after(el, NULL); r; r = row_after(el, r)) {
            const char *t = row_text(r);
            ol_unit w, h;
            if (restyle) r->styled_gen = 0;
            row_style(ui, r, el);
            w = text_width(ui, r, t, strlen(t)) + (listbox(el) ? r->cs.ol.padding_left + r->cs.ol.padding_right : 0);
            h = (r->cs.ol.line_height > r->cs.ol.font_size ? r->cs.ol.line_height : r->cs.ol.font_size) +
                r->cs.ol.padding_top + r->cs.ol.padding_bottom;
            if (w > widest) widest = w;
            if (h > row) row = h;
        }
        text = NULL;
        if (box.width <= 0)
            box.width = widest + st.padding_left + st.padding_right + 2 * cs->border + (listbox(el) ? PX(BAR + 2) : line + PX(4));
        if (box.height <= 0)
            box.height = listbox(el) ? el->rows * row + st.padding_top + st.padding_bottom + 2 * cs->border
                                     : line + st.padding_top + st.padding_bottom;
        break;
    }
    case K_ITEM:
        st.padding_left += el->depth * PX(INDENT) + PX(14);
        if (!text) text = el->title ? el->title : "";
        if (!text[0]) text = " ";
        break;
    case K_LIST:
        /* what it holds scrolls: with height auto it is as tall as its row or what grows gives it */
        if (box.height <= 0) box.height = 3 * line + st.padding_top + st.padding_bottom + 2 * cs->border;
        break;
    case K_PROGRESS:
        text = NULL;
        break;
    case K_SLIDER:
        text = NULL;
        if (box.height <= 0) box.height = line;
        break;
    case K_IMAGE:
        text = NULL;
        break;
    case K_TABBTN:
        if (el->tab && el->tab->parent && el->tab->parent->kind == K_TABS) {
            olui_el *tabs = el->tab->parent, *t;
            int n = 0;
            for (t = tabs->first; t && t != el->tab; t = t->next)
                if (t->kind == K_TAB) ++n;
            if ((n == tabs->ivalue) != ((el->state & S_SELECTED) != 0)) {
                /* the chosen tab's button is :selected: style it again so */
                el->state ^= S_SELECTED;
                olui_style_compute(ui->sheet, el, pcs);
                el->styled_state = el->state;
                restyle = 1;
                st = cs->ol;
                if (!shown) st.display = OL_DISPLAY_NONE;
                st.background = 0;
                box = cs->box;
            }
        }
        break;
    default:
        break;
    }
    if (el->kind == K_BOX && !strcmp(el->tag, "fieldset")) {
        /* with no legend on its top edge, room above as below */
        for (c = el->first; c && strcmp(c->tag, "legend"); c = c->next) ;
        if (!c) st.padding_top += st.padding_bottom > 0 ? st.padding_bottom : PX(6);
    }
    if (el->kind == K_TAB && el->parent && el->parent->kind == K_TABS) {
        olui_el *t;
        int n = 0;
        for (t = el->parent->first; t && t != el; t = t->next)
            if (t->kind == K_TAB) ++n;
        if (n != el->parent->ivalue) st.display = OL_DISPLAY_NONE;
    }
    box.border_width = 0;       /* olui draws the borders too */

    el->node = ol_node_append(ui->doc, parent, role_of(el), text);
    if (!el->node || !map_node(ui, el)) return 0;
    ol_node_set_style(el->node, &st);
    ol_node_set_box(el->node, &box);
    if (el->kind == K_IMAGE) ol_node_set_image(el->node, el->ivalue, PX(el->img_w), PX(el->img_h));
    {
        uint32_t states = 0, actions = 0;
        if (el->state & S_DISABLED) states |= OL_STATE_DISABLED;
        if (el->state & S_CHECKED) states |= OL_STATE_CHECKED;
        if (el->state & S_SELECTED) states |= OL_STATE_SELECTED;
        if (el->state & S_FOCUS) states |= OL_STATE_FOCUSED;
        switch (el->kind) {
        case K_BUTTON: case K_CHECK: case K_RADIO: case K_TABBTN: actions = OL_ACTION_ACTIVATE | OL_ACTION_FOCUS; break;
        case K_SLIDER: actions = OL_ACTION_EDIT | OL_ACTION_FOCUS; break;
        case K_INPUT: case K_TEXTAREA: case K_SELECT: actions = OL_ACTION_EDIT | OL_ACTION_FOCUS; break;
        case K_LIST: actions = OL_ACTION_FOCUS; break;
        case K_ITEM: actions = OL_ACTION_SELECT | OL_ACTION_ACTIVATE; break;
        default: break;
        }
        ol_node_set_states(el->node, states);
        ol_node_set_actions(el->node, actions);
        if (el->id) ol_node_set_name(el->node, el->id);
        if ((el->kind == K_INPUT || el->kind == K_TEXTAREA) && el->text && !(el->flags & F_PASSWORD))
            ol_node_set_value(el->node, el->text);
    }
    if (el->kind == K_LIST)
        return build_items(ui, el, el, el->node, 0, shown && st.display != OL_DISPLAY_NONE, restyle);
    if (el->kind == K_ITEM || el->kind == K_SELECT) return 1;   /* its items go under the list (build_items); a select draws its options */
    for (c = el->first; c; c = c->next)
        if (!build(ui, c, el->node, cs, shown && st.display != OL_DISPLAY_NONE, restyle)) return 0;
    return 1;
}

/* Every node's text ops, for drawing them with their element */
static int index_ops(olui *ui)
{
    size_t i, n = ol_display_count(ui->doc), nodes = ui->map_count + 1;
    size_t *first = (size_t *)calloc(nodes + 1, sizeof(size_t));
    size_t *list = (size_t *)malloc((n ? n : 1) * sizeof(size_t));
    if (!first || !list) {
        free(first);
        free(list);
        return 0;
    }
    for (i = 0; i < n; ++i) {
        const ol_display_op *op = ol_display_get(ui->doc, i);
        if (op->type == OL_OP_TEXT && op->u.text.node_id < ui->map_count) ++first[op->u.text.node_id + 1];
        else if (op->type == OL_OP_IMAGE && op->u.image.node_id < ui->map_count) ++first[op->u.image.node_id + 1];
    }
    for (i = 1; i <= nodes; ++i) first[i] += first[i - 1];
    {
        size_t *at = (size_t *)malloc((nodes + 1) * sizeof(size_t));
        if (!at) {
            free(first);
            free(list);
            return 0;
        }
        memcpy(at, first, (nodes + 1) * sizeof(size_t));
        for (i = 0; i < n; ++i) {
            const ol_display_op *op = ol_display_get(ui->doc, i);
            ol_id id;
            if (op->type == OL_OP_TEXT) id = op->u.text.node_id;
            else if (op->type == OL_OP_IMAGE) id = op->u.image.node_id;
            else continue;
            if (id < ui->map_count) list[at[id]++] = i;
        }
        free(at);
    }
    free(ui->op_first);
    free(ui->op_list);
    ui->op_first = first;
    ui->op_list = list;
    return 1;
}

static void clamp_scroll(olui *ui, olui_el *list);
static int scrolls(const olui_el *el);
static void popup_place(olui *ui);
static void popup_reveal(olui *ui);

static void clear_nodes(olui_el *el)
{
    olui_el *c;
    el->node = NULL;
    for (c = el->first; c; c = c->next) clear_nodes(c);
}

static int relayout(olui *ui)
{
    ol_document *doc;
    ol_box_style box;
    if (!ui->root) return 1;
    doc = ol_document_new_reserved(ui->ops_reserved + 16);
    if (!doc) return 0;
    ol_document_free(ui->doc);
    ui->doc = doc;
    if (ui->map) memset(ui->map, 0, ui->map_count * sizeof(*ui->map));
    clear_nodes(ui->root);
    ui->stale = 0;
    if (!build(ui, ui->root, NULL, NULL, 1, 0)) return 0;
    /* the window's height: what grows fills it */
    box = *ol_node_box(ui->root->node);
    box.height = ol_unscale_y(&ui->scale, ui->height);
    ol_node_set_box(ui->root->node, &box);
    if (!ol_layout(doc, ol_unscale_x(&ui->scale, ui->width > 0 ? ui->width : 1),
                   ui->measure, ui->measure_userdata))
        return 0;
    ui->ops_reserved = ol_display_count(doc);
    if (!index_ops(ui)) return 0;
    {
        /* lists, boxes that scroll, textareas and list boxes keep their scroll inside what they hold */
        size_t i;
        for (i = 0; i < ui->map_count; ++i)
            if (ui->map[i] && scrolls(ui->map[i])) clamp_scroll(ui, ui->map[i]);
    }
    /* a pop-up goes with its select, or closes when that went (or was disabled); a tip goes */
    if (ui->popup) {
        if (visible(ui->popup) && enabled(ui->popup) && option_count(ui->popup)) {
            popup_place(ui);
            popup_reveal(ui);
        } else {
            mark_rows(ui->popup, -1);
            ui->popup = NULL;
        }
    }
    ui->tip_on = 0;
    ui->damaged = 1;
    return 1;
}

int olui_layout(olui *ui, int width, int height)
{
    if (!ui) return 0;
    ui->width = width;
    ui->height = height;
    return relayout(ui);
}

static int fresh(olui *ui)
{
    if (ui->stale && !ui->busy) return relayout(ui);
    return 1;
}

/* ---- Where things are, in pixels ------------------------------------------------- */

static olui_rect rect_of_node(const olui *ui, const ol_node *node)
{
    ol_pixel_rect p = ol_scale_rect(&ui->scale, ol_node_bounds(node));
    olui_rect r;
    r.x = p.x;
    r.y = p.y;
    r.w = p.width;
    r.h = p.height;
    return r;
}

/* A box that scrolls what it holds: a list, or a box with overflow */
static int scroller(const olui_el *el)
{
    return el->kind == K_LIST ||
           (el->cs.overflow != O_VISIBLE && (el->kind == K_BOX || el->kind == K_TAB) && el->node);
}

/* How far the boxes el is in that scroll move it up */
static int32_t scroll_above(const olui_el *el)
{
    int32_t dy = 0;
    for (el = el->parent; el; el = el->parent)
        if (scroller(el)) dy += el->scroll;
    return dy;
}

/* An element's box on the window: moved up by the scroll of the lists and boxes it is in */
static olui_rect rect_of(const olui *ui, const olui_el *el)
{
    olui_rect r = rect_of_node(ui, el->node);
    r.y -= scroll_above(el);
    return r;
}

/* The inside of a box: less its border */
static olui_rect inside(const olui *ui, const olui_el *el, olui_rect r)
{
    int32_t b = el->cs.border > 0 ? px(ui, el->cs.border) : 0;
    r.x += b;
    r.y += b;
    r.w -= 2 * b;
    r.h -= 2 * b;
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

/* What the boxes el is in that scroll cut it to (each one's inside, where
 * it is): 1 when they cut it; and how far they move it up */
static int view_of(const olui *ui, const olui_el *el, int32_t *dy, olui_rect *clip)
{
    const olui_el *up[64], *a;
    int n = 0, clipped = 0;
    *dy = 0;
    for (a = el->parent; a && n < 64; a = a->parent)
        if (scroller(a) && a->node) up[n++] = a;
    while (n--) {
        olui_rect r = rect_of_node(ui, up[n]->node), in;
        r.y -= *dy;
        in = inside(ui, up[n], r);
        *clip = clipped ? cut(in, *clip) : in;
        clipped = 1;
        *dy += up[n]->scroll;
    }
    return clipped;
}

static int in_rect(olui_rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static int32_t list_content_height(const olui *ui, const olui_el *list)
{
    olui_rect in = inside(ui, list, rect_of_node(ui, list->node));
    int32_t bottom = in.y;
    size_t i;
    for (i = 0; i < ui->map_count; ++i) {
        const olui_el *e = ui->map[i];
        if (e && e->kind == K_ITEM && list_of((olui_el *)e) == list && visible(e)) {
            olui_rect r = rect_of_node(ui, e->node);
            if (r.y + r.h > bottom) bottom = r.y + r.h;
        }
    }
    return bottom - in.y + px(ui, list->cs.ol.padding_bottom);
}

/* What a scrolling box holds, from its inside's top: its children and its own text */
static int32_t box_content_height(const olui *ui, const olui_el *el)
{
    olui_rect in = inside(ui, el, rect_of_node(ui, el->node));
    int32_t bottom = in.y;
    const olui_el *c;
    ol_id id = ol_node_id(el->node);
    for (c = el->first; c; c = c->next) {
        if (visible(c)) {
            olui_rect r = rect_of_node(ui, c->node);
            int32_t b = r.y + r.h + px(ui, c->cs.ol.margin_bottom);
            if (b > bottom) bottom = b;
        }
    }
    if (ui->op_first && id < ui->map_count) {
        size_t k;
        for (k = ui->op_first[id]; k < ui->op_first[id + 1]; ++k) {
            const ol_display_op *op = ol_display_get(ui->doc, ui->op_list[k]);
            ol_pixel_rect p = ol_scale_rect(&ui->scale, op->type == OL_OP_TEXT ? op->u.text.bounds : op->u.image.bounds);
            if (p.y + p.height > bottom) bottom = p.y + p.height;
        }
    }
    return bottom - in.y + px(ui, el->cs.ol.padding_bottom);
}

static int32_t scroll_most(olui *ui, olui_el *el);

static void clamp_scroll(olui *ui, olui_el *list)
{
    int32_t most = scroll_most(ui, list);
    if (list->scroll > most) list->scroll = most;
    if (list->scroll < 0) list->scroll = 0;
}

/* The deepest shown element at x, y */
static olui_el *hit(olui *ui, olui_el *el, int x, int y)
{
    olui_el *c, *best = NULL;
    olui_rect r;
    if (!visible(el)) return NULL;
    r = rect_of(ui, el);
    if (scroller(el) && el->kind != K_LIST && !in_rect(r, x, y)) return NULL;  /* what it holds is cut to it */
    if (el->kind == K_LIST) {
        size_t i;
        if (!in_rect(r, x, y)) return NULL;
        for (i = 0; i < ui->map_count; ++i) {
            olui_el *e = ui->map[i];
            if (e && e->kind == K_ITEM && list_of(e) == el && visible(e) && in_rect(rect_of(ui, e), x, y))
                return e;
        }
        return el;
    }
    for (c = el->first; c; c = c->next) {
        olui_el *h = hit(ui, c, x, y);
        if (h) best = h;
    }
    if (best) return best;
    return in_rect(r, x, y) ? el : NULL;
}

/* What a press on el works: the widget it is part of */
static olui_el *widget_of(olui_el *el)
{
    for (; el; el = el->parent) {
        switch (el->kind) {
        case K_BUTTON: case K_CHECK: case K_RADIO: case K_INPUT: case K_ITEM:
        case K_TABBTN: case K_LIST: case K_SLIDER: case K_SELECT: case K_TEXTAREA:
            return el;
        default:
            break;
        }
    }
    return NULL;
}

static int focusable(const olui_el *el)
{
    switch (el->kind) {
    case K_BUTTON: case K_CHECK: case K_RADIO: case K_INPUT: case K_LIST: case K_TABBTN: case K_SLIDER:
    case K_SELECT: case K_TEXTAREA:
        return visible(el) && enabled(el);
    default:
        return 0;
    }
}

/* ---- The widgets' work ------------------------------------------------------------- */

static ol_unit text_width(olui *ui, const olui_el *el, const char *s, size_t n)
{
    ol_text_metrics m;
    memset(&m, 0, sizeof(m));
    if (!n) return 0;
    if (ui->measure) ui->measure(ui->measure_userdata, s, n, &el->cs.ol, &m);
    else m.width = (ol_unit)((int64_t)el->cs.ol.font_size * 3 * (int64_t)n / 5);
    return m.width;
}

static void radio_choose(olui *ui, olui_el *el)
{
    /* the others with its name, under the same window, go off */
    olui_el *stack[64];
    int sp = 0;
    if (el->name && ui->root) {
        stack[sp++] = ui->root;
        while (sp) {
            olui_el *e = stack[--sp], *c;
            if (e != el && e->kind == K_RADIO && e->name && !strcmp(e->name, el->name))
                set_state(ui, e, S_CHECKED, 0);
            for (c = e->first; c && sp < 64; c = c->next) stack[sp++] = c;
        }
    }
    if (!(el->state & S_CHECKED)) {
        set_state(ui, el, S_CHECKED, 1);
        emit(ui, OLUI_CHANGE, el, NULL, el->value ? el->value : (el->id ? el->id : ""), -1, 1);
    }
}

static void press(olui *ui, olui_el *el)
{
    switch (el->kind) {
    case K_BUTTON:
        emit(ui, OLUI_CLICK, el, NULL, el->value, -1, 0);
        break;
    case K_CHECK:
        set_state(ui, el, S_CHECKED, !(el->state & S_CHECKED));
        emit(ui, OLUI_CHANGE, el, NULL, el->value, -1, (el->state & S_CHECKED) != 0);
        break;
    case K_RADIO:
        radio_choose(ui, el);
        break;
    case K_TABBTN:
        if (el->tab && el->tab->parent) {
            olui_el *tabs = el->tab->parent, *t;
            int n = 0;
            for (t = tabs->first; t && t != el->tab; t = t->next)
                if (t->kind == K_TAB) ++n;
            olui_tabs_select(ui, tabs, n);
        }
        break;
    default:
        break;
    }
}

static int item_index(olui *ui, olui_el *list, olui_el *item)
{
    size_t i;
    int n = 0;
    for (i = 0; i < ui->map_count; ++i) {
        olui_el *e = ui->map[i];
        if (e && e->kind == K_ITEM && list_of(e) == list && visible(e)) {
            if (e == item) return n;
            ++n;
        }
    }
    return -1;
}

static void item_choose(olui *ui, olui_el *list, olui_el *item)
{
    olui_el *was = olui_list_selected(list);
    if (was == item) return;
    if (was) set_state(ui, was, S_SELECTED, 0);
    set_state(ui, item, S_SELECTED, 1);
    emit(ui, OLUI_SELECT, list, item, item->value ? item->value : (item->id ? item->id : ""),
         item_index(ui, list, item), 0);
}

static void scroll_to(olui *ui, olui_el *list, olui_el *item)
{
    olui_rect in, r;
    if (!fresh(ui) || !visible(item)) return;
    in = inside(ui, list, rect_of_node(ui, list->node));
    r = rect_of_node(ui, item->node);
    if (r.y - list->scroll < in.y) list->scroll = r.y - in.y;
    else if (r.y + r.h - list->scroll > in.y + in.h) list->scroll = r.y + r.h - in.y - in.h;
    clamp_scroll(ui, list);
    ui->damaged = 1;
}

static int32_t field_x(olui *ui, const olui_el *el, const char *t, size_t n);

/* The caret's byte for an x in a field */
static size_t caret_at(olui *ui, olui_el *el, int x)
{
    olui_rect in = inside(ui, el, rect_of(ui, el));
    const char *t = el->text ? el->text : "";
    size_t i = 0, best = 0;
    int32_t want = x - in.x - px(ui, el->cs.ol.padding_left) + el->xoff, best_d = 0x7fffffff;
    for (;;) {
        int32_t w = field_x(ui, el, t, i), d = w > want ? w - want : want - w;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
        if (!t[i]) break;
        ++i;
        while (t[i] && ((unsigned char)t[i] & 0xc0) == 0x80) ++i;
    }
    return best;
}

static size_t prev_char(const char *t, size_t i)
{
    if (!i) return 0;
    --i;
    while (i && ((unsigned char)t[i] & 0xc0) == 0x80) --i;
    return i;
}

static size_t next_char(const char *t, size_t i)
{
    if (!t[i]) return i;
    ++i;
    while (t[i] && ((unsigned char)t[i] & 0xc0) == 0x80) ++i;
    return i;
}

/* A slider's track: where its knob's middle can go */
static void slider_track(olui *ui, const olui_el *el, int32_t *x0, int32_t *x1)
{
    olui_rect in = inside(ui, el, rect_of(ui, el));
    *x0 = in.x + px(ui, el->cs.ol.padding_left);
    *x1 = in.x + in.w - px(ui, el->cs.ol.padding_right);
    if (*x1 <= *x0) *x1 = *x0 + 1;
}

static void slider_set(olui *ui, olui_el *el, int v)
{
    if (v < el->imin) v = el->imin;
    if (v > el->imax) v = el->imax;
    if (v == el->ivalue) return;
    el->ivalue = v;
    ui->damaged = 1;
    {
        char num[16];
        snprintf(num, sizeof(num), "%d", v);
        emit(ui, OLUI_CHANGE, el, NULL, num, v, 0);
    }
}

static void slider_to(olui *ui, olui_el *el, int x)
{
    int32_t x0, x1, range = el->imax - el->imin;
    slider_track(ui, el, &x0, &x1);
    if (x < x0) x = x0;
    if (x > x1) x = x1;
    slider_set(ui, el, el->imin + (int)(((int64_t)(x - x0) * range * 2 + (x1 - x0)) / (2 * (int64_t)(x1 - x0))));
}

/* ---- 0.3: fields' text, textareas, selects, number fields, tooltips ---------------- */

static int32_t wpx(olui *ui, const olui_el *el, const char *s, size_t n)
{
    return px(ui, text_width(ui, el, s, n));
}

static int32_t line_px(const olui *ui, const ol_style *st)
{
    return px(ui, st->line_height > st->font_size ? st->line_height : st->font_size);
}

/* The baseline of a line of text top pixels down, line pixels high */
static int32_t baseline_in(olui *ui, const ol_style *st, int32_t top, int32_t line)
{
    ol_text_metrics m;
    memset(&m, 0, sizeof(m));
    if (ui->measure) ui->measure(ui->measure_userdata, "Xg", 2, st, &m);
    else { m.ascent = st->font_size * 4 / 5; m.descent = st->font_size - m.ascent; }
    return top + (line - px(ui, m.ascent + m.descent)) / 2 + px(ui, m.ascent);
}

static size_t chars_in(const char *t, size_t n)
{
    size_t i, k = 0;
    for (i = 0; i < n && t[i]; ++i)
        if (((unsigned char)t[i] & 0xc0) != 0x80) ++k;
    return k;
}

/* What a field shows of its text: a password a bullet a character (malloc'd) */
static char *masked(const char *t, size_t *n)
{
    size_t k = chars_in(t, strlen(t)), i;
    char *m = (char *)malloc(k * 3 + 1);
    if (!m) return NULL;
    for (i = 0; i < k; ++i) memcpy(m + i * 3, BULLET, 3);
    m[k * 3] = 0;
    *n = k * 3;
    return m;
}

/* How far into a field its text's first n bytes reach, in pixels */
static int32_t field_x(olui *ui, const olui_el *el, const char *t, size_t n)
{
    if (el->flags & F_PASSWORD) return (int32_t)chars_in(t, n) * wpx(ui, el, BULLET, 3);
    return wpx(ui, el, t, n);
}

static void damage_el(olui *ui, olui_el *el);
static void ta_reveal(olui *ui, olui_el *el);

/* ---- Number fields: fixed point, never floating ---- */

static int pow10i(int d)
{
    int p = 1;
    while (d-- > 0) p *= 10;
    return p;
}

static int places_of(const char *s)
{
    const char *d = s ? strchr(s, '.') : NULL;
    int n = 0;
    if (!d) return 0;
    for (++d; *d >= '0' && *d <= '9'; ++d) ++n;
    return n;
}

/* "-12.5" as a whole number of 10^-dec: 1 when it is a number */
static int fixed_parse(const char *s, int dec, int *out)
{
    long whole = 0, frac = 0, most = 1000000000L / pow10i(dec);
    int neg = 0, digits = 0, fd = 0;
    if (!s) return 0;
    while (*s == ' ') ++s;
    if (*s == '-') { neg = 1; ++s; }
    else if (*s == '+') ++s;
    for (; *s >= '0' && *s <= '9'; ++s, ++digits)
        if (whole < most) whole = whole * 10 + (*s - '0');
    if (*s == '.' || *s == ',') {
        for (++s; *s >= '0' && *s <= '9'; ++s, ++digits)
            if (fd < dec) { frac = frac * 10 + (*s - '0'); ++fd; }
    }
    while (*s == ' ') ++s;
    if (*s || !digits) return 0;
    if (whole >= most) { whole = most - 1; frac = 0; fd = 0; }
    while (fd < dec) { frac *= 10; ++fd; }
    *out = (int)(whole * pow10i(dec) + frac);
    if (neg) *out = -*out;
    return 1;
}

/* buf has room for 24 */
static void fixed_format(int v, int dec, char *buf, size_t n)
{
    unsigned long a = v < 0 ? (unsigned long)-(long)v : (unsigned long)v, p = (unsigned long)pow10i(dec), f;
    size_t k;
    if (dec <= 0) {
        snprintf(buf, n, "%d", v);
        return;
    }
    snprintf(buf, n, "%s%lu.", v < 0 ? "-" : "", a / p);
    k = strlen(buf);
    for (f = a % p, p /= 10; p && k + 1 < n; p /= 10) {
        buf[k++] = (char)('0' + f / p);
        f %= p;
    }
    buf[k] = 0;
}

static int number_clamp(const olui_el *el, int v)
{
    if (v < el->imin) v = el->imin;
    if (v > el->imax) v = el->imax;
    return v;
}

/* min, max, step and value as written, to fixed point (make_parts) */
static int number_setup(olui_el *el)
{
    int dec = places_of(el->text), i, v;
    for (i = 0; i < 3; ++i)
        if (places_of(el->num_attr[i]) > dec) dec = places_of(el->num_attr[i]);
    if (dec > 4) dec = 4;
    el->decimals = dec;
    el->imin = -1000000000 + 1;
    el->imax = 1000000000 - 1;
    if (fixed_parse(el->num_attr[0], dec, &v)) el->imin = v;
    if (fixed_parse(el->num_attr[1], dec, &v)) el->imax = v;
    if (el->imax < el->imin) el->imax = el->imin;
    el->step = fixed_parse(el->num_attr[2], dec, &v) && v > 0 ? v : pow10i(dec);
    if (el->text && el->text[0] && fixed_parse(el->text, dec, &v)) {
        char num[24];
        el->ivalue = number_clamp(el, v);
        fixed_format(el->ivalue, dec, num, sizeof(num));
        return olui_set_str(&el->text, num);
    }
    el->ivalue = number_clamp(el, 0);
    return 1;
}

/* ---- Text: what replaces what, for fields and textareas ---- */

static void set_node_value(olui_el *el)
{
    if (el->node && !(el->flags & F_PASSWORD)) ol_node_set_value(el->node, el->text ? el->text : "");
}

/* Replaces from..to of el's text with ins, as typed (maxlength, a field's
 * one line, a number's characters); the caret after it. 1 when it changed. */
static int text_replace(olui *ui, olui_el *el, size_t from, size_t to, const char *ins)
{
    const char *t = el->text ? el->text : "";
    size_t n = strlen(t), k, m = 0, room = (size_t)-1, i;
    char *add, *s;
    if (el->flags & F_READONLY) return 0;
    if (to > n) to = n;
    if (from > to) from = to;
    if (!ins) ins = "";
    if (!(add = (char *)malloc(strlen(ins) + 1))) return 0;
    if (el->maxlength > 0) {
        size_t have = chars_in(t, n) - chars_in(t + from, to - from);
        room = have >= (size_t)el->maxlength ? 0 : (size_t)el->maxlength - have;
    }
    for (i = 0, k = 0; ins[i];) {
        size_t c = next_char(ins, i);
        char ch = ins[i];
        if (k >= room) break;
        if (el->kind == K_TEXTAREA) {
            if (ch == '\r') { i = c; continue; }
        } else if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        }
        if ((el->flags & F_NUMBER) && !((ch >= '0' && ch <= '9') || ch == '-' || ch == '+' ||
                                        (el->decimals > 0 && (ch == '.' || ch == ',')))) {
            i = c;
            continue;
        }
        if (c - i == 1) add[m++] = ch;
        else { memcpy(add + m, ins + i, c - i); m += c - i; }
        ++k;
        i = c;
    }
    add[m] = 0;
    if (!m && from == to) {
        free(add);
        return 0;
    }
    if (!(s = (char *)malloc(n - (to - from) + m + 1))) {
        free(add);
        return 0;
    }
    memcpy(s, t, from);
    memcpy(s + from, add, m);
    memcpy(s + from + m, t + to, n - to + 1);
    free(add);
    free(el->text);
    el->text = s;
    el->caret = el->anchor = from + m;
    el->want_x = -1;
    ++el->text_rev;
    set_node_value(el);
    if (el->flags & F_NUMBER) {
        int v;
        if (fixed_parse(s, el->decimals, &v)) el->ivalue = v;
    }
    if (el->kind == K_TEXTAREA) ta_reveal(ui, el);
    else damage_el(ui, el);
    emit(ui, OLUI_CHANGE, el, NULL, (el->flags & F_PASSWORD) ? NULL : el->text,
         (el->flags & F_NUMBER) ? el->ivalue : -1, 0);
    return 1;
}

/* A number field's text made its value's, held to min and max (Return, the
 * keyboard going elsewhere); OLUI_CHANGE when that changed it */
static void number_commit(olui *ui, olui_el *el)
{
    int v = el->ivalue;
    char num[24];
    if (!el->text || !el->text[0]) return;
    if (fixed_parse(el->text, el->decimals, &v)) v = number_clamp(el, v);
    else v = el->ivalue;
    fixed_format(v, el->decimals, num, sizeof(num));
    if (!strcmp(num, el->text) && v == el->ivalue) return;
    if (!olui_set_str(&el->text, num)) return;
    el->ivalue = v;
    el->caret = el->anchor = strlen(num);
    ++el->text_rev;
    set_node_value(el);
    damage_el(ui, el);
    emit(ui, OLUI_CHANGE, el, NULL, el->text, v, 0);
}

/* steps up (or down) by its step */
static void number_step(olui *ui, olui_el *el, int steps)
{
    int v;
    long nv;
    char num[24];
    if (!el->text || !el->text[0] || !fixed_parse(el->text, el->decimals, &v)) v = el->ivalue;
    nv = (long)v + (long)steps * el->step;
    if (nv < el->imin) nv = el->imin;
    if (nv > el->imax) nv = el->imax;
    fixed_format((int)nv, el->decimals, num, sizeof(num));
    if ((int)nv == el->ivalue && el->text && !strcmp(el->text, num)) return;
    if (!olui_set_str(&el->text, num)) return;
    el->ivalue = (int)nv;
    el->caret = el->anchor = strlen(num);
    ++el->text_rev;
    set_node_value(el);
    damage_el(ui, el);
    emit(ui, OLUI_CHANGE, el, NULL, el->text, el->ivalue, 0);
}

/* A number field's arrow buttons: the right of its inside, as wide as most of a line */
static olui_rect spin_box(olui *ui, const olui_el *el, olui_rect r)
{
    olui_rect in = inside(ui, el, r), b;
    int32_t line = line_px(ui, &el->cs.ol);
    b.w = line * 3 / 4 > 10 ? line * 3 / 4 : 10;
    if (b.w > in.w / 2) b.w = in.w / 2;
    b.x = in.x + in.w - b.w;
    b.y = in.y;
    b.h = in.h;
    return b;
}

/* ---- Textareas: lines wrapped at words, scrolled in pixels ---- */

static int ta_push(olui_el *el, size_t at)
{
    if (el->nlines == el->lines_cap) {
        int cap = el->lines_cap ? el->lines_cap * 2 : 16;
        size_t *l = (size_t *)realloc(el->lines, (size_t)cap * sizeof(size_t));
        if (!l) return 0;
        el->lines = l;
        el->lines_cap = cap;
    }
    el->lines[el->nlines++] = at;
    return 1;
}

/* el's text in lines no wider than room (words broken only when one is wider) */
static int ta_break(olui *ui, olui_el *el, int32_t room)
{
    const char *t = el->text ? el->text : "";
    size_t len = strlen(t), p = 0;
    el->nlines = 0;
    if (!ta_push(el, 0)) return 0;
    for (;;) {
        size_t e = p, i = p;
        int32_t x = 0;
        while (e < len && t[e] != '\n') ++e;
        while (!(el->flags & F_NOWRAP) && room > 0 && i < e) {
            size_t w1 = i, s1;
            int32_t ww;
            while (w1 < e && t[w1] != ' ') ++w1;
            s1 = w1;
            while (s1 < e && t[s1] == ' ') ++s1;
            ww = w1 > i ? wpx(ui, el, t + i, w1 - i) : 0;
            if (x > 0 && x + ww > room) {
                if (!ta_push(el, i)) return 0;
                x = 0;
            }
            if (x == 0 && ww > room) {
                /* a word wider than the line: broken between its characters */
                size_t c = i;
                while (c < w1) {
                    size_t nc = next_char(t, c);
                    int32_t cw = wpx(ui, el, t + c, nc - c);
                    if (x > 0 && x + cw > room) {
                        if (!ta_push(el, c)) return 0;
                        x = 0;
                    }
                    x += cw;
                    c = nc;
                }
            } else {
                x += ww;
            }
            if (s1 > w1) x += wpx(ui, el, t + w1, s1 - w1);
            i = s1;
        }
        if (e >= len) break;
        p = e + 1;
        if (!ta_push(el, p)) return 0;
    }
    return 1;
}

/* A textarea's inside less its padding, where its lines go; and a line's height */
static olui_rect ta_view(olui *ui, const olui_el *el, olui_rect r, int32_t *line)
{
    olui_rect in = inside(ui, el, r);
    const ol_style *st = &el->cs.ol;
    int32_t pt = px(ui, st->padding_top), pb = px(ui, st->padding_bottom);
    int32_t pl = px(ui, st->padding_left), pr = px(ui, st->padding_right);
    in.x += pl;
    in.y += pt;
    in.w -= pl + pr;
    in.h -= pt + pb;
    if (in.w < 1) in.w = 1;
    if (in.h < 1) in.h = 1;
    *line = line_px(ui, st);
    if (*line < 1) *line = 1;
    return in;
}

/* Its lines as wrapped now, worked out again when its text, width or font changed;
 * 1 when it is taller than its view (it has a scroll bar) */
static int ta_wrap(olui *ui, olui_el *el)
{
    int32_t line;
    olui_rect v = ta_view(ui, el, rect_of_node(ui, el->node), &line);
    if (el->lines && el->wrap_rev == el->text_rev && el->wrap_room == v.w &&
        el->wrap_font == el->cs.ol.font_size + (ol_unit)(el->cs.ol.text_flags & (OL_TEXT_BOLD | OL_TEXT_MONO)))
        return (int32_t)el->nlines * line > v.h;
    if (!ta_break(ui, el, v.w)) {
        el->nlines = el->lines ? 1 : 0;
        if (el->lines) el->lines[0] = 0;
        return 0;
    }
    if ((int32_t)el->nlines * line > v.h && !(el->flags & F_NOWRAP) && v.w > 2 * BAR) {
        /* a bar down its right: the lines are a little narrower */
        if (!ta_break(ui, el, v.w - BAR - 2)) el->nlines = 1;
    }
    el->wrap_rev = el->text_rev;
    el->wrap_room = v.w;
    el->wrap_font = el->cs.ol.font_size + (ol_unit)(el->cs.ol.text_flags & (OL_TEXT_BOLD | OL_TEXT_MONO));
    return (int32_t)el->nlines * line > v.h;
}

static int ta_line_of(const olui_el *el, size_t pos)
{
    int lo = 0, hi = el->nlines - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (el->lines[mid] <= pos) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* Where line i's text ends (before its newline), and the last place the caret goes on it */
static size_t ta_line_end(const olui_el *el, int i, size_t *last)
{
    const char *t = el->text ? el->text : "";
    size_t len = strlen(t), next = i + 1 < el->nlines ? el->lines[i + 1] : len, end = next;
    if (next > el->lines[i] && t[next - 1] == '\n') end = next - 1;
    if (last) *last = i + 1 >= el->nlines || end < next ? end : (next > el->lines[i] ? prev_char(t, next) : next);
    return end;
}

/* The place nearest x pixels across line i */
static size_t ta_pos_in_line(olui *ui, const olui_el *el, int i, int32_t want)
{
    const char *t = el->text ? el->text : "";
    size_t a = el->lines[i], last, p = a, best = a;
    int32_t best_d = 0x7fffffff;
    ta_line_end(el, i, &last);
    for (;;) {
        int32_t w = wpx(ui, el, t + a, p - a), d = w > want ? w - want : want - w;
        if (d < best_d) {
            best_d = d;
            best = p;
        }
        if (p >= last || w > want) break;
        p = next_char(t, p);
    }
    return best;
}

/* The place at x, y on the window */
static size_t ta_pos_at(olui *ui, olui_el *el, int x, int y)
{
    int32_t line, cy;
    olui_rect v = ta_view(ui, el, rect_of(ui, el), &line);
    int i;
    ta_wrap(ui, el);
    cy = y - v.y + el->scroll;
    i = cy < 0 ? 0 : (int)(cy / line);
    if (i >= el->nlines) i = el->nlines - 1;
    return ta_pos_in_line(ui, el, i, x - v.x + el->xoff);
}

static int32_t ta_most(olui *ui, olui_el *el)
{
    int32_t line;
    olui_rect v = ta_view(ui, el, rect_of_node(ui, el->node), &line);
    int32_t most;
    ta_wrap(ui, el);
    most = (int32_t)el->nlines * line - v.h;
    return most > 0 ? most : 0;
}

/* The caret kept in view, and the textarea drawn again */
static void ta_reveal(olui *ui, olui_el *el)
{
    int32_t line, cy, cx;
    olui_rect v;
    int i;
    if (!el->node || ui->stale) {
        ui->damaged = 1;
        return;
    }
    v = ta_view(ui, el, rect_of_node(ui, el->node), &line);
    ta_wrap(ui, el);
    i = ta_line_of(el, el->caret);
    cy = (int32_t)i * line;
    if (cy < el->scroll) el->scroll = cy;
    if (cy + line > el->scroll + v.h) el->scroll = cy + line - v.h;
    if (el->scroll > ta_most(ui, el)) el->scroll = ta_most(ui, el);
    if (el->scroll < 0) el->scroll = 0;
    cx = wpx(ui, el, el->text ? el->text + el->lines[i] : "", el->caret - el->lines[i]);
    if (cx - el->xoff > v.w - 1) el->xoff = cx - v.w + 1;
    if (cx < el->xoff) el->xoff = cx;
    if (el->xoff < 0) el->xoff = 0;
    damage_el(ui, el);
}

static void ta_selection(const olui_el *el, size_t *a, size_t *b)
{
    *a = el->anchor < el->caret ? el->anchor : el->caret;
    *b = el->anchor < el->caret ? el->caret : el->anchor;
}

/* The word round the caret, selected (a double-click) */
static void ta_word(olui_el *el)
{
    const char *t = el->text ? el->text : "";
    size_t a = el->caret, b = el->caret;
    while (a > 0 && t[a - 1] != ' ' && t[a - 1] != '\n') a = prev_char(t, a);
    while (t[b] && t[b] != ' ' && t[b] != '\n') b = next_char(t, b);
    el->anchor = a;
    el->caret = b;
}

static int ta_key(olui *ui, olui_el *f, unsigned code, unsigned qual)
{
    const char *t = f->text ? f->text : "";
    size_t len = strlen(t), a, b;
    int extend = (qual & OLUI_QUAL_SHIFT) != 0, i, move = 1, page;
    int32_t line;
    olui_rect v = ta_view(ui, f, rect_of_node(ui, f->node), &line);
    page = (int)(v.h / line) - 1;
    if (page < 1) page = 1;
    ta_wrap(ui, f);
    if (f->caret > len) f->caret = len;
    if (f->anchor > len) f->anchor = len;
    ta_selection(f, &a, &b);
    i = ta_line_of(f, f->caret);
    switch (code) {
    case OLUI_KEY_LEFT:
        f->caret = !extend && a != b ? a : prev_char(t, f->caret);
        f->want_x = -1;
        break;
    case OLUI_KEY_RIGHT:
        f->caret = !extend && a != b ? b : next_char(t, f->caret);
        f->want_x = -1;
        break;
    case OLUI_KEY_UP: case OLUI_KEY_DOWN: case OLUI_KEY_PAGEUP: case OLUI_KEY_PAGEDOWN: {
        int to = i + (code == OLUI_KEY_UP ? -1 : code == OLUI_KEY_DOWN ? 1 : code == OLUI_KEY_PAGEUP ? -page : page);
        if (f->want_x < 0) f->want_x = wpx(ui, f, t + f->lines[i], f->caret - f->lines[i]);
        if (to < 0) f->caret = 0;
        else if (to >= f->nlines) f->caret = len;
        else f->caret = ta_pos_in_line(ui, f, to, f->want_x);
        if (code == OLUI_KEY_PAGEUP || code == OLUI_KEY_PAGEDOWN) {
            f->scroll += (code == OLUI_KEY_PAGEUP ? -page : page) * line;
            if (f->scroll > ta_most(ui, f)) f->scroll = ta_most(ui, f);
            if (f->scroll < 0) f->scroll = 0;
        }
        break;
    }
    case OLUI_KEY_HOME:
        f->caret = f->lines[i];
        f->want_x = -1;
        break;
    case OLUI_KEY_END:
        ta_line_end(f, i, &f->caret);
        f->want_x = -1;
        break;
    case OLUI_KEY_BACKSPACE:
        if (a != b) text_replace(ui, f, a, b, "");
        else if (f->caret) text_replace(ui, f, prev_char(t, f->caret), f->caret, "");
        move = 0;
        break;
    case OLUI_KEY_DELETE:
        if (a != b) text_replace(ui, f, a, b, "");
        else if (f->caret < len) text_replace(ui, f, f->caret, next_char(t, f->caret), "");
        move = 0;
        break;
    case OLUI_KEY_RETURN:
        text_replace(ui, f, a, b, "\n");
        move = 0;
        break;
    default:
        if (code >= 0x20 && code < 0x110000 && code != 0x7f && code < OLUI_KEY_LEFT) {
            char u[5];
            unsigned long c = code;
            size_t n;
            if (c < 0x80) { u[0] = (char)c; n = 1; }
            else if (c < 0x800) { u[0] = (char)(0xc0 | c >> 6); u[1] = (char)(0x80 | (c & 63)); n = 2; }
            else if (c < 0x10000) { u[0] = (char)(0xe0 | c >> 12); u[1] = (char)(0x80 | (c >> 6 & 63)); u[2] = (char)(0x80 | (c & 63)); n = 3; }
            else { u[0] = (char)(0xf0 | c >> 18); u[1] = (char)(0x80 | (c >> 12 & 63)); u[2] = (char)(0x80 | (c >> 6 & 63)); u[3] = (char)(0x80 | (c & 63)); n = 4; }
            u[n] = 0;
            text_replace(ui, f, a, b, u);
            move = 0;
            break;
        }
        return 0;
    }
    if (move) {
        if (!extend) f->anchor = f->caret;
        ta_reveal(ui, f);
    }
    return 1;
}

/* ---- Selects: options, optgroups' label rows, the pop-up ---- */

/* The row after r (NULL: the first): an option or an optgroup, the options in a group after it */
static olui_el *row_after(const olui_el *sel, const olui_el *r)
{
    const olui_el *c, *up;
    if (!r) { c = sel->first; up = sel; }
    else if (r->kind == K_OPTGROUP && r->first) { c = r->first; up = r; }
    else { c = r->next; up = r->parent; }
    for (;;) {
        if (!c) {
            if (!up || up == sel) return NULL;
            c = up->next;
            up = up->parent;
            continue;
        }
        if ((c->kind == K_OPTION || c->kind == K_OPTGROUP) && !(c->state & S_HIDDEN)) return (olui_el *)c;
        c = c->next;
    }
}

static olui_el *option_at(const olui_el *sel, int index)
{
    olui_el *r;
    if (!sel || index < 0) return NULL;
    for (r = row_after(sel, NULL); r; r = row_after(sel, r))
        if (r->kind == K_OPTION && index-- == 0) return r;
    return NULL;
}

static int option_count(const olui_el *sel)
{
    olui_el *r;
    int n = 0;
    for (r = row_after(sel, NULL); r; r = row_after(sel, r)) n += r->kind == K_OPTION;
    return n;
}

/* An option's row, from 0, counting optgroups' rows */
static int row_of_option(const olui_el *sel, int index)
{
    olui_el *r;
    int row = 0;
    for (r = row_after(sel, NULL); r; r = row_after(sel, r), ++row)
        if (r->kind == K_OPTION && index-- == 0) return row;
    return -1;
}

/* The option on a row, as its index; -1 for a group's row or none */
static int option_at_row(const olui_el *sel, int row)
{
    olui_el *r;
    int index = 0;
    for (r = row_after(sel, NULL); r; r = row_after(sel, r)) {
        if (row-- == 0) return r->kind == K_OPTION ? index : -1;
        index += r->kind == K_OPTION;
    }
    return -1;
}

static const char *row_text(const olui_el *r)
{
    if (r->kind == K_OPTGROUP) return r->title ? r->title : "";
    return r->title ? r->title : r->text ? r->text : "";
}

static const char *option_value(const olui_el *opt)
{
    return opt->value ? opt->value : row_text(opt);
}

static int option_enabled(const olui_el *opt)
{
    return opt && !(opt->state & S_DISABLED) && !(opt->parent && opt->parent->kind == K_OPTGROUP &&
                                                 (opt->parent->state & S_DISABLED));
}

static int listbox(const olui_el *sel)
{
    return sel->kind == K_SELECT && sel->rows > 1;
}

/* A row's style, worked out when its states or the stylesheet changed */
static void row_style(olui *ui, olui_el *r, const olui_el *sel)
{
    const olui_el *p = r->parent && r->parent->kind == K_OPTGROUP ? r->parent : sel;
    if (p != sel) row_style(ui, (olui_el *)p, sel);
    if (r->styled_gen == ui->style_gen && r->styled_state == r->state) return;
    olui_style_compute(ui->sheet, r, &p->cs);
    r->styled_gen = ui->style_gen;
    r->styled_state = r->state;
}

/* The rows' height, pixels: the tallest */
static int32_t row_height(olui *ui, olui_el *sel)
{
    olui_el *r;
    int32_t h = 1;
    for (r = row_after(sel, NULL); r; r = row_after(sel, r)) {
        int32_t rh;
        row_style(ui, r, sel);
        rh = line_px(ui, &r->cs.ol) + px(ui, r->cs.ol.padding_top) + px(ui, r->cs.ol.padding_bottom);
        if (rh > h) h = rh;
    }
    return h;
}

/* Which option is :selected: the pop-up's pointed at, a list box's chosen */
static void mark_rows(olui_el *sel, int index)
{
    olui_el *r;
    int i = 0;
    for (r = row_after(sel, NULL); r; r = row_after(sel, r)) {
        if (r->kind != K_OPTION) continue;
        if (i++ == index) r->state |= S_SELECTED;
        else r->state &= ~(unsigned)S_SELECTED;
    }
}

/* The next enabled option from index, dir away (0: index itself or the next); index when there is none */
static int option_step(const olui_el *sel, int index, int dir)
{
    int n = option_count(sel), i = index;
    if (dir == 0) {
        for (i = index < 0 ? 0 : index; i < n; ++i)
            if (option_enabled(option_at(sel, i))) return i;
        return index;
    }
    for (;;) {
        i += dir;
        if (i < 0 || i >= n) return index;
        if (option_enabled(option_at(sel, i))) return i;
    }
}

static unsigned fold(unsigned c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7)) return c + 32;
    return c;
}

static unsigned first_char(const char *s)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) return u[0];
    if ((u[0] & 0xe0) == 0xc0 && u[1]) return (unsigned)(u[0] & 0x1f) << 6 | (u[1] & 0x3f);
    if ((u[0] & 0xf0) == 0xe0 && u[1] && u[2]) return (unsigned)(u[0] & 0x0f) << 12 | (unsigned)(u[1] & 0x3f) << 6 | (u[2] & 0x3f);
    return u[0];
}

/* The next enabled option after index whose text starts with c, round from the top */
static int option_find(const olui_el *sel, int index, unsigned c)
{
    int n = option_count(sel), k;
    for (k = 1; k <= n; ++k) {
        int i = ((index < 0 ? -1 : index) + k) % n;
        olui_el *o = option_at(sel, i);
        if (option_enabled(o) && fold(first_char(row_text(o))) == fold(c)) return i;
    }
    return index;
}

/* A list box's rows: its inside less its padding */
static olui_rect rows_view(olui *ui, const olui_el *el, olui_rect r)
{
    olui_rect in = inside(ui, el, r);
    int32_t pt = px(ui, el->cs.ol.padding_top), pb = px(ui, el->cs.ol.padding_bottom);
    int32_t pl = px(ui, el->cs.ol.padding_left), pr = px(ui, el->cs.ol.padding_right);
    in.x += pl;
    in.y += pt;
    in.w -= pl + pr;
    in.h -= pt + pb;
    if (in.w < 0) in.w = 0;
    if (in.h < 0) in.h = 0;
    return in;
}

static int32_t listbox_most(olui *ui, olui_el *sel)
{
    olui_rect v = rows_view(ui, sel, rect_of_node(ui, sel->node));
    int32_t n = 0, most;
    olui_el *r;
    for (r = row_after(sel, NULL); r; r = row_after(sel, r)) ++n;
    most = n * row_height(ui, sel) - v.h;
    return most > 0 ? most : 0;
}

static void listbox_reveal(olui *ui, olui_el *sel)
{
    olui_rect v;
    int32_t h, y;
    int row = row_of_option(sel, sel->ivalue);
    if (!sel->node || ui->stale || row < 0) return;
    v = rows_view(ui, sel, rect_of_node(ui, sel->node));
    h = row_height(ui, sel);
    y = row * h;
    if (y < sel->scroll) sel->scroll = y;
    if (y + h > sel->scroll + v.h) sel->scroll = y + h - v.h;
    if (sel->scroll > listbox_most(ui, sel)) sel->scroll = listbox_most(ui, sel);
    if (sel->scroll < 0) sel->scroll = 0;
}

/* The chosen option: OLUI_CHANGE with its index and value when send */
static void select_choose(olui *ui, olui_el *sel, int index, int send)
{
    olui_el *opt = option_at(sel, index);
    if (index == sel->ivalue || (index >= 0 && !opt)) return;
    sel->ivalue = index;
    if (listbox(sel)) {
        mark_rows(sel, index);
        listbox_reveal(ui, sel);
    }
    damage_el(ui, sel);
    if (send) emit(ui, OLUI_CHANGE, sel, opt, opt ? option_value(opt) : NULL, index, 0);
}

static void paint_state(olui *ui, olui_el *el, unsigned bit, int on);

/* Where the pop-up goes: under the select, or over it when there is more room there */
static void popup_place(olui *ui)
{
    olui_el *sel = ui->popup, *r;
    olui_cs *pc;
    olui_rect sr;
    int32_t frame, row_h, below, above, w = 0, h;
    int n = 0, vis;
    if (!sel->node || !visible(sel)) return;
    sr = rect_of(ui, sel);
    olui_style_compute(ui->sheet, ui->popup_box, &sel->cs);
    pc = &ui->popup_box->cs;
    frame = 2 * (pc->border > 0 ? px(ui, pc->border) : 0) + px(ui, pc->ol.padding_top) + px(ui, pc->ol.padding_bottom);
    row_h = row_height(ui, sel);
    for (r = row_after(sel, NULL); r; r = row_after(sel, r)) {
        const char *t = row_text(r);
        int32_t rw = wpx(ui, r, t, strlen(t)) + px(ui, r->cs.ol.padding_left) + px(ui, r->cs.ol.padding_right);
        if (rw > w) w = rw;
        ++n;
    }
    vis = n < POPUP_ROWS ? n : POPUP_ROWS;
    if (vis < 1) vis = 1;
    below = ui->height - (sr.y + sr.h);
    above = sr.y;
    h = vis * row_h + frame;
    if (h > below && h > above) {
        int32_t room = below > above ? below : above;
        if (room < row_h + frame) room = ui->height;
        vis = (int)((room - frame) / row_h);
        if (vis < 1) vis = 1;
        h = vis * row_h + frame;
    }
    ui->popup_rect.y = h <= below || below >= above ? sr.y + sr.h : sr.y - h;
    if (ui->popup_rect.y + h > ui->height) ui->popup_rect.y = ui->height - h;
    if (ui->popup_rect.y < 0) ui->popup_rect.y = 0;
    w += frame - px(ui, pc->ol.padding_top) - px(ui, pc->ol.padding_bottom) + px(ui, pc->ol.padding_left) +
         px(ui, pc->ol.padding_right) + (vis < n ? BAR + 2 : 0);
    if (w < sr.w) w = sr.w;
    if (w > ui->width) w = ui->width;
    ui->popup_rect.x = sr.x + w > ui->width ? ui->width - w : sr.x;
    if (ui->popup_rect.x < 0) ui->popup_rect.x = 0;
    ui->popup_rect.w = w;
    ui->popup_rect.h = h;
    ui->popup_rows = vis;
    ui->popup_row_h = row_h;
}

/* The pointed at option's row kept in view */
static void popup_reveal(olui *ui)
{
    int row = row_of_option(ui->popup, ui->popup_hot), n = 0;
    olui_el *r;
    for (r = row_after(ui->popup, NULL); r; r = row_after(ui->popup, r)) ++n;
    if (row >= 0 && row < ui->popup_top) ui->popup_top = row;
    if (row >= ui->popup_top + ui->popup_rows) ui->popup_top = row - ui->popup_rows + 1;
    if (ui->popup_top > n - ui->popup_rows) ui->popup_top = n - ui->popup_rows;
    if (ui->popup_top < 0) ui->popup_top = 0;
}

static void tip_hide(olui *ui, int until_moved);

static void popup_open(olui *ui, olui_el *sel)
{
    if (!sel || sel->kind != K_SELECT || listbox(sel) || ui->popup == sel || !visible(sel) || !enabled(sel) ||
        !option_count(sel) || !fresh(ui))
        return;
    if (!ui->popup_box && !(ui->popup_box = olui_el_new(K_BOX, "popup"))) return;
    tip_hide(ui, 1);
    ui->popup_box->parent = sel;
    ui->popup = sel;
    ui->popup_hot = option_step(sel, sel->ivalue, 0);
    ui->popup_top = 0;
    ui->popup_fresh = 0;
    mark_rows(sel, ui->popup_hot);
    popup_place(ui);
    popup_reveal(ui);
    paint_state(ui, sel, S_OPEN, 1);
    damage(ui, ui->popup_rect);
}

/* Closes the pop-up; the option pointed at is chosen when choose */
static void popup_close(olui *ui, int choose)
{
    olui_el *sel = ui->popup;
    if (!sel) return;
    ui->popup = NULL;
    damage(ui, ui->popup_rect);
    mark_rows(sel, -1);
    paint_state(ui, sel, S_OPEN, 0);
    if (choose && option_enabled(option_at(sel, ui->popup_hot))) select_choose(ui, sel, ui->popup_hot, 1);
}

static void popup_point(olui *ui, int index)
{
    if (index == ui->popup_hot) return;
    ui->popup_hot = index;
    mark_rows(ui->popup, index);
    popup_reveal(ui);
    damage(ui, ui->popup_rect);
}

/* The pop-up's rows: inside its frame */
static olui_rect popup_rows_rect(olui *ui)
{
    olui_rect in = inside(ui, ui->popup_box, ui->popup_rect);
    const ol_style *st = &ui->popup_box->cs.ol;
    in.x += px(ui, st->padding_left);
    in.y += px(ui, st->padding_top);
    in.w -= px(ui, st->padding_left) + px(ui, st->padding_right);
    in.h -= px(ui, st->padding_top) + px(ui, st->padding_bottom);
    return in;
}

static int popup_key(olui *ui, unsigned code)
{
    olui_el *sel = ui->popup;
    int hot = ui->popup_hot, i;
    switch (code) {
    case OLUI_KEY_UP: popup_point(ui, option_step(sel, hot, -1)); return 1;
    case OLUI_KEY_DOWN: popup_point(ui, option_step(sel, hot, 1)); return 1;
    case OLUI_KEY_PAGEUP: case OLUI_KEY_PAGEDOWN:
        for (i = 1; i < ui->popup_rows; ++i) hot = option_step(sel, hot, code == OLUI_KEY_PAGEUP ? -1 : 1);
        popup_point(ui, hot);
        return 1;
    case OLUI_KEY_HOME: popup_point(ui, option_step(sel, -1, 1)); return 1;
    case OLUI_KEY_END: popup_point(ui, option_step(sel, option_count(sel), -1)); return 1;
    case OLUI_KEY_RETURN: case ' ': popup_close(ui, 1); return 1;
    case OLUI_KEY_ESCAPE: popup_close(ui, 0); return 1;
    default:
        if (code > ' ' && code < OLUI_KEY_LEFT) popup_point(ui, option_find(sel, hot, code));
        return 1;
    }
}

/* The pointer while the pop-up is open: -1 when it is not the pop-up's */
static int popup_mouse(olui *ui, int x, int y, int what)
{
    olui_rect rows = popup_rows_rect(ui);
    int inside_it = in_rect(ui->popup_rect, x, y);
    int index = -1;
    if (in_rect(rows, x, y))
        index = option_at_row(ui->popup, ui->popup_top + (int)((y - rows.y) / ui->popup_row_h));
    switch (what) {
    case OLUI_MOUSE_MOVE:
        if (inside_it) {
            ui->popup_fresh = 0;
            if (index >= 0 && option_enabled(option_at(ui->popup, index))) popup_point(ui, index);
        }
        return 1;
    case OLUI_MOUSE_DOWN:
    case OLUI_MOUSE_DOUBLE:
        if (!inside_it) {
            /* a press elsewhere closes it, and does nothing else */
            popup_close(ui, 0);
            return 1;
        }
        ui->popup_fresh = 0;
        if (index >= 0 && option_enabled(option_at(ui->popup, index))) popup_point(ui, index);
        return 1;
    case OLUI_MOUSE_UP:
        if (ui->popup_fresh) {
            /* the release of the press that opened it */
            ui->popup_fresh = 0;
            return 1;
        }
        if (inside_it && index >= 0 && option_enabled(option_at(ui->popup, index))) {
            popup_point(ui, index);
            popup_close(ui, 1);
        }
        return 1;
    case OLUI_WHEEL_UP:
    case OLUI_WHEEL_DOWN:
        if (inside_it) {
            int n = 0, top = ui->popup_top + (what == OLUI_WHEEL_UP ? -3 : 3);
            olui_el *r;
            for (r = row_after(ui->popup, NULL); r; r = row_after(ui->popup, r)) ++n;
            if (top > n - ui->popup_rows) top = n - ui->popup_rows;
            if (top < 0) top = 0;
            if (top != ui->popup_top) {
                ui->popup_top = top;
                damage(ui, ui->popup_rect);
            }
        }
        return 1;
    default:
        return -1;
    }
}

static int select_key(olui *ui, olui_el *f, unsigned code)
{
    int at = f->ivalue, to = at, i;
    switch (code) {
    case OLUI_KEY_UP: to = option_step(f, at, -1); break;
    case OLUI_KEY_DOWN: to = at < 0 ? option_step(f, 0, 0) : option_step(f, at, 1); break;
    case OLUI_KEY_HOME: to = option_step(f, -1, 1); break;
    case OLUI_KEY_END: to = option_step(f, option_count(f), -1); break;
    case OLUI_KEY_PAGEUP: case OLUI_KEY_PAGEDOWN: {
        int page = 1;
        if (listbox(f) && f->node) page = (int)(rows_view(ui, f, rect_of_node(ui, f->node)).h / row_height(ui, f)) - 1;
        for (i = 0; i < (page > 1 ? page : 1); ++i) to = option_step(f, to, code == OLUI_KEY_PAGEUP ? -1 : 1);
        break;
    }
    case OLUI_KEY_RETURN: case ' ':
        if (listbox(f)) return 0;
        popup_open(ui, f);
        return 1;
    default:
        if (code > ' ' && code < OLUI_KEY_LEFT) {
            to = option_find(f, at, code);
            break;
        }
        return 0;
    }
    if (to >= 0) select_choose(ui, f, to, 1);
    return 1;
}

/* ---- Tooltips ---- */

/* The element whose title the pointer shows: el or one it is in (a tab's,
 * an item's or a checkbox's title that is its text is not a tip) */
static olui_el *tip_target(olui_el *el)
{
    for (; el; el = el->parent) {
        if (!el->title || !el->title[0] || el->kind == K_TAB || el->kind == K_OPTION || el->kind == K_OPTGROUP)
            continue;
        if ((el->kind == K_CHECK || el->kind == K_RADIO || el->kind == K_ITEM) && !el->text) continue;
        return el;
    }
    return NULL;
}

static void tip_hide(olui *ui, int until_moved)
{
    if (ui->tip_on) damage(ui, ui->tip_rect);
    ui->tip_on = 0;
    ui->tip_rest = until_moved ? -1 : 0;
}

static void tip_show(olui *ui)
{
    olui_el *el = ui->tip_for;
    olui_cs *cs;
    int32_t b, w, h;
    if (!ui->tip_box && !(ui->tip_box = olui_el_new(K_BOX, "tooltip"))) return;
    ui->tip_box->parent = el;
    olui_style_compute(ui->sheet, ui->tip_box, &el->cs);
    cs = &ui->tip_box->cs;
    b = cs->border > 0 ? px(ui, cs->border) : 0;
    w = wpx(ui, ui->tip_box, el->title, strlen(el->title)) + px(ui, cs->ol.padding_left) +
        px(ui, cs->ol.padding_right) + 2 * b;
    h = line_px(ui, &cs->ol) + px(ui, cs->ol.padding_top) + px(ui, cs->ol.padding_bottom) + 2 * b;
    if (w > ui->width) w = ui->width;
    ui->tip_rect.w = w;
    ui->tip_rect.h = h;
    ui->tip_rect.x = ui->tip_x + w > ui->width ? ui->width - w : ui->tip_x;
    ui->tip_rect.y = ui->tip_y + 20 + h > ui->height ? ui->tip_y - h - 4 : ui->tip_y + 20;
    if (ui->tip_rect.x < 0) ui->tip_rect.x = 0;
    if (ui->tip_rect.y < 0) ui->tip_rect.y = 0;
    ui->tip_on = 1;
    damage(ui, ui->tip_rect);
}

/* ---- Labels ---- */

static olui_el *label_of(olui_el *el)
{
    for (; el; el = el->parent)
        if (el->kind == K_BOX && !strcmp(el->tag, "label")) return el;
    return NULL;
}

static olui_el *find(olui_el *el, const char *id);

/* The first control in el (or el) */
static olui_el *widget_in(olui_el *el)
{
    olui_el *c, *w;
    switch (el->kind) {
    case K_BUTTON: case K_CHECK: case K_RADIO: case K_INPUT: case K_SELECT: case K_TEXTAREA:
    case K_SLIDER: case K_LIST:
        return el;
    default:
        break;
    }
    for (c = el->first; c; c = c->next)
        if ((w = widget_in(c))) return w;
    return NULL;
}

/* What clicking a label works: its for's control, or the one inside it */
static olui_el *label_target(olui *ui, olui_el *lab)
{
    olui_el *c;
    if (lab->for_id) return find(ui->root, lab->for_id);
    for (c = lab->first; c; c = c->next) {
        olui_el *w = widget_in(c);
        if (w) return w;
    }
    return NULL;
}

/* ---- Damage and scrolling for all of them ---- */

/* A widget drawn again where it is (cut to the boxes it scrolls in), without a layout */
static void damage_el(olui *ui, olui_el *el)
{
    int32_t dy;
    olui_rect r, clip;
    int clipped;
    if (ui->stale || !el->node) {
        ui->damaged = 1;
        return;
    }
    if (!visible(el)) return;
    r = rect_of_node(ui, el->node);
    r.x -= 2;
    r.y -= 2;
    r.w += 4;
    r.h += 4;
    clipped = view_of(ui, el, &dy, &clip);
    r.y -= dy;
    if (clipped) r = cut(r, clip);
    damage(ui, r);
}

/* How far a scrolling thing scrolls at most, pixels */
static int32_t scroll_most(olui *ui, olui_el *el)
{
    int32_t most;
    if (!el->node) return 0;
    if (el->kind == K_TEXTAREA) return ta_most(ui, el);
    if (listbox(el)) return listbox_most(ui, el);
    if (el->kind == K_LIST) {
        olui_rect in = inside(ui, el, rect_of_node(ui, el->node));
        most = list_content_height(ui, el) - in.h;
    } else {
        olui_rect in = inside(ui, el, rect_of_node(ui, el->node));
        most = box_content_height(ui, el) - in.h;
    }
    return most > 0 ? most : 0;
}

static int scrolls(const olui_el *el)
{
    return scroller(el) || el->kind == K_TEXTAREA || listbox(el);
}

/* Scrolls el to y: what it shows drawn again */
static int scroll_set(olui *ui, olui_el *el, int32_t y)
{
    int32_t most = scroll_most(ui, el);
    if (y > most) y = most;
    if (y < 0) y = 0;
    if (y == el->scroll) return 0;
    el->scroll = y;
    damage_el(ui, el);
    return 1;
}

/* The boxes el is in scrolled so it shows (the keyboard went to it) */
static void reveal(olui *ui, olui_el *el)
{
    olui_el *a;
    for (a = el->parent; a; a = a->parent) {
        olui_rect in, r;
        if (!scroller(a) || a->kind == K_LIST || !a->node) continue;
        in = inside(ui, a, rect_of_node(ui, a->node));
        r = rect_of_node(ui, el->node);
        r.y -= scroll_above(el) - scroll_above(a) - a->scroll;     /* where it is in a's content */
        if (r.y - a->scroll < in.y) scroll_set(ui, a, r.y - in.y);
        else if (r.y + r.h - a->scroll > in.y + in.h) scroll_set(ui, a, r.y + r.h - in.y - in.h);
    }
}

/* ---- Input ----------------------------------------------------------------------- */

static void set_focus(olui *ui, olui_el *el)
{
    olui_el *was = ui->focus;
    if (was == el) return;
    if (ui->popup) popup_close(ui, 0);
    if (was) set_state(ui, was, S_FOCUS, 0);
    ui->focus = el;
    /* a number typed is held to its min and max when the keyboard goes elsewhere */
    if (was && (was->flags & F_NUMBER)) number_commit(ui, was);
    if (el) {
        set_state(ui, el, S_FOCUS, 1);
        emit(ui, OLUI_FOCUS, el, NULL, NULL, -1, 0);
    }
}

void olui_focus(olui *ui, olui_el *el)
{
    if (ui) set_focus(ui, el);
}

olui_el *olui_focused(const olui *ui)
{
    return ui ? ui->focus : NULL;
}

/* A label clicked: its control focused, and pressed when it is a button, checkbox or radio button */
static void label_click(olui *ui, olui_el *lab)
{
    olui_el *t = label_target(ui, lab);
    if (!t || !visible(t) || !enabled(t)) return;
    if (focusable(t)) set_focus(ui, t);
    if (t->kind == K_BUTTON || t->kind == K_CHECK || t->kind == K_RADIO) press(ui, t);
}

/* The innermost thing under the pointer that scrolls and can (overflow hidden: only by the program) */
static olui_el *wheel_target(olui *ui, olui_el *el)
{
    for (; el; el = el->parent)
        if (scrolls(el) && el->cs.overflow != O_HIDDEN && visible(el) && scroll_most(ui, el) > 0) return el;
    return NULL;
}

int olui_mouse(olui *ui, int x, int y, int what)
{
    olui_el *under, *w;
    if (!ui || !ui->root || !fresh(ui)) return 0;
    if (ui->popup) {
        int r = popup_mouse(ui, x, y, what);
        if (r >= 0) return r;
    }
    under = hit(ui, ui->root, x, y);
    w = widget_of(under);
    if (w && !enabled(w)) w = NULL;

    switch (what) {
    case OLUI_MOUSE_MOVE: {
        olui_el *tip = tip_target(under);
        if (tip != ui->tip_for) {
            tip_hide(ui, 0);
            ui->tip_for = tip;
        } else if (!ui->tip_on && ui->tip_rest > 0) {
            ui->tip_rest = 0;       /* it rests from here */
        }
        ui->tip_x = x;
        ui->tip_y = y;
        if (ui->hover != w) {
            set_state(ui, ui->hover, S_HOVER, 0);
            ui->hover = w;
            set_state(ui, w, S_HOVER, 1);
        }
        /* a slider follows the pointer while it is held */
        if (ui->pressed && ui->pressed->kind == K_SLIDER) {
            slider_to(ui, ui->pressed, x);
            return 1;
        }
        /* a textarea's selection follows it */
        if (ui->drag) {
            ui->drag->caret = ta_pos_at(ui, ui->drag, x, y);
            ta_reveal(ui, ui->drag);
            return 1;
        }
        /* a pressed button looks pressed only while the pointer is on it */
        if (ui->pressed) set_state(ui, ui->pressed, S_ACTIVE, ui->pressed == w);
        return w != NULL;
    }
    case OLUI_MOUSE_DOWN:
    case OLUI_MOUSE_DOUBLE:
        tip_hide(ui, 1);
        if (!w) {
            olui_el *lab = label_of(under);
            if (lab && label_target(ui, lab)) {
                ui->label_down = lab;
                return 1;
            }
            return 0;
        }
        if (focusable(w)) set_focus(ui, w);
        else if (w->kind == K_ITEM) set_focus(ui, list_of(w));
        if (w->kind == K_INPUT) {
            olui_rect sb = spin_box(ui, w, rect_of(ui, w));
            if ((w->flags & F_NUMBER) && in_rect(sb, x, y)) {
                /* an arrow button: a step now, more while it is held (olui_tick) */
                ui->spin = w;
                ui->spin_dir = y < sb.y + sb.h / 2 ? 1 : -1;
                ui->spin_wait = 400;
                number_step(ui, w, ui->spin_dir);
                damage_el(ui, w);
                return 1;
            }
            w->caret = w->anchor = caret_at(ui, w, x);
            damage_el(ui, w);
        } else if (w->kind == K_TEXTAREA) {
            w->caret = ta_pos_at(ui, w, x, y);
            w->want_x = -1;
            if (what == OLUI_MOUSE_DOUBLE) ta_word(w);
            else w->anchor = w->caret;
            ui->drag = w;
            ta_reveal(ui, w);
        } else if (w->kind == K_SELECT) {
            if (listbox(w)) {
                olui_rect v = rows_view(ui, w, rect_of(ui, w));
                int index = in_rect(v, x, y) ? option_at_row(w, (int)((y - v.y + w->scroll) / row_height(ui, w))) : -1;
                if (index >= 0 && option_enabled(option_at(w, index))) select_choose(ui, w, index, 1);
            } else {
                popup_open(ui, w);
                ui->popup_fresh = ui->popup != NULL;
            }
        } else if (w->kind == K_ITEM) {
            olui_el *list = list_of(w);
            olui_rect r = rect_of(ui, w);
            int32_t arrow_x = r.x + px(ui, w->cs.ol.padding_left + w->depth * PX(INDENT));
            if (has_items(w) && x >= arrow_x - 2 && x < arrow_x + px(ui, PX(14))) {
                olui_item_open(ui, w, !(w->state & S_OPEN));
            } else if (list) {
                item_choose(ui, list, w);
                if (what == OLUI_MOUSE_DOUBLE)
                    emit(ui, OLUI_ACTIVATE, list, w, w->value ? w->value : (w->id ? w->id : ""),
                         item_index(ui, list, w), 0);
            }
        } else if (w->kind != K_LIST) {
            ui->pressed = w;
            set_state(ui, w, S_ACTIVE, 1);
            if (w->kind == K_SLIDER) slider_to(ui, w, x);
        }
        return 1;
    case OLUI_MOUSE_UP:
        if (ui->spin) {
            damage_el(ui, ui->spin);
            ui->spin = NULL;
            return 1;
        }
        if (ui->drag) {
            ui->drag = NULL;
            return 1;
        }
        if (ui->label_down) {
            olui_el *lab = ui->label_down;
            ui->label_down = NULL;
            if (label_of(under) == lab) label_click(ui, lab);
            return 1;
        }
        if (ui->pressed) {
            olui_el *p = ui->pressed;
            ui->pressed = NULL;
            set_state(ui, p, S_ACTIVE, 0);
            if (p == w && p->kind != K_SLIDER) press(ui, p);
            return 1;
        }
        return 0;
    case OLUI_WHEEL_UP:
    case OLUI_WHEEL_DOWN: {
        olui_el *s = wheel_target(ui, under);
        int32_t step;
        tip_hide(ui, 1);
        if (!s) return list_of(under) != NULL;
        if (s->kind == K_TEXTAREA) step = 3 * line_px(ui, &s->cs.ol);
        else if (listbox(s)) step = 3 * row_height(ui, s);
        else step = 3 * px(ui, s->cs.ol.line_height > 0 ? s->cs.ol.line_height : PX(16));
        scroll_set(ui, s, s->scroll + (what == OLUI_WHEEL_UP ? -step : step));
        return 1;
    }
    default:
        return 0;
    }
}

int olui_tick(olui *ui, unsigned ms)
{
    int changed = 0;
    if (!ui || !ui->root) return 0;
    if (ui->spin) {
        int n = 0;
        ui->spin_wait -= (int)(ms > 10000 ? 10000 : ms);
        while (ui->spin_wait <= 0 && n++ < 10) {
            number_step(ui, ui->spin, ui->spin_dir);
            ui->spin_wait += 80;
            changed = 1;
        }
        if (ui->spin_wait <= 0) ui->spin_wait = 80;
    }
    if (ui->tip_for && !ui->tip_on && ui->tip_rest >= 0 && !ui->popup && !ui->pressed && !ui->drag && !ui->spin) {
        ui->tip_rest += (int)(ms > OLUI_TIP_DELAY ? OLUI_TIP_DELAY : ms);
        if (ui->tip_rest >= OLUI_TIP_DELAY && fresh(ui) && visible(ui->tip_for)) {
            tip_show(ui);
            changed = 1;
        }
    }
    return changed;
}

static void collect_focusable(olui_el *el, olui_el **out, int *n, int max)
{
    olui_el *c;
    if (el->kind != K_STRIP && !visible(el)) return;
    if (focusable(el) && *n < max) out[(*n)++] = el;
    if (el->kind == K_LIST || el->kind == K_SELECT) return;
    for (c = el->first; c; c = c->next) collect_focusable(c, out, n, max);
}

/* The shown item step rows from item in list (step 0: the first; from NULL
 * and step -1: the last), held at the ends */
static olui_el *visible_item(olui *ui, olui_el *list, olui_el *from, int step)
{
    olui_el *prev = NULL, *last = NULL;
    size_t i;
    int after = 0;
    for (i = 0; i < ui->map_count; ++i) {
        olui_el *e = ui->map[i];
        if (!e || e->kind != K_ITEM || list_of(e) != list || !visible(e)) continue;
        if (step == 0) return e;
        if (after) return e;
        if (e == from) {
            if (step < 0) return prev ? prev : e;
            after = 1;
        }
        prev = e;
        last = e;
    }
    return last;
}

/* Tab's order: tabindex above 0 first, lowest first; then 0 (or none) in the document's order; below 0 never */
static int tab_order(olui *ui, olui_el **out, int max)
{
    olui_el *all[256];
    int n = 0, m = 0, i, j;
    collect_focusable(ui->root, all, &n, 256);
    for (i = 0; i < n; ++i) {
        if (all[i]->tabindex <= 0) continue;
        for (j = m; j > 0 && out[j - 1]->tabindex > all[i]->tabindex; --j) out[j] = out[j - 1];
        out[j] = all[i];
        if (++m == max) return m;
    }
    for (i = 0; i < n && m < max; ++i)
        if (all[i]->tabindex == 0) out[m++] = all[i];
    return m;
}

int olui_key(olui *ui, unsigned code, unsigned qual)
{
    olui_el *f;
    if (!ui || !ui->root || !fresh(ui)) return 0;
    f = ui->focus;
    tip_hide(ui, 1);
    if (ui->popup) {
        if (code != OLUI_KEY_TAB) return popup_key(ui, code);
        popup_close(ui, 0);
    }
    if (code == OLUI_KEY_TAB) {
        olui_el *all[256];
        int n = tab_order(ui, all, 256), i, at = -1;
        if (!n) return 0;
        for (i = 0; i < n; ++i) if (all[i] == f) at = i;
        if (qual & OLUI_QUAL_SHIFT) at = at <= 0 ? n - 1 : at - 1;
        else at = at < 0 || at == n - 1 ? 0 : at + 1;
        set_focus(ui, all[at]);
        reveal(ui, all[at]);
        return 1;
    }
    if (qual & OLUI_QUAL_COMMAND) {
        /* the Amiga key with x, c, v and a */
        switch (code) {
        case 'x': case 'X': return olui_edit(ui, OLUI_CUT);
        case 'c': case 'C': return olui_edit(ui, OLUI_COPY);
        case 'v': case 'V': return olui_edit(ui, OLUI_PASTE);
        case 'a': case 'A':
            if (f && (f->kind == K_TEXTAREA || f->kind == K_INPUT)) {
                olui_set_selection(ui, f, 0, f->text ? strlen(f->text) : 0);
                return 1;
            }
            return 0;
        default: return 0;
        }
    }
    if (!f || !enabled(f)) return 0;
    if ((qual & OLUI_QUAL_SHIFT) && f->kind != K_TEXTAREA && code >= OLUI_KEY_LEFT && code <= OLUI_KEY_DOWN) {
        /* Shift with an arrow: a page, or an end */
        code = code == OLUI_KEY_UP ? OLUI_KEY_PAGEUP : code == OLUI_KEY_DOWN ? OLUI_KEY_PAGEDOWN :
               code == OLUI_KEY_LEFT ? OLUI_KEY_HOME : OLUI_KEY_END;
        qual &= ~OLUI_QUAL_SHIFT;
    }

    switch (f->kind) {
    case K_BUTTON:
    case K_CHECK:
    case K_RADIO:
    case K_TABBTN:
        if (code == ' ' || code == OLUI_KEY_RETURN) {
            press(ui, f);
            return 1;
        }
        if (f->kind == K_TABBTN && (code == OLUI_KEY_LEFT || code == OLUI_KEY_RIGHT) && f->tab) {
            olui_el *tabs = f->tab->parent;
            int cur = olui_tabs_current(tabs), n = 0;
            olui_el *t;
            for (t = tabs->first; t; t = t->next) if (t->kind == K_TAB) ++n;
            cur += code == OLUI_KEY_LEFT ? -1 : 1;
            if (cur >= 0 && cur < n) {
                /* the strip's buttons are in the tabs' order: the focus goes with the choice */
                olui_el *b = tabs->first && tabs->first->kind == K_STRIP ? tabs->first->first : NULL;
                olui_tabs_select(ui, tabs, cur);
                for (; b && cur; b = b->next) --cur;
                if (b) set_focus(ui, b);
            }
            return 1;
        }
        return 0;
    case K_SLIDER: {
        int step = (f->imax - f->imin) / 10;
        if (step < 1) step = 1;
        switch (code) {
        case OLUI_KEY_LEFT: case OLUI_KEY_DOWN: slider_set(ui, f, f->ivalue - 1); return 1;
        case OLUI_KEY_RIGHT: case OLUI_KEY_UP: slider_set(ui, f, f->ivalue + 1); return 1;
        case OLUI_KEY_PAGEDOWN: slider_set(ui, f, f->ivalue - step); return 1;
        case OLUI_KEY_PAGEUP: slider_set(ui, f, f->ivalue + step); return 1;
        case OLUI_KEY_HOME: slider_set(ui, f, f->imin); return 1;
        case OLUI_KEY_END: slider_set(ui, f, f->imax); return 1;
        default: return 0;
        }
    }
    case K_SELECT:
        return select_key(ui, f, code);
    case K_TEXTAREA:
        return ta_key(ui, f, code, qual);
    case K_INPUT: {
        const char *t = f->text ? f->text : "";
        size_t len = strlen(t), a, b;
        if (f->caret > len) f->caret = len;
        if (f->anchor > len) f->anchor = len;
        ta_selection(f, &a, &b);
        if (f->flags & F_NUMBER) {
            /* a number field: Up and Down a step, PageUp and PageDown ten */
            switch (code) {
            case OLUI_KEY_UP: number_step(ui, f, 1); return 1;
            case OLUI_KEY_DOWN: number_step(ui, f, -1); return 1;
            case OLUI_KEY_PAGEUP: number_step(ui, f, 10); return 1;
            case OLUI_KEY_PAGEDOWN: number_step(ui, f, -10); return 1;
            case OLUI_KEY_RETURN: number_commit(ui, f); break;
            default: break;
            }
        }
        switch (code) {
        case OLUI_KEY_LEFT: f->caret = prev_char(t, f->caret); break;
        case OLUI_KEY_RIGHT: f->caret = next_char(t, f->caret); break;
        case OLUI_KEY_HOME: f->caret = 0; break;
        case OLUI_KEY_END: f->caret = len; break;
        case OLUI_KEY_BACKSPACE:
            if (a != b) text_replace(ui, f, a, b, NULL);
            else if (f->caret) text_replace(ui, f, prev_char(t, f->caret), f->caret, NULL);
            break;
        case OLUI_KEY_DELETE:
            if (a != b) text_replace(ui, f, a, b, NULL);
            else if (f->caret < len) text_replace(ui, f, f->caret, next_char(t, f->caret), NULL);
            break;
        case OLUI_KEY_RETURN:
            emit(ui, OLUI_SUBMIT, f, NULL, (f->flags & F_PASSWORD) ? NULL : f->text ? f->text : "",
                 (f->flags & F_NUMBER) ? f->ivalue : -1, 0);
            break;
        default:
            if (code >= 0x20 && code < 0x110000 && code != 0x7f) {
                char u[5];
                size_t n;
                unsigned long c = code;
                if (c < 0x80) { u[0] = (char)c; n = 1; }
                else if (c < 0x800) { u[0] = (char)(0xc0 | c >> 6); u[1] = (char)(0x80 | (c & 63)); n = 2; }
                else if (c < 0x10000) { u[0] = (char)(0xe0 | c >> 12); u[1] = (char)(0x80 | (c >> 6 & 63)); u[2] = (char)(0x80 | (c & 63)); n = 3; }
                else { u[0] = (char)(0xf0 | c >> 18); u[1] = (char)(0x80 | (c >> 12 & 63)); u[2] = (char)(0x80 | (c >> 6 & 63)); u[3] = (char)(0x80 | (c & 63)); n = 4; }
                u[n] = 0;
                text_replace(ui, f, a, b, u);      /* in the selection's place */
            } else {
                return 0;
            }
        }
        f->anchor = f->caret;
        damage_el(ui, f);
        return 1;
    }
    case K_LIST: {
        olui_el *sel = olui_list_selected(f), *to = NULL;
        int i;
        switch (code) {
        case OLUI_KEY_UP: to = visible_item(ui, f, sel, sel ? -1 : 0); break;
        case OLUI_KEY_DOWN: to = visible_item(ui, f, sel, sel ? 1 : 0); break;
        case OLUI_KEY_HOME: to = visible_item(ui, f, NULL, 0); break;
        case OLUI_KEY_END: to = visible_item(ui, f, NULL, -1); break;
        case OLUI_KEY_PAGEUP:
        case OLUI_KEY_PAGEDOWN:
            to = sel;
            for (i = 0; i < 8 && to; ++i) to = visible_item(ui, f, to, code == OLUI_KEY_PAGEUP ? -1 : 1);
            if (!to) to = visible_item(ui, f, NULL, 0);
            break;
        case OLUI_KEY_RIGHT:
            if (sel && has_items(sel) && !(sel->state & S_OPEN)) olui_item_open(ui, sel, 1);
            else if (sel) to = visible_item(ui, f, sel, 1);
            break;
        case OLUI_KEY_LEFT:
            if (sel && has_items(sel) && (sel->state & S_OPEN)) olui_item_open(ui, sel, 0);
            else if (sel && sel->parent && sel->parent->kind == K_ITEM) to = sel->parent;
            break;
        case OLUI_KEY_RETURN:
            if (sel) emit(ui, OLUI_ACTIVATE, f, sel, sel->value ? sel->value : (sel->id ? sel->id : ""),
                          item_index(ui, f, sel), 0);
            return sel != NULL;
        default:
            return 0;
        }
        if (to && to != sel) {
            item_choose(ui, f, to);
            scroll_to(ui, f, to);
        }
        return 1;
    }
    default:
        return 0;
    }
}

/* ---- Editing: the clipboard's text, through the program ---------------------------- */

int olui_get_selection(const olui_el *el, size_t *from, size_t *to)
{
    size_t a = 0, b = 0;
    if (el && (el->kind == K_TEXTAREA || el->kind == K_INPUT)) ta_selection(el, &a, &b);
    if (from) *from = a;
    if (to) *to = b;
    return a != b;
}

void olui_set_selection(olui *ui, olui_el *el, size_t from, size_t to)
{
    const char *t;
    size_t len;
    if (!ui || !el || (el->kind != K_TEXTAREA && el->kind != K_INPUT)) return;
    t = el->text ? el->text : "";
    len = strlen(t);
    if (from > len) from = len;
    if (to > len) to = len;
    while (from > 0 && ((unsigned char)t[from] & 0xc0) == 0x80) --from;
    while (to > 0 && ((unsigned char)t[to] & 0xc0) == 0x80) --to;
    el->anchor = from;
    el->caret = to;
    el->want_x = -1;
    if (el->kind == K_TEXTAREA && fresh(ui)) ta_reveal(ui, el);
    else damage_el(ui, el);
}

int olui_insert_text(olui *ui, olui_el *el, const char *utf8)
{
    size_t a, b;
    if (!ui || !el || !utf8 || (el->kind != K_TEXTAREA && el->kind != K_INPUT) || !fresh(ui)) return 0;
    ta_selection(el, &a, &b);
    if (el->kind == K_INPUT && a == b) a = b = el->caret;
    return text_replace(ui, el, a, b, utf8);
}

int olui_edit(olui *ui, int what)
{
    olui_el *f;
    size_t a, b;
    if (!ui || !(f = ui->focus) || (f->kind != K_TEXTAREA && f->kind != K_INPUT) || !enabled(f)) return 0;
    ta_selection(f, &a, &b);
    if (f->kind == K_INPUT && a == b) {
        /* a field without a selection: all of it */
        a = 0;
        b = f->text ? strlen(f->text) : 0;
    }
    switch (what) {
    case OLUI_CUT:
    case OLUI_COPY: {
        char *s;
        if (a == b || (f->flags & F_PASSWORD) || (what == OLUI_CUT && (f->flags & F_READONLY))) return 0;
        if (!(s = (char *)malloc(b - a + 1))) return 0;
        memcpy(s, f->text + a, b - a);
        s[b - a] = 0;
        emit(ui, what, f, NULL, s, -1, 0);
        free(s);
        if (what == OLUI_CUT && ui->focus == f) text_replace(ui, f, a, b, NULL);
        return 1;
    }
    case OLUI_PASTE:
        if (f->flags & F_READONLY) return 0;
        emit(ui, OLUI_PASTE, f, NULL, NULL, -1, 0);
        return 1;
    default:
        return 0;
    }
}

/* ---- Drawing --------------------------------------------------------------------- */

typedef struct {
    olui *ui;
    olui_draw_fn fn;
    void *userdata;
    const olui_rect *area;
} drawer;

static int crosses(olui_rect a, const olui_rect *b)
{
    return !b || (a.x < b->x + b->w && b->x < a.x + a.w && a.y < b->y + b->h && b->y < a.y + a.h);
}

/* Where a text's letters may reach past its box (a slant, a kern): half
 * its size and a little more all round. Drawing part of the window draws
 * the texts that reach into it too. */
static olui_rect ink(olui_rect r, int32_t font_pixels)
{
    int32_t m = font_pixels / 2 + 2;
    r.x -= m;
    r.y -= m;
    r.w += 2 * m;
    r.h += 2 * m;
    return r;
}

static void out_box(drawer *d, const olui_el *el, olui_rect r, const olui_cs *cs)
{
    olui_draw_op op;
    int i;
    if (!cs->stops && cs->border <= 0) return;
    if (!crosses(r, d->area)) return;
    memset(&op, 0, sizeof(op));
    op.kind = OLUI_DRAW_BOX;
    op.box = r;
    op.stops = cs->stops;
    for (i = 0; i < cs->stops; ++i) {
        op.stop_argb[i] = cs->stop_argb[i];
        op.stop_pos[i] = cs->stop_pos[i];
    }
    op.border = cs->border > 0 ? px(d->ui, cs->border) : 0;
    op.border_argb = cs->border_argb;
    op.radius = cs->radius > 0 ? px(d->ui, cs->radius) : 0;
    op.el = el;
    d->fn(d->userdata, &op);
}

static void out_flat(drawer *d, const olui_el *el, olui_rect r, uint32_t fill, uint32_t border, int bw, int radius)
{
    olui_draw_op op;
    if (!crosses(r, d->area)) return;
    memset(&op, 0, sizeof(op));
    op.kind = OLUI_DRAW_BOX;
    op.box = r;
    op.stops = (fill >> 24) ? 1 : 0;
    op.stop_argb[0] = fill;
    op.border = bw;
    op.border_argb = border;
    op.radius = radius;
    op.el = el;
    d->fn(d->userdata, &op);
}

static void out_mark(drawer *d, const olui_el *el, olui_rect r, int mark, uint32_t argb)
{
    olui_draw_op op;
    if (!crosses(r, d->area)) return;
    memset(&op, 0, sizeof(op));
    op.kind = OLUI_DRAW_MARK;
    op.box = r;
    op.mark = mark;
    op.argb = argb;
    op.el = el;
    d->fn(d->userdata, &op);
}

static void out_clip(drawer *d, const olui_rect *clip)
{
    olui_draw_op op;
    memset(&op, 0, sizeof(op));
    op.kind = OLUI_DRAW_CLIP;
    if (clip) op.clip = *clip;
    d->fn(d->userdata, &op);
}

static void out_text(drawer *d, const olui_el *el, olui_rect box, int32_t baseline, const char *s, size_t n,
                     const ol_style *st, uint32_t argb)
{
    olui_draw_op op;
    if (!n || !crosses(ink(box, px(d->ui, st->font_size)), d->area)) return;
    memset(&op, 0, sizeof(op));
    op.kind = OLUI_DRAW_TEXT;
    op.box = box;
    op.baseline = baseline;
    op.text = s;
    op.length = n;
    op.font_pixels = px(d->ui, st->font_size);
    op.text_flags = st->text_flags;
    op.argb = argb;
    op.el = el;
    d->fn(d->userdata, &op);
}

/* GFOpenLayout's text and image ops for el's node, moved up by dy; the text
 * in el's colour as it is now (a hover may change it without a layout) */
static void node_ops(drawer *d, const olui_el *el, int32_t dy)
{
    olui *ui = d->ui;
    ol_id id = ol_node_id(el->node);
    size_t k;
    if (!ui->op_first || id >= ui->map_count) return;
    for (k = ui->op_first[id]; k < ui->op_first[id + 1]; ++k) {
        const ol_display_op *op = ol_display_get(ui->doc, ui->op_list[k]);
        if (op->type == OL_OP_TEXT) {
            ol_pixel_rect p = ol_scale_rect(&ui->scale, op->u.text.bounds);
            olui_rect r;
            const char *text = ol_node_text(el->node);
            r.x = p.x; r.y = p.y - dy; r.w = p.width; r.h = p.height;
            if (!text) continue;
            out_text(d, el, r, ol_scale_y(&ui->scale, op->u.text.baseline) - dy,
                     text + op->u.text.text_offset, op->u.text.text_length, &op->u.text.style,
                     el->cs.ol.foreground);
        } else if (op->type == OL_OP_IMAGE) {
            ol_pixel_rect p = ol_scale_rect(&ui->scale, op->u.image.bounds);
            olui_draw_op o;
            memset(&o, 0, sizeof(o));
            o.kind = OLUI_DRAW_IMAGE;
            o.box.x = p.x; o.box.y = p.y - dy; o.box.w = p.width; o.box.h = p.height;
            o.image = op->u.image.image_id;
            o.el = el;
            if (crosses(o.box, d->area)) d->fn(d->userdata, &o);
        }
    }
}

static void draw_el(drawer *d, const olui_el *el, int32_t dy, const olui_rect *clip);

/* Where a field's drawing may go from its box r: its text's clip reaches
 * 2 pixels past it at most */
static olui_rect field_reach(olui_rect r)
{
    r.x -= 2;
    r.y -= 2;
    r.w += 4;
    r.h += 4;
    return r;
}

/* A clip cut to the one it is drawn in (outer): 0 when nothing is left to draw */
static int clip_within(drawer *d, olui_rect c, const olui_rect *outer)
{
    if (outer) c = cut(c, *outer);
    if (c.w <= 0 || c.h <= 0) return 0;
    out_clip(d, &c);
    return 1;
}

static void draw_field(drawer *d, const olui_el *el, olui_rect r, const olui_rect *outer)
{
    olui *ui = d->ui;
    olui_rect in = inside(ui, el, r);
    const ol_style *st = &el->cs.ol;
    const char *t = el->text && el->text[0] ? el->text : NULL;
    int32_t pl = px(ui, st->padding_left), pr = px(ui, st->padding_right);
    int32_t room = in.w - pl - pr, caret_x, line = line_px(ui, st);
    int32_t top = in.y + (in.h - line) / 2;
    int32_t baseline;
    olui_rect text_box, clip = in, sb;
    olui_el *me = (olui_el *)el;
    char *shown = NULL;
    size_t shown_n = 0, a, b;

    if (el->flags & F_NUMBER) {
        sb = spin_box(ui, el, r);
        room -= sb.w;
    }
    baseline = baseline_in(ui, st, top, line);

    /* keep the caret in view */
    caret_x = field_x(ui, el, t ? t : "", t ? el->caret : 0);
    if (caret_x - me->xoff > room - 1) me->xoff = caret_x - room + 1;
    if (caret_x - me->xoff < 0) me->xoff = caret_x;
    if (me->xoff < 0) me->xoff = 0;

    clip.x += pl > 1 ? pl - 1 : 0;
    clip.w = room + 2;
    if (clip_within(d, clip, outer)) {
        text_box.x = in.x + pl - me->xoff;
        text_box.y = top;
        text_box.w = room + me->xoff;
        text_box.h = line;
        ta_selection(el, &a, &b);
        if (t && a != b && b <= strlen(t)) {
            olui_rect sel;
            sel.x = text_box.x + field_x(ui, el, t, a);
            sel.w = text_box.x + field_x(ui, el, t, b) - sel.x;
            sel.y = top;
            sel.h = line;
            out_flat(d, el, sel, el->cs.accent, 0, 0, 0);
        }
        if (t && (el->flags & F_PASSWORD)) shown = masked(t, &shown_n);
        if (shown) out_text(d, el, text_box, baseline, shown, shown_n, st, st->foreground);
        else if (t && !(el->flags & F_PASSWORD)) out_text(d, el, text_box, baseline, t, strlen(t), st, st->foreground);
        else if (!t && el->placeholder && !(el->state & S_FOCUS))
            out_text(d, el, text_box, baseline, el->placeholder, strlen(el->placeholder), st, el->cs.muted);
        free(shown);
        if (el->state & S_FOCUS) {
            olui_rect c;
            c.x = in.x + pl + caret_x - me->xoff;
            c.y = top;
            c.w = 1;
            c.h = line;
            out_mark(d, el, c, OLUI_MARK_CARET, st->foreground);
        }
        out_clip(d, outer);
    }
    if (el->flags & F_NUMBER) {
        /* the arrow buttons: up over down, the one held in the accent colour */
        olui_rect half = sb, m;
        int held = d->ui->spin == el ? d->ui->spin_dir : 0, i;
        uint32_t ink = enabled(el) ? st->foreground : el->cs.muted;
        out_flat(d, el, sb, el->cs.mark_bg, 0, 0, 0);
        for (i = 0; i < 2; ++i) {
            half.h = sb.h / 2;
            half.y = sb.y + (i ? sb.h - half.h : 0);
            if (held == (i ? -1 : 1)) out_flat(d, el, half, el->cs.accent, 0, 0, 0);
            m.w = m.h = half.h - 4 > 4 ? (half.h - 4 < sb.w - 4 ? half.h - 4 : sb.w - 4) : 4;
            m.x = half.x + (half.w - m.w) / 2;
            m.y = half.y + (half.h - m.h) / 2;
            out_mark(d, el, m, i ? OLUI_MARK_SPIN_DOWN : OLUI_MARK_SPIN_UP,
                     held == (i ? -1 : 1) ? el->cs.mark_bg : ink);
        }
        m = sb;
        m.w = 1;
        out_flat(d, el, m, el->cs.mark_border, 0, 0, 0);
        m = sb;
        m.y = sb.y + sb.h / 2;
        m.h = 1;
        out_flat(d, el, m, el->cs.mark_border, 0, 0, 0);
    }
}

/* A thin bar down a box's right, where its view is in what it holds */
static void scroll_bar(drawer *d, const olui_el *el, olui_rect in, int32_t total, int32_t scroll)
{
    olui_rect bar;
    if (total <= in.h || in.h <= 0) return;
    bar.w = 4;
    bar.x = in.x + in.w - bar.w - 1;
    bar.h = (int32_t)((int64_t)in.h * in.h / total);
    if (bar.h < 8) bar.h = 8;
    if (bar.h > in.h) bar.h = in.h;
    bar.y = in.y + (int32_t)((int64_t)(in.h - bar.h) * scroll / (total - in.h));
    out_flat(d, el, bar, (el->cs.border_argb & 0x00ffffffu) | 0xa0000000u, 0, 0, 2);
}

static void draw_textarea(drawer *d, const olui_el *el, olui_rect r, const olui_rect *outer)
{
    olui *ui = d->ui;
    olui_el *me = (olui_el *)el;
    const ol_style *st = &el->cs.ol;
    const char *t = el->text ? el->text : "";
    olui_rect in = inside(ui, el, r), v;
    int32_t line, baseline;
    size_t a, b;
    int i, bar;

    if (!el->node) return;
    bar = ta_wrap(ui, me);
    v = ta_view(ui, el, r, &line);
    if (!clip_within(d, in, outer)) return;
    ta_selection(el, &a, &b);
    if (!t[0] && el->placeholder && !(el->state & S_FOCUS)) {
        olui_rect tb = v;
        tb.h = line;
        out_text(d, el, tb, baseline_in(ui, st, v.y, line), el->placeholder, strlen(el->placeholder), st, el->cs.muted);
    }
    for (i = el->scroll / line; i < el->nlines; ++i) {
        size_t s0 = el->lines[i], last, e = ta_line_end(el, i, &last);
        int32_t y = v.y + (int32_t)i * line - el->scroll, x0 = v.x - el->xoff;
        olui_rect tb;
        if (y >= in.y + in.h) break;
        tb.x = x0;
        tb.y = y;
        tb.w = v.w + el->xoff;
        tb.h = line;
        baseline = baseline_in(ui, st, y, line);
        if (a < b && a <= e && b > s0 && !(a == e && b == e)) {
            /* the selection: its part of the line on the accent colour, in mark-background */
            size_t p = a > s0 ? a : s0, q = b < e ? b : e;
            olui_rect sel;
            sel.x = x0 + wpx(ui, el, t + s0, p - s0);
            sel.w = x0 + wpx(ui, el, t + s0, q - s0) - sel.x + (b > e && e < strlen(t) ? 4 : 0);
            sel.y = y;
            sel.h = line;
            out_flat(d, el, sel, el->cs.accent, 0, 0, 0);
            out_text(d, el, tb, baseline, t + s0, p - s0, st, st->foreground);
            tb.x = sel.x;
            out_text(d, el, tb, baseline, t + p, q - p, st, el->cs.mark_bg);
            tb.x = x0 + wpx(ui, el, t + s0, q - s0);
            out_text(d, el, tb, baseline, t + q, e - q, st, st->foreground);
        } else {
            out_text(d, el, tb, baseline, t + s0, e - s0, st, st->foreground);
        }
    }
    if (el->state & S_FOCUS) {
        olui_rect c;
        i = ta_line_of(el, el->caret);
        c.x = v.x - el->xoff + wpx(ui, el, t + el->lines[i], el->caret - el->lines[i]);
        c.y = v.y + (int32_t)i * line - el->scroll;
        c.w = 1;
        c.h = line;
        out_mark(d, el, c, OLUI_MARK_CARET, st->foreground);
    }
    out_clip(d, outer);
    if (bar) scroll_bar(d, el, in, (int32_t)el->nlines * line + (in.h - v.h), el->scroll);
}

/* A select's rows (a list box's, the pop-up's), from scroll pixels down them, in area */
static void draw_rows(drawer *d, olui_el *sel, olui_rect area, int32_t scroll, int32_t row_h)
{
    olui *ui = d->ui;
    olui_el *r;
    int i = 0;
    for (r = row_after(sel, NULL); r; r = row_after(sel, r), ++i) {
        olui_rect rr, tb;
        const char *t;
        int32_t y = area.y + i * row_h - scroll, line;
        if (y + row_h <= area.y) continue;
        if (y >= area.y + area.h) break;
        row_style(ui, r, sel);
        rr.x = area.x;
        rr.y = y;
        rr.w = area.w;
        rr.h = row_h;
        out_box(d, r, rr, &r->cs);
        t = row_text(r);
        line = line_px(ui, &r->cs.ol);
        tb.x = rr.x + px(ui, r->cs.ol.padding_left);
        tb.y = y + px(ui, r->cs.ol.padding_top);
        tb.w = rr.w;
        tb.h = line;
        out_text(d, r, tb, baseline_in(ui, &r->cs.ol, tb.y, line), t, strlen(t), &r->cs.ol, r->cs.ol.foreground);
    }
}

static void draw_select(drawer *d, const olui_el *el, olui_rect r, const olui_rect *outer)
{
    olui *ui = d->ui;
    olui_el *me = (olui_el *)el;
    olui_rect in = inside(ui, el, r);
    const ol_style *st = &el->cs.ol;
    if (listbox(el)) {
        olui_rect v = rows_view(ui, el, r);
        int32_t row_h = row_height(ui, me), n = 0;
        olui_el *x;
        if (!clip_within(d, in, outer)) return;
        draw_rows(d, me, v, el->scroll, row_h);
        out_clip(d, outer);
        for (x = row_after(el, NULL); x; x = row_after(el, x)) ++n;
        scroll_bar(d, el, in, n * row_h + (in.h - v.h), el->scroll);
    } else {
        /* the chosen option's text, and the drop-down's mark at the right */
        int32_t line = line_px(ui, st), pl = px(ui, st->padding_left), pr = px(ui, st->padding_right);
        olui_rect m, clip = in, tb;
        olui_el *opt = option_at(el, el->ivalue);
        m.w = m.h = line * 2 / 3 > 6 ? line * 2 / 3 : 6;
        m.x = in.x + in.w - pr - m.w;
        m.y = in.y + (in.h - m.h) / 2;
        clip.x += pl > 1 ? pl - 1 : 0;
        clip.w = m.x - 2 - clip.x;
        if (opt && clip_within(d, clip, outer)) {
            const char *t = row_text(opt);
            tb.x = in.x + pl;
            tb.y = in.y + (in.h - line) / 2;
            tb.w = clip.w;
            tb.h = line;
            out_text(d, el, tb, baseline_in(ui, st, tb.y, line), t, strlen(t), st, st->foreground);
            out_clip(d, outer);
        }
        out_mark(d, el, m, OLUI_MARK_DROPDOWN, st->foreground);
    }
}

/* A fieldset's box: its top edge through its legend's middle, broken where the legend's text is */
static void draw_fieldset(drawer *d, const olui_el *el, olui_rect r)
{
    olui *ui = d->ui;
    const olui_el *lg;
    olui_cs fill = el->cs;
    olui_rect lr, box, e;
    int32_t gx0, gx1, bw, shift;
    ol_id id;
    size_t k;
    for (lg = el->first; lg; lg = lg->next)
        if (!strcmp(lg->tag, "legend") && visible(lg)) break;
    if (!lg) {
        out_box(d, el, r, &el->cs);
        return;
    }
    shift = rect_of_node(ui, el->node).y - r.y;
    lr = rect_of_node(ui, lg->node);
    lr.y -= shift;
    box = r;
    box.y = lr.y + lr.h / 2;
    box.h = r.y + r.h - box.y;
    fill.border = 0;
    fill.radius = 0;
    out_box(d, el, box, &fill);
    if (el->cs.border <= 0) return;
    bw = px(ui, el->cs.border);
    /* the gap: the legend's text, and its padding either side */
    gx0 = lr.x + lr.w;
    gx1 = lr.x;
    id = ol_node_id(lg->node);
    if (ui->op_first && id < ui->map_count)
        for (k = ui->op_first[id]; k < ui->op_first[id + 1]; ++k) {
            const ol_display_op *op = ol_display_get(ui->doc, ui->op_list[k]);
            ol_pixel_rect p = ol_scale_rect(&ui->scale, op->type == OL_OP_TEXT ? op->u.text.bounds : op->u.image.bounds);
            if (p.x < gx0) gx0 = p.x;
            if (p.x + p.width > gx1) gx1 = p.x + p.width;
        }
    if (gx1 < gx0) gx0 = gx1 = lr.x;
    gx0 -= px(ui, lg->cs.ol.padding_left);
    gx1 += px(ui, lg->cs.ol.padding_right);
    e = box; e.w = bw; out_flat(d, el, e, el->cs.border_argb | 0xff000000u, 0, 0, 0);
    e.x = box.x + box.w - bw; out_flat(d, el, e, el->cs.border_argb | 0xff000000u, 0, 0, 0);
    e = box; e.y = box.y + box.h - bw; e.h = bw; out_flat(d, el, e, el->cs.border_argb | 0xff000000u, 0, 0, 0);
    e = box; e.h = bw; e.w = gx0 - box.x;
    if (e.w > 0) out_flat(d, el, e, el->cs.border_argb | 0xff000000u, 0, 0, 0);
    e.x = gx1; e.w = box.x + box.w - gx1;
    if (e.w > 0) out_flat(d, el, e, el->cs.border_argb | 0xff000000u, 0, 0, 0);
}

static void draw_children(drawer *d, const olui_el *el, int32_t dy, const olui_rect *clip)
{
    const olui_el *c;
    for (c = el->first; c; c = c->next) draw_el(d, c, dy, clip);
}

static void draw_items(drawer *d, const olui_el *from, int32_t dy, const olui_rect *clip)
{
    const olui_el *c;
    for (c = from->first; c; c = c->next) {
        if (c->kind != K_ITEM) continue;
        draw_el(d, c, dy, clip);
        if (c->state & S_OPEN) draw_items(d, c, dy, clip);
    }
}

static void draw_el(drawer *d, const olui_el *el, int32_t dy, const olui_rect *clip)
{
    olui *ui = d->ui;
    olui_rect r;
    if (!visible(el)) return;
    r = rect_of_node(ui, el->node);
    r.y -= dy;
    if (clip && !crosses(r, clip)) return;
    if (el->kind == K_BOX && !strcmp(el->tag, "fieldset")) draw_fieldset(d, el, r);
    else if (el->kind != K_TEXT && el->kind != K_SLIDER) out_box(d, el, r, &el->cs);   /* a slider's box is its knob */

    switch (el->kind) {
    case K_CHECK:
    case K_RADIO: {
        const ol_style *st = &el->cs.ol;
        int32_t line = px(ui, st->line_height > st->font_size ? st->line_height : st->font_size);
        int32_t size = line - 2 > 8 ? line - 2 : 8;
        olui_rect b;
        b.x = r.x + px(ui, st->padding_left) + 1;
        b.y = r.y + px(ui, st->padding_top) + (line - size) / 2;
        b.w = b.h = size;
        out_flat(d, el, b, el->cs.mark_bg, (el->state & S_FOCUS) ? el->cs.accent : el->cs.mark_border, 1,
                 el->kind == K_RADIO ? size / 2 : 2);
        if (el->state & S_CHECKED)
            out_mark(d, el, b, el->kind == K_RADIO ? OLUI_MARK_DOT : OLUI_MARK_TICK,
                     enabled(el) ? el->cs.accent : el->cs.muted);
        node_ops(d, el, dy);
        return;
    }
    case K_INPUT:
        /* a field measures its text to draw it: not when it is outside what is drawn */
        if (crosses(field_reach(r), d->area)) draw_field(d, el, r, clip);
        return;
    case K_TEXTAREA:
        if (crosses(field_reach(r), d->area)) draw_textarea(d, el, r, clip);
        return;
    case K_SELECT:
        if (crosses(field_reach(r), d->area)) draw_select(d, el, r, clip);
        return;
    case K_PROGRESS: {
        olui_rect in = inside(ui, el, r), bar;
        int v = el->ivalue < 0 ? 0 : el->ivalue > el->imax ? el->imax : el->ivalue;
        bar.x = in.x + px(ui, el->cs.ol.padding_left);
        bar.y = in.y + px(ui, el->cs.ol.padding_top);
        bar.h = in.h - px(ui, el->cs.ol.padding_top) - px(ui, el->cs.ol.padding_bottom);
        bar.w = el->imax > 0
            ? (int32_t)((int64_t)(in.w - px(ui, el->cs.ol.padding_left) - px(ui, el->cs.ol.padding_right)) * v / el->imax)
            : 0;
        if (bar.w > 0 && bar.h > 0) out_flat(d, el, bar, el->cs.accent, 0, 0, 0);
        return;
    }
    case K_SLIDER: {
        /* the track across the middle, filled to the value, and the knob on it */
        olui_rect in = inside(ui, el, r), track, knob;
        int32_t x0, x1, kx, range = el->imax - el->imin;
        slider_track(ui, el, &x0, &x1);
        kx = x0 + (range > 0 ? (int32_t)((int64_t)(x1 - x0) * (el->ivalue - el->imin) / range) : 0);
        track.x = x0;
        track.w = x1 - x0;
        track.h = in.h / 4 > 3 ? in.h / 4 : 3;
        track.y = in.y + (in.h - track.h) / 2;
        out_flat(d, el, track, el->cs.mark_bg, el->cs.mark_border, 1, track.h / 2);
        track.w = kx - x0;
        if (track.w > 0) out_flat(d, el, track, enabled(el) ? el->cs.accent : el->cs.muted, 0, 0, track.h / 2);
        knob.h = in.h;
        knob.w = in.h * 3 / 4 > 6 ? in.h * 3 / 4 : 6;
        knob.x = kx - knob.w / 2;
        knob.y = in.y;
        out_box(d, el, knob, &el->cs);
        return;
    }
    case K_ITEM:
        if (has_items(el)) {
            olui_rect a;
            int32_t line = px(ui, el->cs.ol.line_height);
            a.w = px(ui, PX(10));
            a.h = a.w;
            a.x = r.x + px(ui, el->cs.ol.padding_left + el->depth * PX(INDENT)) + 1;
            a.y = r.y + px(ui, el->cs.ol.padding_top) + (line - a.h) / 2;
            out_mark(d, el, a, (el->state & S_OPEN) ? OLUI_MARK_OPEN : OLUI_MARK_CLOSED, el->cs.ol.foreground);
        }
        node_ops(d, el, dy);
        return;
    case K_LIST: {
        olui_rect in = inside(ui, el, r);
        int32_t total = list_content_height(ui, el);
        if (clip) in = cut(in, *clip);     /* in a box that scrolls: cut to it too */
        if (in.w <= 0 || in.h <= 0) return;
        out_clip(d, &in);
        draw_items(d, el, dy + el->scroll, &in);
        out_clip(d, clip);
        if (total > in.h && in.h > 0) {
            /* where the view is in the list: a thin bar down its right */
            olui_rect bar;
            bar.w = 4;
            bar.x = in.x + in.w - bar.w - 1;
            bar.h = (int32_t)((int64_t)in.h * in.h / total);
            if (bar.h < 8) bar.h = 8;
            bar.y = in.y + (int32_t)((int64_t)(in.h - bar.h) * el->scroll / (total - in.h));
            out_flat(d, el, bar, (el->cs.border_argb & 0x00ffffffu) | 0xa0000000u, 0, 0, 2);
        }
        return;
    }
    default:
        if (scroller(el)) {
            /* a box with overflow: what it holds moved up by its scroll and cut to its inside */
            olui_rect in = inside(ui, el, r);
            if (clip) in = cut(in, *clip);
            if (in.w <= 0 || in.h <= 0) return;
            out_clip(d, &in);
            node_ops(d, el, dy + el->scroll);
            draw_children(d, el, dy + el->scroll, &in);
            out_clip(d, clip);
            if (el->cs.overflow != O_HIDDEN)
                scroll_bar(d, el, inside(ui, el, r), box_content_height(ui, el), el->scroll);
            return;
        }
        node_ops(d, el, dy);
        draw_children(d, el, dy, clip);
        return;
    }
}

/* Over everything else: a select's pop-up list, and a tooltip */
static void draw_over(drawer *d)
{
    olui *ui = d->ui;
    if (ui->popup && crosses(ui->popup_rect, d->area)) {
        olui_rect rows = popup_rows_rect(ui);
        int32_t n = 0;
        olui_el *r;
        out_box(d, ui->popup_box, ui->popup_rect, &ui->popup_box->cs);
        if (clip_within(d, rows, NULL)) {
            draw_rows(d, ui->popup, rows, (int32_t)ui->popup_top * ui->popup_row_h, ui->popup_row_h);
            out_clip(d, NULL);
        }
        olui_rect in = inside(ui, ui->popup_box, ui->popup_rect);
        for (r = row_after(ui->popup, NULL); r; r = row_after(ui->popup, r)) ++n;
        scroll_bar(d, ui->popup_box, in, n * ui->popup_row_h + (in.h - rows.h), (int32_t)ui->popup_top * ui->popup_row_h);
    }
    if (ui->tip_on && ui->tip_for && crosses(ui->tip_rect, d->area)) {
        const olui_cs *cs = &ui->tip_box->cs;
        olui_rect in = inside(ui, ui->tip_box, ui->tip_rect), tb;
        int32_t line = line_px(ui, &cs->ol);
        out_box(d, ui->tip_box, ui->tip_rect, cs);
        if (clip_within(d, in, NULL)) {
            tb.x = in.x + px(ui, cs->ol.padding_left);
            tb.y = in.y + px(ui, cs->ol.padding_top);
            tb.w = in.w;
            tb.h = line;
            out_text(d, ui->tip_box, tb, baseline_in(ui, &cs->ol, tb.y, line), ui->tip_for->title,
                     strlen(ui->tip_for->title), &cs->ol, cs->ol.foreground);
            out_clip(d, NULL);
        }
    }
}

void olui_draw(olui *ui, const olui_rect *area, olui_draw_fn fn, void *userdata)
{
    drawer d;
    if (!ui || !fn || !ui->root || !fresh(ui) || !ui->root->node) return;
    d.ui = ui;
    d.fn = fn;
    d.userdata = userdata;
    d.area = area;
    draw_el(&d, ui->root, 0, NULL);
    draw_over(&d);
    ui->damaged = 0;
    ui->part.w = ui->part.h = 0;
}

int olui_damaged(const olui *ui, olui_rect *area)
{
    if (!ui) return 0;
    if (ui->damaged || ui->stale) {
        if (area) {
            area->x = 0;
            area->y = 0;
            area->w = ui->width;
            area->h = ui->height;
        }
        return 1;
    }
    if (ui->part.w <= 0 || ui->part.h <= 0) return 0;
    if (area) *area = ui->part;
    return 1;
}

/* ---- A hover, press, focus, check or selection, drawn again without a layout ------ */

/* Where an element's ops go, cut as they are cut */
typedef struct {
    olui_rect *area;
    olui_rect clip;
    int clipped;
} extent;

static void extent_op(void *userdata, const olui_draw_op *op)
{
    extent *e = (extent *)userdata;
    olui_rect r = op->box;
    if (op->kind == OLUI_DRAW_CLIP) {
        e->clip = op->clip;
        e->clipped = op->clip.w > 0 && op->clip.h > 0;
        return;
    }
    if (op->kind == OLUI_DRAW_TEXT) r = ink(r, op->font_pixels);
    if (e->clipped) r = cut(r, e->clip);
    grow_to(e->area, r);
}

/* What el draws (and what it holds), as one box on the window, into area */
static void add_extent(olui *ui, olui_el *el, olui_rect *area)
{
    drawer d;
    extent e;
    int32_t dy;
    olui_rect clip;
    int clipped;
    memset(&e, 0, sizeof(e));
    e.area = area;
    d.ui = ui;
    d.fn = extent_op;
    d.userdata = &e;
    d.area = NULL;
    if (!el->node) return;
    clipped = view_of(ui, el, &dy, &clip);
    if (el->kind == K_INPUT || el->kind == K_TEXTAREA || el->kind == K_SELECT) {
        /* known without drawing it, which measures its text */
        if (visible(el)) {
            olui_rect r = rect_of_node(ui, el->node);
            r.y -= dy;
            r = field_reach(r);
            grow_to(area, clipped ? cut(r, clip) : r);
        }
    } else if (clipped) {
        /* in a list or a box that scrolls: moved by its scroll and cut to its inside */
        e.clip = clip;
        e.clipped = 1;
        draw_el(&d, el, dy, &clip);
    } else {
        draw_el(&d, el, 0, NULL);
    }
}

/* What GFOpenLayout lays out from a style: all of it but the text's colour
 * (node_ops draws in the element's) and the box olui paints */
static int same_layout(const olui_cs *a, const olui_cs *b)
{
    ol_style x = a->ol, y = b->ol;
    x.foreground = y.foreground = 0;
    return !memcmp(&x, &y, sizeof(x)) && !memcmp(&a->box, &b->box, sizeof(a->box)) && a->border == b->border &&
           a->column == b->column;
}

/* el and what is under it styled again, from the styles build gives them;
 * 0 when one of them lays out differently now. Where those whose look
 * changed are goes into area. */
static int restyle_tree(olui *ui, olui_el *el, olui_rect *area)
{
    olui_el *list = el->kind == K_ITEM ? list_of(el) : NULL, *c;
    olui_cs was;
    if (!el->node) return 1;    /* not in this layout: what an item holds that isn't an item */
    was = el->cs;
    olui_style_compute(ui->sheet, el, list ? &list->cs : el->parent ? &el->parent->cs : NULL);
    el->styled_state = el->state;
    if (!same_layout(&was, &el->cs)) return 0;
    if (memcmp(&was, &el->cs, sizeof(was))) {
        /* where it was drawn as well as where it is: a box may come or go */
        olui_cs now = el->cs;
        el->cs = was;
        add_extent(ui, el, area);
        el->cs = now;
        add_extent(ui, el, area);
    }
    for (c = el->first; c; c = c->next)
        if (!restyle_tree(ui, c, area)) return 0;
    return 1;
}

/* A hover, press, focus, check or selection on el changed: el and what is under it are
 * styled again (rules reach them through el), and while none of them lays
 * out differently, only where one looks different is damaged. 0: it must
 * be laid out again. */
static int restyle_paint(olui *ui, olui_el *el, unsigned bit)
{
    olui_rect area, win;
    if (ui->stale || !el->node || el->styled_gen != ui->style_gen) return 0;
    memset(&area, 0, sizeof(area));
    if (!(olui_sheet_states(ui->sheet) & bit)) {
        /* no rule names it: every style stays as it is */
        if (el->styled_state == (el->state ^ bit)) el->styled_state = el->state;
    } else if (!restyle_tree(ui, el, &area)) {
        el->styled_state = ~el->state;  /* the layout styles it, and what is under it, again */
        return 0;
    }
    if (bit & (S_FOCUS | S_CHECKED | S_SELECTED)) {
        /* a field's caret and placeholder, a checkbox's edge and tick, a radio button's dot: drawn from
         * the state itself; and the node says it, as build would */
        uint32_t s = ol_node_states(el->node);
        uint32_t b = (bit & S_FOCUS) ? OL_STATE_FOCUSED : (bit & S_CHECKED) ? OL_STATE_CHECKED : OL_STATE_SELECTED;
        ol_node_set_states(el->node, (el->state & bit) ? s | b : s & ~b);
        add_extent(ui, el, &area);
    }
    win.x = win.y = 0;
    win.w = ui->width;
    win.h = ui->height;
    grow_to(&ui->part, cut(area, win));
    return 1;
}

/* A state no rule lays out differently from (a select's :open), drawn again where it changed */
static void paint_state(olui *ui, olui_el *el, unsigned bit, int on)
{
    unsigned was = el->state;
    if (on) el->state |= bit;
    else el->state &= ~bit;
    if (el->state == was) return;
    if (!restyle_paint(ui, el, bit)) touch(ui);
}

/* ---- The widgets, from the program ---------------------------------------------- */

static olui_el *find(olui_el *el, const char *id)
{
    olui_el *c, *f;
    if (!el) return NULL;
    if (el->id && !strcmp(el->id, id)) return el;
    for (c = el->first; c; c = c->next)
        if ((f = find(c, id))) return f;
    return NULL;
}

olui_el *olui_find(olui *ui, const char *id)
{
    return ui && id ? find(ui->root, id) : NULL;
}

const char *olui_id(const olui_el *el) { return el && el->id ? el->id : ""; }
const char *olui_tag(const olui_el *el) { return el ? el->tag : ""; }
const char *olui_get_text(const olui_el *el)
{
    if (el && el->kind == K_SELECT) {
        const olui_el *o = option_at(el, el->ivalue);
        return o ? row_text(o) : "";
    }
    return el && el->text ? el->text : "";
}
const char *olui_get_value(const olui_el *el) { return el ? el->value : NULL; }
int olui_get_checked(const olui_el *el) { return el && (el->state & S_CHECKED) != 0; }
void olui_set_user(olui_el *el, void *user) { if (el) el->user = user; }
void *olui_user(const olui_el *el) { return el ? el->user : NULL; }

int olui_set_text(olui *ui, olui_el *el, const char *utf8)
{
    if (!ui || !el || !olui_set_str(&el->text, utf8 ? utf8 : "")) return 0;
    if (el->kind == K_INPUT) el->caret = el->anchor = strlen(el->text);
    if (el->kind == K_TEXTAREA) {
        el->caret = el->anchor = 0;
        el->scroll = el->xoff = 0;
    }
    if (el->flags & F_NUMBER) {
        int v;
        if (fixed_parse(el->text, el->decimals, &v)) el->ivalue = number_clamp(el, v);
    }
    ++el->text_rev;
    touch(ui);
    return 1;
}

void olui_set_checked(olui *ui, olui_el *el, int checked)
{
    if (!ui || !el) return;
    if (el->kind == K_RADIO && checked) {
        radio_choose(ui, el);
        return;
    }
    set_state(ui, el, S_CHECKED, checked);
}

void olui_set_disabled(olui *ui, olui_el *el, int disabled) { if (ui) set_state(ui, el, S_DISABLED, disabled); }
void olui_set_hidden(olui *ui, olui_el *el, int hidden) { if (ui) set_state(ui, el, S_HIDDEN, hidden); }

int olui_box(olui *ui, const olui_el *el, olui_rect *r)
{
    if (!ui || !el || !r || !el->node || !fresh(ui) || !visible(el)) return 0;
    *r = rect_of(ui, el);
    return 1;
}

int olui_slider_value(const olui_el *el) { return el ? el->ivalue : 0; }

void olui_set_slider(olui *ui, olui_el *el, int value)
{
    if (!ui || !el || el->kind != K_SLIDER) return;
    if (value < el->imin) value = el->imin;
    if (value > el->imax) value = el->imax;
    if (el->ivalue == value) return;
    el->ivalue = value;
    ui->damaged = 1;
}

void olui_set_progress(olui *ui, olui_el *el, int value)
{
    if (!ui || !el || el->ivalue == value) return;
    el->ivalue = value;
    ui->damaged = 1;
}

olui_el *olui_list_add(olui *ui, olui_el *list, olui_el *parent, const char *text, const char *value)
{
    olui_el *item;
    if (!ui || !list) return NULL;
    if (!(item = olui_el_new(K_ITEM, "item"))) return NULL;
    if (!olui_set_str(&item->text, text ? text : "") || (value && !olui_set_str(&item->value, value))) {
        olui_el_free(item);
        return NULL;
    }
    olui_el_append(parent ? parent : list, item);
    touch(ui);
    return item;
}

static void forget(olui *ui, olui_el *el)
{
    olui_el *c;
    if (ui->focus == el) ui->focus = NULL;
    if (ui->hover == el) ui->hover = NULL;
    if (ui->pressed == el) ui->pressed = NULL;
    if (ui->popup == el) ui->popup = NULL;
    if (ui->drag == el) ui->drag = NULL;
    if (ui->label_down == el) ui->label_down = NULL;
    if (ui->spin == el) ui->spin = NULL;
    if (ui->tip_for == el) {
        ui->tip_for = NULL;
        ui->tip_on = 0;
    }
    for (c = el->first; c; c = c->next) forget(ui, c);
}

void olui_list_clear(olui *ui, olui_el *list)
{
    olui_el *c, *next;
    if (!ui || !list) return;
    for (c = list->first; c; c = next) {
        next = c->next;
        if (c->kind != K_ITEM) continue;
        forget(ui, c);
        olui_el_unlink(c);
        olui_el_free(c);
    }
    list->scroll = 0;
    touch(ui);
}

static olui_el *selected_in(const olui_el *el)
{
    const olui_el *c;
    for (c = el->first; c; c = c->next) {
        olui_el *s;
        if (c->kind != K_ITEM) continue;
        if (c->state & S_SELECTED) return (olui_el *)c;
        if ((s = selected_in(c))) return s;
    }
    return NULL;
}

olui_el *olui_list_selected(const olui_el *list)
{
    return list ? selected_in(list) : NULL;
}

void olui_list_select(olui *ui, olui_el *list, olui_el *item)
{
    olui_el *was;
    if (!ui || !list) return;
    was = olui_list_selected(list);
    if (was == item) return;
    if (was) set_state(ui, was, S_SELECTED, 0);
    if (item) {
        olui_el *p;
        set_state(ui, item, S_SELECTED, 1);
        for (p = item->parent; p && p != list; p = p->parent) set_state(ui, p, S_OPEN, 1);
        scroll_to(ui, list, item);
    }
}

void olui_item_open(olui *ui, olui_el *item, int open)
{
    if (!ui || !item || (open != 0) == ((item->state & S_OPEN) != 0)) return;
    set_state(ui, item, S_OPEN, open);
    if (!open) {
        /* a selected item inside goes to the one closed */
        olui_el *list = list_of(item), *sel = list ? olui_list_selected(list) : NULL, *p;
        for (p = sel ? sel->parent : NULL; p; p = p->parent)
            if (p == item) {
                set_state(ui, sel, S_SELECTED, 0);
                set_state(ui, item, S_SELECTED, 1);
                break;
            }
    }
    emit(ui, open ? OLUI_EXPAND : OLUI_COLLAPSE, list_of(item), item,
         item->value ? item->value : (item->id ? item->id : ""), -1, 0);
}

int olui_item_is_open(const olui_el *item)
{
    return item && (item->state & S_OPEN) != 0;
}

int olui_tabs_current(const olui_el *tabs)
{
    return tabs ? tabs->ivalue : -1;
}

void olui_tabs_select(olui *ui, olui_el *tabs, int index)
{
    olui_el *t;
    int n = 0;
    if (!ui || !tabs || tabs->kind != K_TABS || tabs->ivalue == index) return;
    for (t = tabs->first; t; t = t->next) {
        if (t->kind != K_TAB) continue;
        if (n == index) {
            tabs->ivalue = index;
            touch(ui);
            emit(ui, OLUI_SELECT, tabs, t, t->value ? t->value : (t->id ? t->id : ""), index, 0);
            return;
        }
        ++n;
    }
}

/* ---- 0.3's widgets, from the program ---------------------------------------------- */

int32_t olui_get_scroll(const olui_el *el)
{
    return el ? el->scroll : 0;
}

void olui_set_scroll(olui *ui, olui_el *el, int32_t y)
{
    if (!ui || !el || !fresh(ui) || !scrolls(el)) return;
    scroll_set(ui, el, y);
}

int olui_select_index(const olui_el *select)
{
    return select && select->kind == K_SELECT ? select->ivalue : -1;
}

void olui_select_set(olui *ui, olui_el *select, int index)
{
    if (!ui || !select || select->kind != K_SELECT) return;
    if (index < -1 || index >= option_count(select)) return;
    if (ui->popup == select) popup_close(ui, 0);
    select_choose(ui, select, index, 0);
}

int olui_select_count(const olui_el *select)
{
    return select && select->kind == K_SELECT ? option_count(select) : 0;
}

const char *olui_select_option_value(const olui_el *select, int index)
{
    const olui_el *o = select && select->kind == K_SELECT ? option_at(select, index) : NULL;
    return o ? option_value(o) : NULL;
}

const char *olui_select_option_text(const olui_el *select, int index)
{
    const olui_el *o = select && select->kind == K_SELECT ? option_at(select, index) : NULL;
    return o ? row_text(o) : NULL;
}

olui_el *olui_select_add(olui *ui, olui_el *select, const char *text, const char *value)
{
    olui_el *o;
    if (!ui || !select || select->kind != K_SELECT) return NULL;
    if (!(o = olui_el_new(K_OPTION, "option"))) return NULL;
    if (!olui_set_str(&o->text, text ? text : "") || (value && !olui_set_str(&o->value, value))) {
        olui_el_free(o);
        return NULL;
    }
    olui_el_append(select, o);
    if (select->ivalue < 0 && !listbox(select)) select->ivalue = option_step(select, 0, 0);
    if (ui->popup == select) popup_close(ui, 0);
    touch(ui);
    return o;
}

void olui_select_clear(olui *ui, olui_el *select)
{
    olui_el *c, *next;
    if (!ui || !select || select->kind != K_SELECT) return;
    if (ui->popup == select) popup_close(ui, 0);
    for (c = select->first; c; c = next) {
        next = c->next;
        if (c->kind != K_OPTION && c->kind != K_OPTGROUP) continue;
        olui_el_unlink(c);
        olui_el_free(c);
    }
    select->ivalue = -1;
    select->scroll = 0;
    touch(ui);
}

int olui_select_is_open(const olui *ui, const olui_el *select)
{
    return ui && select && ui->popup == select;
}

void olui_select_open(olui *ui, olui_el *select, int open)
{
    if (!ui || !select) return;
    if (open) popup_open(ui, select);
    else if (ui->popup == select) popup_close(ui, 0);
}

int olui_number_value(const olui_el *el)
{
    return el && (el->flags & F_NUMBER) ? el->ivalue : 0;
}

int olui_number_decimals(const olui_el *el)
{
    return el && (el->flags & F_NUMBER) ? el->decimals : 0;
}

void olui_set_number(olui *ui, olui_el *el, int value)
{
    char num[24];
    if (!ui || !el || !(el->flags & F_NUMBER)) return;
    value = number_clamp(el, value);
    fixed_format(value, el->decimals, num, sizeof(num));
    if (value == el->ivalue && el->text && !strcmp(el->text, num)) return;
    if (!olui_set_str(&el->text, num)) return;
    el->ivalue = value;
    el->caret = el->anchor = strlen(num);
    ++el->text_rev;
    set_node_value(el);
    damage_el(ui, el);
}

void olui_free_css(char *css)
{
    free(css);
}
