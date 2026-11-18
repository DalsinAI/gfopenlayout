/* Made by tools/gen-library.py from the headers: don't edit. GFOpenLayout drawing
 * (gfopenlayout_draw.h) through gfopenlayout.library 25: a program links this
 * (libgfopenlayout_link.a) where it compiled draw/oldraw.c and amiga/oldraw_amiga.c in;
 * gfopenlayout_link_base.c opens the library for it, or the program has its own
 * OpenLayoutBase. On an older library the first call stops the program and says which it
 * needs. What an oldraw_ops target or a redraw is given is called in the program, from the
 * library's call, on the caller's task.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdint.h>
#include <stdlib.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <gfopenlayout_draw.h>
#include <proto/gfopenlayout.h>

static void ol_link_draw_needs(void) __attribute__((noreturn));
static void ol_link_draw_needs(void)
{
    static const char why[] = "This needs gfopenlayout.library 25 or later in LIBS:\n";
    struct Library *dos = OpenLibrary((CONST_STRPTR)"dos.library", 36);
    if (dos) {
        BPTR out = LP0(0x3c, BPTR, Output, , dos);     /* Output() */
        if (out) LP3(0x30, LONG, Write, BPTR, out, d1, APTR, (APTR)why, d2, LONG, (LONG)sizeof(why) - 1, d3, , dos);
        CloseLibrary(dos);
    }
    exit(RETURN_FAIL);
}

oldraw * oldraw_new_rastport(struct RastPort *rp, struct Screen *scr, int width, int height)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_NewRastport(rp, scr, width, height);
}

void oldraw_resize(oldraw *d, int width, int height)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Resize(d, width, height);
}

void oldraw_pens_changed(oldraw *d)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_PensChanged(d);
}

void oldraw_outline_text(oldraw *d, int on)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_OutlineText(d, on);
}

void oldraw_theme_changed(oldraw *d)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_ThemeChanged(d);
}

void oldraw_free(oldraw *d)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Free(d);
}

oldraw * oldraw_new(const oldraw_ops *ops, void *target, int width, int height, const oldraw_colour *pens)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_New(ops, target, width, height, pens);
}

oldraw_colour oldraw_resolve(const oldraw *d, oldraw_colour c)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_Resolve(d, c);
}

void oldraw_damage(oldraw *d, const oldraw_rect *r)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Damage(d, r);
}

void oldraw_damage_all(oldraw *d)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_DamageAll(d);
}

int oldraw_damage_count(const oldraw *d)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_DamageCount(d);
}

int oldraw_damage_get(const oldraw *d, int i, oldraw_rect *r)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_DamageGet(d, i, r);
}

int oldraw_redraw(oldraw *d, oldraw_redraw_fn fn, void *userdata)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_Redraw(d, fn, userdata);
}

void oldraw_clip(oldraw *d, const oldraw_rect *r)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Clip(d, r);
}

void oldraw_flush(oldraw *d)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Flush(d);
}

void oldraw_copy(oldraw *d, const oldraw_rect *src, int dx, int dy)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Copy(d, src, dx, dy);
}

void oldraw_scroll(oldraw *d, const oldraw_rect *area, int ddx, int ddy)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Scroll(d, area, ddx, ddy);
}

void oldraw_fill(oldraw *d, const oldraw_rect *r, oldraw_colour c)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Fill(d, r, c);
}

void oldraw_frame(oldraw *d, const oldraw_rect *r, int recessed)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Frame(d, r, recessed);
}

int oldraw_text(oldraw *d, int x, int y, const char *utf8, size_t length, const oldraw_font *font, oldraw_colour c, unsigned style)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_Text(d, x, y, utf8, length, font, c, style);
}

int oldraw_text_measure(oldraw *d, const char *utf8, size_t length, const oldraw_font *font, unsigned style, oldraw_text_size *size)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_TextMeasure(d, utf8, length, font, style, size);
}

size_t oldraw_text_box(oldraw *d, const oldraw_rect *r, const char *utf8, size_t length, const oldraw_font *font, oldraw_colour c, unsigned style, int align)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_TextBox(d, r, utf8, length, font, c, style, align);
}

oldraw_image * oldraw_image_argb(oldraw *d, int w, int h, const uint32_t *argb, int stride)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_ImageArgb(d, w, h, argb, stride);
}

oldraw_image * oldraw_image_icon(oldraw *d, struct DiskObject *icon)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_ImageIcon(d, icon);
}

int oldraw_image_width(const oldraw_image *img)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_ImageWidth(img);
}

int oldraw_image_height(const oldraw_image *img)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_ImageHeight(img);
}

uint32_t oldraw_image_pixel(const oldraw_image *img, int x, int y)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_ImagePixel(img, x, y);
}

void oldraw_image_free(oldraw *d, oldraw_image *img)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_ImageFree(d, img);
}

int oldraw_image_blocks(const oldraw_image *img, const oldraw_rect **blocks)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_ImageBlocks(img, blocks);
}

void oldraw_image_draw(oldraw *d, const oldraw_image *img, int x, int y, unsigned flags)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_ImageDraw(d, img, x, y, flags);
}

void oldraw_list_row(oldraw *d, const oldraw_rect *r, const oldraw_row *row)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_ListRow(d, r, row);
}

int oldraw_list_cell_box(const oldraw_row *row, const oldraw_rect *r, int i, oldraw_rect *cell)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_ListCellBox(row, r, i, cell);
}

void oldraw_select_box(oldraw *d, int x0, int y0, int x1, int y1)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_SelectBox(d, x0, y0, x1, y1);
}

void oldraw_progress(oldraw *d, const oldraw_rect *r, uint32_t done, uint32_t total, unsigned flags)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Progress(d, r, done, total, flags);
}

int oldraw_progress_fill(int w, uint32_t done, uint32_t total)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_ProgressFill(w, done, total);
}

void oldraw_pie(oldraw *d, int cx, int cy, int r, int start, int sweep, oldraw_colour c)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Pie(d, cx, cy, r, start, sweep, c);
}

int oldraw_pie_has(int r, int start, int sweep, int dx, int dy)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_PieHas(r, start, sweep, dx, dy);
}

void oldraw_line(oldraw *d, int x0, int y0, int x1, int y1, int width, oldraw_colour c)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Line(d, x0, y0, x1, y1, width, c);
}

void oldraw_polyline(oldraw *d, const oldraw_point *pts, int n, int closed, int width, oldraw_colour c)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Polyline(d, pts, n, closed, width, c);
}

void oldraw_polygon(oldraw *d, const oldraw_point *pts, int n, oldraw_colour c)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Polygon(d, pts, n, c);
}

void oldraw_ellipse(oldraw *d, const oldraw_rect *r, oldraw_colour c)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_Ellipse(d, r, c);
}

void oldraw_ellipse_frame(oldraw *d, const oldraw_rect *r, int width, oldraw_colour c)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    OLDraw_EllipseFrame(d, r, width, c);
}

int oldraw_ellipse_has(const oldraw_rect *r, int x, int y)
{
    if (OpenLayoutBase->lib_Version < 25) ol_link_draw_needs();
    return OLDraw_EllipseHas(r, x, y);
}

/* ---- the C call the library doesn't carry (two 64-bit numbers) ---- */
unsigned oldraw_pie_sweep(uint64_t used, uint64_t size)
{
    if (!size) return 0;
    if (used >= size) return 3600;
    if (used > UINT64_MAX / 3600u) return (unsigned)(used / (size / 3600u ? size / 3600u : 1u));
    return (unsigned)(used * 3600u / size);
}
