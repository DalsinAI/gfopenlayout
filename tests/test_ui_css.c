/* GFOpenLayout UI's CSS through GFCSS: what the UI gained (attribute and
 * sibling selectors, :not() and :nth-child(), !important, more colours and
 * units), and how long styling a big window takes.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int checks, failed;

#define CHECK(x) do { ++checks; if (!(x)) { \
    ++failed; printf("FAIL line=%d expr=%s\n", __LINE__, #x); \
} } while (0)

static int measure(void *ud, const char *s, size_t n, const ol_style *st, ol_text_metrics *m)
{
    (void)ud;
    (void)s;
    m->width = (ol_unit)n * st->font_size / 2;
    m->ascent = st->font_size * 3 / 4;
    m->descent = st->font_size / 4;
    return 1;
}

typedef struct {
    const olui_el *want;
    uint32_t text_argb;
    int text_px, flags, found_text;
    olui_rect box;
    uint32_t fill;
    int border, found_box;
} look;

static void catch_op(void *ud, const olui_draw_op *op)
{
    look *l = (look *)ud;
    if (op->el != l->want) return;
    if (op->kind == OLUI_DRAW_TEXT && !l->found_text) {
        l->text_argb = op->argb;
        l->text_px = op->font_pixels;
        l->flags = (int)op->text_flags;
        l->found_text = 1;
    }
    if (op->kind == OLUI_DRAW_BOX && !l->found_box) {
        l->box = op->box;
        l->fill = op->stops ? op->stop_argb[0] : 0;
        l->border = op->border;
        l->found_box = 1;
    }
}

static look look_at(olui *ui, const char *id)
{
    look l;
    memset(&l, 0, sizeof(l));
    l.want = olui_find(ui, id);
    olui_draw(ui, NULL, catch_op, &l);
    return l;
}

static const char markup[] =
    "<column id=c>"
    "<label id=l1 class='a b'>one</label>"
    "<label id=l2>two</label>"
    "<label id=l3 title=hint>three</label>"
    "<button id=b1 value=go>Go</button>"
    "<button id=b2 disabled>No</button>"
    "<input id=f placeholder='Type here'>"
    "</column>";

static const char css[] =
    "label:nth-child(2){color:hsl(120,100%,25%)}"
    "label[title]{color:rebeccapurple;font-size:1.5em}"
    ".a.b{font-weight:bolder;text-decoration:line-through}"
    "label + label + label ~ button{background:#abc}"
    "button:not(:disabled){border:3px solid red}"
    "button:enabled{width:calc(50px + 2em)}"
    "#l1{color:red !important}"
    "label{color:blue}"
    "[placeholder^=Type]{padding:0.5rem}"
    "@media print{#l2{color:red}}"
    "@media screen{#l3{font-style:italic}}";

static void test_gains(void)
{
    olui *ui = olui_new();
    char err[128];
    look l;
    CHECK(ui && olui_load(ui, markup, css, err, sizeof(err)));
    olui_set_measure(ui, measure, NULL);
    CHECK(olui_layout(ui, 400, 300));
    l = look_at(ui, "l1");
    CHECK(l.found_text && l.text_argb == 0xffff0000u);              /* !important over a later rule */
    CHECK((l.flags & OL_TEXT_BOLD) && (l.flags & OL_TEXT_STRIKE));
    l = look_at(ui, "l2");
    CHECK(l.text_argb == 0xff008000u);                              /* :nth-child(), hsl() */
    l = look_at(ui, "l3");
    CHECK(l.text_argb == 0xff663399u && l.text_px == 19 && (l.flags & OL_TEXT_ITALIC));
    l = look_at(ui, "b1");
    CHECK(l.found_box && l.fill == 0xffaabbccu && l.border == 3);
    CHECK(l.box.w == 50 + 26);                                      /* 2em of 13px */
    l = look_at(ui, "b2");
    CHECK(l.found_box && l.border == 1 && l.fill == 0xffaabbccu);
    {
        olui_rect r;
        /* a line of 16 px and 0.5rem (6.5 px) above and below it, for the UI's 3px */
        CHECK(olui_box(ui, olui_find(ui, "f"), &r) && r.h == 29);
    }
    /* a state change restyles what a selector with it reaches */
    olui_set_disabled(ui, olui_find(ui, "b1"), 1);
    l = look_at(ui, "b1");
    CHECK(l.border == 1);
    olui_free(ui);
}

/* ui/olui_css.c: a named font family by its number in text_flags. This is
 * internal to the resident library, so its linked-client build checks the
 * public numeric contract but not this private name table. */
#ifndef OLUI_LINK_LIBRARY
const char *olui_family_name(unsigned id);
#endif

/* font-family's first name reaches the renderer as a number in text_flags
 * (OL_TEXT_FONT); generics stay 0, so the theme's faces are used */
static void test_families(void)
{
    olui *ui = olui_new();
    char err[128];
    look l;
    unsigned f1, f2;
    CHECK(ui && olui_load(ui, "<column><label id=n1>a</label><label id=n2>b</label>"
                              "<label id=n3>c</label><label id=n4>d</label></column>",
                          "#n1{font-family:\"DejaVu Sans Mono\",monospace}"
                          "#n2{font-family:dejavu sans mono}"
                          "#n3{font-family:monospace}", err, sizeof(err)));
    olui_set_measure(ui, measure, NULL);
    CHECK(olui_layout(ui, 400, 300));
    l = look_at(ui, "n1");
    f1 = OL_TEXT_FONT_OF((uint32_t)l.flags);
    CHECK(f1 != 0 && (l.flags & OL_TEXT_MONO));
#ifndef OLUI_LINK_LIBRARY
    CHECK(olui_family_name(f1) && strcmp(olui_family_name(f1), "DejaVu Sans Mono") == 0);
#endif
    l = look_at(ui, "n2");
    CHECK(OL_TEXT_FONT_OF((uint32_t)l.flags) == f1);             /* the same name, any case */
    l = look_at(ui, "n3");
    CHECK(OL_TEXT_FONT_OF((uint32_t)l.flags) == 0 && (l.flags & OL_TEXT_MONO));
    l = look_at(ui, "n4");
    CHECK(OL_TEXT_FONT_OF((uint32_t)l.flags) == 0);
    /* a style set later (as a toolkit's SetFont does) */
    olui_set_style(ui, olui_find(ui, "n4"), "font-family:Courier;font-size:10px");
    CHECK(olui_layout(ui, 400, 300));
    l = look_at(ui, "n4");
    f2 = OL_TEXT_FONT_OF((uint32_t)l.flags);
    CHECK(f2 != 0 && f2 != f1);
#ifndef OLUI_LINK_LIBRARY
    CHECK(strcmp(olui_family_name(f2), "Courier") == 0);
    CHECK(olui_family_name(0) == NULL);
#endif
    olui_free(ui);
}

/* a window of many widgets, styled again from the start each time */
static void test_speed(void)
{
    static char big[200000];
    size_t n = 0;
    int i, rounds = 50;
    char err[128];
    olui *ui = olui_new();
    clock_t t0, t1;
    n += (size_t)sprintf(big + n, "<column>");
    for (i = 0; i < 60; ++i)
        n += (size_t)sprintf(big + n, "<row class=r%d><label>Name %d</label><input id=i%d style='flex:1'>"
                             "<checkbox id=c%d>On</checkbox><button id=b%d>Go</button></row>", i % 7, i, i, i, i);
    n += (size_t)sprintf(big + n, "<list id=l style='height:200px'>");
    for (i = 0; i < 100; ++i) n += (size_t)sprintf(big + n, "<item>Item %d</item>", i);
    n += (size_t)sprintf(big + n, "</list></column>");
    CHECK(ui && olui_load(ui, big, "row.r3 button{color:red} item:nth-child(odd){background:#eee}", err, sizeof(err)));
    olui_set_measure(ui, measure, NULL);
    CHECK(olui_layout(ui, 600, 4000));
    t0 = clock();
    for (i = 0; i < rounds; ++i) {
        olui_add_css(ui, "");               /* every element styled again */
        olui_layout(ui, 600, 4000);
    }
    t1 = clock();
    printf("ui speed: a window of %d widgets styled and laid out in %ld us\n", 60 * 4 + 100 + 61,
           (long)((t1 - t0) * 1000000 / CLOCKS_PER_SEC / rounds));
    olui_free(ui);
}

/* justify-content's space modes share a row's spare room out between its children */
static void test_row_space(void)
{
    static const char *const mode[4] = { "flex-start", "space-between", "space-around", "space-evenly" };
    /* three boxes 40 px wide, no gap, in a window 300 px wide with 10 px of padding: 160 px to spare */
    static const int at[4][3] = { { 10, 50, 90 }, { 10, 130, 250 }, { 37, 130, 223 }, { 50, 130, 210 } };
    int k, i;
    for (k = 0; k < 4; ++k) {
        olui *ui = olui_new();
        char err[128], m[400];
        static const char *const id[3] = { "x", "y", "z" };
        snprintf(m, sizeof m, "<row style='gap:0;justify-content:%s'><box id=x style='width:40px;height:10px'></box>"
                 "<box id=y style='width:40px;height:10px'></box><box id=z style='width:40px;height:10px'></box></row>",
                 mode[k]);
        CHECK(ui && olui_load(ui, m, NULL, err, sizeof(err)));
        olui_set_measure(ui, measure, NULL);
        CHECK(olui_layout(ui, 300, 100));
        for (i = 0; i < 3; ++i) {
            olui_rect r;
            CHECK(olui_box(ui, olui_find(ui, id[i]), &r) && r.x >= at[k][i] - 1 && r.x <= at[k][i] + 1 && r.w == 40);
            if (r.x < at[k][i] - 1 || r.x > at[k][i] + 1) printf("  %s %s at %d\n", mode[k], id[i], (int)r.x);
        }
        olui_free(ui);
    }
    /* and down a column 200 px high: two boxes 20 px high, 160 px to spare, under 6 px of padding */
    {
        static const char *const cmode[6] = { "flex-start", "center", "flex-end", "space-between", "space-around",
                                              "space-evenly" };
        static const int cat[6][2] = { { 6, 26 }, { 86, 106 }, { 166, 186 }, { 6, 186 }, { 46, 146 }, { 59, 133 } };
        for (k = 0; k < 6; ++k) {
            olui *ui = olui_new();
            char err[128], m[300];
            olui_rect r1, r2;
            snprintf(m, sizeof m, "<column style='height:200px;gap:0;justify-content:%s'>"
                     "<box id=x style='height:20px'></box><box id=y style='height:20px'></box></column>", cmode[k]);
            CHECK(ui && olui_load(ui, m, NULL, err, sizeof(err)));
            olui_set_measure(ui, measure, NULL);
            CHECK(olui_layout(ui, 300, 300));
            CHECK(olui_box(ui, olui_find(ui, "x"), &r1) && olui_box(ui, olui_find(ui, "y"), &r2));
            CHECK(r1.y >= cat[k][0] - 1 && r1.y <= cat[k][0] + 1 && r2.y >= cat[k][1] - 1 && r2.y <= cat[k][1] + 1);
            olui_free(ui);
        }
    }
}

int main(void)
{
    test_gains();
    test_row_space();
    test_families();
    test_speed();
    printf("OLUI CSS %d checks, %d failed\n", checks, failed);
    if (failed) return 20;
    printf("OLUI CSS PASS\n");
    return 0;
}
