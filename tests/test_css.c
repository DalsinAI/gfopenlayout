/* GFCSS on the host: parsing and its mistakes, every selector and
 * combinator on a small tree, specificity and the cascade, !important,
 * inheritance, @media, each unit and colour, shorthands, GFOpenLayout's
 * styles from a computed style, and how long 2,000 rules take over 2,000
 * elements.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "gfopenlayout_css.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int checks, failed;

#define CHECK(x) do { ++checks; if (!(x)) { \
    ++failed; printf("FAIL line=%d expr=%s\n", __LINE__, #x); \
} } while (0)

/* ---- A small element tree ---- */
typedef struct tel {
    const char *tag, *id, *cls, *style;
    uint32_t state;
    const char *attr_name[4], *attr_value[4];
    int empty;
    struct tel *parent, *prev, *next, *first, *last;
} tel;

static tel pool[4096];
static int npool;

static tel *mk(tel *parent, const char *tag, const char *id, const char *cls)
{
    tel *t = &pool[npool++];
    memset(t, 0, sizeof(*t));
    t->tag = tag;
    t->id = id;
    t->cls = cls;
    t->empty = 1;
    t->parent = parent;
    if (parent) {
        parent->empty = 0;
        t->prev = parent->last;
        if (parent->last) parent->last->next = t;
        else parent->first = t;
        parent->last = t;
    }
    return t;
}

static void set_attr(tel *t, const char *name, const char *value)
{
    int i;
    for (i = 0; i < 4 && t->attr_name[i]; ++i) {}
    if (i < 4) {
        t->attr_name[i] = name;
        t->attr_value[i] = value;
    }
}

static void t_info(void *ud, const void *el, gfcss_info *in)
{
    const tel *t = (const tel *)el;
    (void)ud;
    in->tag = t->tag;
    in->id = t->id;
    in->classes = t->cls;
    in->style = t->style;
    in->state = t->state;
    in->index = 0;
    in->count = 0;
    in->empty = t->empty;
}

static const void *t_parent(void *ud, const void *el) { (void)ud; return ((const tel *)el)->parent; }
static const void *t_prev(void *ud, const void *el) { (void)ud; return ((const tel *)el)->prev; }
static const void *t_next(void *ud, const void *el) { (void)ud; return ((const tel *)el)->next; }

static const char *t_attr(void *ud, const void *el, const char *name)
{
    const tel *t = (const tel *)el;
    int i;
    (void)ud;
    if (!strcmp(name, "id")) return t->id;
    if (!strcmp(name, "class")) return t->cls;
    for (i = 0; i < 4 && t->attr_name[i]; ++i)
        if (!strcmp(t->attr_name[i], name)) return t->attr_value[i];
    return NULL;
}

static const gfcss_element_ops ops = { t_info, t_parent, t_prev, t_next, t_attr };

/* el's style, its ancestors' worked out first */
static void style_of(gfcss_engine *e, const tel *el, gfcss_computed *out)
{
    const tel *chain[64];
    int n = 0, i;
    gfcss_computed a, b, *cur = &a, *prev = NULL;
    for (; el && n < 64; el = el->parent) chain[n++] = el;
    for (i = n - 1; i >= 0; --i) {
        gfcss_computed_init(cur);
        gfcss_compute(e, &ops, NULL, chain[i], GFCSS_PSEUDO_NONE, prev, cur);
        prev = cur;
        cur = cur == &a ? &b : &a;
    }
    *out = *prev;
}

static int parse_str(gfcss_sheet *s, const char *css)
{
    return gfcss_sheet_parse(s, css, strlen(css));
}

static gfcss_engine *engine_with(const char *css)
{
    gfcss_engine *e = gfcss_new();
    gfcss_sheet *s = gfcss_sheet_new(e, GFCSS_ORIGIN_AUTHOR);
    gfcss_sheet_parse(s, css, strlen(css));
    return e;
}

/* the colour of el under css */
static uint32_t colour_under(const char *css, const tel *el)
{
    gfcss_engine *e = engine_with(css);
    gfcss_computed cs;
    style_of(e, el, &cs);
    gfcss_free(e);
    return cs.color;
}

#define RED 0xffff0000u
#define BLACK 0xff000000u
#define GREEN 0xff008000u
#define BLUE 0xff0000ffu

/* the tree:
 * html
 *   body.page#top
 *     div#main.box.wide [lang=en-GB data-x="Hello World"]
 *       p.first           "a"
 *       p                 (empty)
 *       span.note
 *       p.last#lastp      [href=http://x/y.pdf]
 *     ul
 *       li li li li li
 *     a.link:hover (state LINK|HOVER)
 *     input[type=checkbox] :checked
 *     button :disabled
 */
static tel *html, *body, *divm, *p1, *p2, *span1, *p3, *ul, *li[5], *link, *input, *button;

static void build_tree(void)
{
    int i;
    npool = 0;
    html = mk(NULL, "html", NULL, NULL);
    body = mk(html, "body", "top", "page");
    divm = mk(body, "div", "main", "box wide");
    set_attr(divm, "lang", "en-GB");
    set_attr(divm, "data-x", "Hello World");
    p1 = mk(divm, "p", NULL, "first");
    p1->empty = 0;
    p2 = mk(divm, "p", NULL, NULL);
    span1 = mk(divm, "span", NULL, "note");
    span1->empty = 0;
    p3 = mk(divm, "p", "lastp", "last");
    set_attr(p3, "href", "http://x/y.pdf");
    p3->empty = 0;
    ul = mk(body, "ul", NULL, NULL);
    for (i = 0; i < 5; ++i) li[i] = mk(ul, "li", NULL, NULL);
    link = mk(body, "a", NULL, "link");
    link->state = GFCSS_STATE_LINK | GFCSS_STATE_HOVER;
    input = mk(body, "input", NULL, NULL);
    set_attr(input, "type", "checkbox");
    input->state = GFCSS_STATE_CHECKED | GFCSS_STATE_ENABLED;
    button = mk(body, "BUTTON", NULL, NULL);
    button->state = GFCSS_STATE_DISABLED;
}

static void test_selectors(void)
{
    /* type, universal, class, id */
    CHECK(colour_under("p{color:red}", p1) == RED);
    CHECK(colour_under("P{color:red}", p1) == RED);         /* types without regard to case */
    CHECK(colour_under("button{color:red}", button) == RED);
    CHECK(colour_under("*{color:red}", p1) == RED);
    CHECK(colour_under(".first{color:red}", p1) == RED);
    CHECK(colour_under(".First{color:red}", p1) == BLACK);  /* classes with it */
    CHECK(colour_under(".box.wide{color:red}", divm) == RED);
    CHECK(colour_under(".box.narrow{color:red}", divm) == BLACK);
    CHECK(colour_under("#main{color:red}", divm) == RED);
    CHECK(colour_under("div#main.box{color:red}", divm) == RED);
    CHECK(colour_under("span#main{color:red}", divm) == BLACK);
    /* attributes */
    CHECK(colour_under("[lang]{color:red}", divm) == RED);
    CHECK(colour_under("[lang=en-GB]{color:red}", divm) == RED);
    CHECK(colour_under("[lang=en-gb]{color:red}", divm) == BLACK);
    CHECK(colour_under("[lang=en-gb i]{color:red}", divm) == RED);
    CHECK(colour_under("[lang|=en]{color:red}", divm) == RED);
    CHECK(colour_under("[lang|=e]{color:red}", divm) == BLACK);
    CHECK(colour_under("[data-x~=World]{color:red}", divm) == RED);
    CHECK(colour_under("[data-x~=Wor]{color:red}", divm) == BLACK);
    CHECK(colour_under("[href^='http://']{color:red}", p3) == RED);
    CHECK(colour_under("[href$=\".pdf\"]{color:red}", p3) == RED);
    CHECK(colour_under("[href*=x\\/y]{color:red}", p3) == RED);
    CHECK(colour_under("[href*=zz]{color:red}", p3) == BLACK);
    CHECK(colour_under("[class~=wide]{color:red}", divm) == RED);
    CHECK(colour_under("[id=main]{color:red}", divm) == RED);
    CHECK(colour_under("[type=checkbox]:checked{color:red}", input) == RED);
    /* combinators */
    CHECK(colour_under("body p{color:red}", p1) == RED);
    CHECK(colour_under("html p{color:red}", p1) == RED);
    CHECK(colour_under("body > p{color:red}", p1) == BLACK);
    CHECK(colour_under("div>p{color:red}", p1) == RED);
    CHECK(colour_under(".first + p{color:red}", p2) == RED);
    CHECK(colour_under(".first + p{color:red}", p3) == BLACK);
    CHECK(colour_under(".first ~ p{color:red}", p3) == RED);
    CHECK(colour_under(".first ~ span{color:red}", span1) == RED);
    CHECK(colour_under("span ~ .first{color:red}", p1) == BLACK);
    CHECK(colour_under("body > div > p + p ~ p.last{color:red}", p3) == RED);
    CHECK(colour_under("ul div p{color:red}", p1) == BLACK);
    /* structural pseudo-classes */
    CHECK(colour_under("p:first-child{color:red}", p1) == RED);
    CHECK(colour_under("p:first-child{color:red}", p2) == BLACK);
    CHECK(colour_under("p:last-child{color:red}", p3) == RED);
    CHECK(colour_under("li:nth-child(2n+1){color:red}", li[2]) == RED);
    CHECK(colour_under("li:nth-child(2n+1){color:red}", li[1]) == BLACK);
    CHECK(colour_under("li:nth-child(odd){color:red}", li[4]) == RED);
    CHECK(colour_under("li:nth-child(even){color:red}", li[1]) == RED);
    CHECK(colour_under("li:nth-child(3){color:red}", li[2]) == RED);
    CHECK(colour_under("li:nth-child(-n+2){color:red}", li[1]) == RED);
    CHECK(colour_under("li:nth-child(-n+2){color:red}", li[2]) == BLACK);
    CHECK(colour_under("li:nth-child( 2n - 1 ){color:red}", li[0]) == RED);
    CHECK(colour_under("li:nth-last-child(1){color:red}", li[4]) == RED);
    CHECK(colour_under("li:nth-last-child(2){color:red}", li[3]) == RED);
    CHECK(colour_under("li:only-child{color:red}", li[0]) == BLACK);
    CHECK(colour_under("p:first-of-type{color:red}", p1) == RED);
    CHECK(colour_under("span:first-of-type{color:red}", span1) == RED);
    CHECK(colour_under("span:only-of-type{color:red}", span1) == RED);
    CHECK(colour_under("p:last-of-type{color:red}", p3) == RED);
    CHECK(colour_under("p:last-of-type{color:red}", p2) == BLACK);
    CHECK(colour_under("p:nth-of-type(2){color:red}", p2) == RED);
    CHECK(colour_under("p:nth-last-of-type(2){color:red}", p2) == RED);
    CHECK(colour_under(":root{color:red}", html) == RED);
    CHECK(colour_under(":root{color:red}", body) == RED);   /* inherited from the root */
    CHECK(colour_under("body:root{color:red}", body) == BLACK);
    CHECK(colour_under("p:empty{color:red}", p2) == RED);
    CHECK(colour_under("p:empty{color:red}", p1) == BLACK);
    /* :not() */
    CHECK(colour_under("p:not(.first){color:red}", p2) == RED);
    CHECK(colour_under("p:not(.first){color:red}", p1) == BLACK);
    CHECK(colour_under("p:not(.first, #lastp){color:red}", p3) == BLACK);
    CHECK(colour_under("p:not(span):not([href]){color:red}", p2) == RED);
    CHECK(colour_under(":not(p):not(:not(span)){color:red}", span1) == RED);
    /* states */
    CHECK(colour_under("a:hover{color:red}", link) == RED);
    CHECK(colour_under("a:link{color:red}", link) == RED);
    CHECK(colour_under("a:visited{color:red}", link) == BLACK);
    CHECK(colour_under("a:active{color:red}", link) == BLACK);
    CHECK(colour_under("a:focus{color:red}", link) == BLACK);
    CHECK(colour_under(":disabled{color:red}", button) == RED);
    CHECK(colour_under(":enabled{color:red}", input) == RED);
    CHECK(colour_under(":enabled{color:red}", button) == BLACK);
    CHECK(colour_under("input:checked{color:red}", input) == RED);
    /* comma lists; a selector we don't know drops the whole rule */
    CHECK(colour_under("h1, p.first, h2{color:red}", p1) == RED);
    CHECK(colour_under("p.first, p:unknown-thing{color:red}", p1) == BLACK);
    CHECK(colour_under("p.first, {color:red}", p1) == BLACK);
    CHECK(colour_under("p.first,, p{color:red}", p1) == BLACK);
    CHECK(colour_under("p.first > {color:red}", p1) == BLACK);
    CHECK(colour_under("ns|p{color:red}", p1) == BLACK);
    CHECK(colour_under("p::before:hover{color:red} p{color:green}", p1) == GREEN);
    /* escapes in selectors */
    CHECK(colour_under(".\\66irst{color:red}", p1) == RED);
    CHECK(colour_under("#ma\\in{color:red}", divm) == RED);
}

static void test_cascade(void)
{
    gfcss_engine *e;
    gfcss_sheet *ua, *user, *author;
    gfcss_computed cs;
    /* specificity, then order */
    CHECK(colour_under("#main{color:red} div{color:green}", divm) == RED);
    CHECK(colour_under(".box{color:red} div{color:green}", divm) == RED);
    CHECK(colour_under("div.box{color:red} .box.wide{color:green}", divm) == GREEN);
    CHECK(colour_under("div{color:red} div{color:green}", divm) == GREEN);
    CHECK(colour_under("div{color:red;color:green}", divm) == GREEN);
    CHECK(colour_under(":not(#x){color:red} div.box.wide{color:green}", divm) == RED);
    CHECK(colour_under("p:nth-child(1){color:red} p.first{color:green}", p1) == GREEN);
    /* !important */
    CHECK(colour_under("div{color:red!important} #main{color:green}", divm) == RED);
    CHECK(colour_under("div{color:red !important} div{color:green !important}", divm) == GREEN);
    CHECK(colour_under("div{color:red ! important}", divm) == RED);
    CHECK(colour_under("div{color:green} div{color:red !imp}", divm) == GREEN);   /* bad: left out */
    /* the style attribute: after the rules; important rules over it, its own important over them */
    divm->style = "color:blue";
    CHECK(colour_under("#main{color:red}", divm) == BLUE);
    CHECK(colour_under("#main{color:red!important}", divm) == RED);
    divm->style = "color:blue!important";
    CHECK(colour_under("#main{color:red!important}", divm) == BLUE);
    divm->style = "color:; color:blue; bogus:1; margin:";
    CHECK(colour_under("", divm) == BLUE);
    divm->style = NULL;
    /* origins */
    e = gfcss_new();
    ua = gfcss_sheet_new(e, GFCSS_ORIGIN_UA);
    user = gfcss_sheet_new(e, GFCSS_ORIGIN_USER);
    author = gfcss_sheet_new(e, GFCSS_ORIGIN_AUTHOR);
    CHECK(parse_str(ua, "#main{color:red;font-weight:bold!important}p{margin:1px}"));
    CHECK(parse_str(user, "div{color:green;font-style:italic!important;text-align:center}"));
    CHECK(parse_str(author, "div{color:blue;font-weight:normal!important;font-style:normal!important;"
                            "text-align:right}"));
    style_of(e, divm, &cs);
    CHECK(cs.color == BLUE);                        /* author over user over UA, whatever the specificity */
    CHECK(cs.font_weight == 700);                   /* UA !important over all */
    CHECK(cs.font_style == GFCSS_FONT_ITALIC);      /* user !important over author !important */
    CHECK(cs.text_align == GFCSS_ALIGN_RIGHT);
    CHECK(gfcss_is_set(&cs, GFCSS_P_COLOR) && !gfcss_is_set(&cs, GFCSS_P_MARGIN_TOP));
    gfcss_sheet_free(author);
    style_of(e, divm, &cs);
    CHECK(cs.color == GREEN && cs.text_align == GFCSS_ALIGN_CENTER);
    gfcss_free(e);
}

static void test_inheritance(void)
{
    gfcss_engine *e = engine_with(
        "body{color:green;font-size:20px;line-height:1.5;text-align:center;margin:5px;border:1px solid;"
        "letter-spacing:2px;list-style:square inside;cursor:pointer;visibility:hidden;white-space:pre}"
        "div{line-height:30px}"
        "p.first{margin:inherit;border-top-color:inherit;color:initial;font-size:unset;padding:unset}"
        "span{text-align:initial;font-size:1.5em;line-height:inherit}"
        "#lastp{line-height:200%}");
    gfcss_computed cs;
    style_of(e, divm, &cs);
    CHECK(cs.color == GREEN && cs.font_size == OL_CSSPX(20) && cs.text_align == GFCSS_ALIGN_CENTER);
    CHECK(cs.margin[0].units == 0);                 /* margins aren't inherited */
    CHECK(cs.border_width[0] == 0);
    CHECK(cs.letter_spacing == OL_CSSPX(2) && cs.list_style_type == GFCSS_LIST_SQUARE &&
          cs.list_style_position == GFCSS_LIST_INSIDE && cs.cursor == GFCSS_CURSOR_POINTER &&
          cs.visibility == GFCSS_HIDDEN && cs.white_space == GFCSS_WS_PRE);
    CHECK(cs.line_height_kind == GFCSS_LINE_LENGTH && cs.line_height == OL_CSSPX(30));
    style_of(e, p1, &cs);
    CHECK(cs.color == BLACK);                       /* initial */
    CHECK(cs.font_size == OL_CSSPX(20));            /* unset: inherited */
    CHECK(cs.margin[0].units == 0);                 /* inherit: div's, which is 0 */
    CHECK(cs.line_height == OL_CSSPX(30));
    style_of(e, span1, &cs);
    CHECK(cs.text_align == GFCSS_ALIGN_START && cs.font_size == OL_CSSPX(30));
    style_of(e, p3, &cs);
    CHECK(cs.line_height_kind == GFCSS_LINE_LENGTH && cs.line_height == OL_CSSPX(40));
    style_of(e, ul, &cs);
    CHECK(cs.line_height_kind == GFCSS_LINE_NUMBER && cs.line_height == 1500);   /* a number inherits as one */
    gfcss_free(e);
}

static int lengths_px(const char *decl, const tel *el, gfcss_computed *cs)
{
    char css[256];
    gfcss_engine *e;
    snprintf(css, sizeof(css), "html{font-size:10px}body{font-size:20px}#main{%s}", decl);
    e = engine_with(css);
    gfcss_set_media(e, GFCSS_MEDIA_SCREEN, OL_CSSPX(800), OL_CSSPX(600));
    style_of(e, el, cs);
    gfcss_free(e);
    return 1;
}

static void test_units(void)
{
    gfcss_computed cs;
    lengths_px("margin-left:10px", divm, &cs);
    CHECK(cs.margin[3].units == 600);
    lengths_px("margin-left:12pt", divm, &cs);
    CHECK(cs.margin[3].units == 960);
    lengths_px("margin-left:1pc", divm, &cs);
    CHECK(cs.margin[3].units == 960);
    lengths_px("margin-left:1in", divm, &cs);
    CHECK(cs.margin[3].units == 5760);
    lengths_px("margin-left:2.54cm", divm, &cs);
    CHECK(cs.margin[3].units == 5760);
    lengths_px("margin-left:25.4mm", divm, &cs);
    CHECK(cs.margin[3].units == 5760);
    lengths_px("margin-left:1.5em", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(30));
    lengths_px("margin-left:2rem", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(20));     /* of the root's 10px */
    lengths_px("margin-left:2ex", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(20));     /* an ex and a ch are half an em */
    lengths_px("margin-left:1ch", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(10));
    lengths_px("margin-left:10vw", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(80));
    lengths_px("margin-left:10vh", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(60));
    lengths_px("margin-left:10vmin;margin-right:10vmax", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(60) && cs.margin[1].units == OL_CSSPX(80));
    lengths_px("margin-left:37.5%", divm, &cs);
    CHECK(cs.margin[3].pct == 37500 && cs.margin[3].units == 0);
    CHECK(gfcss_resolve(&cs.margin[3], OL_CSSPX(400)) == OL_CSSPX(150));
    lengths_px("margin-left:0", divm, &cs);
    CHECK(cs.margin[3].units == 0 && gfcss_is_set(&cs, GFCSS_P_MARGIN_LEFT));
    lengths_px("margin-left:10", divm, &cs);
    CHECK(!gfcss_is_set(&cs, GFCSS_P_MARGIN_LEFT));    /* no unit: left out */
    lengths_px("margin-left:-5px;padding-left:-5px", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(-5) && !gfcss_is_set(&cs, GFCSS_P_PADDING_LEFT));
    lengths_px("margin-left:.5px;margin-right:1e1px;margin-top:+3PX", divm, &cs);
    CHECK(cs.margin[3].units == 30 && cs.margin[1].units == 600 && cs.margin[0].units == 180);
    lengths_px("width:calc(100% - 20px)", divm, &cs);
    CHECK(cs.width.kind == GFCSS_LEN && cs.width.pct == 100000 && cs.width.units == OL_CSSPX(-20));
    CHECK(gfcss_resolve(&cs.width, OL_CSSPX(500)) == OL_CSSPX(480));
    lengths_px("width:calc((2em + 10px) * 2 - 50% / 2)", divm, &cs);
    CHECK(cs.width.units == OL_CSSPX(100) && cs.width.pct == -25000);
    lengths_px("width:calc(100%-20px)", divm, &cs);
    CHECK(!gfcss_is_set(&cs, GFCSS_P_WIDTH));          /* - needs spaces round it */
    lengths_px("width:calc(10px * 2px)", divm, &cs);
    CHECK(!gfcss_is_set(&cs, GFCSS_P_WIDTH));
    lengths_px("width:calc(10px / 0)", divm, &cs);
    CHECK(!gfcss_is_set(&cs, GFCSS_P_WIDTH));
    lengths_px("width:calc(3 * 4px)", divm, &cs);
    CHECK(cs.width.units == OL_CSSPX(12));
    lengths_px("width:auto;max-width:none;min-height:5%", divm, &cs);
    CHECK(cs.width.kind == GFCSS_LEN_AUTO && cs.max_width.kind == GFCSS_LEN_NONE && cs.min_height.pct == 5000);
    lengths_px("width:99999999px", divm, &cs);
    CHECK(cs.width.units > 0);                          /* saturated, not wrapped round */
    /* font sizes */
    lengths_px("font-size:150%", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(30));
    lengths_px("font-size:2em", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(40));
    lengths_px("font-size:x-large", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(24));
    lengths_px("font-size:smaller", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(20) * 5 / 6);
    lengths_px("font-size:larger", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(24));
    lengths_px("font-size:-2px", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(20));
    lengths_px("font-size:1rem", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(10));
    lengths_px("font-size:xx-small", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(9));
    lengths_px("font-size:xxx-large", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(48));
    lengths_px("font-size:calc(1em + 2px)", divm, &cs);
    CHECK(cs.font_size == OL_CSSPX(22));
    /* line heights */
    lengths_px("line-height:normal", divm, &cs);
    CHECK(cs.line_height_kind == GFCSS_LINE_NORMAL);
    lengths_px("line-height:1.25", divm, &cs);
    CHECK(cs.line_height_kind == GFCSS_LINE_NUMBER && cs.line_height == 1250);
    lengths_px("line-height:150%", divm, &cs);
    CHECK(cs.line_height_kind == GFCSS_LINE_LENGTH && cs.line_height == OL_CSSPX(30));
    lengths_px("line-height:2em", divm, &cs);
    CHECK(cs.line_height == OL_CSSPX(40));
    lengths_px("line-height:-1", divm, &cs);
    CHECK(!gfcss_is_set(&cs, GFCSS_P_LINE_HEIGHT));
    /* numbers */
    lengths_px("opacity:0.5", divm, &cs);
    CHECK(cs.opacity == 500);
    lengths_px("opacity:150%", divm, &cs);
    CHECK(cs.opacity == 1000);
    lengths_px("opacity:-1", divm, &cs);
    CHECK(cs.opacity == 0);
    lengths_px("z-index:-3", divm, &cs);
    CHECK(cs.z_index == -3 && !cs.z_auto);
    lengths_px("z-index:1.5", divm, &cs);
    CHECK(cs.z_auto);
    lengths_px("font-weight:bolder", divm, &cs);
    CHECK(cs.font_weight == 700);
    lengths_px("font-weight:lighter", divm, &cs);
    CHECK(cs.font_weight == 100);
    lengths_px("font-weight:850", divm, &cs);
    CHECK(cs.font_weight == 850);
    lengths_px("font-weight:0", divm, &cs);
    CHECK(cs.font_weight == 400);
}

static uint32_t colour_of(const char *value)
{
    char css[256];
    snprintf(css, sizeof(css), "#main{color:%s}", value);
    return colour_under(css, divm);
}

static void test_colours(void)
{
    uint32_t c;
    CHECK(colour_of("#f00") == RED);
    CHECK(colour_of("#F00a") == 0xaaff0000u);
    CHECK(colour_of("#ff0000") == RED);
    CHECK(colour_of("#ff000080") == 0x80ff0000u);
    CHECK(colour_of("#ff00") == 0x00ffff00u);
    CHECK(colour_of("#ff000") == BLACK);       /* five digits: not a colour */
    CHECK(colour_of("#gg0000") == BLACK);
    CHECK(colour_of("rgb(255,0,0)") == RED);
    CHECK(colour_of("rgb( 255 , 0 , 0 )") == RED);
    CHECK(colour_of("rgb(255 0 0)") == RED);
    CHECK(colour_of("rgb(100%,0%,0%)") == RED);
    CHECK(colour_of("rgba(255,0,0,0.5)") == 0x80ff0000u);
    CHECK(colour_of("rgb(255 0 0 / 50%)") == 0x80ff0000u);
    CHECK(colour_of("rgba(255,0,0,.25)") == 0x40ff0000u);
    CHECK(colour_of("rgb(300,-5,0)") == RED);
    CHECK(colour_of("rgb(127.6,0,0)") == 0xff800000u);
    CHECK(colour_of("rgb(255,0)") == BLACK);
    CHECK(colour_of("rgb(255 0, 0)") == BLACK);
    CHECK(colour_of("hsl(0, 100%, 50%)") == RED);
    CHECK(colour_of("hsl(120deg 100% 25%)") == GREEN);
    CHECK(colour_of("hsl(240, 100%, 50%)") == BLUE);
    CHECK(colour_of("hsla(0.5turn, 100%, 50%, 0.5)") == 0x8000ffffu);
    CHECK(colour_of("hsl(0, 0%, 100%)") == 0xffffffffu);
    CHECK(colour_of("hsl(60, 100%, 50%)") == 0xffffff00u);
    CHECK(colour_of("hsl(-120, 100%, 50%)") == BLUE);
    CHECK(colour_of("rebeccapurple") == 0xff663399u);
    CHECK(colour_of("LightGoldenRodYellow") == 0xfffafad2u);
    CHECK(colour_of("transparent") == 0);
    CHECK(colour_of("notacolour") == BLACK);
    CHECK(gfcss_parse_colour("aliceblue", 9, &c) && c == 0xfff0f8ffu);
    CHECK(gfcss_parse_colour("yellowgreen", 11, &c) && c == 0xff9acd32u);
    CHECK(gfcss_parse_colour("grey", 4, &c) && c == 0xff808080u);
    CHECK(gfcss_parse_colour(" #abc ", 6, &c) && c == 0xffaabbccu);
    CHECK(!gfcss_parse_colour("currentColor", 12, &c));
    CHECK(!gfcss_parse_colour("red blue", 8, &c));
    {
        extern int gfcss_named_colours(void);
        CHECK(gfcss_named_colours() == 148);
    }
    /* currentColor */
    {
        gfcss_engine *e = engine_with("#main{color:red;border:2px solid;background-color:currentcolor;"
                                      "outline:1px solid}p.first{color:currentColor;border:1px solid blue;"
                                      "border-left-color:currentColor}");
        gfcss_computed cs;
        style_of(e, divm, &cs);
        CHECK(cs.border_color[0] == RED && cs.border_color[3] == RED && cs.background_color == RED);
        CHECK(cs.outline_color == RED && cs.outline_width == OL_CSSPX(1));
        style_of(e, p1, &cs);
        CHECK(cs.color == RED && cs.border_color[0] == BLUE && cs.border_color[3] == RED);
        gfcss_free(e);
    }
}

static void test_shorthands(void)
{
    gfcss_engine *e;
    gfcss_computed cs;
    char buf[64];
    lengths_px("margin:1px 2px 3px 4px", divm, &cs);
    CHECK(cs.margin[0].units == OL_CSSPX(1) && cs.margin[1].units == OL_CSSPX(2) &&
          cs.margin[2].units == OL_CSSPX(3) && cs.margin[3].units == OL_CSSPX(4));
    lengths_px("margin:1px 2px", divm, &cs);
    CHECK(cs.margin[2].units == OL_CSSPX(1) && cs.margin[3].units == OL_CSSPX(2));
    lengths_px("margin:1px 2px 3px", divm, &cs);
    CHECK(cs.margin[3].units == OL_CSSPX(2));
    lengths_px("margin:0 auto", divm, &cs);
    CHECK(cs.margin[1].kind == GFCSS_LEN_AUTO && cs.margin[3].kind == GFCSS_LEN_AUTO && cs.margin[0].kind == GFCSS_LEN);
    lengths_px("margin:1px 2px 3px 4px 5px", divm, &cs);
    CHECK(!gfcss_is_set(&cs, GFCSS_P_MARGIN_TOP));
    lengths_px("padding:1em 10%", divm, &cs);
    CHECK(cs.padding[0].units == OL_CSSPX(20) && cs.padding[1].pct == 10000);
    lengths_px("border:3px dashed #00f", divm, &cs);
    CHECK(cs.border_width[2] == OL_CSSPX(3) && cs.border_style[1] == GFCSS_BORDER_DASHED &&
          cs.border_color[3] == BLUE);
    lengths_px("border:#00f solid", divm, &cs);
    CHECK(cs.border_width[0] == OL_CSSPX(3));      /* medium */
    lengths_px("border:5px", divm, &cs);
    CHECK(cs.border_width[0] == 0);                 /* no style: none */
    lengths_px("border:thin solid;border-bottom:thick double red;border-left-width:0", divm, &cs);
    CHECK(cs.border_width[0] == OL_CSSPX(1) && cs.border_width[2] == OL_CSSPX(5) &&
          cs.border_style[2] == GFCSS_BORDER_DOUBLE && cs.border_color[2] == RED && cs.border_width[3] == 0);
    lengths_px("border-width:1px 2px;border-style:solid none;border-color:red green blue", divm, &cs);
    CHECK(cs.border_width[0] == OL_CSSPX(1) && cs.border_width[1] == 0 && cs.border_color[1] == GREEN &&
          cs.border_color[3] == GREEN && cs.border_color[2] == BLUE);
    lengths_px("border:none", divm, &cs);
    CHECK(cs.border_width[0] == 0 && cs.border_style[0] == GFCSS_BORDER_NONE);
    lengths_px("border:1px solid red blue", divm, &cs);
    CHECK(!gfcss_is_set(&cs, GFCSS_P_BORDER_TOP_WIDTH));
    lengths_px("border-radius:4px 50%", divm, &cs);
    CHECK(cs.border_radius[0].units == OL_CSSPX(4) && cs.border_radius[1].pct == 50000 &&
          cs.border_radius[2].units == OL_CSSPX(4));
    lengths_px("border-radius:4px / 2px", divm, &cs);
    CHECK(cs.border_radius[3].units == OL_CSSPX(4));
    lengths_px("outline:2px dotted green", divm, &cs);
    CHECK(cs.outline_width == OL_CSSPX(2) && cs.outline_style == GFCSS_BORDER_DOTTED && cs.outline_color == GREEN);
    /* background */
    e = engine_with("#main{background:url(\"a b.png\") no-repeat right top #ff0}"
                    "p.first{background:linear-gradient(#fff, rgba(0,0,0,.5) 30%), blue}"
                    "p.last{background:url(x.png);background-color:red;background-position:center}"
                    "span{background:red; background:none}");
    style_of(e, divm, &cs);
    CHECK(cs.background_color == 0xffffff00u && cs.background_image.kind == GFCSS_TEXT_URL &&
          gfcss_text_copy(&cs.background_image, buf, sizeof(buf)) == 7 && !strcmp(buf, "a b.png"));
    CHECK(cs.background_repeat == GFCSS_NO_REPEAT && cs.background_position[0].pct == 100000 &&
          cs.background_position[1].pct == 0);
    style_of(e, p1, &cs);
    CHECK(cs.background_image.kind == GFCSS_TEXT_FUNCTION && cs.background_color == BLUE);
    CHECK(cs.background_image.length > 16 && !memcmp(cs.background_image.text, "linear-gradient(", 16));
    style_of(e, p3, &cs);
    CHECK(cs.background_color == RED && cs.background_image.kind == GFCSS_TEXT_URL &&
          cs.background_position[0].pct == 50000 && cs.background_position[1].pct == 50000);
    style_of(e, span1, &cs);
    CHECK(cs.background_color == 0 && cs.background_image.kind == GFCSS_TEXT_NONE);
    gfcss_free(e);
    /* font */
    e = engine_with("#main{font:italic small-caps bold 12px/1.5 \"Courier New\", serif}"
                    "p.first{font:16px Times}"
                    "p.last{font-family:Fancy Name, monospace}"
                    "span{font:bold}"
                    "html{font-family:sans-serif}");
    style_of(e, divm, &cs);
    CHECK(cs.font_style == GFCSS_FONT_ITALIC && cs.font_variant == GFCSS_VARIANT_SMALL_CAPS &&
          cs.font_weight == 700 && cs.font_size == OL_CSSPX(12) && cs.line_height_kind == GFCSS_LINE_NUMBER &&
          cs.line_height == 1500 && cs.font_generic == GFCSS_FONT_MONOSPACE);
    CHECK(gfcss_text_copy(&cs.font_family, buf, sizeof(buf)) == 11 && !strcmp(buf, "Courier New"));
    style_of(e, p1, &cs);
    CHECK(cs.font_style == GFCSS_FONT_NORMAL && cs.font_weight == 400 && cs.line_height_kind == GFCSS_LINE_NORMAL &&
          cs.font_generic == GFCSS_FONT_SERIF && cs.font_size == OL_CSSPX(16));
    style_of(e, p3, &cs);
    CHECK(cs.font_generic == GFCSS_FONT_MONOSPACE && gfcss_text_copy(&cs.font_family, buf, sizeof(buf)) &&
          !strcmp(buf, "Fancy Name"));
    style_of(e, span1, &cs);
    CHECK(!gfcss_is_set(&cs, GFCSS_P_FONT_WEIGHT));    /* font needs a size and a family */
    style_of(e, body, &cs);
    CHECK(cs.font_generic == GFCSS_FONT_SANS_SERIF);
    gfcss_free(e);
    /* list-style, flex, gap, overflow, text-decoration */
    lengths_px("list-style:none", divm, &cs);
    CHECK(cs.list_style_type == GFCSS_LIST_NONE);
    lengths_px("list-style:inside url(dot.png) lower-roman", divm, &cs);
    CHECK(cs.list_style_type == GFCSS_LIST_LOWER_ROMAN && cs.list_style_position == GFCSS_LIST_INSIDE &&
          cs.list_style_image.kind == GFCSS_TEXT_URL);
    lengths_px("flex:2", divm, &cs);
    CHECK(cs.flex_grow == 2000 && cs.flex_shrink == 1000 && cs.flex_basis.kind == GFCSS_LEN && cs.flex_basis.units == 0);
    lengths_px("flex:1 0 50%", divm, &cs);
    CHECK(cs.flex_grow == 1000 && cs.flex_shrink == 0 && cs.flex_basis.pct == 50000);
    lengths_px("flex:none", divm, &cs);
    CHECK(cs.flex_grow == 0 && cs.flex_shrink == 0 && cs.flex_basis.kind == GFCSS_LEN_AUTO);
    lengths_px("flex:auto", divm, &cs);
    CHECK(cs.flex_grow == 1000 && cs.flex_basis.kind == GFCSS_LEN_AUTO);
    lengths_px("flex:100px", divm, &cs);
    CHECK(cs.flex_grow == 1000 && cs.flex_basis.units == OL_CSSPX(100));
    lengths_px("flex-flow:column wrap;gap:4px 8px", divm, &cs);
    CHECK(cs.flex_direction == GFCSS_COLUMN && cs.flex_wrap == GFCSS_WRAP && cs.row_gap.units == OL_CSSPX(4) &&
          cs.column_gap.units == OL_CSSPX(8));
    lengths_px("gap:normal;align-items:flex-end;justify-content:space-between", divm, &cs);
    CHECK(cs.row_gap.kind == GFCSS_LEN_NORMAL && cs.align_items == GFCSS_FLEX_END &&
          cs.justify_content == GFCSS_FLEX_SPACE_BETWEEN);
    lengths_px("overflow:hidden scroll", divm, &cs);
    CHECK(cs.overflow_x == GFCSS_OVERFLOW_HIDDEN && cs.overflow_y == GFCSS_OVERFLOW_SCROLL);
    lengths_px("text-decoration:underline line-through red wavy", divm, &cs);
    CHECK(cs.text_decoration == (GFCSS_UNDERLINE | GFCSS_LINE_THROUGH));
    lengths_px("text-decoration-line:overline", divm, &cs);
    CHECK(cs.text_decoration == GFCSS_OVERLINE);
    lengths_px("border-spacing:2px 4px;border-collapse:collapse;caption-side:bottom", divm, &cs);
    CHECK(cs.border_spacing[0] == OL_CSSPX(2) && cs.border_spacing[1] == OL_CSSPX(4) &&
          cs.border_collapse == GFCSS_COLLAPSE_BORDERS && cs.caption_side == GFCSS_CAPTION_BOTTOM);
    lengths_px("margin:inherit", divm, &cs);
    CHECK(gfcss_is_set(&cs, GFCSS_P_MARGIN_LEFT));
    lengths_px("display:inline-block;position:absolute;float:right;clear:both;box-sizing:border-box", divm, &cs);
    CHECK(cs.display == GFCSS_DISPLAY_INLINE_BLOCK && cs.position == GFCSS_POSITION_ABSOLUTE &&
          cs.floating == GFCSS_FLOAT_RIGHT && cs.clear == GFCSS_CLEAR_BOTH && cs.box_sizing == GFCSS_BORDER_BOX);
    lengths_px("text-transform:uppercase;white-space:nowrap;vertical-align:-2px;text-indent:2em;word-spacing:1px", divm, &cs);
    CHECK(cs.text_transform == GFCSS_UPPERCASE && cs.white_space == GFCSS_WS_NOWRAP &&
          cs.vertical_align == GFCSS_VALIGN_LENGTH && cs.vertical_align_length.units == OL_CSSPX(-2) &&
          cs.text_indent.units == OL_CSSPX(40) && cs.word_spacing == OL_CSSPX(1));
    lengths_px("cursor:url(a.cur) 2 3, url(b.cur), pointer", divm, &cs);
    CHECK(cs.cursor == GFCSS_CURSOR_POINTER);
    lengths_px("font-variant:small-caps;font-style:oblique 10deg", divm, &cs);
    CHECK(cs.font_variant == GFCSS_VARIANT_SMALL_CAPS && cs.font_style == GFCSS_FONT_OBLIQUE);
}

static void test_parsing(void)
{
    gfcss_engine *e;
    gfcss_sheet *s;
    gfcss_computed cs;
    static const char *bad[] = {
        "", "{", "}", "p{", "p{color:", "p{color:red", "p{;;;}", "@", "@media", "@media screen{",
        "p{color:red}}}}p{", "/* unclosed", "p{color:'unclosed", "p{background:url(a b c)}", "[[[",
        "@import", "@import url(", "p{color:rgb(}", "p{color:rgb(1,2,3", "p{x:calc((((}", "\\", "#{}",
        "p{color:red;;color:}", "@supports (", "@media (min-width:", "p{--x:1}", "p:not({}", "p:nth-child(){}",
        "a{b:c{d:e}}f{color:red}", "<!-- p{color:red} -->", "@font-face{src:url(x)}"
    };
    size_t i;
    /* nothing breaks it; the engine still works after */
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        e = engine_with(bad[i]);
        style_of(e, p1, &cs);
        gfcss_free(e);
    }
    CHECK(1);
    /* recovery: what is good around a mistake stays */
    CHECK(colour_under("p{color:red;margin:5;}", p1) == RED);
    CHECK(colour_under("p{margin:5;color:red}", p1) == RED);
    CHECK(colour_under("p{color:red; @foo bar; }", p1) == RED);
    CHECK(colour_under("p{color:red; foo{bar} }", p1) == RED);
    CHECK(colour_under("p{color:red green}", p1) == BLACK);
    CHECK(colour_under("@unknown foo { p{color:green} } p{color:red}", p1) == RED);
    CHECK(colour_under("@unknown foo; p{color:red}", p1) == RED);
    CHECK(colour_under("@font-face{font-family:x;src:url(y)} p{color:red}", p1) == RED);
    CHECK(colour_under("@keyframes k{from{color:green}to{color:blue}} p{color:red}", p1) == RED);
    CHECK(colour_under("@page :first{margin:1in} p{color:red}", p1) == RED);
    CHECK(colour_under("@charset \"utf-8\"; p{color:red}", p1) == RED);
    CHECK(colour_under("<!-- p{color:red} -->", p1) == RED);
    CHECK(colour_under("p{color:green} p{color:red", p1) == RED);   /* an unclosed block ends at the end */
    CHECK(colour_under("p{color:green} p{content:'}'; color:red}", p1) == RED);
    CHECK(colour_under("p{color:green} /* p{color:red} */", p1) == GREEN);
    CHECK(colour_under("p/**/.first{color:red}", p1) == RED);
    CHECK(colour_under("p{COLOR:RED}", p1) == RED);
    /* a bad string: its declaration runs on to the block's end */
    CHECK(colour_under("p{color:red} p{color:blue;x:\"unclosed\n color:green}", p1) == BLUE);
    CHECK(colour_under("p{color:\\72 ed}", p1) == RED);     /* an escape in a keyword */
    CHECK(colour_under("p { color : red ; }", p1) == RED);
    CHECK(colour_under("p{color:red} p{color:url(x)}", p1) == RED);
    CHECK(colour_under("p{color:red} x{}{color:green}", p1) == RED);
    /* @supports */
    CHECK(colour_under("@supports (display:flex){p{color:red}}", p1) == RED);
    CHECK(colour_under("@supports (display:grid-lanes){p{color:red}}", p1) == BLACK);
    CHECK(colour_under("@supports not (display:grid-lanes){p{color:red}}", p1) == RED);
    CHECK(colour_under("@supports (color:red) and (margin:1px){p{color:red}}", p1) == RED);
    CHECK(colour_under("@supports (foo:bar) or (margin:1px){p{color:red}}", p1) == RED);
    CHECK(colour_under("@supports selector(p){p{color:red}}", p1) == BLACK);
    /* @import */
    e = gfcss_new();
    s = gfcss_sheet_new(e, GFCSS_ORIGIN_AUTHOR);
    CHECK(parse_str(s, "@charset 'x'; @import url(\"a.css\"); @import 'b\\.css' print;"
                            "@import url(c.css) screen and (min-width:100px); p{color:green}"
                            "@import 'late.css';"));
    CHECK(gfcss_sheet_import_count(s) == 3);
    CHECK(!strcmp(gfcss_sheet_import_url(s, 0), "a.css") && !strcmp(gfcss_sheet_import_url(s, 1), "b.css") &&
          !strcmp(gfcss_sheet_import_url(s, 2), "c.css"));
    CHECK(gfcss_sheet_import_applies(s, 0) && !gfcss_sheet_import_applies(s, 1) && !gfcss_sheet_import_applies(s, 2));
    gfcss_set_media(e, GFCSS_MEDIA_SCREEN, OL_CSSPX(200), OL_CSSPX(100));
    CHECK(gfcss_sheet_import_applies(s, 2));
    {
        /* an import's rules come before the sheet's own */
        gfcss_sheet *a = gfcss_sheet_import(s, 0), *b = gfcss_sheet_import(s, 1);
        CHECK(a && b && parse_str(a, "p{color:red;margin:1px}"));
        CHECK(parse_str(b, "p{padding:1px}"));
        style_of(e, p1, &cs);
        CHECK(cs.color == GREEN && cs.margin[0].units == OL_CSSPX(1) && cs.padding[0].units == 0);
        gfcss_set_media(e, GFCSS_MEDIA_PRINT, 0, 0);
        style_of(e, p1, &cs);
        CHECK(cs.padding[0].units == OL_CSSPX(1));
    }
    gfcss_free(e);
    /* the sheet keeps its own copy of the text */
    {
        char text[] = "p{content:'xy'; font-family:'Abc'}";
        gfcss_computed b;
        e = engine_with(text);
        memset(text, 'z', sizeof(text) - 1);
        style_of(e, p1, &b);
        CHECK(b.font_family.length == 3 && !memcmp(b.font_family.text, "Abc", 3));
        gfcss_free(e);
    }
    /* unescaping */
    {
        char out[32];
        CHECK(gfcss_unescape("a\\62 c", 6, out, sizeof(out)) == 3 && !strcmp(out, "abc"));
        CHECK(gfcss_unescape("\\e9", 3, out, sizeof(out)) == 2 && !strcmp(out, "\xc3\xa9"));
        CHECK(gfcss_unescape("\\1F600", 6, out, sizeof(out)) == 4);
        CHECK(gfcss_unescape("\\\"q", 3, out, sizeof(out)) == 2 && !strcmp(out, "\"q"));
        CHECK(gfcss_unescape("abcdef", 6, out, 4) == 6 && !strcmp(out, "abc"));
    }
}

/* random pieces of CSS, and random bytes: nothing may break (run under the sanitizers) */
static void test_fuzz(void)
{
    static const char *bits[] = {
        "p", "div", ".a", "#b", "*", " ", ">", "+", "~", ",", "{", "}", "(", ")", "[", "]", ":", "::", ";",
        "color", "margin", "font", "background", "border", "calc(", "rgb(", "hsl(", "url(", "'", "\"", "\\",
        "/*", "*/", "@media", "@import", "@supports", "@font-face", "!important", "1px", "-2.5em", "50%",
        "1e9", "red", "#fff", "inherit", "not", "and", "only", "(min-width:1px)", ":not(", ":nth-child(",
        "2n+1", "odd", "=", "~=", "|=", "^=", "$=", "*=", " i", "\n", "-", "--x", "<!--", "-->", "0", "/",
        "linear-gradient(", "content", "attr(", "\xc3\xa9", "\x01", "@", "#"
    };
    unsigned long seed = 12345;
    int round, k;
    char css[600];
    for (round = 0; round < 3000; ++round) {
        size_t n = 0;
        gfcss_engine *e;
        gfcss_computed cs;
        int pieces = 1 + (int)(seed % 40);
        for (k = 0; k < pieces; ++k) {
            const char *b;
            size_t bl;
            seed = seed * 1103515245u + 12345u;
            if (round % 5 == 4) {
                css[n++] = (char)(seed >> 16);      /* bytes, any */
                continue;
            }
            b = bits[(seed >> 16) % (sizeof(bits) / sizeof(bits[0]))];
            bl = strlen(b);
            if (n + bl >= sizeof(css) - 1) break;
            memcpy(css + n, b, bl);
            n += bl;
        }
        css[n] = 0;
        e = gfcss_new();
        gfcss_sheet_parse(gfcss_sheet_new(e, GFCSS_ORIGIN_AUTHOR), css, n);
        divm->style = css;
        style_of(e, p1, &cs);
        style_of(e, divm, &cs);
        divm->style = NULL;
        gfcss_free(e);
    }
    /* very deep nesting: no recursion runs away */
    {
        static char deep[200000];
        gfcss_engine *e;
        gfcss_computed cs;
        size_t n;
        memset(deep, '(', sizeof(deep) - 1);
        memcpy(deep, "p{color:red} p{x:", 17);
        e = engine_with(deep);
        style_of(e, p1, &cs);
        CHECK(cs.color == RED);
        gfcss_free(e);
        for (n = 0; n + 5 < sizeof(deep) - 1; n += 5) memcpy(deep + n, ":not(", 5);
        e = engine_with(deep);
        gfcss_free(e);
        memcpy(deep, "@supports ", 10);
        e = engine_with(deep);
        gfcss_free(e);
    }
    CHECK(1);
}

static void test_media(void)
{
    static const char css[] =
        "p{color:black}"
        "@media screen{p.first{color:red}}"
        "@media print{p.first{color:green}}"
        "@media all and (min-width:600px){#lastp{color:red}}"
        "@media (max-width:599px){#lastp{color:green}}"
        "@media only screen and (min-width:400px) and (max-width:800px){span{color:red}}"
        "@media not print{li{color:red}}"
        "@media tv, (orientation:landscape){ul{color:red}}"
        "@media (orientation:portrait){ul{color:green}}"
        "@media screen and (min-height:30em){body{color:blue}}"
        "@media screen{@media (min-width:700px){div{color:red}}}"
        "@media (min-resolution:2dppx){html{color:green}}"
        "@media screen and{html{color:red}}";
    gfcss_engine *e = engine_with(css);
    gfcss_computed cs;
    gfcss_set_media(e, GFCSS_MEDIA_SCREEN, OL_CSSPX(640), OL_CSSPX(480));
    style_of(e, p1, &cs); CHECK(cs.color == RED);
    style_of(e, p3, &cs); CHECK(cs.color == RED);
    style_of(e, span1, &cs); CHECK(cs.color == RED);
    style_of(e, li[0], &cs); CHECK(cs.color == RED);
    style_of(e, ul, &cs); CHECK(cs.color == RED);
    style_of(e, body, &cs); CHECK(cs.color == BLUE);        /* 480px is 30em */
    gfcss_set_media(e, GFCSS_MEDIA_SCREEN, OL_CSSPX(640), OL_CSSPX(479));
    style_of(e, body, &cs); CHECK(cs.color == BLACK);
    gfcss_set_media(e, GFCSS_MEDIA_SCREEN, OL_CSSPX(640), OL_CSSPX(500));
    style_of(e, body, &cs); CHECK(cs.color == BLUE);
    style_of(e, divm, &cs); CHECK(cs.color == BLUE);         /* nested @media: 640 < 700 */
    style_of(e, html, &cs); CHECK(cs.color == BLACK);        /* unknown feature, "and" at the end: false */
    gfcss_set_media(e, GFCSS_MEDIA_SCREEN, OL_CSSPX(720), OL_CSSPX(900));
    style_of(e, divm, &cs); CHECK(cs.color == RED);
    style_of(e, ul, &cs); CHECK(cs.color == GREEN);
    gfcss_set_media(e, GFCSS_MEDIA_PRINT, OL_CSSPX(500), OL_CSSPX(900));
    style_of(e, p1, &cs); CHECK(cs.color == GREEN);
    style_of(e, p3, &cs); CHECK(cs.color == GREEN);
    style_of(e, span1, &cs); CHECK(cs.color == BLACK);
    style_of(e, li[0], &cs); CHECK(cs.color == GREEN);      /* not print: no; its ul's portrait: yes */
    gfcss_set_media(e, GFCSS_MEDIA_SCREEN, 0, 0);           /* a size not known: features are false */
    style_of(e, p3, &cs); CHECK(cs.color == BLACK);
    style_of(e, p1, &cs); CHECK(cs.color == RED);
    gfcss_free(e);
}

static int ext_colour(void *ud, int slot, const char *v, size_t n, uint32_t *out)
{
    (void)ud;
    (void)slot;
    return gfcss_parse_colour(v, n, out);
}

static void test_extensions(void)
{
    gfcss_engine *e = gfcss_new();
    gfcss_sheet *s;
    gfcss_computed cs;
    int accent, mark;
    CHECK(gfcss_add_state(e, "selected", GFCSS_STATE_USER(0)));
    CHECK(gfcss_add_state(e, "open", GFCSS_STATE_USER(1)));
    accent = gfcss_add_property(e, "accent-color", GFCSS_INHERITED, 0xff3366ccu, ext_colour, NULL);
    mark = gfcss_add_property(e, "mark-background", 0, 0xffffffffu, ext_colour, NULL);
    CHECK(accent == 0 && mark == 1);
    s = gfcss_sheet_new(e, GFCSS_ORIGIN_AUTHOR);
    CHECK(parse_str(s, "body{accent-color:red;mark-background:blue}"
                            "li:selected{color:red} ul:open > li{color:green}"
                            "p{accent-color:nonsense} #lastp{accent-color:inherit;mark-background:inherit}"
                            "span{accent-color:initial}"));
    CHECK(gfcss_sheet_states(s) == (GFCSS_STATE_USER(0) | GFCSS_STATE_USER(1)));
    style_of(e, body, &cs);
    CHECK(cs.ext[accent] == RED && cs.ext[mark] == BLUE && gfcss_is_set(&cs, GFCSS_P_COUNT + accent));
    style_of(e, divm, &cs);
    CHECK(cs.ext[accent] == RED && cs.ext[mark] == 0xffffffffu);   /* one inherits, one doesn't */
    style_of(e, p1, &cs);
    CHECK(cs.ext[accent] == RED);
    style_of(e, p3, &cs);
    CHECK(cs.ext[mark] == 0xffffffffu);
    style_of(e, span1, &cs);
    CHECK(cs.ext[accent] == 0xff3366ccu);
    li[1]->state = GFCSS_STATE_USER(0);
    style_of(e, li[1], &cs);
    CHECK(cs.color == RED);
    ul->state = GFCSS_STATE_USER(1);
    style_of(e, li[2], &cs);
    CHECK(cs.color == GREEN);
    style_of(e, li[1], &cs);
    CHECK(cs.color == GREEN);                   /* ul:open > li (0,1,2) is more specific than li:selected */
    li[1]->state = 0;
    ul->state = 0;
    gfcss_free(e);
    /* a state nobody named: the rule is left out */
    CHECK(colour_under("li:selected{color:red}", li[0]) == BLACK);
}

static void test_pseudo_elements(void)
{
    gfcss_engine *e = engine_with("p.first::before{content:\"\\2022  \";color:red}"
                                  "p.first:after{content:'!'}"
                                  "p.last::before{content:none}"
                                  "span::after{content:'x';display:none}"
                                  "p::first-line{font-weight:bold}"
                                  "p.first{color:green}");
    gfcss_computed el, ps;
    char buf[16];
    gfcss_computed_init(&ps);
    style_of(e, p1, &el);
    CHECK(el.color == GREEN);                   /* ::before's rules aren't the element's */
    CHECK(gfcss_compute(e, &ops, NULL, p1, GFCSS_PSEUDO_BEFORE, &el, &ps) == 1);
    CHECK(ps.color == RED && ps.content.kind == GFCSS_TEXT_STRING);
    CHECK(gfcss_text_copy(&ps.content, buf, sizeof(buf)) == 4 && !strcmp(buf, "\xe2\x80\xa2 "));
    CHECK(gfcss_compute(e, &ops, NULL, p1, GFCSS_PSEUDO_AFTER, &el, &ps) == 1 && ps.color == GREEN);
    CHECK(gfcss_compute(e, &ops, NULL, p3, GFCSS_PSEUDO_BEFORE, &el, &ps) == 0);
    CHECK(gfcss_compute(e, &ops, NULL, p3, GFCSS_PSEUDO_AFTER, &el, &ps) == 0);
    CHECK(gfcss_compute(e, &ops, NULL, span1, GFCSS_PSEUDO_AFTER, &el, &ps) == 0);
    CHECK(gfcss_compute(e, &ops, NULL, p2, GFCSS_PSEUDO_FIRST_LINE, &el, &ps) == 1 && ps.font_weight == 700);
    gfcss_free(e);
}

static void test_to_ol(void)
{
    gfcss_engine *e = engine_with(
        "#main{display:block;margin:10px auto 0 5%;padding:2px 4px;border:1px solid red;border-left:3px solid blue;"
        "width:200px;height:50px;font:bold italic 15px/2 monospace;color:#123456;background:#abcdef;"
        "text-align:center;text-indent:1em;text-decoration:underline}"
        "p.first{display:flex;justify-content:center;align-items:center;gap:3px;box-sizing:border-box;width:50%}"
        "p.last{display:flex;flex-direction:column;row-gap:7px;flex:3;vertical-align:super}"
        "span{display:inline;text-align:right}"
        "li{display:none}"
        "p{margin:0 auto;width:100px}");
    gfcss_computed cs;
    ol_style st;
    ol_box_style box;
    style_of(e, divm, &cs);
    gfcss_to_ol(&cs, OL_CSSPX(400), &st, &box);
    CHECK(st.display == OL_DISPLAY_BLOCK);
    CHECK(st.margin_top == OL_CSSPX(10) && st.margin_bottom == 0 && st.margin_left == OL_CSSPX(20));
    CHECK(st.padding_left == OL_CSSPX(4) && st.padding_top == OL_CSSPX(2));
    CHECK(st.font_size == OL_CSSPX(15) && st.line_height == OL_CSSPX(30));
    CHECK(st.text_flags == (OL_TEXT_BOLD | OL_TEXT_ITALIC | OL_TEXT_MONO | OL_TEXT_UNDERLINE));
    CHECK(st.foreground == 0xff123456u && st.background == 0xffabcdefu && st.text_align == OL_ALIGN_CENTER);
    CHECK(box.text_indent == OL_CSSPX(15));
    CHECK(box.border_width == OL_CSSPX(3) && box.border_argb == BLUE && box.border_sides == OL_BORDER_ALL);
    /* content box: the padding and borders are added to the width */
    CHECK(box.width == OL_CSSPX(200 + 8 + 1 + 3) && box.height == OL_CSSPX(50 + 4 + 2));
    style_of(e, p1, &cs);
    gfcss_to_ol(&cs, OL_CSSPX(400), &st, &box);
    CHECK((box.flags & OL_BOX_ROW) && st.text_align == OL_ALIGN_CENTER && box.align == OL_CROSS_CENTRE &&
          box.gap == OL_CSSPX(3));
    CHECK(box.width == OL_CSSPX(200));
#ifndef OL_HAS_WEB_BOXES
    CHECK(st.margin_left == OL_CSSPX(100) && st.margin_right == OL_CSSPX(100));    /* centred */
#endif
    style_of(e, p3, &cs);
    gfcss_to_ol(&cs, OL_CSSPX(400), &st, &box);
    CHECK(!(box.flags & OL_BOX_ROW) && box.gap == OL_CSSPX(7) && box.grow == 3 && (st.text_flags & OL_TEXT_SUPER));
    style_of(e, span1, &cs);
    gfcss_to_ol(&cs, OL_CSSPX(400), &st, &box);
    CHECK(st.display == OL_DISPLAY_INLINE && st.text_align == OL_ALIGN_END && box.width == 0);
    style_of(e, li[0], &cs);
    gfcss_to_ol(&cs, OL_CSSPX(400), &st, &box);
    CHECK(st.display == OL_DISPLAY_NONE);
    style_of(e, ul, &cs);
    gfcss_to_ol(&cs, OL_CSSPX(400), &st, &box);
    CHECK(st.line_height == OL_CSSPX(16) * GFCSS_LINE_NORMAL_MILLI / 1000 && box.border_width == 0);
    gfcss_free(e);
}

/* a smaller gfcss_computed (a program built for an earlier GFCSS) is written no further than its size */
static void test_sizes(void)
{
    gfcss_engine *e = engine_with("p{color:red;content:'x'}");
    unsigned char buf[sizeof(gfcss_computed) + 16];
    gfcss_computed *cs = (gfcss_computed *)(void *)buf;
    size_t small = offsetof(gfcss_computed, opacity);
    memset(buf, 0xee, sizeof(buf));
    cs->size = (uint32_t)small;
    CHECK(gfcss_compute(e, &ops, NULL, p1, 0, NULL, cs));
    CHECK(cs->color == RED && cs->size == small);
    CHECK(buf[small] == 0xee && buf[sizeof(gfcss_computed) - 1] == 0xee);
    gfcss_free(e);
}

/* ---- 2,000 rules over 2,000 elements ---- */
static void test_speed(void)
{
    char *css = (char *)malloc(400000);
    size_t n = 0;
    int i;
    gfcss_engine *e;
    gfcss_sheet *s;
    tel *root, *sec[20];
    gfcss_computed *styles;
    clock_t t0, t1, t2;
    long matched = 0;
    if (!css) return;
    for (i = 0; i < 2000; ++i) {
        switch (i % 8) {
        case 0: n += (size_t)sprintf(css + n, ".c%d{color:#%06x;margin:%dpx}\n", i, i * 97, i % 9); break;
        case 1: n += (size_t)sprintf(css + n, "#e%d{padding:1px 2px}\n", i); break;
        case 2: n += (size_t)sprintf(css + n, "div.c%d > p.c%d{font-size:%dpx}\n", i, i - 1, 10 + i % 7); break;
        case 3: n += (size_t)sprintf(css + n, "section .c%d span{color:red}\n", i); break;
        case 4: n += (size_t)sprintf(css + n, "p:nth-child(%dn+1).c%d{border:1px solid}\n", i % 5 + 1, i); break;
        case 5: n += (size_t)sprintf(css + n, "[data-k=\"v%d\"]{width:%d%%}\n", i, i % 100); break;
        case 6: n += (size_t)sprintf(css + n, "@media screen{.c%d:hover{background:#eee}}\n", i); break;
        default: n += (size_t)sprintf(css + n, "ul li.c%d a, .c%d::before{text-decoration:none}\n", i, i); break;
        }
    }
    n += (size_t)sprintf(css + n, "p{line-height:1.4}div{display:block}span{font-weight:bold}");
    npool = 0;
    t0 = clock();
    e = gfcss_new();
    s = gfcss_sheet_new(e, GFCSS_ORIGIN_AUTHOR);
    CHECK(gfcss_sheet_parse(s, css, n));
    CHECK(gfcss_sheet_rules(s) >= 2000);
    root = mk(NULL, "html", NULL, NULL);
    for (i = 0; i < 20; ++i) sec[i] = mk(root, "section", NULL, NULL);
    while (npool < 2000) {
        static char ids[2000][8], cls[2000][16];
        int k = npool;
        tel *par = sec[k % 20];
        tel *el;
        if (k % 3 == 0 && par->last) par = par->last;
        sprintf(ids[k], "e%d", k);
        sprintf(cls[k], "c%d c%d", k % 2000, (k * 7) % 2000);
        el = mk(par, k % 4 == 0 ? "div" : k % 4 == 1 ? "p" : k % 4 == 2 ? "span" : "a", ids[k], cls[k]);
        (void)el;
    }
    styles = (gfcss_computed *)malloc((size_t)npool * sizeof(gfcss_computed));
    if (!styles) {
        free(css);
        gfcss_free(e);
        return;
    }
    t1 = clock();
    /* elements were made parents first: each one's parent is before it */
    for (i = 0; i < npool; ++i) {
        const tel *el = &pool[i];
        const gfcss_computed *parent = el->parent ? &styles[el->parent - pool] : NULL;
        gfcss_computed_init(&styles[i]);
        gfcss_compute(e, &ops, NULL, el, 0, parent, &styles[i]);
        if (gfcss_is_set(&styles[i], GFCSS_P_COLOR) || gfcss_is_set(&styles[i], GFCSS_P_PADDING_TOP)) ++matched;
    }
    t2 = clock();
    printf("speed: %lu rules parsed and indexed in %ld ms; %d elements styled in %ld ms (%ld with rules)\n",
           (unsigned long)gfcss_sheet_rules(s), (long)((t1 - t0) * 1000 / CLOCKS_PER_SEC), npool,
           (long)((t2 - t1) * 1000 / CLOCKS_PER_SEC), matched);
    CHECK(matched > 100);
    CHECK((t2 - t1) * 1000 / CLOCKS_PER_SEC < 2000);
    free(styles);
    free(css);
    gfcss_free(e);
}

int main(void)
{
    build_tree();
    test_selectors();
    test_cascade();
    test_inheritance();
    test_units();
    test_colours();
    test_shorthands();
    test_parsing();
    test_fuzz();
    test_media();
    test_extensions();
    test_pseudo_elements();
    test_to_ol();
    test_sizes();
    test_speed();
    printf("GFCSS %d checks, %d failed\n", checks, failed);
    if (failed) return 20;
    printf("GFCSS PASS version=%s\n", GFCSS_VERSION);
    return 0;
}
