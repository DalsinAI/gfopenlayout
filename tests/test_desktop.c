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

int main(void)
{
    test_positions();
    test_look();
    printf("OLUI DESKTOP %d checks, %d failed\n", checks, failed);
    if (failed) return 20;
    printf("OLUI DESKTOP PASS version=%s\n", OLUI_VERSION);
    return 0;
}
