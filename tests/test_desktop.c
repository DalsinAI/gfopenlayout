/* GFOpenLayout UI 0.4 on the host: what a desktop and its drawers need
 * (OpenBench on GFOpenLayout): elements the program makes, places and
 * takes away; position:absolute with left and top; z-index's layers for
 * drawing and for the pointer.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;

#define CHECK(x) do { ++checks; if (!(x)) { \
    ++failed; printf("FAIL line=%d expr=%s\n", __LINE__, #x); \
} } while (0)

/* every character 7 px wide, ascent 10, descent 3, at 13 px */
static int measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    (void)userdata;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * style->font_size * 7 / 13;
    m->ascent = style->font_size * 10 / 13;
    m->descent = style->font_size * 3 / 13;
    return 1;
}

/* What was drawn: the ids of the boxes' elements, in order */
typedef struct {
    char order[64][16];
    int n;
    int ops;
} seen;

static void record(void *userdata, const olui_draw_op *op)
{
    seen *s = (seen *)userdata;
    ++s->ops;
    if (op->kind == OLUI_DRAW_BOX && op->el && olui_id(op->el)[0] && s->n < 64) {
        strncpy(s->order[s->n], olui_id(op->el), 15);
        s->order[s->n][15] = 0;
        ++s->n;
    }
}

static int drawn_at(const seen *s, const char *id)
{
    int i;
    for (i = 0; i < s->n; ++i)
        if (!strcmp(s->order[i], id)) return i;
    return -1;
}

static void draw_all(olui *ui, seen *s)
{
    memset(s, 0, sizeof(*s));
    olui_draw(ui, NULL, record, s);
}

static olui_rect box_of(olui *ui, const char *id)
{
    olui_rect r;
    memset(&r, 0, sizeof(r));
    olui_box(ui, olui_find(ui, id), &r);
    return r;
}

static const char desk_markup[] =
    "<style>.icon{background:#cccccc;width:40px;height:40px}#flow{background:#eeeeee}</style>"
    "<div id=desk style='flex:1'>"
    "  <p id=flow>In the flow</p>"
    "  <div id=a class=icon style='position:absolute;left:10px;top:20px;z-index:3'>A</div>"
    "  <div id=b class=icon style='position:absolute;left:30px;top:30px;z-index:2'>B</div>"
    "  <div id=c style='position:absolute;left:200px;top:5px;padding:0 2px'>Label text</div>"
    "</div>";

/* ---- Positions and layers ---- */
static void test_positions(void)
{
    olui *ui = olui_new();
    char err[128];
    olui_rect r, flow;
    seen s;
    olui_el *d, *desk;
    olui_set_measure(ui, measure, NULL);
    CHECK(olui_load(ui, desk_markup, NULL, err, sizeof err));
    CHECK(olui_layout(ui, 400, 300));
    /* the window's padding is 6 px: the desk's content corner is at 6, 6 */
    r = box_of(ui, "a");
    CHECK(r.x == 16 && r.y == 26 && r.w == 40 && r.h == 40);
    r = box_of(ui, "b");
    CHECK(r.x == 36 && r.y == 36);
    /* no width: as wide as its text and padding */
    r = box_of(ui, "c");
    CHECK(r.x == 206 && r.y == 11 && r.w == 10 * 7 + 4);
    /* out of the flow: the paragraph is at the desk's top */
    flow = box_of(ui, "flow");
    CHECK(flow.y == 6 && flow.x == 6);
    /* the layers: the flow, then b (z 2), then a (z 3) */
    draw_all(ui, &s);
    CHECK(drawn_at(&s, "flow") >= 0 && drawn_at(&s, "flow") < drawn_at(&s, "b"));
    CHECK(drawn_at(&s, "b") < drawn_at(&s, "a"));
    /* the pointer reaches the top one where they overlap; and b where only b is */
    CHECK(olui_hit(ui, 40, 40) == olui_find(ui, "a"));
    CHECK(olui_hit(ui, 70, 70) == olui_find(ui, "b"));
    CHECK(olui_hit(ui, 8, 8) == olui_find(ui, "flow"));
    CHECK(olui_hit(ui, 210, 14) == olui_find(ui, "c"));     /* its text's element is the one that says it */
    CHECK(olui_hit(ui, 390, 290) == olui_find(ui, "desk"));
    /* below 0: under the flow */
    CHECK(olui_set_style(ui, olui_find(ui, "b"), "position:absolute;left:0;top:0;z-index:-1"));
    draw_all(ui, &s);
    CHECK(drawn_at(&s, "b") >= 0 && drawn_at(&s, "b") < drawn_at(&s, "flow"));
    CHECK(olui_hit(ui, 8, 8) == olui_find(ui, "flow"));
    r = box_of(ui, "b");
    CHECK(r.x == 6 && r.y == 6 && r.w == 40);           /* its class's width stays */
    /* placed by the program, then given back to its CSS */
    olui_set_place(ui, olui_find(ui, "a"), 100, 150);
    r = box_of(ui, "a");
    CHECK(r.x == 106 && r.y == 156);
    CHECK(olui_hit(ui, 110, 160) == olui_find(ui, "a"));
    olui_set_place(ui, olui_find(ui, "a"), OLUI_PLACE_CSS, 0);
    r = box_of(ui, "a");
    CHECK(r.x == 16 && r.y == 26);
    /* at twice the size: places scale with the window */
    olui_set_zoom(ui, 200);
    olui_set_place(ui, olui_find(ui, "a"), 100, 150);
    r = box_of(ui, "a");
    CHECK(r.x == 12 + 100 && r.y == 12 + 150 && r.w == 80);
    olui_set_zoom(ui, 100);

    /* made and taken away by the program */
    desk = olui_find(ui, "desk");
    d = olui_add(ui, desk, "<div id=d class=icon style='position:absolute;left:50px;top:200px'>D</div>"
                           "<div id=e class=icon style='position:absolute;left:100px;top:200px'>E</div>",
                 err, sizeof err);
    CHECK(d && d == olui_find(ui, "d") && olui_find(ui, "e") && olui_next(d) == olui_find(ui, "e"));
    CHECK(olui_parent(d) == desk && olui_first_child(desk) == olui_find(ui, "flow"));
    r = box_of(ui, "d");
    CHECK(r.x == 56 && r.y == 206 && r.w == 40);
    CHECK(olui_mouse(ui, 60, 210, OLUI_MOUSE_MOVE) == 0);   /* a plain box: nothing to do */
    CHECK(olui_remove(ui, d));
    CHECK(!olui_find(ui, "d") && olui_find(ui, "e"));
    CHECK(olui_hit(ui, 60, 210) == desk);
    CHECK(!olui_remove(ui, NULL));
    CHECK(!olui_add(ui, desk, "<div>open", err, sizeof err) && err[0]);
    CHECK(!olui_add(ui, desk, "", err, sizeof err));
    /* its <style> is the window's */
    CHECK(olui_add(ui, NULL, "<style>#f{position:absolute;left:300px;top:250px;width:9px;height:9px}</style><div id=f></div>",
                   err, sizeof err));
    r = box_of(ui, "f");
    CHECK(r.x == 306 && r.y == 256 && r.w == 9);
    draw_all(ui, &s);
    CHECK(s.ops > 0);
    olui_free(ui);
}

/* ---- The look: opacity, text-shadow, outline, background-image ---- */

typedef struct {
    int kind;
    char id[16];
    olui_rect box;
    int alpha, border, stops, image, repeat, ax, ay;
    uint32_t argb, border_argb;
    char text[16], name[16];
} rec;

typedef struct {
    rec r[256];
    int n;
} log_t;

static void log_op(void *userdata, const olui_draw_op *op)
{
    log_t *l = (log_t *)userdata;
    rec *r;
    if (l->n == 256) return;
    r = &l->r[l->n++];
    memset(r, 0, sizeof(*r));
    r->kind = op->kind;
    if (op->el) strncpy(r->id, olui_id(op->el), 15);
    r->box = op->box;
    r->alpha = op->alpha;
    r->border = op->border;
    r->stops = op->stops;
    r->image = op->image;
    r->repeat = op->image_repeat;
    r->ax = op->image_align_x;
    r->ay = op->image_align_y;
    r->argb = op->argb;
    r->border_argb = op->border_argb;
    if (op->kind == OLUI_DRAW_TEXT) memcpy(r->text, op->text, op->length < 15 ? op->length : 15);
    if (op->image_name) strncpy(r->name, op->image_name, 15);
}

/* the first op of a kind for an id (NULL: any), from from on */
static const rec *find_op(const log_t *l, int from, int kind, const char *id)
{
    int i;
    for (i = from; i < l->n; ++i)
        if (l->r[i].kind == kind && (!id || !strcmp(l->r[i].id, id))) return &l->r[i];
    return NULL;
}

static void test_look(void)
{
    static const char markup[] =
        "<div id=desk style='flex:1;background-image:url(7);background-repeat:no-repeat;"
        "background-position:center'>"
        " <div id=ghost style='position:absolute;left:10px;top:10px;width:50px;height:50px;opacity:.5;"
        "background:#ff0000'><div id=inner style='background:#00ff00'>x</div></div>"
        " <div id=label style='position:absolute;left:100px;top:10px;color:#ffffff;"
        "text-shadow:1px 0 #000,-1px 0 #000,0 1px #000,0 -1px #000'>Work</div>"
        " <div id=sel style='position:absolute;left:10px;top:100px;width:20px;height:20px;"
        "outline:2px solid #3366cc'></div>"
        " <div id=named style='position:absolute;left:50px;top:100px;width:20px;height:20px;"
        "background-image:url(\"backdrop\")'></div>"
        " <div id=gone style='position:absolute;left:80px;top:100px;width:20px;height:20px;"
        "background:#000;opacity:0'></div>"
        "</div>";
    olui *ui = olui_new();
    char err[128];
    log_t *l = (log_t *)calloc(1, sizeof(log_t));
    const rec *r, *t;
    int i, k;
    olui_set_measure(ui, measure, NULL);
    CHECK(olui_load(ui, markup, NULL, err, sizeof err));
    if (err[0]) printf("%s\n", err);
    CHECK(olui_layout(ui, 400, 300));
    olui_draw(ui, NULL, log_op, l);
    /* opacity: the element's, and those it holds multiplied by it */
    r = find_op(l, 0, OLUI_DRAW_BOX, "ghost");
    CHECK(r && r->alpha == 128);
    r = find_op(l, 0, OLUI_DRAW_BOX, "inner");
    CHECK(r && r->alpha == 128 * 255 / 255);
    r = find_op(l, 0, OLUI_DRAW_BOX, "desk");
    CHECK(!r);                                          /* no background colour: no box */
    r = find_op(l, 0, OLUI_DRAW_BOX, "gone");
    CHECK(r && r->alpha == 0);
    /* text-shadow: four shadows a pixel round, the last first, then the text */
    for (i = 0; i < l->n && !(l->r[i].kind == OLUI_DRAW_TEXT && !strcmp(l->r[i].text, "Work")); ++i) {}
    CHECK(i + 4 < l->n);
    if (i + 4 < l->n) {
        t = &l->r[i + 4];
        CHECK(t->argb == 0xffffffffu && t->alpha == 255);
        for (k = 0; k < 4; ++k) CHECK(l->r[i + k].argb == 0xff000000u && !strcmp(l->r[i + k].text, "Work"));
        CHECK(l->r[i].box.y == t->box.y - 1 && l->r[i].box.x == t->box.x);           /* 0 -1px, drawn first */
        CHECK(l->r[i + 3].box.x == t->box.x + 1 && l->r[i + 3].box.y == t->box.y);   /* 1px 0, under the text */
    }
    /* outline: a box round the border box, after the element */
    r = find_op(l, 0, OLUI_DRAW_BOX, "sel");
    CHECK(r && r->border == 2 && r->stops == 0 && r->border_argb == 0xff3366ccu);
    {
        olui_rect b;
        CHECK(olui_box(ui, olui_find(ui, "sel"), &b));
        CHECK(r && r->box.x == b.x - 2 && r->box.y == b.y - 2 && r->box.w == b.w + 4 && r->box.h == b.h + 4);
    }
    /* background-image: picture 7 in the desk's box, once, in the middle; one by name */
    r = find_op(l, 0, OLUI_DRAW_IMAGE, "desk");
    CHECK(r && r->image == 7 && r->repeat == OLUI_NO_REPEAT && r->ax == 500 && r->ay == 500 && !strcmp(r->name, "7"));
    {
        olui_rect b;
        CHECK(olui_box(ui, olui_find(ui, "desk"), &b));
        CHECK(r && r->box.x == b.x && r->box.w == b.w && r->box.h == b.h);
    }
    /* drawn over its background, under what it holds */
    CHECK(find_op(l, 0, OLUI_DRAW_IMAGE, "desk") < find_op(l, 0, OLUI_DRAW_BOX, "ghost"));
    r = find_op(l, 0, OLUI_DRAW_IMAGE, "named");
    CHECK(r && r->image == -1 && !strcmp(r->name, "backdrop"));
    CHECK(olui_image_name(ui, "backdrop", 3));
    l->n = 0;
    olui_draw(ui, NULL, log_op, l);
    r = find_op(l, 0, OLUI_DRAW_IMAGE, "named");
    CHECK(r && r->image == 3);
    /* every op says how much shows */
    for (i = 0; i < l->n; ++i)
        if (l->r[i].kind != OLUI_DRAW_CLIP && strcmp(l->r[i].id, "ghost") && strcmp(l->r[i].id, "inner") &&
            strcmp(l->r[i].id, "gone"))
            CHECK(l->r[i].alpha == 255);
    /* a part drawn again reaches the shadows' ink */
    {
        olui_rect b, part;
        CHECK(olui_box(ui, olui_find(ui, "label"), &b));
        part.x = b.x + b.w;         /* just right of the label: its 1px shadow's ink is there */
        part.y = b.y;
        part.w = 1;
        part.h = b.h;
        l->n = 0;
        olui_draw(ui, &part, log_op, l);
        CHECK(find_op(l, 0, OLUI_DRAW_TEXT, NULL) != NULL);
    }
    free(l);
    olui_free(ui);
}

/* ---- Selecting and dragging ---- */

typedef struct {
    int selections, starts, moves, drops, cancels, activates;
    int last_index, dx, dy, x, y;
    unsigned qual;
    char target[16], el[16], value[16];
    int dragged_seen;
    char dragged[8][16];
} drag_log;

static void on_desk(olui *ui, const olui_event *ev, void *userdata)
{
    drag_log *g = (drag_log *)userdata;
    int i;
    switch (ev->type) {
    case OLUI_SELECTION: ++g->selections; break;
    case OLUI_DRAG_START: ++g->starts; break;
    case OLUI_DRAG_MOVE: ++g->moves; break;
    case OLUI_DROP: ++g->drops; break;
    case OLUI_DRAG_CANCEL: ++g->cancels; break;
    case OLUI_ACTIVATE: ++g->activates; break;
    default: return;
    }
    g->last_index = ev->index;
    g->dx = ev->dx;
    g->dy = ev->dy;
    g->x = ev->x;
    g->y = ev->y;
    g->qual = ev->qualifiers;
    snprintf(g->el, sizeof g->el, "%s", ev->id);
    snprintf(g->target, sizeof g->target, "%s", ev->item ? olui_id(ev->item) : "");
    snprintf(g->value, sizeof g->value, "%s", ev->value ? ev->value : "");
    if (ev->type == OLUI_DROP) {
        olui_el *d;
        g->dragged_seen = 0;
        for (i = 0; (d = olui_drag_item(ui, i)) != NULL && i < 8; ++i) {
            snprintf(g->dragged[i], 16, "%s", olui_id(d));
            ++g->dragged_seen;
        }
        /* the program moves them: by dx, dy on the desk, or into the drawer */
        if (ev->item && !strcmp(olui_id(ev->item), "trash")) {
            for (i = 0; (d = olui_drag_item(ui, i)) != NULL; ++i) olui_set_hidden(ui, d, 1);
        } else {
            for (i = 0; (d = olui_drag_item(ui, i)) != NULL; ++i) {
                olui_rect r, p;
                olui_box(ui, d, &r);
                olui_box(ui, olui_parent(d), &p);
                olui_set_place(ui, d, r.x - p.x + ev->dx, r.y - p.y + ev->dy);
            }
        }
    }
}

static int is_sel(olui *ui, const char *id)
{
    return olui_is_selected(olui_find(ui, id));
}

static const char drag_markup[] =
    "<style>.icon{position:absolute;width:40px;height:40px;background:#cccccc}"
    ".icon:selected{background:#3366cc}.icon:dragging{opacity:.6}#trash:drop-target{outline:2px solid red}</style>"
    "<div id=desk marquee droppable style='flex:1'>"
    "  <div id=i1 class=icon selectable draggable style='left:10px;top:10px' value=Work>Work</div>"
    "  <div id=i2 class=icon selectable draggable style='left:60px;top:10px'>RAM</div>"
    "  <div id=i3 class=icon selectable draggable style='left:110px;top:10px'>DF0</div>"
    "  <div id=i4 class=icon selectable draggable style='left:10px;top:200px'>Far</div>"
    "  <div id=trash class=icon droppable style='left:300px;top:200px'>Trash</div>"
    "</div>";

static void test_selection(void)
{
    olui *ui = olui_new();
    char err[128];
    drag_log g;
    olui_el *e;
    int n;
    memset(&g, 0, sizeof g);
    olui_set_measure(ui, measure, NULL);
    CHECK(olui_load(ui, drag_markup, NULL, err, sizeof err));
    CHECK(olui_on(ui, NULL, 0, on_desk, &g));
    CHECK(olui_layout(ui, 400, 300));
    /* icons at 16+, 66+, 116+ across, 16 down (the desk's corner is 6, 6) */
    /* a click selects one alone */
    CHECK(olui_mouse(ui, 20, 20, OLUI_MOUSE_DOWN) && olui_mouse(ui, 20, 20, OLUI_MOUSE_UP));
    CHECK(is_sel(ui, "i1") && !is_sel(ui, "i2") && g.selections == 1 && !strcmp(g.el, "desk") && g.last_index == 1);
    CHECK(olui_mouse(ui, 70, 20, OLUI_MOUSE_DOWN) && olui_mouse(ui, 70, 20, OLUI_MOUSE_UP));
    CHECK(!is_sel(ui, "i1") && is_sel(ui, "i2") && g.selections == 2 && !strcmp(g.target, "i2"));
    /* Shift adds, and takes away */
    CHECK(olui_mouse_qual(ui, 120, 20, OLUI_MOUSE_DOWN, OLUI_QUAL_SHIFT));
    olui_mouse_qual(ui, 120, 20, OLUI_MOUSE_UP, OLUI_QUAL_SHIFT);
    CHECK(is_sel(ui, "i2") && is_sel(ui, "i3") && g.last_index == 2 && g.qual == OLUI_QUAL_SHIFT);
    olui_mouse_qual(ui, 20, 20, OLUI_MOUSE_DOWN, OLUI_QUAL_SHIFT);
    olui_mouse_qual(ui, 20, 20, OLUI_MOUSE_UP, OLUI_QUAL_SHIFT);
    CHECK(is_sel(ui, "i1") && is_sel(ui, "i2") && is_sel(ui, "i3"));
    olui_mouse_qual(ui, 70, 20, OLUI_MOUSE_DOWN, OLUI_QUAL_SHIFT);
    olui_mouse_qual(ui, 70, 20, OLUI_MOUSE_UP, OLUI_QUAL_SHIFT);
    CHECK(is_sel(ui, "i1") && !is_sel(ui, "i2") && is_sel(ui, "i3") && g.last_index == 2);
    /* the selected, in order */
    e = olui_next_selected(ui, olui_find(ui, "desk"), NULL);
    CHECK(e == olui_find(ui, "i1"));
    e = olui_next_selected(ui, NULL, e);
    CHECK(e == olui_find(ui, "i3") && !olui_next_selected(ui, NULL, e));
    /* a click on one of several selected: alone when let go */
    n = g.selections;
    olui_mouse(ui, 20, 20, OLUI_MOUSE_DOWN);
    CHECK(is_sel(ui, "i3") && g.selections == n);
    olui_mouse(ui, 20, 20, OLUI_MOUSE_UP);
    CHECK(is_sel(ui, "i1") && !is_sel(ui, "i3") && g.selections == n + 1);
    /* a click on the space: none */
    olui_mouse(ui, 250, 120, OLUI_MOUSE_DOWN);
    olui_mouse(ui, 250, 120, OLUI_MOUSE_UP);
    CHECK(!is_sel(ui, "i1") && olui_next_selected(ui, NULL, NULL) == NULL);
    /* a double-click: activated */
    olui_mouse(ui, 70, 20, OLUI_MOUSE_DOWN);
    olui_mouse(ui, 70, 20, OLUI_MOUSE_UP);
    olui_mouse(ui, 70, 20, OLUI_MOUSE_DOUBLE);
    olui_mouse(ui, 70, 20, OLUI_MOUSE_UP);
    CHECK(g.activates == 1 && !strcmp(g.el, "i2") && !strcmp(g.value, "i2"));
    /* the program's */
    CHECK(olui_select_all(ui, NULL, 1) == 3 && olui_select_all(ui, olui_find(ui, "desk"), 1) == 0);
    CHECK(olui_select_all(ui, NULL, 0) == 4);
    olui_set_selected(ui, olui_find(ui, "i4"), 1);
    CHECK(is_sel(ui, "i4"));

    /* a band from the space: what it touches, as it moves */
    n = g.selections;
    olui_mouse(ui, 5 + 6 + 150, 5 + 6 + 60, OLUI_MOUSE_DOWN);
    CHECK(!is_sel(ui, "i4") && g.selections == n + 1);   /* the click cleared it */
    olui_mouse(ui, 60, 40, OLUI_MOUSE_MOVE);            /* touches i2 and i3 (66..106, 116..156), not i1 (16..56) */
    CHECK(!is_sel(ui, "i1") && is_sel(ui, "i2") && is_sel(ui, "i3"));
    {
        olui_rect area;
        seen s;
        CHECK(olui_damaged(ui, &area));
        draw_all(ui, &s);
        CHECK(s.ops > 0);
    }
    olui_mouse(ui, 18, 40, OLUI_MOUSE_MOVE);            /* now i1 too */
    CHECK(is_sel(ui, "i1") && is_sel(ui, "i2") && is_sel(ui, "i3"));
    olui_mouse(ui, 18, 40, OLUI_MOUSE_UP);
    CHECK(g.selections == n + 2 && g.last_index == 3 && !g.target[0]);
    /* a Shift band adds */
    olui_mouse_qual(ui, 8, 290, OLUI_MOUSE_DOWN, OLUI_QUAL_SHIFT);
    olui_mouse_qual(ui, 30, 230, OLUI_MOUSE_MOVE, OLUI_QUAL_SHIFT);
    olui_mouse_qual(ui, 30, 230, OLUI_MOUSE_UP, OLUI_QUAL_SHIFT);
    CHECK(is_sel(ui, "i1") && is_sel(ui, "i3") && is_sel(ui, "i4"));

    /* dragging i1 drags the four selected; nothing moves under 4 px */
    olui_select_all(ui, NULL, 0);
    olui_set_selected(ui, olui_find(ui, "i1"), 1);
    olui_set_selected(ui, olui_find(ui, "i2"), 1);
    olui_mouse(ui, 20, 20, OLUI_MOUSE_DOWN);
    olui_mouse(ui, 22, 21, OLUI_MOUSE_MOVE);
    CHECK(g.starts == 0);
    olui_mouse(ui, 30, 30, OLUI_MOUSE_MOVE);
    CHECK(g.starts == 1 && g.last_index == 2 && !strcmp(g.el, "i1") && g.moves == 1 && g.dx == 10 && g.dy == 10);
    CHECK(olui_drag_item(ui, 0) == olui_find(ui, "i1") && olui_drag_item(ui, 1) == olui_find(ui, "i2") &&
          !olui_drag_item(ui, 2));
    /* over the trash: the drop target; the dragged aren't under the pointer */
    olui_mouse(ui, 310, 210, OLUI_MOUSE_MOVE);
    CHECK(!strcmp(g.target, "trash") && olui_hit(ui, 20, 20) == olui_find(ui, "desk"));
    {
        /* the ghost: drawn last, moved by the pointer, at the :dragging opacity */
        log_t *l = (log_t *)calloc(1, sizeof(log_t));
        const rec *r;
        olui_rect at;
        olui_draw(ui, NULL, log_op, l);
        r = find_op(l, 0, OLUI_DRAW_BOX, "i1");
        CHECK(r && r->box.x == 16 + 290 && r->box.y == 16 + 190 && r->alpha == 153);
        CHECK(r && find_op(l, 0, OLUI_DRAW_BOX, "trash") < r);
        r = find_op(l, 0, OLUI_DRAW_BOX, "trash");
        CHECK(r && find_op(l, (int)(r - l->r), OLUI_DRAW_BOX, "trash"));  /* and its :drop-target outline */
        /* a part drawn again: only what crosses it, ghost too */
        at.x = 300; at.y = 200; at.w = 20; at.h = 20;
        l->n = 0;
        olui_draw(ui, &at, log_op, l);
        CHECK(find_op(l, 0, OLUI_DRAW_BOX, "i1") && !find_op(l, 0, OLUI_DRAW_BOX, "i3"));
        free(l);
    }
    olui_mouse(ui, 310, 210, OLUI_MOUSE_UP);
    CHECK(g.drops == 1 && !strcmp(g.target, "trash") && g.dragged_seen == 2 && !strcmp(g.dragged[1], "i2"));
    CHECK(!olui_drag_item(ui, 0));
    CHECK(!olui_box(ui, olui_find(ui, "i1"), &(olui_rect){ 0, 0, 0, 0 }));   /* the program hid them */
    /* dragged onto the desk: the program moves it by dx, dy */
    olui_select_all(ui, NULL, 0);
    olui_mouse(ui, 120, 20, OLUI_MOUSE_DOWN);
    olui_mouse(ui, 150, 60, OLUI_MOUSE_MOVE);
    olui_mouse(ui, 170, 120, OLUI_MOUSE_MOVE);
    olui_mouse(ui, 170, 120, OLUI_MOUSE_UP);
    CHECK(g.drops == 2 && !strcmp(g.target, "desk") && g.dx == 50 && g.dy == 100 && g.dragged_seen == 1);
    {
        olui_rect r;
        CHECK(olui_box(ui, olui_find(ui, "i3"), &r) && r.x == 166 && r.y == 116);
    }
    /* Escape: nothing moves */
    olui_mouse(ui, 20, 210, OLUI_MOUSE_DOWN);
    olui_mouse(ui, 80, 250, OLUI_MOUSE_MOVE);
    CHECK(g.starts == 3);
    CHECK(olui_key(ui, OLUI_KEY_ESCAPE, 0));
    CHECK(g.cancels == 1 && !olui_drag_item(ui, 0));
    olui_mouse(ui, 80, 250, OLUI_MOUSE_UP);
    CHECK(g.drops == 2);
    {
        olui_rect r;
        CHECK(olui_box(ui, olui_find(ui, "i4"), &r) && r.x == 16 && r.y == 206);
    }
    /* taken away while dragged: the drag goes on without it */
    olui_mouse(ui, 20, 210, OLUI_MOUSE_DOWN);
    olui_mouse(ui, 80, 250, OLUI_MOUSE_MOVE);
    CHECK(olui_remove(ui, olui_find(ui, "i4")) && !olui_drag_item(ui, 0));
    olui_mouse(ui, 90, 250, OLUI_MOUSE_MOVE);
    olui_mouse(ui, 90, 250, OLUI_MOUSE_UP);
    olui_free(ui);
}

/* ---- Long lists, context menus, several windows ---- */

static int asked;               /* cells the vlist asked for */

static const char *cell(void *userdata, int row, int column, int *image)
{
    static char buf[32];
    (void)userdata;
    ++asked;
    if (column == 0) {
        *image = 100 + row % 3;
        snprintf(buf, sizeof buf, "File %d", row);
    } else {
        snprintf(buf, sizeof buf, "%d KB", row * 2);
    }
    return buf;
}

typedef struct {
    int selects, activates, contexts, last_index, last_checked;
    char el[16], item[16], value[16];
} list_log;

static void on_list(olui *ui, const olui_event *ev, void *userdata)
{
    list_log *g = (list_log *)userdata;
    (void)ui;
    if (ev->type == OLUI_SELECT) ++g->selects;
    else if (ev->type == OLUI_ACTIVATE) ++g->activates;
    else if (ev->type == OLUI_CONTEXT) ++g->contexts;
    else return;
    g->last_index = ev->index;
    g->last_checked = ev->checked;
    snprintf(g->el, sizeof g->el, "%s", ev->id);
    snprintf(g->item, sizeof g->item, "%s", ev->item ? olui_id(ev->item) : "");
    snprintf(g->value, sizeof g->value, "%s", ev->value ? ev->value : "");
}

static void test_lists_menus_windows(void)
{
    static const char markup[] =
        "<vlist id=files columns='100 60' style='height:200px' contextmenu=filemenu></vlist>"
        "<div id=space style='flex:1' contextmenu=deskmenu>Desk</div>"
        "<menu id=filemenu><option value=open id=o1>Open</option><option value=info>Information</option>"
        "<option value=del disabled>Delete</option></menu>"
        "<menu id=deskmenu><option value=clean>Clean Up</option></menu>";
    olui *ui = olui_new(), *ui2;
    char err[128];
    list_log g;
    olui_el *v;
    log_t *l = (log_t *)calloc(1, sizeof(log_t));
    olui_rect r;
    const rec *op;
    int rh, i, n;
    memset(&g, 0, sizeof g);
    olui_set_measure(ui, measure, NULL);
    CHECK(olui_load(ui, markup, NULL, err, sizeof err));
    CHECK(olui_on(ui, NULL, 0, on_list, &g));
    CHECK(olui_layout(ui, 400, 300));
    v = olui_find(ui, "files");
    olui_vlist_set(ui, v, 5000, cell, NULL);
    CHECK(olui_vlist_rows(v) == 5000 && olui_vlist_current(v) == -1);
    /* a row: 16 px of line and 1 px above and below */
    rh = 18;
    CHECK(olui_box(ui, v, &r) && r.h == 200);
    /* drawing asks only for the rows that show: 200 / 18 rows, two cells each */
    asked = 0;
    olui_draw(ui, NULL, log_op, l);
    CHECK(asked > 0 && asked <= 2 * (198 / rh + 2));
    op = find_op(l, 0, OLUI_DRAW_TEXT, "files");
    CHECK(op && !strcmp(op->text, "File 0"));
    op = find_op(l, 0, OLUI_DRAW_IMAGE, "files");
    CHECK(op && op->image == 100 && op->box.w == 16);
    /* the second column 100 px on, the image and 4 px before the first's text */
    for (i = 0; i < l->n && !(l->r[i].kind == OLUI_DRAW_TEXT && !strcmp(l->r[i].text, "0 KB")); ++i) {}
    CHECK(i < l->n && l->r[i].box.x == r.x + 1 + 4 + 100);
    /* a click selects a row; Shift adds; a double-click activates */
    CHECK(olui_mouse(ui, r.x + 20, r.y + 1 + rh * 2 + 5, OLUI_MOUSE_DOWN));
    olui_mouse(ui, r.x + 20, r.y + 1 + rh * 2 + 5, OLUI_MOUSE_UP);
    CHECK(g.selects == 1 && g.last_index == 2 && olui_vlist_selected(v, 2) && olui_vlist_current(v) == 2);
    olui_mouse_qual(ui, r.x + 20, r.y + 1 + rh * 4 + 5, OLUI_MOUSE_DOWN, OLUI_QUAL_SHIFT);
    CHECK(olui_vlist_selected(v, 2) && olui_vlist_selected(v, 4) && !olui_vlist_selected(v, 3));
    olui_mouse(ui, r.x + 20, r.y + 1 + rh * 3 + 5, OLUI_MOUSE_DOWN);
    CHECK(!olui_vlist_selected(v, 2) && olui_vlist_selected(v, 3) && !olui_vlist_selected(v, 4));
    olui_mouse(ui, r.x + 20, r.y + 1 + rh * 3 + 5, OLUI_MOUSE_DOUBLE);
    CHECK(g.activates == 1 && g.last_index == 3 && !strcmp(g.el, "files"));
    /* the selected row is drawn :selected */
    l->n = 0;
    olui_draw(ui, NULL, log_op, l);
    for (i = 0, n = 0; i < l->n; ++i)
        if (l->r[i].kind == OLUI_DRAW_BOX && !strcmp(l->r[i].id, "files") && l->r[i].stops == 1 &&
            l->r[i].border == 0 && l->r[i].box.h == rh)
            ++n;
    CHECK(n == 1);
    /* keys: End to the last, scrolled into view; Up; Return */
    CHECK(olui_key(ui, OLUI_KEY_END, 0));
    CHECK(olui_vlist_current(v) == 4999 && olui_vlist_selected(v, 4999) && olui_get_scroll(v) == 5000 * rh - 198);
    CHECK(olui_key(ui, OLUI_KEY_UP, 0) && olui_vlist_current(v) == 4998);
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0) && g.activates == 2 && g.last_index == 4998);
    CHECK(olui_key(ui, OLUI_KEY_HOME, 0) && olui_get_scroll(v) == 0);
    /* the wheel */
    CHECK(olui_mouse(ui, r.x + 20, r.y + 20, OLUI_WHEEL_DOWN) && olui_get_scroll(v) == 3 * rh);
    asked = 0;
    l->n = 0;
    olui_draw(ui, NULL, log_op, l);
    op = find_op(l, 0, OLUI_DRAW_TEXT, "files");
    CHECK(op && !strcmp(op->text, "File 3"));
    /* the scroll bar down its right: a thumb dragged, the track paged; no row chosen */
    {
        int32_t bx = r.x + r.w - 1 - 6, top = r.y + 1, ty, cur = olui_vlist_current(v);
        olui_set_scroll(ui, v, 0);
        l->n = 0;
        olui_draw(ui, NULL, log_op, l);
        for (i = 0, ty = -1; i < l->n; ++i)
            if (l->r[i].kind == OLUI_DRAW_BOX && !strcmp(l->r[i].id, "files") && l->r[i].box.w == 8) ty = l->r[i].box.y;
        CHECK(ty == top + 1);                                   /* the thumb at the top */
        CHECK(olui_mouse(ui, bx, top + 5, OLUI_MOUSE_DOWN));
        CHECK(olui_mouse(ui, bx, top + 5 + 198, OLUI_MOUSE_MOVE) && olui_get_scroll(v) == 5000 * rh - 198);
        CHECK(olui_mouse(ui, bx, top + 5 + 99, OLUI_MOUSE_MOVE) && olui_get_scroll(v) > 0 &&
              olui_get_scroll(v) < 5000 * rh - 198);
        olui_mouse(ui, bx, top + 5 + 99, OLUI_MOUSE_UP);
        olui_set_scroll(ui, v, 0);
        CHECK(olui_mouse(ui, bx, top + 150, OLUI_MOUSE_DOWN) && olui_get_scroll(v) == 198 - rh);   /* a page */
        olui_mouse(ui, bx, top + 150, OLUI_MOUSE_UP);
        CHECK(olui_vlist_current(v) == cur);
        /* the rows stop short of the bar */
        l->n = 0;
        olui_draw(ui, NULL, log_op, l);
        for (i = 0, n = 0; i < l->n; ++i)
            if (l->r[i].kind == OLUI_DRAW_BOX && !strcmp(l->r[i].id, "files") && l->r[i].box.h == rh &&
                l->r[i].box.x + l->r[i].box.w > r.x + r.w - 1 - 12)
                ++n;
        CHECK(n == 0);
        olui_set_scroll(ui, v, 3 * rh);
    }
    /* fewer rows: the selection of those that stay kept */
    olui_vlist_select(ui, v, 7, 1);
    olui_vlist_select(ui, v, 12, 1);
    olui_vlist_set(ui, v, 10, cell, NULL);
    CHECK(olui_vlist_selected(v, 7) && !olui_vlist_selected(v, 12) && olui_get_scroll(v) == 0);
    olui_vlist_set(ui, v, 20, cell, NULL);
    CHECK(!olui_vlist_selected(v, 12));
    olui_vlist_set_current(ui, v, 15);
    CHECK(olui_vlist_current(v) == 15);

    /* the menu button: OLUI_CONTEXT, and the contextmenu's menu at the pointer */
    CHECK(olui_mouse(ui, r.x + 20, r.y + 20, OLUI_MOUSE_MENU));
    CHECK(g.contexts == 1 && !strcmp(g.el, "files") && !strcmp(g.item, "filemenu"));
    CHECK(olui_select_is_open(ui, olui_find(ui, "filemenu")));
    l->n = 0;
    olui_draw(ui, NULL, log_op, l);
    op = find_op(l, 0, OLUI_DRAW_BOX, "");     /* the pop-up's box: the made popup element */
    for (i = 0; i < l->n && !(l->r[i].kind == OLUI_DRAW_TEXT && !strcmp(l->r[i].text, "Open")); ++i) {}
    CHECK(i < l->n && l->r[i].box.y >= r.y + 20 && l->r[i].box.x >= r.x + 20);
    /* its release chooses nothing; a click on Information chooses it */
    olui_mouse(ui, r.x + 20, r.y + 20, OLUI_MOUSE_UP);
    CHECK(g.activates == 2 && olui_select_is_open(ui, olui_find(ui, "filemenu")));
    {
        int y = l->r[i].box.y + 3 + 18;     /* the next row */
        olui_mouse(ui, l->r[i].box.x + 5, y, OLUI_MOUSE_MOVE);
        olui_mouse(ui, l->r[i].box.x + 5, y, OLUI_MOUSE_DOWN);
        olui_mouse(ui, l->r[i].box.x + 5, y, OLUI_MOUSE_UP);
    }
    CHECK(g.activates == 3 && !strcmp(g.el, "filemenu") && !strcmp(g.value, "info") && g.last_index == 1);
    CHECK(!olui_select_is_open(ui, olui_find(ui, "filemenu")));
    /* elsewhere, another; the menu button again closes the first; Escape closes it */
    olui_mouse(ui, 50, 280, OLUI_MOUSE_MENU);
    CHECK(g.contexts == 2 && !strcmp(g.el, "space") && olui_select_is_open(ui, olui_find(ui, "deskmenu")));
    CHECK(olui_key(ui, OLUI_KEY_ESCAPE, 0) && !olui_select_is_open(ui, olui_find(ui, "deskmenu")));
    CHECK(g.activates == 3);
    /* opened by the program, chosen with the keys */
    CHECK(olui_menu_open(ui, olui_find(ui, "deskmenu"), 10, 10));
    CHECK(olui_key(ui, OLUI_KEY_DOWN, 0) && olui_key(ui, OLUI_KEY_RETURN, 0));
    CHECK(g.activates == 4 && !strcmp(g.value, "clean"));
    CHECK(!olui_menu_open(ui, v, 10, 10));
    /* a menu takes no room and no click */
    CHECK(olui_hit(ui, 50, 285) == olui_find(ui, "space"));

    /* a second window sharing the sheet: CSS added to one is both's */
    ui2 = olui_new_sharing(ui);
    CHECK(ui2 != NULL);
    olui_set_measure(ui2, measure, NULL);
    CHECK(olui_load(ui2, "<div id=d2 class=late>Drawer</div>", NULL, err, sizeof err));
    CHECK(olui_layout(ui2, 200, 100));
    CHECK(olui_add_css(ui, ".late{height:33px}"));
    {
        olui_rect b;
        CHECK(olui_damaged(ui2, NULL));
        CHECK(olui_box(ui2, olui_find(ui2, "d2"), &b) && b.h == 33);
    }
    CHECK(olui_add_css(ui2, "#space{background:#123456}"));
    l->n = 0;
    olui_draw(ui, NULL, log_op, l);
    op = find_op(l, 0, OLUI_DRAW_BOX, "space");
    CHECK(op && op->stops == 1);
    olui_free(ui);                      /* the sheet stays with the second */
    l->n = 0;
    olui_draw(ui2, NULL, log_op, l);
    CHECK(find_op(l, 0, OLUI_DRAW_TEXT, "d2") != NULL);
    olui_free(ui2);
    free(l);
}

int main(void)
{
    test_positions();
    test_look();
    test_selection();
    test_lists_menus_windows();
    printf("OLUI DESKTOP %d checks, %d failed\n", checks, failed);
    if (failed) return 20;
    printf("OLUI DESKTOP PASS version=%s\n", OLUI_VERSION);
    return 0;
}
