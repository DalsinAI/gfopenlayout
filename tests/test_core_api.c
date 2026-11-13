/* gfopenlayout_core.h, the ROM core's API (wave 2 review rule 2): the header stands alone in C89, C99 and C++,
 * the openintuition hook table has exactly its 18 entries in openintuition's order, the back end table its 11, functions with
 * openintuition's hook shapes go into it as they are, and pens never look like colours.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_core.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

/* functions shaped as openintuition's shell's (openintuition_lib.c), with its types as void * */
static void *h_obtain_rp(void *e, void *g) { (void)e; return g; }
static void h_release_rp(void *e, void *rp) { (void)e; (void)rp; }
static void h_draw_image_state(void *e, void *rp, void *im, int32_t x, int32_t y, uint32_t st, void *dri)
{ (void)e; (void)rp; (void)im; (void)x; (void)y; (void)st; (void)dri; }
static void h_erase_rect(void *e, void *rp, int32_t a, int32_t b, int32_t c, int32_t d)
{ (void)e; (void)rp; (void)a; (void)b; (void)c; (void)d; }
static void h_fill_rect(void *e, void *rp, uint32_t p, int32_t a, int32_t b, int32_t c, int32_t d)
{ (void)e; (void)rp; (void)p; (void)a; (void)b; (void)c; (void)d; }
static void h_text(void *e, void *rp, uint32_t f, uint32_t b, uint32_t m, void *font, int tf, int32_t x, int32_t y,
                   const char *s, uint32_t n)
{ (void)e; (void)rp; (void)f; (void)b; (void)m; (void)font; (void)tf; (void)x; (void)y; (void)s; (void)n; }
static int32_t h_text_length(void *e, void *font, int tf, const char *s, uint32_t n)
{ (void)e; (void)font; (void)tf; (void)s; return (int32_t)n * 8; }
static int32_t h_font_height(void *e, void *font, int tf) { (void)e; (void)font; (void)tf; return 8; }
static void h_polyline(void *e, void *rp, uint32_t f, uint32_t b, uint32_t m, int32_t x, int32_t y, const int16_t *xy,
                       uint32_t n)
{ (void)e; (void)rp; (void)f; (void)b; (void)m; (void)x; (void)y; (void)xy; (void)n; }
static void h_blit_image(void *e, void *rp, void *im, int32_t x, int32_t y) { (void)e; (void)rp; (void)im; (void)x; (void)y; }
static void h_fill_pattern(void *e, void *rp, uint32_t f, uint32_t b, uint32_t m, const uint16_t *pat, int32_t ps,
                           int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{ (void)e; (void)rp; (void)f; (void)b; (void)m; (void)pat; (void)ps; (void)x0; (void)y0; (void)x1; (void)y1; }
static int h_deep(void *e, void *dri) { (void)e; (void)dri; return 1; }
static void *h_limit_rect(void *e, void *rp, int32_t a, int32_t b, int32_t c, int32_t d, void **old)
{ (void)e; (void)rp; (void)a; (void)b; (void)c; (void)d; *old = 0; return 0; }
static void h_limit_end(void *e, void *rp, void *l, void *old) { (void)e; (void)rp; (void)l; (void)old; }
static void h_set_font(void *w, void *rp, void *font) { (void)w; (void)rp; (void)font; }
static void h_complement(void *w, void *rp, int32_t a, int32_t b, int32_t c, int32_t d)
{ (void)w; (void)rp; (void)a; (void)b; (void)c; (void)d; }
static void h_blit_bitmap(void *w, void *rp, void *bm, int32_t x, int32_t y, int32_t ww, int32_t hh)
{ (void)w; (void)rp; (void)bm; (void)x; (void)y; (void)ww; (void)hh; }
static void h_scroll_raster(void *w, void *rp, int32_t dx, int32_t dy, int32_t a, int32_t b, int32_t c, int32_t d)
{ (void)w; (void)rp; (void)dx; (void)dy; (void)a; (void)b; (void)c; (void)d; }

int main(void)
{
    /* in openintuition's order: struct oi_env's fourteen, then struct oi_wenv's four */
    static const size_t at[OLC_OI_HOOKS] = {
        offsetof(olc_oi_hooks, obtain_rp), offsetof(olc_oi_hooks, release_rp),
        offsetof(olc_oi_hooks, draw_image_state), offsetof(olc_oi_hooks, erase_rect),
        offsetof(olc_oi_hooks, fill_rect), offsetof(olc_oi_hooks, text), offsetof(olc_oi_hooks, text_length),
        offsetof(olc_oi_hooks, font_height), offsetof(olc_oi_hooks, polyline), offsetof(olc_oi_hooks, blit_image),
        offsetof(olc_oi_hooks, fill_pattern), offsetof(olc_oi_hooks, deep), offsetof(olc_oi_hooks, limit_rect),
        offsetof(olc_oi_hooks, limit_end), offsetof(olc_oi_hooks, set_font), offsetof(olc_oi_hooks, complement),
        offsetof(olc_oi_hooks, blit_bitmap), offsetof(olc_oi_hooks, scroll_raster)
    };
    olc_oi_hooks t;
    int i;
    olc_box b;

    /* exactly 18 pointers, nothing between them, in that order */
    CHECK(sizeof(olc_oi_hooks) == OLC_OI_HOOKS * sizeof(void (*)(void)));
    for (i = 0; i < OLC_OI_HOOKS; i++) CHECK(at[i] == (size_t)i * sizeof(void (*)(void)));

    /* openintuition-shaped functions go in as they are (a changed shape fails to build with -Werror) */
    memset(&t, 0, sizeof t);
    t.obtain_rp = h_obtain_rp;
    t.release_rp = h_release_rp;
    t.draw_image_state = h_draw_image_state;
    t.erase_rect = h_erase_rect;
    t.fill_rect = h_fill_rect;
    t.text = h_text;
    t.text_length = h_text_length;
    t.font_height = h_font_height;
    t.polyline = h_polyline;
    t.blit_image = h_blit_image;
    t.fill_pattern = h_fill_pattern;
    t.deep = h_deep;
    t.limit_rect = h_limit_rect;
    t.limit_end = h_limit_end;
    t.set_font = h_set_font;
    t.complement = h_complement;
    t.blit_bitmap = h_blit_bitmap;
    t.scroll_raster = h_scroll_raster;
    {
        void (*const *p)(void) = (void (*const *)(void))(const void *)&t;
        for (i = 0; i < OLC_OI_HOOKS; i++) CHECK(p[i] != 0);
    }
    CHECK(t.text_length(0, 0, 0, "abc", 3) == 24);

    /* the back end's table: a version, its context, then eleven calls in this order (ABI) */
    {
        static const size_t ac[] = {
            offsetof(olc_accel, begin), offsetof(olc_accel, bind), offsetof(olc_accel, upload),
            offsetof(olc_accel, clip), offsetof(olc_accel, fill), offsetof(olc_accel, copy),
            offsetof(olc_accel, composite), offsetof(olc_accel, submit), offsetof(olc_accel, abort),
            offsetof(olc_accel, retire), offsetof(olc_accel, detach)
        };
        size_t first = offsetof(olc_accel, begin);
        CHECK(offsetof(olc_accel, version) == 0 && offsetof(olc_accel, ctx) < first);
        for (i = 0; i < 11; i++) CHECK(ac[i] == first + (size_t)i * sizeof(void (*)(void)));
        CHECK(sizeof(olc_accel) == first + 11 * sizeof(void (*)(void)));
        CHECK(OLC_ACCEL_VERSION == 1);
    }

    /* pens and colours */
    CHECK(OLC_IS_PEN(OLC_PEN(0)) && OLC_IS_PEN(OLC_PEN(255)));
    CHECK(!OLC_IS_PEN(0xFF000000UL) && !OLC_IS_PEN(0xFFFFFFFFUL) && !OLC_IS_PEN(0x00000000UL));
    CHECK(!OLC_IS_PEN(0x80FF0000UL));               /* half see-through red */
    CHECK((OLC_PEN(300) & 0xFFUL) == (300 & 0xFF));  /* a pen number past 255 doesn't spill into the alpha */

    /* the enums keep their numbers (they are in the ROM's ABI) */
    CHECK(OLC_FMT_CLUT8 == 1 && OLC_FMT_ARGB32 == 4);
    CHECK(OLC_MEM_ANY == 0 && OLC_MEM_RTG == 3);
    CHECK(OLC_SCENE_DIRECT == 0 && OLC_SCENE_COMPOSITED == 1);
    CHECK(OLC_LOOK_OPACITY == 1 && OLC_LOOK_HIDDEN == 3);
    CHECK(OLC_VERSION_MAJOR == 1);

    /* a box is four 32-bit numbers, both corners inside */
    CHECK(sizeof b == 16);
    b.x0 = 0; b.y0 = 0; b.x1 = 0; b.y1 = 0;
    CHECK(b.x1 - b.x0 + 1 == 1);

    printf("test_core_api: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
