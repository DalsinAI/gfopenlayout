/* GFOpenLayout UI on the host: markup and CSS to a laid out window, the
 * widgets' events through C callbacks, the keyboard, the theme as CSS.
 * With an argument, writes the window's draw ops there as lines of text
 * (tests/ui_render.py draws them as a picture).
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
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
        olui_free(plain);
    }
    printf("frames: %d partial draws, at most %ld ops each, each the same as a full draw\n", partial, most);
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
