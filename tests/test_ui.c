/* GFOpenLayout UI on the host: markup and CSS to a laid out window, the
 * widgets' events through C callbacks, the keyboard, the theme as CSS.
 * With an argument, writes the window's draw ops there as lines of text
 * (tests/ui_render.py draws them as a picture); a second, the window as it
 * opens; a third, 0.3's widgets' window.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); return 20; \
} } while (0)

static long measured;          /* texts measured, all told */

/* every character 7 px wide (8 bold), ascent 10, descent 3, at 13 px */
static int measure(void *userdata, const char *utf8, size_t length,
                   const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    (void)userdata;
    ++measured;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * style->font_size * ((style->text_flags & OL_TEXT_BOLD) ? 8 : 7) / 13;
    m->ascent = style->font_size * 10 / 13;
    m->descent = style->font_size * 3 / 13;
    return 1;
}

static const char markup[] =
    "<style>#title{font-size:16px}</style>\n"
    "<h1 id=title>Mail folders</h1>\n"
    "<row>\n"
    "  <label style='width:60px'>Search</label>\n"
    "  <input id=search placeholder='Type to search' style='flex:1'>\n"
    "  <button id=go>Go</button>\n"
    "</row>\n"
    "<row style='flex:1'>\n"
    "  <tree id=folders style='width:180px;height:auto'>\n"
    "    <item id=inbox value=INBOX open>Inbox\n"
    "      <item id=work>Work</item>\n"
    "      <item id=family>Family</item>\n"
    "    </item>\n"
    "    <item id=sent>Sent</item>\n"
    "    <item id=archive>Archive<item id=y2025>2025</item><item id=y2026>2026</item></item>\n"
    "  </tree>\n"
    "  <tabs id=tabs style='flex:1'>\n"
    "    <tab title=Message id=t1>\n"
    "      <checkbox id=html checked>Show HTML</checkbox>\n"
    "      <radio name=sort id=bydate value=date checked>By date</radio>\n"
    "      <radio name=sort id=byname value=name>By sender</radio>\n"
    "      <p>Some <b>bold</b> text &amp; more.</p>\n"
    "    </tab>\n"
    "    <tab title=Settings id=t2><label>Settings page</label></tab>\n"
    "  </tabs>\n"
    "</row>\n"
    "<progress id=bar max=10></progress>\n"
    "<row style='justify-content:end'><button id=ok>OK</button><button id=cancel disabled>Cancel</button></row>\n";

static int clicks, changes, submits, selects, activates, expands, collapses, tab_selects;
static char last_value[64];

static void on_event(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ui;
    (void)userdata;
    switch (ev->type) {
    case OLUI_CLICK: ++clicks; break;
    case OLUI_CHANGE: ++changes; break;
    case OLUI_SUBMIT: ++submits; break;
    case OLUI_SELECT: if (!strcmp(ev->id, "tabs")) ++tab_selects; else ++selects; break;
    case OLUI_ACTIVATE: ++activates; break;
    case OLUI_EXPAND: ++expands; break;
    case OLUI_COLLAPSE: ++collapses; break;
    default: break;
    }
    if (ev->value) {
        strncpy(last_value, ev->value, sizeof(last_value) - 1);
        last_value[sizeof(last_value) - 1] = 0;
    }
}

static int ok_clicks;
static void on_ok(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ui;
    (void)ev;
    *(int *)userdata += 1;
}

typedef struct {
    FILE *out;
    int boxes, texts, marks;
    int saw_text_ok;
} sink;

static void ignore_op(void *userdata, const olui_draw_op *op)
{
    (void)userdata;
    (void)op;
}

static void take_away(olui *ui, const olui_event *ev, void *userdata)
{
    (void)userdata;
    olui_remove(ui, ev->el);
}

static void clear_it(olui *ui, const olui_event *ev, void *userdata)
{
    (void)userdata;
    olui_list_clear(ui, ev->el);
}

static void load_again(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ev;
    (void)userdata;
    olui_load(ui, "<label>new</label>", NULL, NULL, 0);
}

static void retext(olui *ui, const olui_event *ev, void *userdata)
{
    (void)userdata;
    olui_set_text(ui, ev->el, "x");
}

static void renumber(olui *ui, const olui_event *ev, void *userdata)
{
    (void)userdata;
    olui_set_number(ui, ev->el, ev->index + 1);
}

static void read_value(olui *ui, const olui_event *ev, void *userdata)
{
    (void)ui;
    *(size_t *)userdata = ev->value ? strlen(ev->value) : 0;
}

/* A handler takes away what it was called for: the code that called it, and the handlers
 * after it, are still using that. Run it under AddressSanitizer to see them. */
static int test_handlers_take_away(void)
{
    char err[120];
    olui *ui = olui_new();
    olui_rect b;
    size_t got = 0;
    CHECK(ui && olui_load(ui, "<input id=f value=abc>", NULL, err, sizeof(err)) && olui_layout(ui, 300, 100));
    olui_on(ui, "f", OLUI_SUBMIT, take_away, NULL);
    olui_focus(ui, olui_find(ui, "f"));
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0));
    CHECK(!olui_find(ui, "f") && !olui_focused(ui));
    olui_draw(ui, NULL, ignore_op, NULL);
    olui_free(ui);

    ui = olui_new();
    CHECK(ui && olui_load(ui, "<input id=f value=abc>", NULL, err, sizeof(err)) && olui_layout(ui, 300, 100));
    olui_on(ui, "f", OLUI_SUBMIT, load_again, NULL);
    olui_focus(ui, olui_find(ui, "f"));
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0));
    CHECK(!olui_find(ui, "f") && !olui_focused(ui));
    olui_draw(ui, NULL, ignore_op, NULL);
    olui_free(ui);

    ui = olui_new();
    CHECK(ui && olui_load(ui, "<list id=l><item>a</item><item>b</item></list>", NULL, err, sizeof(err)) &&
          olui_layout(ui, 300, 100));
    olui_on(ui, "l", OLUI_SELECT, clear_it, NULL);
    olui_focus(ui, olui_find(ui, "l"));
    olui_key(ui, OLUI_KEY_DOWN, 0);
    olui_draw(ui, NULL, ignore_op, NULL);
    olui_free(ui);

    /* the handler after it reads the event's text */
    ui = olui_new();
    CHECK(ui && olui_load(ui, "<input id=f>", NULL, err, sizeof(err)) && olui_layout(ui, 300, 100));
    olui_on(ui, "f", OLUI_CHANGE, read_value, &got);
    olui_on(ui, "f", OLUI_CHANGE, retext, NULL);    /* the last added is called first */
    olui_focus(ui, olui_find(ui, "f"));
    CHECK(olui_key(ui, 'q', 0) && got == 1);
    olui_free(ui);

    /* and a number field's, made again by the handler before */
    ui = olui_new();
    got = 0;
    CHECK(ui && olui_load(ui, "<input id=f type=number value=1 min=0 max=99>", NULL, err, sizeof(err)) &&
          olui_layout(ui, 300, 100));
    olui_on(ui, "f", OLUI_CHANGE, read_value, &got);
    olui_on(ui, "f", OLUI_CHANGE, renumber, NULL);
    olui_focus(ui, olui_find(ui, "f"));
    CHECK(olui_key(ui, '5', 0) && got > 0);
    olui_free(ui);

    /* a spin button goes while its arrow is held */
    ui = olui_new();
    CHECK(ui && olui_load(ui, "<input id=n type=number value=1 min=0 max=99>", NULL, err, sizeof(err)) &&
          olui_layout(ui, 300, 100));
    CHECK(olui_box(ui, olui_find(ui, "n"), &b));
    olui_on(ui, "n", OLUI_CHANGE, take_away, NULL);
    olui_mouse(ui, b.x + b.w - 3, b.y + 3, OLUI_MOUSE_DOWN);
    olui_tick(ui, 2000);
    olui_free(ui);

    /* as deep as markup may go */
    {
        static char deep[700];
        char *d = deep;
        int i;
        ui = olui_new();
        for (i = 0; i < 60; ++i) { memcpy(d, "<div>", 5); d += 5; }
        for (i = 0; i < 60; ++i) { memcpy(d, "</div>", 6); d += 6; }
        *d = 0;
        CHECK(ui && olui_load(ui, deep, NULL, err, sizeof(err)) && olui_layout(ui, 300, 100));
        olui_free(ui);
    }
    return 0;
}

static void draw(void *userdata, const olui_draw_op *op)
{
    sink *s = (sink *)userdata;
    switch (op->kind) {
    case OLUI_DRAW_BOX: {
        int i;
        ++s->boxes;
        if (s->out) {
            fprintf(s->out, "box %ld %ld %ld %ld %d %08lx %d %d", (long)op->box.x, (long)op->box.y,
                    (long)op->box.w, (long)op->box.h, op->border, (unsigned long)op->border_argb, op->radius,
                    op->stops);
            for (i = 0; i < op->stops; ++i)
                fprintf(s->out, " %08lx %d", (unsigned long)op->stop_argb[i], op->stop_pos[i]);
            fputc('\n', s->out);
        }
        break;
    }
    case OLUI_DRAW_TEXT:
        ++s->texts;
        if (op->length == 2 && !memcmp(op->text, "OK", 2)) s->saw_text_ok = 1;
        if (s->out)
            fprintf(s->out, "text %ld %ld %d %08lx %lu %.*s\n", (long)op->box.x, (long)op->baseline,
                    op->font_pixels, (unsigned long)op->argb, (unsigned long)op->text_flags,
                    (int)op->length, op->text);
        break;
    case OLUI_DRAW_MARK:
        ++s->marks;
        if (s->out)
            fprintf(s->out, "mark %ld %ld %ld %ld %d %08lx\n", (long)op->box.x, (long)op->box.y,
                    (long)op->box.w, (long)op->box.h, op->mark, (unsigned long)op->argb);
        break;
    case OLUI_DRAW_CLIP:
        if (s->out)
            fprintf(s->out, "clip %ld %ld %ld %ld\n", (long)op->clip.x, (long)op->clip.y,
                    (long)op->clip.w, (long)op->clip.h);
        break;
    default:
        break;
    }
}

/* A picture that asks to be drawn again while it is drawn (a paint handler's Refresh) */
typedef struct { olui *ui; olui_el *el; int asked; } reasker;
static void reask(void *userdata, const olui_draw_op *op)
{
    reasker *r = (reasker *)userdata;
    if (!r->asked && op->el == r->el) {
        r->asked = 1;
        olui_repaint(r->ui, r->el, NULL);
    }
}

static olui_rect box_of(olui *ui, const char *id);

/* The centre of an element, found by drawing and catching its box */
typedef struct { const olui_el *want; olui_rect r; int found; } finder;
static void find_box(void *userdata, const olui_draw_op *op)
{
    finder *f = (finder *)userdata;
    if (!f->found && op->el == f->want && (op->kind == OLUI_DRAW_BOX || op->kind == OLUI_DRAW_TEXT)) {
        f->r = op->box;
        f->found = 1;
    }
}

static olui_rect box_of(olui *ui, const char *id)
{
    finder f;
    memset(&f, 0, sizeof(f));
    f.want = olui_find(ui, id);
    olui_draw(ui, NULL, find_box, &f);
    return f.r;
}

static void click(olui *ui, olui_rect r)
{
    olui_mouse(ui, r.x + r.w / 2, r.y + r.h / 2, OLUI_MOUSE_MOVE);
    olui_mouse(ui, r.x + r.w / 2, r.y + r.h / 2, OLUI_MOUSE_DOWN);
    olui_mouse(ui, r.x + r.w / 2, r.y + r.h / 2, OLUI_MOUSE_UP);
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *s;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    s = (char *)malloc((size_t)n + 1);
    if (s && fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); s = NULL; }
    if (s) s[n] = 0;
    fclose(f);
    return s;
}

/* ---- Frames: the window's pixels from its draw ops (each op only inside
 * its box, cut to the clip and to what is drawn), to see that drawing only
 * what a hover, press or focus damaged ends with the picture drawing it all
 * gives ---- */
#define FW 520
#define FH 340

typedef struct {
    uint32_t *px;
    olui_rect clip;
    int clipped;
    olui_rect area;
    long ops;
} frame;

static void frame_fill(frame *f, olui_rect r, uint32_t argb)
{
    int32_t x, y, x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    if ((argb >> 24) < 0x80) return;    /* see-through: left out, as the Amiga's buffer does */
    if (f->clipped) {
        if (x0 < f->clip.x) x0 = f->clip.x;
        if (y0 < f->clip.y) y0 = f->clip.y;
        if (x1 > f->clip.x + f->clip.w) x1 = f->clip.x + f->clip.w;
        if (y1 > f->clip.y + f->clip.h) y1 = f->clip.y + f->clip.h;
    }
    if (x0 < f->area.x) x0 = f->area.x;
    if (y0 < f->area.y) y0 = f->area.y;
    if (x1 > f->area.x + f->area.w) x1 = f->area.x + f->area.w;
    if (y1 > f->area.y + f->area.h) y1 = f->area.y + f->area.h;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) f->px[y * FW + x] = argb;
}

static uint32_t frame_mix(uint32_t a, uint32_t b, int t)
{
    uint32_t out = 0xff000000u;
    int k;
    for (k = 0; k < 24; k += 8) {
        int va = (int)((a >> k) & 255), vb = (int)((b >> k) & 255);
        out |= (uint32_t)(va + (vb - va) * t / 1000) << k;
    }
    return out;
}

static void frame_op(void *userdata, const olui_draw_op *op)
{
    frame *f = (frame *)userdata;
    olui_rect r = op->box;
    int32_t i;
    ++f->ops;
    switch (op->kind) {
    case OLUI_DRAW_CLIP:
        f->clip = op->clip;
        f->clipped = op->clip.w > 0 && op->clip.h > 0;
        break;
    case OLUI_DRAW_BOX:
        /* row by row down the stops, corners cut in, the border round */
        for (i = 0; i < op->box.h; ++i) {
            int32_t edge = i < op->box.h - 1 - i ? i : op->box.h - 1 - i;
            int32_t in = edge < op->radius ? (op->radius - edge) / 2 : 0, b = op->border;
            uint32_t fill = op->stops ? frame_mix(op->stop_argb[0], op->stop_argb[op->stops - 1],
                                                  op->box.h > 1 ? (int)(i * 1000 / (op->box.h - 1)) : 0) : 0;
            r.x = op->box.x + in;
            r.y = op->box.y + i;
            r.w = op->box.w - 2 * in;
            r.h = 1;
            if (b && (i < b || i >= op->box.h - b)) {
                frame_fill(f, r, op->border_argb | 0xff000000u);
                continue;
            }
            if (op->stops) {
                olui_rect m = r;
                m.x += b;
                m.w -= 2 * b;
                frame_fill(f, m, fill);
            }
            if (b) {
                olui_rect side = r;
                side.w = b;
                frame_fill(f, side, op->border_argb | 0xff000000u);
                side.x = r.x + r.w - b;
                frame_fill(f, side, op->border_argb | 0xff000000u);
            }
        }
        break;
    case OLUI_DRAW_TEXT: {
        /* a stroke a character, where and how long by the character */
        int32_t asc = op->font_pixels * 10 / 13, cw = op->font_pixels * 7 / 13;
        size_t k;
        if (cw < 2) cw = 2;
        for (k = 0; k < op->length; ++k) {
            unsigned c = (unsigned char)op->text[k];
            r.x = op->box.x + (int32_t)k * cw + (int32_t)(c % (unsigned)(cw - 1));
            r.y = op->baseline - asc + (int32_t)(c % 3);
            r.w = 1 + (int32_t)(c & 1);
            r.h = op->font_pixels - (int32_t)(c % 3);
            frame_fill(f, r, op->argb | 0xff000000u);
        }
        break;
    }
    case OLUI_DRAW_MARK:
        frame_fill(f, r, (op->argb | 0xff000000u) ^ (uint32_t)op->mark);
        break;
    case OLUI_DRAW_IMAGE:
        frame_fill(f, r, 0xff000000u | (uint32_t)op->image * 0x010101u);
        break;
    default:
        break;
    }
}

/* Draws ui into px: what crosses area, or all of it on a fresh frame */
static long frame_draw(olui *ui, uint32_t *px, const olui_rect *area)
{
    frame f;
    memset(&f, 0, sizeof(f));
    f.px = px;
    if (area) {
        f.area = *area;
    } else {
        int i;
        for (i = 0; i < FW * FH; ++i) px[i] = 0xffff00ffu;
        f.area.w = FW;
        f.area.h = FH;
    }
    olui_draw(ui, area, frame_op, &f);
    return f.ops;
}

/* What happens to the window, and what it may do: lay out again or not */
enum { ACT_MOVE, ACT_DOWN, ACT_UP, ACT_TAB, ACT_WHEEL, ACT_WHEEL_UP, ACT_OPEN };

#define FOLDERS 40

/* Where the pointer goes for a step: an element's middle by its id;
 * "itemN" the program's Nth item; "edge" the item the tree's top cuts;
 * "strip" the first tab button; NULL the window's padding */
static int aim(olui *ui, olui_el **added, const char *id, int *x, int *y)
{
    olui_rect at, tree;
    int i;
    *x = 3;
    *y = FH - 2;
    if (!id) return 1;
    if (!strcmp(id, "edge")) {
        if (!olui_box(ui, olui_find(ui, "folders"), &tree)) return 0;
        for (i = 0; i < FOLDERS; ++i)
            if (olui_box(ui, added[i], &at) && at.y < tree.y + 1 && at.y + at.h > tree.y + 3) {
                *x = at.x + at.w / 2;
                *y = tree.y + 2;
                return 1;
            }
        return 0;
    }
    if (!strcmp(id, "strip")) {
        if (!olui_box(ui, olui_find(ui, "tabs"), &at)) return 0;
        *x = at.x + 10;
        *y = at.y + 4;
        return 1;
    }
    if (!strncmp(id, "item", 4)) {
        if (!olui_box(ui, added[atoi(id + 4)], &at)) return 0;
    } else if (!olui_box(ui, olui_find(ui, id), &at)) {
        return 0;
    }
    *x = at.x + at.w / 2;
    *y = at.y + at.h / 2;
    return 1;
}

static int frames(void)
{
    static const char css[] =
        "item{padding:2px 4px}"
        "button:hover{background:linear-gradient(#fff4dd,#e8c890);color:#803000}"
        "item:hover{background:#dde8ff}"
        "checkbox:hover{color:#0000aa}"
        "tabbutton:hover{border-color:#cc6600}"
        "input:focus{background:#ffffee}"
        "#go:hover{padding:3px 20px}"   /* one that changes its size: laid out again */
        "item:selected item{color:#aa0000}"     /* rules that reach the items under an item */
        "item:open item{color:#006600}";
    /* what a step may do: damage a widget's place (an eighth of the window
     * at most), damage across the window (two widgets far apart), lay out
     * again, or damage all of it without a layout */
    enum { SMALL, WIDE, LAYOUT, ALL };
    static const struct { int act; const char *id; int may; } steps[] = {
        { ACT_MOVE, "ok", SMALL },          /* hover on */
        { ACT_MOVE, "go", LAYOUT },         /* off OK, on Go, which grows */
        { ACT_MOVE, NULL, LAYOUT },         /* off Go: back to its size */
        { ACT_MOVE, "ok", SMALL },
        { ACT_DOWN, "ok", SMALL },          /* pressed (button:active) and the keyboard */
        { ACT_MOVE, NULL, SMALL },          /* off it while held: not pressed-looking */
        { ACT_MOVE, "ok", SMALL },
        { ACT_UP, "ok", SMALL },            /* a click */
        { ACT_TAB, NULL, WIDE },            /* the keyboard from OK to the field: caret, no placeholder */
        { ACT_TAB, NULL, SMALL },           /* to Go, beside it */
        { ACT_TAB, NULL, WIDE },            /* to the tree */
        { ACT_MOVE, NULL, SMALL },
        { ACT_MOVE, "html", SMALL },        /* a checkbox's text colour */
        { ACT_MOVE, NULL, SMALL },
        { ACT_MOVE, "work", SMALL },        /* an item */
        { ACT_MOVE, "family", SMALL },
        { ACT_WHEEL, "folders", ALL },      /* scrolled */
        { ACT_MOVE, "item3", SMALL },       /* an item, moved by the scroll */
        { ACT_MOVE, "item4", SMALL },
        { ACT_MOVE, "edge", SMALL },        /* one the tree's top cuts */
        { ACT_MOVE, NULL, SMALL },
        { ACT_MOVE, "strip", SMALL },       /* a tab button */
        { ACT_MOVE, NULL, SMALL },
        { ACT_DOWN, "html", WIDE },         /* focus from the tree to the checkbox */
        { ACT_UP, "html", SMALL },          /* unchecked: its tick goes */
        { ACT_DOWN, "byname", SMALL },
        { ACT_UP, "byname", SMALL },        /* a radio button chosen, the other not */
        { ACT_WHEEL_UP, "folders", ALL },
        { ACT_DOWN, "sent", WIDE },         /* an item selected, the focus to its tree */
        { ACT_DOWN, "inbox", SMALL },       /* another: the items under it are styled again */
        { ACT_DOWN, "work", SMALL },
        { ACT_OPEN, "archive", LAYOUT },    /* items shown: laid out, the items under it styled again */
        { ACT_DOWN, "y2025", SMALL }
    };
    olui *ui = olui_new();
    uint32_t *screen = (uint32_t *)malloc(FW * FH * sizeof(uint32_t));
    uint32_t *full = (uint32_t *)malloc(FW * FH * sizeof(uint32_t));
    olui_el *added[FOLDERS];
    char err[128];
    size_t k;
    int i, partial = 0;
    long most = 0;

    if (!screen || !full) {
        printf("frames: no memory for two frames, left out\n");
        free(screen);
        free(full);
        olui_free(ui);
        return 0;
    }
    CHECK(ui);
    olui_set_measure(ui, measure, NULL);
    CHECK(olui_load(ui, markup, css, err, sizeof(err)));
    {
        /* enough items for the tree to scroll */
        olui_el *tree = olui_find(ui, "folders");
        char text[24];
        for (i = 0; i < FOLDERS; ++i) {
            sprintf(text, "Folder %d", i);
            CHECK((added[i] = olui_list_add(ui, tree, NULL, text, NULL)));
        }
    }
    CHECK(olui_layout(ui, FW, FH));
    frame_draw(ui, screen, NULL);

    for (k = 0; k < sizeof(steps) / sizeof(steps[0]); ++k) {
        olui_rect r;
        long before;
        int x, y;
        CHECK(aim(ui, added, steps[k].id, &x, &y));
        before = measured;
        switch (steps[k].act) {
        case ACT_MOVE: olui_mouse(ui, x, y, OLUI_MOUSE_MOVE); break;
        case ACT_DOWN: olui_mouse(ui, x, y, OLUI_MOUSE_DOWN); break;
        case ACT_UP: olui_mouse(ui, x, y, OLUI_MOUSE_UP); break;
        case ACT_TAB: olui_key(ui, OLUI_KEY_TAB, 0); break;
        case ACT_WHEEL:
            for (i = 0; i < 3; ++i) olui_mouse(ui, x, y, OLUI_WHEEL_DOWN);
            break;
        case ACT_WHEEL_UP:
            for (i = 0; i < 3; ++i) olui_mouse(ui, x, y, OLUI_WHEEL_UP);
            break;
        case ACT_OPEN: olui_item_open(ui, olui_find(ui, steps[k].id), 1); break;
        default: break;
        }
        /* a hover, press, focus, check or selection that changes no size lays nothing out
         * (measures nothing), and damages only where it changed */
        if (steps[k].may == SMALL || steps[k].may == WIDE) {
            if (measured != before) printf("frames: step %d measured %ld\n", (int)k, measured - before);
            CHECK(measured == before);
            if (olui_damaged(ui, &r)) {
                if (getenv("OLUI_DEBUG"))
                    printf("frames: step %d damage %ld %ld %ld %ld\n", (int)k, (long)r.x, (long)r.y, (long)r.w, (long)r.h);
                CHECK(r.w > 0 && r.h > 0 && (long)r.w * r.h < (long)FW * FH / (steps[k].may == SMALL ? 8 : 1));
                ++partial;
            }
        }
        /* the window's loop: draw what is damaged */
        if (olui_damaged(ui, &r)) {
            long n = frame_draw(ui, screen, &r);
            if (steps[k].may == SMALL && n > most) most = n;
        }
        /* the same as drawing it all, as laid out now */
        frame_draw(ui, full, NULL);
        if (memcmp(screen, full, FW * FH * sizeof(uint32_t))) printf("frames: step %d differs from a full draw\n", (int)k);
        CHECK(!memcmp(screen, full, FW * FH * sizeof(uint32_t)));
        /* and as every element styled and laid out again from the start */
        CHECK(olui_add_css(ui, ""));
        frame_draw(ui, full, NULL);
        if (memcmp(screen, full, FW * FH * sizeof(uint32_t))) printf("frames: step %d differs from a new layout\n", (int)k);
        CHECK(!memcmp(screen, full, FW * FH * sizeof(uint32_t)));
    }
    CHECK(partial >= 12);

    /* with no rule for :hover, a hover damages nothing */
    {
        olui *plain = olui_new();
        olui_rect r, b;
        CHECK(plain);
        olui_set_measure(plain, measure, NULL);
        CHECK(olui_load(plain, markup, NULL, err, sizeof(err)) && olui_layout(plain, FW, FH));
        frame_draw(plain, full, NULL);
        CHECK(olui_box(plain, olui_find(plain, "ok"), &b));
        CHECK(olui_mouse(plain, b.x + 2, b.y + 2, OLUI_MOUSE_MOVE) && !olui_damaged(plain, &r));
        CHECK(!olui_mouse(plain, 3, FH - 2, OLUI_MOUSE_MOVE) && !olui_damaged(plain, &r));
        /* olui_repaint: the part asked for, inside the element, and no more */
        {
            olui_rect part;
            part.x = b.x + 1; part.y = b.y + 1; part.w = 3; part.h = 2;
            olui_repaint(plain, olui_find(plain, "ok"), &part);
            CHECK(olui_damaged(plain, &r) && r.x == b.x + 1 && r.y == b.y + 1 && r.w == 3 && r.h == 2);
            frame_draw(plain, full, NULL);
            part.x = b.x - 50; part.y = b.y - 50; part.w = 10; part.h = 10;   /* outside it: nothing */
            olui_repaint(plain, olui_find(plain, "ok"), &part);
            CHECK(!olui_damaged(plain, &r));
            olui_repaint(plain, olui_find(plain, "ok"), NULL);                 /* all of it */
            CHECK(olui_damaged(plain, &r) && r.x <= b.x && r.y <= b.y && r.x + r.w >= b.x + b.w && r.y + r.h >= b.y + b.h);
            frame_draw(plain, full, NULL);
        }
        /* a repaint asked while drawing is kept for the next draw, not lost */
        {
            reasker ra;
            ra.ui = plain;
            ra.el = olui_find(plain, "ok");
            ra.asked = 0;
            olui_draw(plain, NULL, reask, &ra);
            CHECK(ra.asked && olui_damaged(plain, &r) && r.x <= b.x && r.y <= b.y);
            frame_draw(plain, full, NULL);
            CHECK(!olui_damaged(plain, &r));
        }
        olui_free(plain);
    }
    printf("frames: %d partial draws, at most %ld ops each, each the same as a full draw\n", partial, most);
    free(screen);
    free(full);
    olui_free(ui);
    return 0;
}

/* ---- 0.3's widgets: select, textarea, password, number, fieldset, label,
 * Tab's order, a box that scrolls, tooltips; each step drawn only where it
 * damaged, and that the same as drawing it all ---- */

static const char widget_markup[] =
    "<row style='align-items:start'>\n"
    "  <label for=fruit id=lfruit style='width:50px'>Fruit</label>\n"
    "  <select id=fruit title='Pick one'><option value=a>Apple</option>\n"
    "    <optgroup label=Citrus><option value=o selected>Orange<option>Lemon</optgroup>\n"
    "    <option disabled>Mango</option><option value=p>Pear</option></select>\n"
    "  <select id=lb size=3><option>One<option>Two<option selected>Three<option>Four<option>Five</select>\n"
    "  <input type=password id=pw maxlength=6>\n"
    "  <spinner id=num min=-1 max=2.5 step=0.5 value=1>\n"
    "</row>\n"
    "<row><textarea id=ta rows=3 cols=10 placeholder=Notes></textarea>\n"
    "  <textarea id=ro readonly rows=2 cols=8>Fixed text</textarea>\n"
    "  <textarea id=nw wrap=off rows=2 cols=6 maxlength=12></textarea>\n"
    "  <input type=number id=whole value=5></row>\n"
    "<fieldset id=fs><legend id=lg>Options</legend>\n"
    "  <label id=lw>Wrap <checkbox id=cb>Bold</checkbox></label>\n"
    "  <row><label for=cb2 id=l2>Italic too</label><checkbox id=cb2 tabindex=-1>It</checkbox></row>\n"
    "</fieldset>\n"
    "<fieldset id=plain><label id=nolg>No legend</label></fieldset>\n"
    "<div id=sb style='height:40px;overflow:auto'><button id=b1>One</button><button id=b2>Two</button>"
    "<button id=b3 tabindex=1>Three</button></div>\n";

static int w_events, w_last_type, w_last_index, w_paste_ok;
static char w_last_value[64];
static const char *w_last_id = "";

static void on_widget(olui *ui, const olui_event *ev, void *userdata)
{
    (void)userdata;
    if (ev->type == OLUI_FOCUS) return;
    ++w_events;
    w_last_type = ev->type;
    w_last_index = ev->index;
    w_last_id = ev->id;
    if (ev->value) {
        strncpy(w_last_value, ev->value, sizeof(w_last_value) - 1);
        w_last_value[sizeof(w_last_value) - 1] = 0;
    } else {
        strcpy(w_last_value, "(none)");
    }
    if (ev->type == OLUI_PASTE) w_paste_ok = olui_insert_text(ui, ev->el, "PASTED");
}

/* What draw ops say: a text drawn, a box of an element's, the last op's element */
typedef struct {
    const char *want;
    int found;
    const olui_el *el_of_want;
    const olui_el *box_el;
    int boxes;
    const olui_el *last_el;
    int bullets;
} looker;

static void look(void *userdata, const olui_draw_op *op)
{
    looker *l = (looker *)userdata;
    if (op->kind == OLUI_DRAW_TEXT) {
        size_t n = strlen(l->want);
        if (op->length == n && !memcmp(op->text, l->want, n)) {
            l->found = 1;
            l->el_of_want = op->el;
        }
        if (op->length >= 3 && !memcmp(op->text, "\xe2\x80\xa2", 3)) l->bullets = (int)op->length / 3;
    }
    if (op->kind == OLUI_DRAW_BOX && l->box_el && op->el == l->box_el) ++l->boxes;
    if (op->kind != OLUI_DRAW_CLIP) l->last_el = op->el;
}

static uint32_t *w_screen;     /* the frame drawn only where damaged */
static const char *w_ops_path; /* where to write the widgets' window as ops (argv[3]) */

/* Draws the window to see what it shows: what was damaged is drawn on
 * w_screen first, as the window's loop would have */
/* A separator row: drawn as a box one pixel high, never as text */
typedef struct { const olui_el *sep; int lines, texts; } sep_look;
static void look_sep(void *userdata, const olui_draw_op *op)
{
    sep_look *l = (sep_look *)userdata;
    if (op->el != l->sep) return;
    if (op->kind == OLUI_DRAW_BOX && op->box.h == 1 && op->box.w > 0) ++l->lines;
    if (op->kind == OLUI_DRAW_TEXT) ++l->texts;
}

static looker seek(olui *ui, const char *text, const olui_el *box_el)
{
    looker l;
    olui_rect r;
    if (w_screen && olui_damaged(ui, &r)) frame_draw(ui, w_screen, &r);
    memset(&l, 0, sizeof(l));
    l.want = text;
    l.box_el = box_el;
    olui_draw(ui, NULL, look, &l);
    return l;
}

/* Draws what is damaged onto screen, all of it onto full: 1 when they are the same */
static int same_as_full(olui *ui, uint32_t *screen, uint32_t *full, int may_be_all)
{
    olui_rect r;
    if (olui_damaged(ui, &r)) {
        if (!may_be_all && r.w == FW && r.h == FH) {
            printf("widgets: the whole window damaged\n");
            return 0;
        }
        if (getenv("OLUI_DEBUG")) printf("widgets: damage %ld %ld %ld %ld\n", (long)r.x, (long)r.y, (long)r.w, (long)r.h);
        frame_draw(ui, screen, &r);
    }
    frame_draw(ui, full, NULL);
    if (memcmp(screen, full, FW * FH * sizeof(uint32_t)) && getenv("OLUI_DEBUG")) {
        int i, x0 = FW, y0 = FH, x1 = -1, y1 = -1;
        for (i = 0; i < FW * FH; ++i)
            if (screen[i] != full[i]) {
                if (i % FW < x0) x0 = i % FW;
                if (i % FW > x1) x1 = i % FW;
                if (i / FW < y0) y0 = i / FW;
                if (i / FW > y1) y1 = i / FW;
            }
        printf("widgets: differs from %d,%d to %d,%d\n", x0, y0, x1, y1);
    }
    return !memcmp(screen, full, FW * FH * sizeof(uint32_t));
}

static void press_at(olui *ui, int x, int y)
{
    olui_mouse(ui, x, y, OLUI_MOUSE_MOVE);
    olui_mouse(ui, x, y, OLUI_MOUSE_DOWN);
    olui_mouse(ui, x, y, OLUI_MOUSE_UP);
}

#define SAME(all) CHECK(same_as_full(ui, screen, full, all))

static int widgets(void)
{
    olui *ui = olui_new();
    uint32_t *screen = (uint32_t *)malloc(FW * FH * sizeof(uint32_t));
    uint32_t *full = (uint32_t *)malloc(FW * FH * sizeof(uint32_t));
    olui_el *fruit, *lb, *pw, *num, *ta, *ro, *nw, *whole, *cb, *cb2, *sb, *b1, *b2, *b3;
    olui_rect r, d;
    char err[128];
    size_t a, b;
    int i, was;
    looker l;

    if (!screen || !full) {
        printf("widgets: no memory for two frames, left out\n");
        free(screen);
        free(full);
        olui_free(ui);
        return 0;
    }
    CHECK(ui);
    olui_set_measure(ui, measure, NULL);
    if (!olui_load(ui, widget_markup, NULL, err, sizeof(err))) printf("widgets: %s\n", err);
    CHECK(olui_load(ui, widget_markup, "option{padding:1px 4px}", err, sizeof(err)));
    CHECK(olui_on(ui, NULL, 0, on_widget, NULL));
    CHECK(olui_layout(ui, FW, FH));
    frame_draw(ui, screen, NULL);
    w_screen = screen;
    fruit = olui_find(ui, "fruit"); lb = olui_find(ui, "lb"); pw = olui_find(ui, "pw");
    num = olui_find(ui, "num"); ta = olui_find(ui, "ta"); ro = olui_find(ui, "ro"); nw = olui_find(ui, "nw");
    whole = olui_find(ui, "whole"); cb = olui_find(ui, "cb"); cb2 = olui_find(ui, "cb2"); sb = olui_find(ui, "sb");
    b1 = olui_find(ui, "b1"); b2 = olui_find(ui, "b2"); b3 = olui_find(ui, "b3");
    CHECK(fruit && lb && pw && num && ta && ro && nw && whole && cb && cb2 && sb && b1 && b2 && b3);

    /* ---- select: parsed (an option left open, optgroups), sized to its widest option ---- */
    CHECK(!strcmp(olui_tag(fruit), "select") && olui_select_count(fruit) == 5);
    CHECK(olui_select_index(fruit) == 1 && !strcmp(olui_get_text(fruit), "Orange"));
    CHECK(!strcmp(olui_select_option_value(fruit, 0), "a") && !strcmp(olui_select_option_value(fruit, 2), "Lemon"));
    CHECK(!strcmp(olui_select_option_text(fruit, 4), "Pear") && !olui_select_option_value(fruit, 5));
    CHECK(olui_box(ui, fruit, &r));
    /* "Citrus", the group's label in bold: 6 characters of 8 px; padding 6 + 6, border 1 + 1, the mark a line and 4 */
    CHECK(r.w == 48 + 12 + 2 + 16 + 4 && r.h == 16 + 6);
    CHECK(olui_select_count(lb) == 5 && olui_select_index(lb) == 2);
    CHECK(olui_box(ui, lb, &r) && r.h == 3 * (16 + 2) + 2 + 2);
    l = seek(ui, "Orange", NULL);
    CHECK(l.found && l.el_of_want == fruit);
    l = seek(ui, "Lemon", NULL);
    CHECK(!l.found);                                    /* closed: only the chosen one shows */

    /* the pop-up: opened by a click, over everything, the keyboard in it */
    w_events = 0;
    CHECK(olui_box(ui, fruit, &r));
    olui_mouse(ui, r.x + 5, r.y + 5, OLUI_MOUSE_DOWN);
    CHECK(olui_select_is_open(ui, fruit) && olui_focused(ui) == fruit);
    CHECK(olui_damaged(ui, &d) && d.w < FW);
    SAME(0);
    olui_mouse(ui, r.x + 5, r.y + 5, OLUI_MOUSE_UP);      /* the release of the press that opened it */
    CHECK(olui_select_is_open(ui, fruit) && w_events == 0);
    l = seek(ui, "Lemon", NULL);
    CHECK(l.found && l.last_el && l.last_el != fruit);  /* drawn over the rest, last */
    l = seek(ui, "Citrus", NULL);
    CHECK(l.found);                                     /* the group's label row */
    CHECK(olui_key(ui, OLUI_KEY_DOWN, 0) && olui_select_index(fruit) == 1 && w_events == 0);
    SAME(0);
    CHECK(olui_key(ui, OLUI_KEY_DOWN, 0));             /* past Mango, disabled, to Pear */
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0) && !olui_select_is_open(ui, fruit));
    CHECK(w_events == 1 && w_last_type == OLUI_CHANGE && w_last_index == 4 && !strcmp(w_last_value, "p"));
    CHECK(olui_select_index(fruit) == 4 && !strcmp(olui_get_text(fruit), "Pear"));
    SAME(0);
    /* Return opens it, Escape closes it with nothing chosen */
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0) && olui_select_is_open(ui, fruit));
    CHECK(olui_key(ui, OLUI_KEY_HOME, 0) && olui_key(ui, OLUI_KEY_ESCAPE, 0));
    CHECK(!olui_select_is_open(ui, fruit) && olui_select_index(fruit) == 4 && w_events == 1);
    SAME(0);
    /* the first letter, closed: chosen at once */
    CHECK(olui_key(ui, 'l', 0) && olui_select_index(fruit) == 2 && w_last_index == 2 && w_events == 2);
    CHECK(olui_key(ui, OLUI_KEY_UP, 0) && olui_select_index(fruit) == 1);
    SAME(0);
    /* a click on a row chooses it; a click elsewhere closes it */
    CHECK(olui_box(ui, fruit, &r));
    press_at(ui, r.x + 5, r.y + 5);
    CHECK(olui_select_is_open(ui, fruit));
    SAME(0);
    {
        /* Apple is the first row, under the select */
        olui_mouse(ui, r.x + 10, r.y + r.h + 2 + 9, OLUI_MOUSE_MOVE);
        olui_mouse(ui, r.x + 10, r.y + r.h + 2 + 9, OLUI_MOUSE_DOWN);
        olui_mouse(ui, r.x + 10, r.y + r.h + 2 + 9, OLUI_MOUSE_UP);
        CHECK(!olui_select_is_open(ui, fruit) && olui_select_index(fruit) == 0 && w_last_index == 0);
        CHECK(!strcmp(w_last_value, "a"));
    }
    SAME(0);
    press_at(ui, r.x + 5, r.y + 5);
    CHECK(olui_select_is_open(ui, fruit));
    was = w_events;
    olui_mouse(ui, FW - 3, FH - 3, OLUI_MOUSE_DOWN);
    olui_mouse(ui, FW - 3, FH - 3, OLUI_MOUSE_UP);
    CHECK(!olui_select_is_open(ui, fruit) && w_events == was && olui_select_index(fruit) == 0);
    SAME(0);
    olui_select_set(ui, fruit, 3);
    CHECK(olui_select_index(fruit) == 3 && w_events == was);
    SAME(0);

    /* the list box: in place, the keys move its choice, it scrolls */
    olui_focus(ui, lb);
    CHECK(olui_key(ui, OLUI_KEY_DOWN, 0) && olui_select_index(lb) == 3 && w_last_index == 3);
    CHECK(!strcmp(w_last_value, "Four"));
    CHECK(olui_key(ui, OLUI_KEY_END, 0) && olui_select_index(lb) == 4 && olui_get_scroll(lb) > 0);
    SAME(0);
    CHECK(olui_key(ui, OLUI_KEY_HOME, 0) && olui_select_index(lb) == 0 && olui_get_scroll(lb) == 0);
    CHECK(olui_box(ui, lb, &r));
    olui_mouse(ui, r.x + 8, r.y + r.h / 2, OLUI_WHEEL_DOWN);
    CHECK(olui_get_scroll(lb) == 5 * 18 - 3 * 18);
    SAME(0);
    press_at(ui, r.x + 8, r.y + 2 + 9);                 /* the top row shown: Three */
    CHECK(olui_select_index(lb) == 2);
    SAME(0);
    {
        olui_el *o = olui_select_add(ui, lb, "Six", "6");
        CHECK(o && olui_select_count(lb) == 6 && !strcmp(olui_select_option_value(lb, 5), "6"));
        olui_select_clear(ui, lb);
        CHECK(olui_select_count(lb) == 0 && olui_select_index(lb) == -1);
        SAME(1);
    }

    /* ---- password: masked, maxlength, its value the program's ---- */
    olui_focus(ui, pw);
    w_events = 0;
    for (i = 0; "secret!"[i]; ++i) olui_key(ui, (unsigned)"secret!"[i], 0);
    CHECK(!strcmp(olui_get_text(pw), "secret") && w_events == 6 && !strcmp(w_last_value, "(none)"));
    SAME(0);
    l = seek(ui, "secret", NULL);
    CHECK(!l.found && l.bullets == 6);
    olui_focus(ui, pw);
    CHECK(!olui_edit(ui, OLUI_COPY));                   /* never copied */
    /* all of it selected (the Amiga key with a): what is typed takes its place */
    CHECK(olui_key(ui, 'a', OLUI_QUAL_COMMAND) && olui_get_selection(pw, &a, &b) && a == 0 && b == 6);
    SAME(0);
    CHECK(olui_key(ui, 'k', 0) && !strcmp(olui_get_text(pw), "k") && !olui_get_selection(pw, &a, &b));
    SAME(0);

    /* ---- number: fixed point, the arrows, typed, held to min and max ---- */
    CHECK(olui_number_decimals(num) == 1 && olui_number_value(num) == 10 && !strcmp(olui_get_text(num), "1.0"));
    CHECK(olui_number_value(whole) == 5 && olui_number_decimals(whole) == 0);
    olui_focus(ui, num);
    w_events = 0;
    CHECK(olui_key(ui, OLUI_KEY_UP, 0) && olui_number_value(num) == 15 && !strcmp(olui_get_text(num), "1.5"));
    CHECK(w_last_type == OLUI_CHANGE && w_last_index == 15 && !strcmp(w_last_value, "1.5"));
    SAME(0);
    for (i = 0; i < 4; ++i) olui_key(ui, OLUI_KEY_UP, 0);
    CHECK(olui_number_value(num) == 25 && !strcmp(olui_get_text(num), "2.5"));
    CHECK(olui_key(ui, OLUI_KEY_PAGEDOWN, 0) && olui_number_value(num) == -10 && !strcmp(olui_get_text(num), "-1.0"));
    olui_set_text(ui, num, "");
    CHECK(olui_key(ui, 'x', 0) && !strcmp(olui_get_text(num), ""));     /* not a number's */
    CHECK(olui_key(ui, '7', 0) && olui_number_value(num) == 70);
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0) && olui_number_value(num) == 25 && !strcmp(olui_get_text(num), "2.5"));
    SAME(1);
    CHECK(olui_key(ui, OLUI_KEY_BACKSPACE, 0) && olui_key(ui, OLUI_KEY_BACKSPACE, 0) && olui_key(ui, '.', 0) &&
          olui_key(ui, '2', 0) && olui_key(ui, '5', 0) && !strcmp(olui_get_text(num), "2.25"));
    CHECK(olui_number_value(num) == 22);                /* places past its own: left out */
    olui_focus(ui, pw);                                 /* the keyboard going elsewhere: made its value's */
    CHECK(!strcmp(olui_get_text(num), "2.2") && w_last_index == 22);
    olui_set_number(ui, num, 0);
    CHECK(olui_number_value(num) == 0 && !strcmp(olui_get_text(num), "0.0"));
    SAME(0);
    /* the arrow buttons: a step on the press, more while held */
    CHECK(olui_box(ui, num, &r));
    olui_mouse(ui, r.x + r.w - 4, r.y + 3, OLUI_MOUSE_DOWN);
    CHECK(olui_number_value(num) == 5 && olui_focused(ui) == num);
    SAME(0);
    CHECK(!olui_tick(ui, 300) && olui_number_value(num) == 5);
    CHECK(olui_tick(ui, 150) && olui_number_value(num) == 10);      /* at 400 ms, then each 80 */
    CHECK(olui_tick(ui, 80) && olui_number_value(num) == 15);
    SAME(0);
    olui_mouse(ui, r.x + r.w - 4, r.y + 3, OLUI_MOUSE_UP);
    CHECK(!olui_tick(ui, 1000) && olui_number_value(num) == 15);
    olui_mouse(ui, r.x + r.w - 4, r.y + r.h - 3, OLUI_MOUSE_DOWN);
    olui_mouse(ui, r.x + r.w - 4, r.y + r.h - 3, OLUI_MOUSE_UP);
    CHECK(olui_number_value(num) == 10);
    SAME(0);

    /* ---- textarea: sized by rows and cols, wrapped at words, edited, selected, scrolled ---- */
    CHECK(olui_box(ui, ta, &r) && r.w == 10 * 7 + 8 + 2 && r.h == 3 * 16 + 6 + 2);
    l = seek(ui, "Notes", NULL);
    CHECK(l.found && l.el_of_want == ta);
    press_at(ui, r.x + 10, r.y + 10);
    CHECK(olui_focused(ui) == ta);
    w_events = 0;
    for (i = 0; "hello world foo bar"[i]; ++i) olui_key(ui, (unsigned)"hello world foo bar"[i], 0);
    CHECK(!strcmp(olui_get_text(ta), "hello world foo bar") && w_events == 19 && !strcmp(w_last_id, "ta"));
    SAME(0);
    l = seek(ui, "world foo ", NULL);                   /* its second line, as wrapped */
    CHECK(l.found && l.el_of_want == ta);
    l = seek(ui, "bar", NULL);
    CHECK(l.found);
    /* Up keeps to the caret's x: from the end of "bar" to "wor|ld" */
    CHECK(olui_key(ui, OLUI_KEY_UP, 0) && olui_get_selection(ta, &a, &b) == 0 && a == 9);
    CHECK(olui_key(ui, OLUI_KEY_HOME, 0) && olui_get_selection(ta, &a, &b) == 0 && a == 6);
    CHECK(olui_key(ui, OLUI_KEY_END, 0) && olui_get_selection(ta, &a, &b) == 0 && a == 15);
    CHECK(olui_key(ui, OLUI_KEY_DOWN, 0) && olui_get_selection(ta, &a, &b) == 0 && a == 19);
    /* Shift with the arrows selects; typing takes the selection's place */
    CHECK(olui_key(ui, OLUI_KEY_LEFT, OLUI_QUAL_SHIFT) && olui_key(ui, OLUI_KEY_LEFT, OLUI_QUAL_SHIFT) &&
          olui_key(ui, OLUI_KEY_LEFT, OLUI_QUAL_SHIFT));
    CHECK(olui_get_selection(ta, &a, &b) && a == 16 && b == 19);
    SAME(0);
    CHECK(olui_edit(ui, OLUI_COPY) && w_last_type == OLUI_COPY && !strcmp(w_last_value, "bar"));
    CHECK(olui_key(ui, 'c', OLUI_QUAL_COMMAND) && w_last_type == OLUI_COPY);
    CHECK(olui_key(ui, 'B', 0) && !strcmp(olui_get_text(ta), "hello world foo B"));
    SAME(0);
    /* Return a new line; Backspace; a selection dragged with the pointer */
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0) && !strcmp(olui_get_text(ta), "hello world foo B\n"));
    CHECK(olui_key(ui, OLUI_KEY_BACKSPACE, 0) && !strcmp(olui_get_text(ta), "hello world foo B"));
    CHECK(olui_box(ui, ta, &r));
    olui_mouse(ui, r.x + 5, r.y + 4 + 8, OLUI_MOUSE_DOWN);
    olui_mouse(ui, r.x + 5 + 5 * 7, r.y + 4 + 8, OLUI_MOUSE_MOVE);
    olui_mouse(ui, r.x + 5 + 5 * 7, r.y + 4 + 8, OLUI_MOUSE_UP);
    CHECK(olui_get_selection(ta, &a, &b) && a == 0 && b == 5);
    SAME(0);
    CHECK(olui_key(ui, 'x', OLUI_QUAL_COMMAND) && w_last_type == OLUI_CHANGE && !strcmp(olui_get_text(ta), " world foo B"));
    w_paste_ok = 0;
    CHECK(olui_edit(ui, OLUI_PASTE) && w_paste_ok && !strcmp(olui_get_text(ta), "PASTED world foo B"));
    SAME(0);
    /* a double-click selects a word: "world", on the second line */
    olui_mouse(ui, r.x + 5 + 2 * 7, r.y + 4 + 16 + 8, OLUI_MOUSE_DOUBLE);
    olui_mouse(ui, r.x + 5 + 2 * 7, r.y + 4 + 16 + 8, OLUI_MOUSE_UP);
    CHECK(olui_get_selection(ta, &a, &b) && a == 7 && b == 12);
    /* taller than its box: it scrolls, the caret kept in view, a bar shown */
    olui_set_selection(ui, ta, 100, 100);
    for (i = 0; i < 6; ++i) {
        olui_key(ui, OLUI_KEY_RETURN, 0);
        olui_key(ui, 'a' + i, 0);
    }
    /* nine lines of 16 px in a view of three: scrolled to the last */
    CHECK(olui_get_scroll(ta) == 9 * 16 - 3 * 16);
    SAME(0);
    l = seek(ui, "f", NULL);
    CHECK(l.found);                                     /* the last line shows */
    CHECK(olui_key(ui, OLUI_KEY_PAGEUP, 0) && olui_get_scroll(ta) < 7 * 16);
    CHECK(olui_key(ui, 'a', OLUI_QUAL_COMMAND) && olui_get_selection(ta, &a, &b) && a == 0 &&
          b == strlen(olui_get_text(ta)));
    olui_mouse(ui, r.x + 10, r.y + 10, OLUI_WHEEL_DOWN);
    SAME(0);
    olui_set_scroll(ui, ta, 0);
    CHECK(olui_get_scroll(ta) == 0);
    SAME(0);
    /* readonly: the caret moves and text is copied, nothing changes */
    olui_focus(ui, ro);
    was = w_events;
    CHECK(olui_key(ui, 'z', 0) && !strcmp(olui_get_text(ro), "Fixed text") && w_events == was);
    CHECK(olui_key(ui, OLUI_KEY_END, OLUI_QUAL_SHIFT) && olui_edit(ui, OLUI_COPY) && !strcmp(w_last_value, "Fixed"));
    CHECK(!olui_edit(ui, OLUI_CUT) && !olui_edit(ui, OLUI_PASTE));
    /* wrap=off: one line until a newline, maxlength held */
    olui_focus(ui, nw);
    for (i = 0; i < 20; ++i) olui_key(ui, 'm', 0);
    CHECK(strlen(olui_get_text(nw)) == 12);
    l = seek(ui, "mmmmmmmmmmmm", NULL);
    CHECK(l.found);
    SAME(0);

    /* ---- fieldset: its legend breaks its top edge ---- */
    {
        olui_rect fs, lg;
        olui_el *fse = olui_find(ui, "fs");
        CHECK(olui_box(ui, fse, &fs) && olui_box(ui, olui_find(ui, "lg"), &lg));
        CHECK(lg.y == fs.y && lg.h == 16);
        l = seek(ui, "Options", fse);
        CHECK(l.found && l.boxes == 5);                 /* no fill: three sides, and the top either side of it */
        l = seek(ui, "Options", olui_find(ui, "plain"));
        CHECK(l.boxes == 1);                            /* no legend: its box whole */
        CHECK(olui_box(ui, olui_find(ui, "plain"), &fs) && olui_box(ui, olui_find(ui, "nolg"), &lg));
        CHECK(lg.y == fs.y + 6);                        /* with no legend, room above as below */
    }

    /* ---- label: for, and around its control ---- */
    CHECK(!olui_get_checked(cb2));
    CHECK(olui_box(ui, olui_find(ui, "l2"), &r));
    press_at(ui, r.x + 3, r.y + r.h / 2);
    CHECK(olui_get_checked(cb2) && olui_focused(ui) == cb2 && w_last_type == OLUI_CHANGE && !strcmp(w_last_id, "cb2"));
    SAME(0);
    CHECK(olui_box(ui, olui_find(ui, "lw"), &r));
    press_at(ui, r.x + 3, r.y + r.h / 2);               /* "Wrap", before the checkbox */
    CHECK(olui_get_checked(cb) && olui_focused(ui) == cb);
    SAME(0);
    CHECK(olui_box(ui, olui_find(ui, "lfruit"), &r));
    press_at(ui, r.x + 3, r.y + r.h / 2);
    CHECK(olui_focused(ui) == fruit && !olui_select_is_open(ui, fruit));

    /* ---- Tab's order: tabindex 1 first, then the document's, -1 never ---- */
    olui_focus(ui, NULL);
    CHECK(olui_key(ui, OLUI_KEY_TAB, 0) && olui_focused(ui) == b3);
    CHECK(olui_key(ui, OLUI_KEY_TAB, 0) && olui_focused(ui) == fruit);
    CHECK(olui_key(ui, OLUI_KEY_TAB, 0) && olui_focused(ui) == lb);
    CHECK(olui_key(ui, OLUI_KEY_TAB, OLUI_QUAL_SHIFT) && olui_focused(ui) == fruit);
    CHECK(olui_key(ui, OLUI_KEY_TAB, OLUI_QUAL_SHIFT) && olui_focused(ui) == b3);
    CHECK(olui_key(ui, OLUI_KEY_TAB, OLUI_QUAL_SHIFT) && olui_focused(ui) == b2);
    for (i = 0; i < 20 && olui_focused(ui) != cb; ++i) olui_key(ui, OLUI_KEY_TAB, 0);
    CHECK(olui_focused(ui) == cb);
    CHECK(olui_key(ui, OLUI_KEY_TAB, 0) && olui_focused(ui) == b1);     /* past cb2, tabindex -1 */
    SAME(1);

    /* ---- a box with overflow: scrolled by the wheel and the program, cut to itself ---- */
    CHECK(olui_box(ui, sb, &r) && r.h == 40);
    olui_set_scroll(ui, sb, 0);
    SAME(0);
    {
        olui_rect r3;
        int32_t most;
        CHECK(olui_box(ui, b3, &r3) && r3.y + r3.h > r.y + r.h);  /* Three is below its bottom */
        was = w_events;
        olui_mouse(ui, r3.x + 3, r3.y + r3.h - 2, OLUI_MOUSE_DOWN);
        olui_mouse(ui, r3.x + 3, r3.y + r3.h - 2, OLUI_MOUSE_UP);
        CHECK(w_events == was);                         /* not where it is cut off */
        olui_mouse(ui, r.x + 3, r.y + 3, OLUI_WHEEL_DOWN);
        most = 3 * 22 - 40;
        CHECK(olui_get_scroll(sb) == most);
        SAME(0);
        CHECK(olui_box(ui, b3, &r3) && r3.y + r3.h == r.y + r.h);
        was = w_events;
        click(ui, r3);
        CHECK(w_events == was + 1 && w_last_type == OLUI_CLICK && !strcmp(w_last_id, "b3"));
        olui_set_scroll(ui, sb, -5);
        CHECK(olui_get_scroll(sb) == 0);
        SAME(0);
        olui_focus(ui, b2);
        olui_key(ui, OLUI_KEY_TAB, OLUI_QUAL_SHIFT);    /* to b1: in view */
        olui_set_scroll(ui, sb, most);
        olui_focus(ui, cb);
        CHECK(olui_key(ui, OLUI_KEY_TAB, 0) && olui_focused(ui) == b1 && olui_get_scroll(sb) == 0);
        SAME(0);
    }

    /* ---- tooltips: after the pointer rests, through olui_tick ---- */
    CHECK(olui_box(ui, fruit, &r));
    olui_mouse(ui, r.x + 4, r.y + 4, OLUI_MOUSE_MOVE);
    SAME(0);
    CHECK(!olui_tick(ui, 300));
    l = seek(ui, "Pick one", NULL);
    CHECK(!l.found);
    CHECK(olui_tick(ui, 500));
    CHECK(olui_damaged(ui, &d) && d.w < FW && d.y >= r.y + 4);
    SAME(0);
    l = seek(ui, "Pick one", NULL);
    CHECK(l.found && l.el_of_want && !strcmp(olui_tag(l.el_of_want), "tooltip"));
    olui_mouse(ui, FW - 3, FH - 3, OLUI_MOUSE_MOVE);   /* off it: the tip goes */
    l = seek(ui, "Pick one", NULL);
    CHECK(!l.found);
    SAME(0);
    olui_mouse(ui, r.x + 4, r.y + 4, OLUI_MOUSE_MOVE);
    olui_mouse(ui, r.x + 4, r.y + 4, OLUI_MOUSE_DOWN);   /* a press: no tip until it goes to another */
    olui_mouse(ui, r.x + 4, r.y + 4, OLUI_MOUSE_UP);
    olui_key(ui, OLUI_KEY_ESCAPE, 0);
    CHECK(!olui_tick(ui, 2000));
    SAME(0);

    /* ---- a separator option: a line in the pop-up, skipped by keys, never chosen ---- */
    {
        olui *u3 = olui_new();
        olui_el *ms, *sep;
        sep_look sl;
        CHECK(u3);
        w_screen = NULL;
        olui_set_measure(u3, measure, NULL);
        CHECK(olui_load(u3, "<select id=ms><option>Open</option><option id=sep separator></option>"
                            "<option>Quit</option></select>", NULL, err, sizeof(err)) && olui_layout(u3, 300, 200));
        ms = olui_find(u3, "ms");
        sep = olui_find(u3, "sep");
        CHECK(ms && sep && olui_select_count(ms) == 3 && olui_select_index(ms) == 0);
        olui_select_open(u3, ms, 1);
        CHECK(olui_popup(u3) == ms);
        memset(&sl, 0, sizeof(sl));
        sl.sep = sep;
        olui_draw(u3, NULL, look_sep, &sl);
        CHECK(sl.lines == 1 && sl.texts == 0);
        CHECK(olui_key(u3, OLUI_KEY_DOWN, 0) && olui_key(u3, OLUI_KEY_RETURN, 0));
        CHECK(olui_select_index(ms) == 2);              /* past the separator, to Quit */
        olui_free(u3);
    }

    /* ---- a long select: its pop-up shows 12 rows and scrolls; a group disabled; the API opens it ---- */
    {
        olui *u2 = olui_new();
        olui_el *big, *hid;
        char m[2048] = "<select id=big><optgroup label=Off disabled><option>Zero</optgroup>";
        olui_rect pr;
        looker l2;
        for (i = 1; i < 30; ++i) sprintf(m + strlen(m), "<option value=v%d>%c%d</option>", i, 'A' + i % 26, i);
        strcat(m, "</select><div id=hid style='height:20px;overflow:hidden'><p>one</p><p>two</p><p>three</p></div>");
        CHECK(u2);
        w_screen = NULL;                                /* another window: not drawn on this one's frame */
        olui_set_measure(u2, measure, NULL);
        CHECK(olui_load(u2, m, NULL, err, sizeof(err)) && olui_layout(u2, 300, 400));
        CHECK(olui_on(u2, NULL, 0, on_widget, NULL));
        big = olui_find(u2, "big");
        hid = olui_find(u2, "hid");
        CHECK(olui_select_count(big) == 30 && olui_select_index(big) == 1);   /* Zero's group is disabled */
        CHECK(!olui_popup(u2));
        olui_select_open(u2, big, 1);
        CHECK(olui_select_is_open(u2, big) && olui_popup(u2) == big);
        CHECK(olui_key(u2, OLUI_KEY_END, 0) && olui_key(u2, OLUI_KEY_RETURN, 0));
        CHECK(olui_select_index(big) == 29 && !strcmp(w_last_value, "v29"));
        olui_select_open(u2, big, 1);
        CHECK(olui_key(u2, OLUI_KEY_HOME, 0) && olui_key(u2, 'c', 0));         /* C2, then C28 */
        CHECK(olui_key(u2, 'c', 0) && olui_key(u2, OLUI_KEY_RETURN, 0) && olui_select_index(big) == 28);
        olui_select_open(u2, big, 1);
        CHECK(olui_key(u2, OLUI_KEY_HOME, 0));
        l2 = seek(u2, "A26", NULL);
        CHECK(!l2.found);                               /* below the 12 rows shown */
        CHECK(olui_box(u2, big, &pr));
        olui_mouse(u2, pr.x + 4, pr.y + pr.h + 30, OLUI_WHEEL_DOWN);
        olui_mouse(u2, pr.x + 4, pr.y + pr.h + 30, OLUI_WHEEL_DOWN);
        olui_mouse(u2, pr.x + 4, pr.y + pr.h + 30, OLUI_WHEEL_DOWN);
        olui_mouse(u2, pr.x + 4, pr.y + pr.h + 30, OLUI_WHEEL_DOWN);
        olui_mouse(u2, pr.x + 4, pr.y + pr.h + 30, OLUI_WHEEL_DOWN);
        l2 = seek(u2, "A26", NULL);
        CHECK(l2.found);                                /* scrolled to it */
        olui_select_open(u2, big, 0);
        CHECK(!olui_select_is_open(u2, big) && olui_select_index(big) == 28 && !olui_popup(u2));
        olui_select_open(u2, big, 1);
        olui_set_disabled(u2, big, 1);                  /* disabled while open: it closes */
        CHECK(olui_layout(u2, 300, 400) && !olui_select_is_open(u2, big));
        olui_select_open(u2, big, 1);
        CHECK(!olui_select_is_open(u2, big));
        olui_set_disabled(u2, big, 0);
        /* overflow hidden: cut to the box, no bar, scrolled by the program only */
        olui_mouse(u2, 10, pr.y + pr.h + 16, OLUI_WHEEL_DOWN);
        CHECK(olui_get_scroll(hid) == 0);
        olui_set_scroll(u2, hid, 10);
        CHECK(olui_get_scroll(hid) == 10);
        olui_free(u2);
        w_screen = screen;
    }
    /* a textarea left open says so */
    {
        olui *bad = olui_new();
        CHECK(bad && !olui_load(bad, "<row>\n<textarea>text", NULL, err, sizeof(err)) && strstr(err, "line 2"));
        olui_free(bad);
    }

    if (w_ops_path) {
        /* the window with the select's pop-up open and a tooltip, to look at */
        sink s;
        memset(&s, 0, sizeof(s));
        olui_focus(ui, ta);
        olui_set_selection(ui, ta, 0, 6);
        olui_mouse(ui, 3, FH - 3, OLUI_MOUSE_MOVE);
        CHECK(olui_box(ui, olui_find(ui, "b1"), &r));
        olui_mouse(ui, r.x + 4, r.y + 4, OLUI_MOUSE_MOVE);
        olui_tick(ui, 1000);
        olui_select_open(ui, fruit, 1);
        s.out = fopen(w_ops_path, "w");
        CHECK(s.out);
        fprintf(s.out, "size %d %d\n", FW, FH);
        olui_draw(ui, NULL, draw, &s);
        fclose(s.out);
    }
    printf("widgets: select, list box, password, number, textarea, fieldset, label, Tab, overflow, tooltip\n");
    w_screen = NULL;
    free(screen);
    free(full);
    olui_free(ui);
    return 0;
}

int main(int argc, char **argv)
{
    olui *ui = olui_new();
    char err[128];
    sink s;
    olui_el *search, *folders, *tabs, *html, *bydate, *byname, *bar, *inbox, *work, *ok, *cancel;
    olui_rect r;
    const char *theme_path = getenv("OLUI_THEME_FILE");

    if (test_handlers_take_away()) return 20;
    CHECK(ui);
    olui_set_measure(ui, measure, NULL);
    if (theme_path) {
        char *text = read_file(theme_path), *css;
        CHECK(text);
        css = olui_theme_css(text, 0, err, sizeof(err));
        if (!css) printf("theme: %s\n", err);
        CHECK(css);
        CHECK(strstr(css, "button{background:linear-gradient("));
        CHECK(olui_add_css(ui, css));
        free(css);
        free(text);
    }
    CHECK(olui_load(ui, markup, "item{padding:2px 4px}", err, sizeof(err)));
    CHECK(olui_on(ui, NULL, 0, on_event, NULL));
    CHECK(olui_on(ui, "ok", OLUI_CLICK, on_ok, &ok_clicks));
    CHECK(olui_layout(ui, 520, 340));

    search = olui_find(ui, "search");
    folders = olui_find(ui, "folders");
    tabs = olui_find(ui, "tabs");
    html = olui_find(ui, "html");
    bydate = olui_find(ui, "bydate");
    byname = olui_find(ui, "byname");
    bar = olui_find(ui, "bar");
    inbox = olui_find(ui, "inbox");
    work = olui_find(ui, "work");
    ok = olui_find(ui, "ok");
    cancel = olui_find(ui, "cancel");
    CHECK(search && folders && tabs && html && bydate && byname && bar && inbox && work && ok && cancel);
    CHECK(!strcmp(olui_tag(folders), "tree"));
    CHECK(olui_get_checked(html) && olui_get_checked(bydate) && !olui_get_checked(byname));

    memset(&s, 0, sizeof(s));
    if (argc > 2) {
        /* the window as it opens */
        s.out = fopen(argv[2], "w");
        CHECK(s.out);
        fprintf(s.out, "size 520 340\n");
    }
    olui_draw(ui, NULL, draw, &s);
    if (s.out) fclose(s.out);
    s.out = NULL;
    CHECK(s.boxes > 10 && s.texts > 10 && s.marks >= 3);
    CHECK(s.saw_text_ok);

    /* the window is laid out to its size: OK's row is at the bottom */
    r = box_of(ui, "ok");
    CHECK(r.w > 0 && r.y + r.h <= 340 - 6 && r.y + r.h >= 340 - 8);
    CHECK(r.x + r.w < 520 - 6);                     /* justify-content:end, Cancel after it */
    /* the tree is 180 px wide and as tall as its row */
    r = box_of(ui, "folders");
    CHECK(r.w == 180 && r.h > 100);

    /* a click on OK: the handler for its id, and the one for all */
    click(ui, box_of(ui, "ok"));
    CHECK(ok_clicks == 1 && clicks == 1);
    /* a disabled button does nothing */
    click(ui, box_of(ui, "cancel"));
    CHECK(clicks == 1);

    /* the checkbox and the radio buttons */
    r = box_of(ui, "html");
    if (getenv("OLUI_DEBUG")) printf("html box %ld %ld %ld %ld checked %d changes %d\n", (long)r.x, (long)r.y, (long)r.w, (long)r.h, olui_get_checked(html), changes);
    click(ui, box_of(ui, "html"));
    if (getenv("OLUI_DEBUG")) printf("after: checked %d changes %d\n", olui_get_checked(html), changes);
    CHECK(!olui_get_checked(html) && changes == 1);
    click(ui, box_of(ui, "byname"));
    CHECK(olui_get_checked(byname) && !olui_get_checked(bydate) && !strcmp(last_value, "name"));

    /* typing in the field */
    click(ui, box_of(ui, "search"));
    CHECK(olui_key(ui, 'h', 0) && olui_key(ui, 'i', 0) && olui_key(ui, 0xe9, 0));
    CHECK(!strcmp(olui_get_text(search), "hi\xc3\xa9"));
    CHECK(olui_key(ui, OLUI_KEY_BACKSPACE, 0));
    CHECK(olui_key(ui, OLUI_KEY_HOME, 0) && olui_key(ui, '>', 0));
    CHECK(!strcmp(olui_get_text(search), ">hi"));
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0) && submits == 1 && !strcmp(last_value, ">hi"));

    /* the tree: a click selects, the arrow keys move and open */
    click(ui, box_of(ui, "work"));
    CHECK(olui_list_selected(folders) == work && selects == 1);
    CHECK(olui_key(ui, OLUI_KEY_DOWN, 0));
    CHECK(olui_list_selected(folders) == olui_find(ui, "family"));
    CHECK(olui_key(ui, OLUI_KEY_DOWN, 0) && olui_key(ui, OLUI_KEY_DOWN, 0));
    CHECK(olui_list_selected(folders) == olui_find(ui, "archive"));
    CHECK(!olui_item_is_open(olui_find(ui, "archive")));
    CHECK(olui_key(ui, OLUI_KEY_RIGHT, 0) && olui_item_is_open(olui_find(ui, "archive")) && expands == 1);
    CHECK(olui_key(ui, OLUI_KEY_DOWN, 0) && olui_list_selected(folders) == olui_find(ui, "y2025"));
    CHECK(olui_key(ui, OLUI_KEY_LEFT, 0) && olui_list_selected(folders) == olui_find(ui, "archive"));
    CHECK(olui_key(ui, OLUI_KEY_LEFT, 0) && collapses == 1);
    CHECK(olui_key(ui, OLUI_KEY_RETURN, 0) && activates == 1 && !strcmp(last_value, "archive"));
    /* closing the inbox with the program's call; its selected child goes to it */
    olui_list_select(ui, folders, work);
    olui_item_open(ui, inbox, 0);
    CHECK(olui_list_selected(folders) == inbox);
    /* items added by the program */
    {
        olui_el *more = olui_list_add(ui, folders, NULL, "Drafts", "DRAFTS");
        CHECK(more && olui_get_value(more) && !strcmp(olui_get_value(more), "DRAFTS"));
        olui_list_select(ui, folders, more);
        CHECK(olui_list_selected(folders) == more);
    }
    /* many items: the list scrolls to keep the selected one in view */
    {
        int i;
        olui_el *last = NULL;
        char name[24];
        for (i = 0; i < 40; ++i) {
            sprintf(name, "Folder %d", i);
            last = olui_list_add(ui, folders, NULL, name, NULL);
            CHECK(last);
        }
        olui_list_select(ui, folders, last);
        r = box_of(ui, "folders");
        {
            finder f;
            memset(&f, 0, sizeof(f));
            f.want = last;
            olui_draw(ui, NULL, find_box, &f);
            CHECK(f.found && f.r.y >= r.y && f.r.y + f.r.h <= r.y + r.h);
        }
    }

    /* the tabs: a click on the second's button shows its page */
    CHECK(olui_tabs_current(tabs) == 0);
    {
        finder f;
        memset(&f, 0, sizeof(f));
        f.want = NULL;
        /* the strip's second button: Tab from the tree's focus reaches it */
        CHECK(olui_key(ui, OLUI_KEY_TAB, 0));
        CHECK(olui_key(ui, OLUI_KEY_TAB, 0));
        CHECK(olui_key(ui, OLUI_KEY_RIGHT, 0));
        CHECK(olui_tabs_current(tabs) == 1 && tab_selects == 1);
        olui_tabs_select(ui, tabs, 0);
    }

    olui_set_progress(ui, bar, 7);
    CHECK(olui_damaged(ui, &r) && r.w == 520);

    /* a slider: dragged, keyed and set; olui_box says where it is */
    {
        olui *sl = olui_new();
        olui_el *red;
        olui_rect b;
        int was = changes;
        CHECK(sl && olui_load(sl, "<row><label>Red</label><slider id=red min=0 max=255 value=100 style='flex:1'></row>",
                              "slider{padding:0}", err, sizeof(err)));
        CHECK(olui_on(sl, NULL, 0, on_event, NULL));
        CHECK(olui_layout(sl, 300, 40));
        red = olui_find(sl, "red");
        CHECK(red && olui_slider_value(red) == 100);
        CHECK(olui_box(sl, red, &b) && b.w > 100 && b.h > 0);
        CHECK(olui_mouse(sl, b.x + b.w - 1, b.y + b.h / 2, OLUI_MOUSE_DOWN));
        CHECK(olui_slider_value(red) == 255 && changes == was + 1 && !strcmp(last_value, "255"));
        CHECK(olui_mouse(sl, b.x - 20, b.y + b.h / 2, OLUI_MOUSE_MOVE));    /* held: it follows past the end */
        CHECK(olui_slider_value(red) == 0);
        CHECK(olui_mouse(sl, b.x + b.w / 2, b.y + b.h / 2, OLUI_MOUSE_MOVE));
        CHECK(olui_slider_value(red) >= 126 && olui_slider_value(red) <= 129);
        CHECK(olui_mouse(sl, b.x + b.w / 2, b.y + b.h / 2, OLUI_MOUSE_UP));
        CHECK(olui_mouse(sl, b.x + 2, b.y + b.h / 2, OLUI_MOUSE_MOVE) && olui_slider_value(red) > 100);   /* let go */
        CHECK(olui_key(sl, OLUI_KEY_HOME, 0) && olui_slider_value(red) == 0);
        CHECK(olui_key(sl, OLUI_KEY_RIGHT, 0) && olui_slider_value(red) == 1);
        CHECK(olui_key(sl, OLUI_KEY_PAGEUP, 0) && olui_slider_value(red) == 26);
        CHECK(olui_key(sl, OLUI_KEY_END, 0) && olui_slider_value(red) == 255);
        was = changes;
        olui_set_slider(sl, red, 300);
        CHECK(olui_slider_value(red) == 255 && changes == was);
        olui_set_slider(sl, red, 7);
        CHECK(olui_slider_value(red) == 7 && changes == was);
        olui_set_hidden(sl, red, 1);
        CHECK(!olui_box(sl, red, &b) || b.w == 0);
        olui_free(sl);
    }

    /* a hover, press or focus drawn again where it changed, as a full draw would */
    CHECK(frames() == 0);

    /* 0.3's widgets */
    if (argc > 3) w_ops_path = argv[3];
    CHECK(widgets() == 0);

    /* a bad markup says where */
    {
        olui *bad = olui_new();
        CHECK(bad && !olui_load(bad, "<row><button>x</row>", NULL, err, sizeof(err)));
        CHECK(strstr(err, "line 1"));
        olui_free(bad);
    }

    if (argc > 1) {
        memset(&s, 0, sizeof(s));
        s.out = fopen(argv[1], "w");
        CHECK(s.out);
        fprintf(s.out, "size 520 340\n");
        olui_draw(ui, NULL, draw, &s);
        fclose(s.out);
    }

    olui_free(ui);
    printf("OLUI PASS version=%s\n", OLUI_VERSION);
    return 0;
}
