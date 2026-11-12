/* GFOpenLayout UI's outline text on true-colour screens (library 19).
 *
 * Text measured and drawn from gfopenfont.library's outlines: shaped with
 * OF_ShapeText, each glyph rasterised once as 8-bit coverage at the real
 * pixel size, never scaled from a bitmap, and blended onto what is under
 * it through cybergraphics.library's WritePixelArrayAlpha. Hinting is
 * light: the glyphs are scaled up or down (vertically only) so the face's
 * x-height lands on a whole pixel, every advance is a whole pixel and the
 * baseline is a whole row, so the stems of "m", "n" and "a" come out even.
 * On a screen with a palette (16 to 256 pens, AGA's or an 8-bit RTG one)
 * the grey levels are made from the screen's own pens: what is under the
 * text is read back (ReadPixelArray8), each edge pixel's blend of it and
 * the text's colour is given the nearest pen (FindColor, no pen taken) and
 * written again. Screens of 2 to 8 pens keep the caller's Text() (0).
 *
 * One cache of faces and glyphs for every window and program the library
 * serves, behind one semaphore. Integer maths only. It is part of
 * olui_amiga.c (included at its end), so what builds that builds this.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_ui_amiga.h"
#include "olui_amiga_text.h"

#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <inline/macros.h>

#include <string.h>

/* ---- gfopenfont.library 1.3 on, through its vectors (its SDK isn't a build
 * dependency of this one) ---- */
struct OFFace;
typedef struct {
    ULONG gl_Glyph, gl_Cluster;
    LONG gl_XAdvance, gl_YAdvance, gl_XOffset, gl_YOffset;     /* 26.6 */
    UWORD gl_Face, gl_Flags;
} of_glyph;
typedef struct {
    ULONG bm_Width, bm_Height;
    LONG bm_Left, bm_Top, bm_XAdvance;
    ULONG bm_Stride;
    UBYTE *bm_Pixels;
} of_bitmap;
typedef struct {
    ULONG fm_UnitsPerEm, fm_PixelSize;
    LONG fm_Ascender, fm_Descender, fm_LineGap, fm_XHeight, fm_CapHeight;
    LONG fm_UnderlinePosition, fm_UnderlineThickness, fm_StrikeoutPosition, fm_StrikeoutThickness;
    LONG fm_MaxAdvance, fm_SpaceAdvance;
    ULONG fm_Flags;
} of_metrics;
typedef struct {
    char fi_Family[64], fi_Style[64];
    UWORD fi_Weight, fi_Width, fi_Flags, fi_UnitsPerEm;
    ULONG fi_PixelSize, fi_NumGlyphs, fi_NumFaces;
    LONG fi_Ascender, fi_Descender, fi_Height;
    ULONG fi_UnicodeRanges[4];
} of_info;              /* 176 bytes */
#define OFFF_ITALIC 0x0001
#define OFA_Dummy (TAG_USER + 0x4F460000)
#define OFA_Weight (OFA_Dummy + 3)
#define OFA_Italic (OFA_Dummy + 4)
#define OFA_Fallbacks (OFA_Dummy + 5)
#define OFA_Exact (OFA_Dummy + 15)

static struct Library *OluiFontBase;
#define OF_CLOSEFACE(f) LP1NR(0x2a, OF_CloseFace, struct OFFace *, (f), a0, , OluiFontBase)
#define OF_SETSIZE(f, px) LP2(0x30, LONG, OF_SetSize, struct OFFace *, (f), a0, ULONG, (px), d0, , OluiFontBase)
#define OF_SHAPETEXT(f, s, n, g, cap) LP5(0x3c, ULONG, OF_ShapeText, struct OFFace *, (f), a0, CONST_STRPTR, (s), a1, \
    ULONG, (n), d0, of_glyph *, (g), a2, ULONG, (cap), d1, , OluiFontBase)
#define OF_FREEBITMAP(bm) LP1NR(0x4e, OF_FreeBitmap, of_bitmap *, (bm), a0, , OluiFontBase)
#define OF_FINDFACEA(fam, tags) LP2(0x54, struct OFFace *, OF_FindFaceA, CONST_STRPTR, (fam), a0, \
    const struct TagItem *, (tags), a1, , OluiFontBase)
#define OF_GETFALLBACK(f, n) LP2(0x66, struct OFFace *, OF_GetFallback, struct OFFace *, (f), a0, ULONG, (n), d0, , \
    OluiFontBase)
#define OF_RASTERMATRIX(f, g, m, bm) LP4(0x96, LONG, OF_RasterGlyphMatrix, struct OFFace *, (f), a0, ULONG, (g), d0, \
    const LONG *, (m), a1, of_bitmap *, (bm), a2, , OluiFontBase)
#define OF_GETFACEMETRICS(f, m) LP2(0xae, LONG, OF_GetFaceMetrics, struct OFFace *, (f), a0, of_metrics *, (m), a1, , \
    OluiFontBase)
#define OF_ADDFONTDIR(d) LP1(0x5a, LONG, OF_AddFontDir, CONST_STRPTR, (d), a0, , OluiFontBase)
#define OF_GETFACEINFO(f, i) LP2(0x36, LONG, OF_GetFaceInfo, struct OFFace *, (f), a0, of_info *, (i), a1, , OluiFontBase)

/* ---- cybergraphics.library ---- */
static struct Library *OluiCgxBase;
#define AA_MAPATTR(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, (bm), a0, ULONG, (a), d0, , OluiCgxBase)
#define AA_WPA_ALPHA(src, sx, sy, mod, rp, dx, dy, w, h, alpha) LP10(0xd8, ULONG, WritePixelArrayAlpha, APTR, (src), \
    a0, UWORD, (sx), d0, UWORD, (sy), d1, UWORD, (mod), d2, struct RastPort *, (rp), a1, UWORD, (dx), d3, UWORD, (dy), \
    d4, UWORD, (w), d5, UWORD, (h), d6, ULONG, (alpha), d7, , OluiCgxBase)
#ifndef CYBRMATTR_DEPTH
#define CYBRMATTR_DEPTH 0x80000007UL
#define CYBRMATTR_ISCYBERGFX 0x80000008UL
#endif

/* ---- the cache ---- */
#define AA_FACES 8
#define AA_GLYPHS 128           /* a face's glyphs, by hash */
#define AA_RUNS 48              /* shaped runs, for measuring and drawing them again */
#define AA_RUN_BYTES 64         /* longer runs are shaped each time */
#define AA_RUN_GLYPHS 64

typedef struct {
    ULONG key;                  /* glyph | face in the chain << 24; 0xffffffff: empty */
    of_bitmap bm;
} aa_glyph;

typedef struct {
    char family[48];
    int px;
    unsigned style;             /* OLUI_AMIGA_TEXT_BOLD | _ITALIC */
    struct OFFace *face;        /* NULL: the family isn't an outline font here */
    int ascent, descent;        /* whole pixels */
    int smear;                  /* bold asked for and the face isn't: drawn twice, a pixel apart */
    int ul_pos, ul_thick;       /* the underline: rows below the baseline */
    LONG matrix[4];
    ULONG used;
    aa_glyph glyphs[AA_GLYPHS];
} aa_face;

typedef struct {
    UWORD glyph;
    UBYTE face;
    BYTE dy;                    /* down from the baseline */
    WORD x;                     /* the pen, from the run's start */
} aa_g;

typedef struct {
    aa_face *face;
    ULONG hash;
    UWORD bytes, count;
    int width;
    ULONG used;
    char text[AA_RUN_BYTES];
    aa_g g[AA_RUN_GLYPHS];
} aa_run;

static struct SignalSemaphore aa_lock;
static int aa_lock_ready;
static int aa_tried;            /* gfopenfont.library was looked for */
static aa_face *aa_faces;       /* AA_FACES of them */
static aa_run *aa_runs;
static ULONG aa_clock;

static void aa_obtain(void)
{
    if (!aa_lock_ready) {
        Forbid();
        if (!aa_lock_ready) {
            InitSemaphore(&aa_lock);
            aa_lock_ready = 1;
        }
        Permit();
    }
    ObtainSemaphore(&aa_lock);
}

static void aa_release(void)
{
    ReleaseSemaphore(&aa_lock);
}

static int aa_dirs_added;

/* with the lock: the folders of outline fonts in FONTS:, once */
static void add_font_dirs(void)
{
    static const char *const dirs[] = { "FONTS:_ttf", "FONTS:_otf", "FONTS:_bullet_outlines", "FONTS:TrueType", NULL };
    struct Task *me = FindTask(NULL);
    int i;
    if (aa_dirs_added || !OluiFontBase || me->tc_Node.ln_Type != NT_PROCESS) return;
    aa_dirs_added = 1;
    for (i = 0; dirs[i]; ++i) {
        BPTR l = Lock((STRPTR)dirs[i], ACCESS_READ);
        if (!l) continue;
        UnLock(l);
        OF_ADDFONTDIR((CONST_STRPTR)dirs[i]);
    }
}

/* with the lock: gfopenfont.library 1.3 on and the cache, once */
static int engine(void)
{
    if (aa_tried) return OluiFontBase != NULL;
    aa_tried = 1;
    /* without it (an AGA machine with no RTG): palette screens only */
    if (!OluiCgxBase) OluiCgxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
    OluiFontBase = OpenLibrary((STRPTR)"gfopenfont.library", 1);
    if (OluiFontBase && OluiFontBase->lib_Version == 1 && OluiFontBase->lib_Revision < 3) {
        CloseLibrary(OluiFontBase);
        OluiFontBase = NULL;
    }
    if (!OluiFontBase) return 0;
    aa_faces = (aa_face *)AllocVec(sizeof(aa_face) * AA_FACES, MEMF_ANY | MEMF_CLEAR);
    aa_runs = (aa_run *)AllocVec(sizeof(aa_run) * AA_RUNS, MEMF_ANY | MEMF_CLEAR);
    if (!aa_faces || !aa_runs) {
        if (aa_faces) FreeVec(aa_faces);
        if (aa_runs) FreeVec(aa_runs);
        aa_faces = NULL;
        aa_runs = NULL;
        CloseLibrary(OluiFontBase);
        OluiFontBase = NULL;
        return 0;
    }
    /* gfopenfont.library indexes FONTS: itself but not the folders in it,
     * where an Amiga keeps its outline fonts (FONTS:_ttf/DejaVuSans.ttf,
     * beside the .otag bullet.library reads): those too, for this library's
     * life. From a process (the index is read there); else the first
     * process to draw text adds them. */
    add_font_dirs();
    return 1;
}

static void drop_face(aa_face *f)
{
    int i;
    if (f->face) {
        for (i = 0; i < AA_GLYPHS; ++i)
            if (f->glyphs[i].key != 0xffffffffUL && f->glyphs[i].bm.bm_Pixels) OF_FREEBITMAP(&f->glyphs[i].bm);
        OF_CLOSEFACE(f->face);
    }
    for (i = 0; i < AA_RUNS; ++i)
        if (aa_runs[i].face == f) aa_runs[i].face = NULL;
    memset(f, 0, sizeof(*f));
}

void olui_aa_cleanup(void)
{
    int i;
    if (!aa_lock_ready) return;
    aa_obtain();
    if (aa_faces) {
        for (i = 0; i < AA_FACES; ++i)
            if (aa_faces[i].px) drop_face(&aa_faces[i]);
        FreeVec(aa_faces);
    }
    if (aa_runs) FreeVec(aa_runs);
    aa_faces = NULL;
    aa_runs = NULL;
    if (OluiFontBase) CloseLibrary(OluiFontBase);
    if (OluiCgxBase) CloseLibrary(OluiCgxBase);
    OluiFontBase = OluiCgxBase = NULL;
    aa_tried = 0;
    aa_dirs_added = 0;
    aa_release();
}

static int is_generic(const char *s)
{
    static const char *const g[] = { "sans-serif", "serif", "monospace", "sans", "mono", "system-ui", "cursive",
                                     "fantasy", "ui-monospace", "ui-sans-serif", "ui-serif", NULL };
    int i;
    for (i = 0; g[i]; ++i)
        if (!strcmp(s, g[i])) return 1;
    return 0;
}

static int has(const char *s, const char *w)
{
    size_t n = strlen(w), i;
    for (; *s; ++s) {
        for (i = 0; i < n; ++i) {
            char c = s[i];
            if (!c) return 0;
            if (c >= 'A' && c <= 'Z') c += 32;
            if (c != w[i]) break;
        }
        if (i == n) return 1;
    }
    return 0;
}

/* the generic family nearest a named one */
static const char *generic_of(const char *name)
{
    if (has(name, "mono") || has(name, "courier") || has(name, "topaz") || has(name, "fixed") || has(name, "gothic") ||
        has(name, "console") || has(name, "code"))
        return "monospace";
    if (!has(name, "sans") && (has(name, "times") || has(name, "serif") || has(name, "roman") || has(name, "garamond")))
        return "serif";
    return "sans-serif";
}

/* a family by its name, as the theme (an Amiga font's file, "DejaVuSans.font")
 * or CSS ("DejaVu Sans", "sans-serif") gives it: the name, then with spaces
 * put back where a capital follows a small letter, in every combination
 * ("DejaVuSans" is "DejaVu Sans", not "Deja Vu Sans"; four places at most);
 * only the generic families take the nearest face; a name with no outline
 * face here takes its generic family */
static struct OFFace *find_family(const char *family, unsigned style)
{
    struct TagItem tags[5];
    char name[48], split[64];
    int at[4], nat = 0, combo;
    size_t n = 0, i, k;
    struct OFFace *f;
    struct Task *me = FindTask(NULL);
    if (me->tc_Node.ln_Type != NT_PROCESS) return NULL;    /* the font index is read from a process */
    add_font_dirs();
    while (family[n] && n + 1 < sizeof(name)) { name[n] = family[n]; ++n; }
    name[n] = 0;
    if (n > 5 && !strcmp(name + n - 5, ".font")) name[n -= 5] = 0;
    if (!n) return NULL;
    tags[0].ti_Tag = OFA_Weight;
    tags[0].ti_Data = (style & OLUI_AMIGA_TEXT_BOLD) ? 700 : 400;
    tags[1].ti_Tag = OFA_Italic;
    tags[1].ti_Data = (style & OLUI_AMIGA_TEXT_ITALIC) ? 1 : 0;
    tags[2].ti_Tag = OFA_Exact;
    tags[2].ti_Data = is_generic(name) ? 0 : 1;
    tags[3].ti_Tag = OFA_Fallbacks;
    tags[3].ti_Data = 1;
    tags[4].ti_Tag = TAG_END;
    if ((f = OF_FINDFACEA((CONST_STRPTR)name, tags)) != NULL) return f;
    for (i = 1; name[i] && nat < 4; ++i)
        if (name[i] >= 'A' && name[i] <= 'Z' && name[i - 1] >= 'a' && name[i - 1] <= 'z') at[nat++] = (int)i;
    /* the fewest spaces first: "DejaVu Sans" before "Deja Vu Sans" */
    {
        int want;
        for (want = 1; want <= nat; ++want)
            for (combo = 1; combo < (1 << nat); ++combo) {
                int bits = 0, b, j = 0;
                for (b = 0; b < nat; ++b) bits += (combo >> b) & 1;
                if (bits != want) continue;
                for (i = 0, k = 0; name[i] && k + 2 < sizeof(split); ++i) {
                    if (j < nat && (int)i == at[j]) {
                        if ((combo >> j) & 1) split[k++] = ' ';
                        ++j;
                    }
                    split[k++] = name[i];
                }
                split[k] = 0;
                if ((f = OF_FINDFACEA((CONST_STRPTR)split, tags)) != NULL) return f;
            }
    }
    /* a bitmap-only screen font (CGTriumvirate, topaz, XEN) has no outline
       here: its kind of generic family instead, so every screen font draws
       anti-aliased */
    if (!tags[2].ti_Data) return NULL;
    tags[2].ti_Data = 0;
    return OF_FINDFACEA((CONST_STRPTR)generic_of(name), tags);
}

/* with the lock: the face for family at px (whole pixels), found once;
 * NULL when the family isn't an outline font here */
static aa_face *face_for(const char *family, int px, unsigned style)
{
    aa_face *f, *old = NULL;
    of_metrics m;
    int i;
    LONG xh, target;
    style &= OLUI_AMIGA_TEXT_BOLD | OLUI_AMIGA_TEXT_ITALIC;
    if (!family || !family[0]) family = "sans-serif";
    if (px < 4) px = 4;
    if (px > 255) px = 255;
    for (i = 0; i < AA_FACES; ++i) {
        f = &aa_faces[i];
        if (f->px == px && f->style == style && !strcmp(f->family, family)) {
            f->used = ++aa_clock;
            return f->face ? f : NULL;
        }
        if (!old || !f->px || (old->px && f->used < old->used)) old = f;
    }
    f = old;
    if (f->px) drop_face(f);
    strncpy(f->family, family, sizeof(f->family) - 1);
    f->px = px;
    f->style = style;
    f->used = ++aa_clock;
    for (i = 0; i < AA_GLYPHS; ++i) f->glyphs[i].key = 0xffffffffUL;
    f->face = find_family(family, style);
    if (!f->face) {
        struct Task *me = FindTask(NULL);
        /* only a process can look: from a task (input.device drawing a
           title) it isn't known yet, so it is asked again next time */
        if (me->tc_Node.ln_Type != NT_PROCESS) f->px = 0;
        return NULL;
    }
    OF_SETSIZE(f->face, (ULONG)px);
    memset(&m, 0, sizeof(m));
    OF_GETFACEMETRICS(f->face, &m);
    f->ascent = (int)((m.fm_Ascender + 63) >> 6);
    f->descent = (int)((-m.fm_Descender + 63) >> 6);
    if (f->ascent < 1) f->ascent = px - px / 5;
    if (f->descent < 0) f->descent = 0;
    /* the x-height on a whole pixel: the glyphs scaled that much vertically */
    xh = m.fm_XHeight;
    target = (xh + 32) & ~63L;
    if (target < 64) target = 64;
    f->matrix[0] = (LONG)px << 16;
    f->matrix[1] = 0;
    f->matrix[2] = 0;
    f->matrix[3] = xh > 0 ? (LONG)(((long long)px << 16) * target / xh) : (LONG)px << 16;
    if (style) {
        /* bold or italic asked for and the family has none: made from the regular */
        of_info info;
        memset(&info, 0, sizeof(info));
        if (!OF_GETFACEINFO(f->face, &info)) {
            if ((style & OLUI_AMIGA_TEXT_BOLD) && info.fi_Weight < 600) f->smear = 1;
            if ((style & OLUI_AMIGA_TEXT_ITALIC) && !(info.fi_Flags & OFFF_ITALIC)) f->matrix[1] = ((LONG)px << 16) / 5;
        }
    }
    f->ul_pos = (int)((-m.fm_UnderlinePosition + 32) >> 6);
    f->ul_thick = (int)((m.fm_UnderlineThickness + 32) >> 6);
    if (f->ul_pos < 1) f->ul_pos = 1;
    if (f->ul_thick < 1) f->ul_thick = 1;
    return f;
}

/* with the lock: glyph g of the face's chain (n: its n-th fallback) as coverage */
static const of_bitmap *glyph_of(aa_face *f, ULONG g, unsigned n)
{
    ULONG key = (g & 0xffffffUL) | ((ULONG)n << 24);
    aa_glyph *e = &f->glyphs[(g * 31 + n) & (AA_GLYPHS - 1)];
    struct OFFace *face = f->face;
    if (e->key == key) return &e->bm;
    if (e->key != 0xffffffffUL && e->bm.bm_Pixels) OF_FREEBITMAP(&e->bm);
    memset(&e->bm, 0, sizeof(e->bm));
    e->key = 0xffffffffUL;
    if (n && !(face = OF_GETFALLBACK(f->face, n))) return NULL;
    if (OF_RASTERMATRIX(face, g, f->matrix, &e->bm) != 0) {
        memset(&e->bm, 0, sizeof(e->bm));
        return NULL;
    }
    e->key = key;
    return &e->bm;
}

static ULONG hash_of(const char *s, size_t n)
{
    ULONG h = 2166136261UL;
    while (n--) h = (h ^ (UBYTE)*s++) * 16777619UL;
    return h;
}

/* with the lock: the run shaped, its pens in whole pixels (each advance
 * rounded, a smeared bold one a pixel wider). Short runs are kept; a long
 * one is shaped into *own, the caller's to FreeVec. */
static const aa_run *shape(aa_face *f, const char *s, size_t n, aa_run **own)
{
    of_glyph local[AA_RUN_GLYPHS], *gl = local;
    aa_run *r = NULL, *old = NULL;
    ULONG h = hash_of(s, n), count, i;
    int x = 0;
    *own = NULL;
    if (n <= AA_RUN_BYTES) {
        for (i = 0; i < AA_RUNS; ++i) {
            aa_run *c = &aa_runs[i];
            if (c->face == f && c->hash == h && c->bytes == n && !memcmp(c->text, s, n)) {
                c->used = ++aa_clock;
                return c;
            }
            if (!old || !c->face || (old->face && c->used < old->used)) old = c;
        }
    }
    count = OF_SHAPETEXT(f->face, (CONST_STRPTR)s, (ULONG)n, local, AA_RUN_GLYPHS);
    if (count > AA_RUN_GLYPHS || n > AA_RUN_BYTES) {
        ULONG room = count > AA_RUN_GLYPHS ? count : AA_RUN_GLYPHS;
        r = (aa_run *)AllocVec(sizeof(aa_run) + sizeof(aa_g) * (room - AA_RUN_GLYPHS), MEMF_ANY);
        if (!r) return NULL;
        if (count > AA_RUN_GLYPHS) {
            gl = (of_glyph *)AllocVec(sizeof(of_glyph) * count, MEMF_ANY);
            if (!gl) { FreeVec(r); return NULL; }
            count = OF_SHAPETEXT(f->face, (CONST_STRPTR)s, (ULONG)n, gl, count);
        }
        *own = r;
    } else {
        r = old;
    }
    r->face = f;
    r->hash = h;
    r->bytes = (UWORD)(n <= AA_RUN_BYTES ? n : 0);
    if (n <= AA_RUN_BYTES) memcpy(r->text, s, n);
    r->count = (UWORD)count;
    r->used = ++aa_clock;
    for (i = 0; i < count; ++i) {
        r->g[i].glyph = (UWORD)gl[i].gl_Glyph;
        r->g[i].face = (UBYTE)gl[i].gl_Face;
        r->g[i].dy = (BYTE)(-((gl[i].gl_YOffset + 32) >> 6));
        r->g[i].x = (WORD)(x + ((gl[i].gl_XOffset + 32) >> 6));
        x += (int)((gl[i].gl_XAdvance + 32) >> 6) + f->smear;
    }
    r->width = x;
    if (gl != local) FreeVec(gl);
    if (*own) r->face = NULL;      /* not in the cache */
    return r;
}

/* with the lock: 2 a true-colour bitmap, 1 one of 16 to 256 pens, 0 fewer */
static int kind_of(struct BitMap *bm)
{
    if (OluiCgxBase && AA_MAPATTR(bm, CYBRMATTR_ISCYBERGFX))
        return AA_MAPATTR(bm, CYBRMATTR_DEPTH) > 8 ? 2 : AA_MAPATTR(bm, CYBRMATTR_DEPTH) >= 4;
    return GetBitMapAttr(bm, BMA_DEPTH) >= 4 && GetBitMapAttr(bm, BMA_DEPTH) <= 8;
}

int olui_aa_target(struct RastPort *rp)
{
    struct BitMap *bm;
    int ok;
    if (!rp || !(bm = rp->BitMap)) return 0;
    aa_obtain();
    ok = engine() && kind_of(bm) > 0;
    aa_release();
    return ok;
}

/* the screen whose pens a RastPort's are: its window's, else the default
 * public screen's (an offscreen friend BitMap of the Workbench's) */
static struct ColorMap *colours_of(struct RastPort *rp, struct Screen **locked)
{
    struct Screen *scr;
    *locked = NULL;
    if (rp->Layer && rp->Layer->Window) return ((struct Window *)rp->Layer->Window)->WScreen->ViewPort.ColorMap;
    if (!(scr = LockPubScreen(NULL))) return NULL;
    *locked = scr;
    return scr->ViewPort.ColorMap;
}

/* cov (and sh, the shadow's) blended onto the pens there: dx, dy, cw by ch of
 * rp from sx, sy of the w-wide coverage. Five levels of each, the nearest pen
 * of every blend kept for the call. 0: no memory, nothing drawn. */
static int palette_blend(struct RastPort *rp, struct ColorMap *cm, int depth, const UBYTE *cov, const UBYTE *sh, int w,
                         int sx, int sy, int dx, int dy, int cw, int ch, ULONG rgb, ULONG srgb)
{
    int aw = ((cw + 15) >> 4) << 4, row, col, maxpen = (1 << depth) - 1;
    UBYTE *pix;
    WORD *near;                 /* [pen][shadow level][text level]: the nearest pen, -1 not yet */
    ULONG *pen_rgb;             /* the pens' colours, 0xff000000 not yet read */
    struct RastPort trp;
    struct BitMap *tbm;
    ULONG size = (ULONG)aw * ch + 256UL * 25 * sizeof(WORD) + 256UL * sizeof(ULONG);
    UBYTE *mem = (UBYTE *)AllocVec(size, MEMF_ANY);
    if (!mem) return 0;
    if (!(tbm = AllocBitMap((ULONG)aw, 1, (ULONG)depth, 0, NULL))) {
        FreeVec(mem);
        return 0;
    }
    pen_rgb = (ULONG *)mem;
    near = (WORD *)(mem + 256UL * sizeof(ULONG));
    pix = mem + 256UL * sizeof(ULONG) + 256UL * 25 * sizeof(WORD);
    memset(pen_rgb, 0xff, 256UL * sizeof(ULONG));
    memset(near, 0xff, 256UL * 25 * sizeof(WORD));
    trp = *rp;
    trp.Layer = NULL;
    trp.BitMap = tbm;
    ReadPixelArray8(rp, (UWORD)dx, (UWORD)dy, (UWORD)(dx + cw - 1), (UWORD)(dy + ch - 1), pix, &trp);
    for (row = 0; row < ch; ++row) {
        const UBYTE *c = cov + (ULONG)(sy + row) * w + sx, *s = sh ? sh + (ULONG)(sy + row) * w + sx : NULL;
        UBYTE *p = pix + (ULONG)row * aw;
        for (col = 0; col < cw; ++col) {
            int tl = (c[col] * 4 + 127) / 255, sl = s ? (s[col] * 4 + 127) / 255 : 0, i;
            WORD *n;
            if (!tl && !sl) continue;
            n = &near[(p[col] * 5 + sl) * 5 + tl];
            if (*n < 0) {
                ULONG bg = pen_rgb[p[col]], out = 0;
                int shift;
                if (bg == 0xffffffffUL) {
                    ULONG t[3];
                    GetRGB32(cm, p[col], 1, t);
                    bg = pen_rgb[p[col]] = ((t[0] >> 24) << 16) | ((t[1] >> 24) << 8) | (t[2] >> 24);
                }
                for (shift = 0; shift <= 16; shift += 8) {
                    int b = (int)((bg >> shift) & 255), sc = (int)((srgb >> shift) & 255), tc = (int)((rgb >> shift) & 255);
                    b += (sc - b) * sl / 4;
                    b += (tc - b) * tl / 4;
                    out |= (ULONG)b << shift;
                }
                i = (int)FindColor(cm, ((out >> 16) & 255) * 0x01010101UL, ((out >> 8) & 255) * 0x01010101UL,
                                   (out & 255) * 0x01010101UL, maxpen);
                *n = (WORD)(i < 0 ? p[col] : i);
            }
            p[col] = (UBYTE)*n;
        }
    }
    WritePixelArray8(rp, (UWORD)dx, (UWORD)dy, (UWORD)(dx + cw - 1), (UWORD)(dy + ch - 1), pix, &trp);
    FreeBitMap(tbm);
    FreeVec(mem);
    return 1;
}

int olui_aa_measure(const char *family, int px, unsigned flags, const char *utf8, size_t length,
                    olui_amiga_text_size *size)
{
    aa_face *f;
    aa_run *own;
    const aa_run *r;
    int ok = 0;
    aa_obtain();
    if (engine() && (f = face_for(family, px, flags)) != NULL) {
        size->ascent = f->ascent;
        size->descent = f->descent;
        size->width = 0;
        ok = 1;
        if (length && (r = shape(f, utf8, length, &own)) != NULL) {
            size->width = r->width;
            if (own) FreeVec(own);
        }
    }
    aa_release();
    return ok;
}

/* the coverage of the run, one byte a pixel, into cov (w by h, its top-left at
 * ox, oy from the pen and baseline), shifted by dx, dy, as the brighter of
 * what's there and the glyph */
static void cover(aa_face *f, const aa_run *r, UBYTE *cov, int w, int h, int ox, int oy, int dx, int dy)
{
    unsigned i;
    int s;
    for (s = 0; s <= f->smear; ++s)
        for (i = 0; i < r->count; ++i) {
            const of_bitmap *bm = glyph_of(f, r->g[i].glyph, r->g[i].face);
            int gx, gy, row, col, bw, bh;
            if (!bm || !bm->bm_Pixels) continue;
            gx = r->g[i].x + bm->bm_Left - ox + dx + s;
            gy = r->g[i].dy - bm->bm_Top - oy + dy;
            bw = (int)bm->bm_Width;
            bh = (int)bm->bm_Height;
            for (row = 0; row < bh; ++row) {
                const UBYTE *src = bm->bm_Pixels + (ULONG)row * bm->bm_Stride;
                UBYTE *dst;
                int y = gy + row, c0 = 0, c1 = bw;
                if (y < 0 || y >= h) continue;
                if (gx < 0) c0 = -gx;
                if (gx + c1 > w) c1 = w - gx;
                dst = cov + (ULONG)y * w + gx;
                for (col = c0; col < c1; ++col)
                    if (src[col] > dst[col]) dst[col] = src[col];
            }
        }
}

static int max_i(int a, int b) { return a > b ? a : b; }

int olui_aa_draw(struct RastPort *rp, int x, int y, const char *utf8, size_t length, const char *family, int px,
                 unsigned long argb, unsigned flags, unsigned long shadow, struct ColorMap *pens)
{
    aa_face *f;
    aa_run *own = NULL;
    const aa_run *r;
    int top, bottom, left, right, w, h, ox, oy, i, pad;
    unsigned k;
    ULONG *buf = NULL;
    UBYTE *cov, *sh = NULL;
    struct BitMap *bm;
    int kind = 0;
    if (!rp || !(bm = rp->BitMap)) return 0;
    aa_obtain();
    if (!engine() || !(kind = kind_of(bm)) || !(f = face_for(family, px, flags))) {
        aa_release();
        return 0;
    }
    if (!length || !(r = shape(f, utf8, length, &own))) {
        aa_release();
        return 1;
    }
    /* the ink's box, from the pen and baseline */
    top = 0;
    bottom = 0;
    left = 0;
    right = r->width;
    for (k = 0; k < r->count; ++k) {
        const of_bitmap *g = glyph_of(f, r->g[k].glyph, r->g[k].face);
        if (!g || !g->bm_Pixels) continue;
        left = left < r->g[k].x + g->bm_Left ? left : r->g[k].x + g->bm_Left;
        right = max_i(right, r->g[k].x + g->bm_Left + (int)g->bm_Width + f->smear);
        top = top < r->g[k].dy - g->bm_Top ? top : r->g[k].dy - g->bm_Top;
        bottom = max_i(bottom, r->g[k].dy - g->bm_Top + (int)g->bm_Height);
    }
    if (flags & OLUI_AMIGA_TEXT_UNDERLINE) bottom = max_i(bottom, f->ul_pos + f->ul_thick);
    pad = (flags & (OLUI_AMIGA_TEXT_SHADOW | OLUI_AMIGA_TEXT_OUTLINE)) ? 1 : 0;
    ox = left - pad;
    oy = top - pad;
    w = right - left + 2 * pad;
    h = bottom - top + 2 * pad;
    /* the ARGB pixels (true colour only), the coverage, the shadow's */
    if (w > 0 && h > 0 && w <= 4096 && h <= 1024)
        buf = (ULONG *)AllocVec((ULONG)w * h * ((kind == 2 ? 4 : 0) + (pad ? 2 : 1)), MEMF_ANY | MEMF_CLEAR);
    if (!buf) {
        if (own) FreeVec(own);
        aa_release();
        return w > 0 && h > 0 ? 0 : 1;
    }
    cov = (UBYTE *)buf + (kind == 2 ? (ULONG)w * h * 4 : 0);
    cover(f, r, cov, w, h, ox, oy, 0, 0);
    if (flags & OLUI_AMIGA_TEXT_UNDERLINE)
        for (i = f->ul_pos; i < f->ul_pos + f->ul_thick; ++i)
            if (i - oy >= 0 && i - oy < h) memset(cov + (ULONG)(i - oy) * w + (0 - ox), 255, (size_t)r->width);
    if (pad) {
        sh = cov + (ULONG)w * h;
        if (flags & OLUI_AMIGA_TEXT_OUTLINE) {
            int dx, dy;
            for (dy = -1; dy <= 1; ++dy)
                for (dx = -1; dx <= 1; ++dx)
                    if (dx || dy) cover(f, r, sh, w, h, ox, oy, dx, dy);
        } else {
            cover(f, r, sh, w, h, ox, oy, 1, 1);
        }
    }
    if (own) FreeVec(own);
    aa_release();
    /* coverage to ARGB: the text's colour, its coverage the alpha; over a
     * shadow, the text over the shadow's colour */
    if (kind == 2) {
        ULONG rgb = argb & 0xffffffUL, srgb = shadow & 0xffffffUL;
        ULONG n = (ULONG)w * h, p;
        for (p = 0; p < n; ++p) {
            ULONG c = cov[p];
            if (sh && sh[p] && c < 255) {
                ULONG s = (ULONG)sh[p] * (255 - c) / 255, a = c + s, out = 0;
                int shift;
                for (shift = 0; shift <= 16; shift += 8) {
                    ULONG t = (rgb >> shift) & 255, b = (srgb >> shift) & 255;
                    out |= ((t * c + b * s) / a) << shift;
                }
                buf[p] = (a << 24) | out;
            } else {
                buf[p] = (c << 24) | rgb;
            }
        }
    }
    /* to the RastPort, cut to its left and top edges (and, without a layer,
     * to its bitmap: a layer cuts the rest) */
    {
        int dx = x + ox, dy = y + oy, sx = 0, sy = 0, cw = w, ch = h;
        if (dx < 0) { sx = -dx; cw += dx; dx = 0; }
        if (dy < 0) { sy = -dy; ch += dy; dy = 0; }
        if (!rp->Layer) {
            int bw = (int)GetBitMapAttr(bm, BMA_WIDTH), bh = (int)GetBitMapAttr(bm, BMA_HEIGHT);
            if (dx + cw > bw) cw = bw - dx;
            if (dy + ch > bh) ch = bh - dy;
        }
        if (cw > 0 && ch > 0 && kind == 2)
            AA_WPA_ALPHA(buf, (UWORD)sx, (UWORD)sy, (UWORD)(w * 4), rp, (UWORD)dx, (UWORD)dy, (UWORD)cw, (UWORD)ch,
                          0xffffffffUL);
        else if (cw > 0 && ch > 0) {
            struct Screen *locked;
            struct ColorMap *cm = pens;
            locked = NULL;
            if (!cm) cm = colours_of(rp, &locked);
            int ok = cm && palette_blend(rp, cm, (int)GetBitMapAttr(bm, BMA_DEPTH), cov, sh, w, sx, sy, dx, dy, cw,
                                         ch, argb & 0xffffffUL, shadow & 0xffffffUL);
            if (locked) UnlockPubScreen(NULL, locked);
            if (!ok) { FreeVec(buf); return 0; }
        }
    }
    FreeVec(buf);
    return 1;
}

/* ---- the public calls (library 19) ---- */

int olui_amiga_text(struct RastPort *rp, int x, int y, const char *utf8, size_t length, const char *family, int px,
                    unsigned long argb, unsigned flags, unsigned long shadow)
{
    if (!utf8) return 0;
    return olui_aa_draw(rp, x, y, utf8, length, family, px, argb, flags, shadow, NULL);
}

int olui_amiga_text_width(struct RastPort *rp, const char *utf8, size_t length, const char *family, int px,
                          unsigned flags, olui_amiga_text_size *size)
{
    if (!size || (length && !utf8)) return 0;
    if (rp && !olui_aa_target(rp)) return 0;
    return olui_aa_measure(family, px, flags, utf8, length, size);
}
