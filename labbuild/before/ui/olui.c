/* OpenLayout UI: the widgets, their layout through OpenLayout, the
 * pointer and the keyboard, and drawing.
 *
 * The program's markup is kept as a tree of elements. Each layout styles
 * them (olui_css.c), builds a fresh OpenLayout document from them (one
 * node an element; a list's items, open or not, flattened under it) and
 * lays it out at the window's size. Drawing walks the elements back to
 * front: each one's box, its marks, then OpenLayout's text ops for its
 * node, a list's items cut to the list and moved by its scroll.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "olui_internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define PX(n) OL_CSSPX(n)
#define MAX_HANDLERS 128
#define INDENT 16           /* a tree level, CSS px */

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
    "tab{display:flex;flex-direction:column;gap:6px;padding:6px;border:1px solid #808080}";

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
    int damaged;
    handler handlers[MAX_HANDLERS];
    int nhandlers;
    olui_el *focus, *hover, *pressed;
    int busy;                   /* calling a handler: changes it makes wait for the next layout */
    unsigned style_gen;         /* moves on when the stylesheet changes: every element is styled again */
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

static void set_state(olui *ui, olui_el *el, unsigned bit, int on)
{
    unsigned was;
    if (!el) return;
    was = el->state;
    if (on) el->state |= bit;
    else el->state &= ~bit;
    if (el->state != was) touch(ui);
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
    if ((el->kind == K_ITEM || el->kind == K_BUTTON || el->kind == K_CHECK || el->kind == K_RADIO) &&
        !el->text) {
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
        el->caret = el->text ? strlen(el->text) : 0;
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
    case K_CHECK: case K_RADIO: case K_INPUT: return OL_ROLE_INPUT;
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

/* An element's node, styled; and its children's, unless it is a list */
static int build(olui *ui, olui_el *el, ol_node *parent, const olui_cs *pcs, int shown, int restyle);

static int build_items(olui *ui, olui_el *list, olui_el *from, ol_node *parent, int depth, int shown,
                       int restyle)
{
    olui_el *c;
    for (c = from->first; c; c = c->next) {
        if (c->kind != K_ITEM) continue;
        c->depth = depth;
        if (!build(ui, c, parent, &list->cs, shown, restyle)) return 0;
        if (!build_items(ui, list, c, parent, depth + 1, shown && (c->state & S_OPEN) && visible(c),
                         restyle))
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
        st.display = OL_DISPLAY_BLOCK;  /* widgets are boxes; a picture stays in its line, where OpenLayout places it */
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
        case K_INPUT: actions = OL_ACTION_EDIT | OL_ACTION_FOCUS; break;
        case K_LIST: actions = OL_ACTION_FOCUS; break;
        case K_ITEM: actions = OL_ACTION_SELECT | OL_ACTION_ACTIVATE; break;
        default: break;
        }
        ol_node_set_states(el->node, states);
        ol_node_set_actions(el->node, actions);
        if (el->id) ol_node_set_name(el->node, el->id);
        if (el->kind == K_INPUT && el->text) ol_node_set_value(el->node, el->text);
    }
    if (el->kind == K_LIST)
        return build_items(ui, el, el, el->node, 0, shown && st.display != OL_DISPLAY_NONE, restyle);
    if (el->kind == K_ITEM) return 1;   /* its items go under the list (build_items) */
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
        /* lists keep their scroll inside what they hold */
        size_t i;
        for (i = 0; i < ui->map_count; ++i)
            if (ui->map[i] && ui->map[i]->kind == K_LIST) clamp_scroll(ui, ui->map[i]);
    }
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

/* An element's box on the window: moved up by the scroll of the list it is in */
static olui_rect rect_of(const olui *ui, const olui_el *el)
{
    olui_rect r = rect_of_node(ui, el->node);
    const olui_el *l = el->kind == K_LIST ? NULL : list_of((olui_el *)el);
    if (l) r.y -= l->scroll;
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

static void clamp_scroll(olui *ui, olui_el *list)
{
    olui_rect in = inside(ui, list, rect_of_node(ui, list->node));
    int32_t most = list_content_height(ui, list) - in.h;
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
        case K_TABBTN: case K_LIST: case K_SLIDER:
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

/* The caret's byte for an x in a field */
static size_t caret_at(olui *ui, olui_el *el, int x)
{
    olui_rect in = inside(ui, el, rect_of(ui, el));
    const char *t = el->text ? el->text : "";
    size_t i = 0, best = 0;
    int32_t want = x - in.x - px(ui, el->cs.ol.padding_left) + el->xoff, best_d = 0x7fffffff;
    for (;;) {
        int32_t w = px(ui, text_width(ui, el, t, i)), d = w > want ? w - want : want - w;
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

static void field_edit(olui *ui, olui_el *el, const char *insert, size_t cut_from, size_t cut_to)
{
    const char *t = el->text ? el->text : "";
    size_t n = strlen(t), ins = insert ? strlen(insert) : 0;
    char *s = (char *)malloc(n - (cut_to - cut_from) + ins + 1);
    if (!s) return;
    memcpy(s, t, cut_from);
    if (ins) memcpy(s + cut_from, insert, ins);
    memcpy(s + cut_from + ins, t + cut_to, n - cut_to + 1);
    free(el->text);
    el->text = s;
    el->caret = cut_from + ins;
    touch(ui);
    emit(ui, OLUI_CHANGE, el, NULL, el->text, -1, 0);
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

/* ---- Input ----------------------------------------------------------------------- */

static void set_focus(olui *ui, olui_el *el)
{
    if (ui->focus == el) return;
    if (ui->focus) set_state(ui, ui->focus, S_FOCUS, 0);
    ui->focus = el;
    if (el) {
        set_state(ui, el, S_FOCUS, 1);
        emit(ui, OLUI_FOCUS, el, NULL, NULL, -1, 0);
    }
}

void olui_focus(olui *ui, olui_el *el)
{
    if (ui) set_focus(ui, el);
}

int olui_mouse(olui *ui, int x, int y, int what)
{
    olui_el *under, *w;
    if (!ui || !ui->root || !fresh(ui)) return 0;
    under = hit(ui, ui->root, x, y);
    w = widget_of(under);
    if (w && !enabled(w)) w = NULL;

    switch (what) {
    case OLUI_MOUSE_MOVE:
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
        /* a pressed button looks pressed only while the pointer is on it */
        if (ui->pressed) set_state(ui, ui->pressed, S_ACTIVE, ui->pressed == w);
        return w != NULL;
    case OLUI_MOUSE_DOWN:
    case OLUI_MOUSE_DOUBLE:
        if (!w) return 0;
        if (focusable(w)) set_focus(ui, w);
        else if (w->kind == K_ITEM) set_focus(ui, list_of(w));
        if (w->kind == K_INPUT) {
            w->caret = caret_at(ui, w, x);
            ui->damaged = 1;
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
        olui_el *list = list_of(under);
        int32_t step;
        if (!list) return 0;
        step = 3 * px(ui, list->cs.ol.line_height > 0 ? list->cs.ol.line_height : PX(16));
        list->scroll += what == OLUI_WHEEL_UP ? -step : step;
        clamp_scroll(ui, list);
        ui->damaged = 1;
        return 1;
    }
    default:
        return 0;
    }
}

static void collect_focusable(olui_el *el, olui_el **out, int *n, int max)
{
    olui_el *c;
    if (el->kind != K_STRIP && !visible(el)) return;
    if (focusable(el) && *n < max) out[(*n)++] = el;
    if (el->kind == K_LIST) return;
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

int olui_key(olui *ui, unsigned code, unsigned qual)
{
    olui_el *f;
    if (!ui || !ui->root || !fresh(ui)) return 0;
    f = ui->focus;
    if (code == OLUI_KEY_TAB) {
        olui_el *all[256];
        int n = 0, i, at = -1;
        collect_focusable(ui->root, all, &n, 256);
        if (!n) return 0;
        for (i = 0; i < n; ++i) if (all[i] == f) at = i;
        if (qual & OLUI_QUAL_SHIFT) at = at <= 0 ? n - 1 : at - 1;
        else at = at < 0 || at == n - 1 ? 0 : at + 1;
        set_focus(ui, all[at]);
        return 1;
    }
    if (!f || !enabled(f)) return 0;

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
    case K_INPUT: {
        const char *t = f->text ? f->text : "";
        size_t len = strlen(t);
        if (f->caret > len) f->caret = len;
        switch (code) {
        case OLUI_KEY_LEFT: f->caret = prev_char(t, f->caret); break;
        case OLUI_KEY_RIGHT: f->caret = next_char(t, f->caret); break;
        case OLUI_KEY_HOME: f->caret = 0; break;
        case OLUI_KEY_END: f->caret = len; break;
        case OLUI_KEY_BACKSPACE:
            if (f->caret) field_edit(ui, f, NULL, prev_char(t, f->caret), f->caret);
            break;
        case OLUI_KEY_DELETE:
            if (f->caret < len) field_edit(ui, f, NULL, f->caret, next_char(t, f->caret));
            break;
        case OLUI_KEY_RETURN:
            emit(ui, OLUI_SUBMIT, f, NULL, t, -1, 0);
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
                field_edit(ui, f, u, f->caret, f->caret);
            } else {
                return 0;
            }
        }
        ui->damaged = 1;
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
    if (!n || !crosses(box, d->area)) return;
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

/* OpenLayout's text and image ops for el's node, moved up by dy */
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
                     op->u.text.style.foreground);
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

static void draw_field(drawer *d, const olui_el *el, olui_rect r)
{
    olui *ui = d->ui;
    olui_rect in = inside(ui, el, r);
    const ol_style *st = &el->cs.ol;
    const char *t = el->text && el->text[0] ? el->text : NULL;
    int32_t pl = px(ui, st->padding_left), pr = px(ui, st->padding_right);
    int32_t room = in.w - pl - pr, caret_x, line = px(ui, st->line_height > st->font_size ? st->line_height : st->font_size);
    int32_t top = in.y + (in.h - line) / 2;
    int32_t baseline;
    olui_rect text_box, clip = in;
    olui_el *me = (olui_el *)el;
    ol_text_metrics m;

    memset(&m, 0, sizeof(m));
    if (ui->measure) ui->measure(ui->measure_userdata, "Xg", 2, st, &m);
    else { m.ascent = st->font_size * 4 / 5; m.descent = st->font_size - m.ascent; }
    baseline = top + (line - px(ui, m.ascent + m.descent)) / 2 + px(ui, m.ascent);

    /* keep the caret in view */
    caret_x = px(ui, text_width(ui, el, t ? t : "", t ? el->caret : 0));
    if (caret_x - me->xoff > room - 1) me->xoff = caret_x - room + 1;
    if (caret_x - me->xoff < 0) me->xoff = caret_x;
    if (me->xoff < 0) me->xoff = 0;

    clip.x += pl > 1 ? pl - 1 : 0;
    clip.w = room + 2;
    out_clip(d, &clip);
    text_box.x = in.x + pl - me->xoff;
    text_box.y = top;
    text_box.w = room + me->xoff;
    text_box.h = line;
    if (t) out_text(d, el, text_box, baseline, t, strlen(t), st, st->foreground);
    else if (el->placeholder && !(el->state & S_FOCUS))
        out_text(d, el, text_box, baseline, el->placeholder, strlen(el->placeholder), st, el->cs.muted);
    if (el->state & S_FOCUS) {
        olui_rect c;
        c.x = in.x + pl + caret_x - me->xoff;
        c.y = top;
        c.w = 1;
        c.h = line;
        out_mark(d, el, c, OLUI_MARK_CARET, st->foreground);
    }
    out_clip(d, NULL);
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
    if (el->kind != K_TEXT && el->kind != K_SLIDER) out_box(d, el, r, &el->cs);   /* a slider's box is its knob */

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
        draw_field(d, el, r);
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
        node_ops(d, el, dy);
        draw_children(d, el, dy, clip);
        return;
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
    ui->damaged = 0;
}

int olui_damaged(const olui *ui, olui_rect *area)
{
    if (!ui || !(ui->damaged || ui->stale)) return 0;
    if (area) {
        area->x = 0;
        area->y = 0;
        area->w = ui->width;
        area->h = ui->height;
    }
    return 1;
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
const char *olui_get_text(const olui_el *el) { return el && el->text ? el->text : ""; }
const char *olui_get_value(const olui_el *el) { return el ? el->value : NULL; }
int olui_get_checked(const olui_el *el) { return el && (el->state & S_CHECKED) != 0; }
void olui_set_user(olui_el *el, void *user) { if (el) el->user = user; }
void *olui_user(const olui_el *el) { return el ? el->user : NULL; }

int olui_set_text(olui *ui, olui_el *el, const char *utf8)
{
    if (!ui || !el || !olui_set_str(&el->text, utf8 ? utf8 : "")) return 0;
    if (el->kind == K_INPUT) el->caret = strlen(el->text);
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
