/* GFOpenLayout's ROM core: the small, versioned API that goes in the thin ROM (Workbench redesign, wave 2 review
 * rule 2, 13 November 2026). Everything above it (CSS, olui, markup, documents, tables, wave 1's oldraw calls)
 * loads on demand from the module set and calls down into it; nothing in the ROM calls up.
 *
 * The core is exactly:
 *   1. the drawing hooks openintuition takes from its shell today (struct olc_oi_hooks, below: struct oi_env's
 *      fourteen and struct oi_wenv's four), so Intuition draws through the core with no change to its cores;
 *   2. surfaces: a layer's pixels, in Fast, host or RTG memory, never Chip RAM;
 *   3. regions: lists of rectangles (damage, clips, what is visible);
 *   4. compose: the visible layers onto the screen, as one batch with one fence, for the damage only;
 *   5. text: one outline rasteriser with a glyph cache.
 *
 * Rules the core keeps (designs/WORKBENCH-REVIEW-SUMMARY-20261113.md):
 *   - Boot is CPU-direct. olc_init probes nothing: no GPU, no fence, no compositor memory. Acceleration is
 *     attached later (olc_accel_attach), once boards and RTG are up.
 *   - Direct mode is today's rasteriser unchanged. A screen is composited only when olc_scene_new asks for it,
 *     and only Workbench and public RTG screens are; custom, private and AGA screens stay direct.
 *   - One GPU batch and one fence per compose. Nothing is done once per primitive, and nothing is read back from
 *     the GPU: a surface's CPU pixels are the master copy.
 *   - olc_compose never waits: not for the fence, nor for drawing to settle.
 *   - Locks (designs/WORKBENCH-REDESIGN-20261113.md, "Lock order"): a scene's lock is number 5, the compositor,
 *     taken after a Layer_Info's and its layers'. The glyph cache's lock is a leaf. No core call waits on DOS,
 *     ramlib or a requester while it holds either.
 *
 * Coordinates are whole pixels. A box is x0, y0 to x1, y1 with both corners inside, as layers and Intuition
 * have them; one with x1 < x0 or y1 < y0 is empty. Colours are 0xAARRGGBB, or OLC_PEN(n), a pen of the target.
 * No call uses floating point.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFOPENLAYOUT_CORE_H
#define GFOPENLAYOUT_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The API's version. A call is only ever added at the end of a table; a version that changes or removes one is a
 * new major version, and the ROM keeps the old one's table beside it. */
#define OLC_VERSION_MAJOR 1
#define OLC_VERSION_MINOR 0
uint32_t olc_version(void);                 /* major << 16 | minor */

typedef struct olc_box {
    int32_t x0, y0, x1, y1;
} olc_box;

typedef uint32_t olc_colour;
/* A colour argument (never image pixels: an ARGB32 surface may hold alpha 1) is a pen when its alpha is 1, which no
 * colour a caller passes uses. */
#define OLC_PEN(n) (0x01000000UL | ((uint32_t)(n) & 0xFFUL))
#define OLC_IS_PEN(c) (((c) & 0xFF000000UL) == 0x01000000UL)

/* ---- start ---- */
/* Called once, by the ROM's init, before any other call. Probes nothing (see above). 0 when out of memory. */
int olc_init(void);

/* ---- 1. openintuition's drawing hooks ----
 * The same names, arguments and meaning as openintuition's struct oi_env (boopsi_core.h) and struct oi_wenv
 * (window_core.h), in that order, with Intuition's own types as void * (rp: a struct RastPort *; image: a
 * struct Image *; bm: a struct BitMap *; font: a struct TextAttr *, or with textfont set a struct TextFont *).
 * env is the caller's (openintuition's env or wenv) and the core never looks inside it. openintuition's shell
 * copies these into its env in place of its own graphics.library calls. Every hook works in direct mode and on
 * a composited screen alike: on one, what it draws marks the layer's damage for the next compose. */
typedef struct olc_oi_hooks {
    /* struct oi_env's */
    void *(*obtain_rp)(void *env, void *ginfo);
    void (*release_rp)(void *env, void *rp);
    void (*draw_image_state)(void *env, void *rp, void *image, int32_t x, int32_t y, uint32_t state,
                             void *drawinfo);
    void (*erase_rect)(void *env, void *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void (*fill_rect)(void *env, void *rp, uint32_t pen, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void (*text)(void *env, void *rp, uint32_t fpen, uint32_t bpen, uint32_t mode, void *font, int textfont,
                 int32_t x, int32_t y, const char *s, uint32_t len);
    int32_t (*text_length)(void *env, void *font, int textfont, const char *s, uint32_t len);
    int32_t (*font_height)(void *env, void *font, int textfont);
    void (*polyline)(void *env, void *rp, uint32_t fpen, uint32_t bpen, uint32_t mode, int32_t x, int32_t y,
                     const int16_t *xy, uint32_t count);
    void (*blit_image)(void *env, void *rp, void *image, int32_t x, int32_t y);
    void (*fill_pattern)(void *env, void *rp, uint32_t fpen, uint32_t bpen, uint32_t mode, const uint16_t *pattern,
                         int32_t patsize, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    int (*deep)(void *env, void *drawinfo);
    void *(*limit_rect)(void *env, void *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, void **old);
    void (*limit_end)(void *env, void *rp, void *limit, void *old);
    /* struct oi_wenv's */
    void (*set_font)(void *wenv, void *rp, void *font);
    void (*complement)(void *wenv, void *rp, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
    void (*blit_bitmap)(void *wenv, void *rp, void *bm, int32_t x, int32_t y, int32_t w, int32_t h);
    void (*scroll_raster)(void *wenv, void *rp, int32_t dx, int32_t dy, int32_t x0, int32_t y0, int32_t x1,
                          int32_t y1);
} olc_oi_hooks;
#define OLC_OI_HOOKS 18
/* The core's own; the same table for the life of the system. */
const olc_oi_hooks *olc_oi(void);

/* ---- 2. surfaces ---- */
typedef struct olc_surface olc_surface;
enum {
    OLC_FMT_CLUT8 = 1,          /* pens, as an 8-bit screen */
    OLC_FMT_RGB16,              /* R5G6B5 */
    OLC_FMT_XRGB32,
    OLC_FMT_ARGB32              /* with its own alpha (a shadow, a rounded corner) */
};
enum {
    OLC_MEM_ANY = 0,            /* RTG memory when the screen's board has room, else Fast, else host */
    OLC_MEM_FAST,
    OLC_MEM_HOST,
    OLC_MEM_RTG
    /* there is no Chip RAM choice: a surface is never in Chip RAM */
};
/* A surface w by h in format fmt, its pixels where says. like: a screen bitmap (struct BitMap *) whose board it
 * should suit, or NULL. NULL when there is no memory. */
olc_surface *olc_surface_new(int32_t w, int32_t h, uint32_t fmt, uint32_t where, void *like);
void olc_surface_free(olc_surface *s);
/* The CPU's pixels (the master copy) and the bytes from one row to the next. The pointer is good until
 * olc_surface_free. Writing them marks nothing: say what changed with olc_surface_damage. */
void *olc_surface_pixels(olc_surface *s, int32_t *bytes_per_row);
void olc_surface_size(const olc_surface *s, int32_t *w, int32_t *h, uint32_t *fmt);
void olc_surface_damage(olc_surface *s, const olc_box *b);
/* A RastPort-shaped target on the surface, for the hooks above and graphics.library's faces; NULL when out of
 * memory. Freed with the surface. */
void *olc_surface_rastport(olc_surface *s);

/* ---- 3. regions ---- */
typedef struct olc_region olc_region;
olc_region *olc_region_new(void);
void olc_region_free(olc_region *r);
void olc_region_clear(olc_region *r);
/* Each answers 0, or -1 when out of memory (the region is then as it was). Boxes in a region never overlap. */
int olc_region_or_box(olc_region *r, const olc_box *b);
int olc_region_and_box(olc_region *r, const olc_box *b);
int olc_region_sub_box(olc_region *r, const olc_box *b);
int olc_region_or(olc_region *r, const olc_region *other);
int olc_region_copy(olc_region *to, const olc_region *from);
int32_t olc_region_count(const olc_region *r);
/* The i-th box, 0 when i is past the end */
int olc_region_box(const olc_region *r, int32_t i, olc_box *b);
/* The smallest box round it all; 0 for an empty region */
int olc_region_bounds(const olc_region *r, olc_box *b);

/* ---- 4. compose ---- */
typedef struct olc_scene olc_scene;
typedef struct olc_layer olc_layer;
enum {
    OLC_SCENE_DIRECT = 0,       /* today's path: layers draw into the screen; nothing is composed */
    OLC_SCENE_COMPOSITED = 1
};
/* A scene on a screen bitmap (struct BitMap *), w by h, direct or composited. A composited scene's layers each
 * have a surface. NULL when out of memory, or when screen can't be composited (it is then direct: ask
 * olc_scene_new again with OLC_SCENE_DIRECT). */
olc_scene *olc_scene_new(void *screen, int32_t w, int32_t h, uint32_t mode);
void olc_scene_free(olc_scene *sc);
uint32_t olc_scene_mode(const olc_scene *sc);
/* A layer at x0, y0 to x1, y1, in front of front (NULL: in front of all). Its surface is the core's. */
olc_layer *olc_layer_new(olc_scene *sc, const olc_box *at, olc_layer *front);
void olc_layer_free(olc_layer *l);
olc_surface *olc_layer_surface(olc_layer *l);
int olc_layer_move(olc_layer *l, const olc_box *at);            /* moved or sized; -1 when out of memory */
void olc_layer_restack(olc_layer *l, olc_layer *front);           /* NULL: to the front */
enum {
    OLC_LOOK_OPACITY = 1,       /* value 0 to 255 */
    OLC_LOOK_SHADOW,            /* value 0 or 1 */
    OLC_LOOK_HIDDEN             /* value 0 or 1 */
};
void olc_layer_look(olc_layer *l, uint32_t what, uint32_t value);
/* Composes damage (screen coordinates; NULL: what the layers marked since the last compose) onto the screen, as
 * one batch with one fence, and returns without waiting for it. Answers 0, 1 when a layer was busy (its lock
 * was taken: that part is composed next time), or -1 when out of memory (compose all next time). On a direct
 * scene it does nothing and answers 0. */
int olc_compose(olc_scene *sc, const olc_region *damage);

/* Acceleration, attached once boards and RTG are up (never at boot); until then the CPU does it all. ops is the
 * GFOpenGPU or AC090 native back end's table (row N1), which stays the caller's. 0 when the core takes it. */
typedef struct olc_accel olc_accel;
int olc_accel_attach(const olc_accel *ops);

/* ---- 5. text ---- */
typedef struct olc_face olc_face;
/* An outline face (a file the core reads once, or one built into the ROM: name NULL) at px pixels high. NULL when
 * it can't be read or there is no memory. Opening reads; it is never called with a lock held (lock rule 3). */
olc_face *olc_face_open(const char *name, int32_t px);
void olc_face_close(olc_face *f);
typedef struct olc_text_size {
    int32_t width, ascent, descent;
} olc_text_size;
/* UTF-8 text with its baseline at x, y on target (a RastPort, or olc_surface_rastport's), cut to clip (NULL: the
 * target), anti-aliased onto what is there on a deep target and with pen colour c. 1 when drawn. */
int olc_text(void *target, int32_t x, int32_t y, const char *utf8, size_t len, olc_face *f, olc_colour c,
             const olc_box *clip);
int olc_text_measure(olc_face *f, const char *utf8, size_t len, olc_text_size *size);
/* The glyph cache's most bytes (0: the core's default). Answers what it was. */
uint32_t olc_glyph_cache_limit(uint32_t bytes);

#ifdef __cplusplus
}
#endif

#endif
