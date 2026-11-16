/* OIHooksTest: olc_oi()'s hooks (core/olc_oi_amiga.c) on the Amiga, run in a lab. Each hook draws into one
 * bitmap, and the shell's own graphics calls (openintuition_lib.c, as they were) into another; the two must be
 * the same bit for bit. Also: the entries Intuition keeps are NULL, and a program's area pattern and draw mode
 * are left as they were. Prints PASS or FAIL lines; RC 0, or 10 on a failure.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olc_core_internal.h"
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>

#define W 96
#define H 40
#define D 4

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static struct BitMap *bma, *bmb;
static struct RastPort ra, rb;

static void clear(void)
{
    SetRast(&ra, 0);
    SetRast(&rb, 0);
}

static int same(void)
{
    int p;
    ULONG bytes = (ULONG)bma->BytesPerRow * bma->Rows;
    if (bma->BytesPerRow != bmb->BytesPerRow) return 0;
    for (p = 0; p < D; p++)
        if (memcmp(bma->Planes[p], bmb->Planes[p], bytes)) return 0;
    return 1;
}

static UWORD pat[2] = { 0xAAAA, 0x5555 };
static UWORD chip_img[2 * 2 * 8];   /* 2 planes, 2 words a row, 8 rows (copied to Chip RAM below) */

int main(void)
{
    const olc_oi_hooks *t = olc_oi();
    UWORD *imgdata;
    struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, 0 };
    short xy[] = { 0, 0, 20, 5, 30, 25, 2, 30 };
    int i;
    printf("OIHooksTest: olc_oi() against the shell's graphics calls\n");
    CHECK(t != NULL);
    if (!t) return 10;
    CHECK(!t->obtain_rp && !t->release_rp && !t->draw_image_state && !t->deep && !t->limit_rect && !t->limit_end);
    CHECK(t->erase_rect && t->fill_rect && t->text && t->text_length && t->font_height && t->polyline &&
          t->blit_image && t->fill_pattern && t->set_font && t->complement && t->blit_bitmap && t->scroll_raster);
    CHECK(olc_oi() == t);
    bma = AllocBitMap(W, H, D, BMF_CLEAR, NULL);
    bmb = AllocBitMap(W, H, D, BMF_CLEAR, NULL);
    imgdata = (UWORD *)AllocVec(sizeof chip_img, MEMF_CHIP | MEMF_CLEAR);
    if (!bma || !bmb || !imgdata) {
        printf("FAIL no memory\n");
        return 10;
    }
    InitRastPort(&ra);
    InitRastPort(&rb);
    ra.BitMap = bma;
    rb.BitMap = bmb;

    /* fill_rect: solid even with a program's pattern left in the RastPort, which stays */
    clear();
    ra.AreaPtrn = rb.AreaPtrn = pat;
    ra.AreaPtSz = rb.AreaPtSz = 1;
    t->fill_rect(NULL, &ra, 3, 4, 2, 50, 20);
    rb.AreaPtrn = NULL; rb.AreaPtSz = 0;
    SetAPen(&rb, 3); SetDrMd(&rb, JAM1); RectFill(&rb, 4, 2, 50, 20);
    rb.AreaPtrn = pat; rb.AreaPtSz = 1;
    CHECK(same());
    CHECK(ra.AreaPtrn == pat && ra.AreaPtSz == 1);
    t->fill_rect(NULL, &ra, 5, 10, 10, 9, 9);              /* empty: nothing */
    CHECK(same());

    /* fill_pattern: with its pattern, then the program's back */
    clear();
    t->fill_pattern(NULL, &ra, 2, 1, JAM2, pat, 1, 0, 0, W - 1, H - 1);
    rb.AreaPtrn = pat; rb.AreaPtSz = 1;
    SetAPen(&rb, 2); SetBPen(&rb, 1); SetDrMd(&rb, JAM2); RectFill(&rb, 0, 0, W - 1, H - 1);
    CHECK(same());
    CHECK(ra.AreaPtrn == pat && ra.AreaPtSz == 1);
    ra.AreaPtrn = rb.AreaPtrn = NULL;
    ra.AreaPtSz = rb.AreaPtSz = 0;

    /* complement: its draw mode put back */
    SetDrMd(&ra, JAM2);
    t->complement(NULL, &ra, 8, 8, 40, 30);
    SetDrMd(&rb, COMPLEMENT); RectFill(&rb, 8, 8, 40, 30); SetDrMd(&rb, JAM2);
    CHECK(same());
    CHECK(ra.DrawMode == JAM2);

    /* erase_rect */
    t->erase_rect(NULL, &ra, 10, 5, 30, 15);
    EraseRect(&rb, 10, 5, 30, 15);
    CHECK(same());

    /* polyline, one point and many */
    clear();
    t->polyline(NULL, &ra, 7, 0, JAM1, 10, 3, xy, 4);
    t->polyline(NULL, &ra, 6, 0, JAM1, 60, 3, xy, 1);
    SetAPen(&rb, 7); SetBPen(&rb, 0); SetDrMd(&rb, JAM1);
    Move(&rb, 10, 3);
    for (i = 1; i < 4; i++) Draw(&rb, 10 + xy[2 * i], 3 + xy[2 * i + 1]);
    SetAPen(&rb, 6); Move(&rb, 60, 3); Draw(&rb, 60, 3);
    CHECK(same());

    /* text: a TextAttr opened for the call; text_length and font_height as graphics measures them */
    clear();
    {
        struct TextFont *tf = OpenFont(&ta);
        CHECK(tf != NULL);
        if (tf) {
            struct RastPort m;
            t->text(NULL, &ra, 1, 0, JAM1, &ta, 0, 2, 2, "OpenBench", 9);
            SetFont(&rb, tf);
            SetAPen(&rb, 1); SetBPen(&rb, 0); SetDrMd(&rb, JAM1);
            Move(&rb, 2, 2 + tf->tf_Baseline);
            Text(&rb, (CONST_STRPTR)"OpenBench", 9);
            CHECK(same());
            InitRastPort(&m);
            SetFont(&m, tf);
            CHECK(t->text_length(NULL, &ta, 0, "OpenBench", 9) == TextLength(&m, (CONST_STRPTR)"OpenBench", 9));
            CHECK(t->text_length(NULL, tf, 1, "Ab", 2) == TextLength(&m, (CONST_STRPTR)"Ab", 2));
            CHECK(t->font_height(NULL, &ta, 0) == tf->tf_YSize);
            CHECK(t->font_height(NULL, NULL, 0) > 0);
            t->set_font(NULL, (struct oi_RastPort *)&ra, tf);
            CHECK(ra.Font == tf);
            CloseFont(tf);
        }
    }

    /* blit_image: two planes given, PlaneOnOff filling another */
    clear();
    for (i = 0; i < 32; i++) chip_img[i] = (UWORD)(0x1234u * (unsigned)(i + 1));
    memcpy(imgdata, chip_img, sizeof chip_img);
    {
        struct Image im;
        struct BitMap ib;
        memset(&im, 0, sizeof im);
        im.Width = 20;
        im.Height = 8;
        im.Depth = 2;
        im.ImageData = imgdata;
        im.PlanePick = 0x05;
        im.PlaneOnOff = 0x02;
        t->blit_image(NULL, &ra, &im, 7, 9);
        InitBitMap(&ib, 8, 20, 8);
        for (i = 0; i < 8; i++) ib.Planes[i] = NULL;
        ib.Planes[0] = (PLANEPTR)imgdata;
        ib.Planes[2] = (PLANEPTR)(imgdata + 2 * 8);
        ib.Planes[1] = (PLANEPTR)-1;
        BltBitMapRastPort(&ib, 0, 0, &rb, 7, 9, 20, 8, 0xC0);
        CHECK(same());
    }

    /* blit_bitmap and scroll_raster */
    t->blit_bitmap(NULL, &ra, (struct oi_BitMap *)bmb, 50, 2, 30, 20);
    BltBitMapRastPort(bmb, 0, 0, &rb, 50, 2, 30, 20, 0xC0);
    /* (b drew from itself as it was before; a drew from b as it was: the same pixels) */
    CHECK(same());
    t->scroll_raster(NULL, &ra, 3, -2, 0, 0, W - 1, H - 1);
    ScrollRasterBF(&rb, 3, -2, 0, 0, W - 1, H - 1);
    CHECK(same());

    WaitBlit();
    FreeVec(imgdata);
    FreeBitMap(bma);
    FreeBitMap(bmb);
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 10 : 0;
}
