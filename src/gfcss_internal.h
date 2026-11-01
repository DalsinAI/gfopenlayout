/* GFCSS's insides, shared by its files (gfcss_parse.c, gfcss_values.c,
 * gfcss_select.c). Only malloc, calloc, realloc, free, memcpy, memset,
 * strlen and strcmp from the C library, so that it builds into the
 * resident gfopenlayout.library, whose runtime has no more.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef GFCSS_INTERNAL_H
#define GFCSS_INTERNAL_H

#include "gfopenlayout_css.h"

#include <stdlib.h>
#include <string.h>

#define G_PROPS (GFCSS_P_COUNT + GFCSS_MAX_EXT)
#define G_MAX_STATES 16
#define G_SAT 2000000000

/* ---- Tokens (CSS Syntax 3) ---- */
enum {
    T_EOF = 0, T_WS, T_IDENT, T_FUNC, T_AT, T_HASH, T_STRING, T_BADSTR, T_URL, T_BADURL, T_DELIM,
    T_NUMBER, T_PERCENT, T_DIM, T_CDO, T_CDC, T_COLON, T_SEMI, T_COMMA, T_LBRACK, T_RBRACK, T_LPAREN,
    T_RPAREN, T_LBRACE, T_RBRACE
};

typedef struct {
    int type;
    const char *start, *stop;   /* the token in the text */
    const char *s;              /* ident, function, at-keyword, hash: the name (decoded into buf
                                   when it has escapes); string, url: what is inside, as written */
    size_t n;
    int esc;                    /* string, url: it has escapes */
    int32_t num;                /* number, percentage, dimension: thousandths, saturated */
    int is_int;
    int hash_id;                /* a hash that could be an id */
    const char *unit;           /* dimension: its unit (decoded into ubuf when escaped) */
    size_t unit_n;
    unsigned delim;
    char buf[64];
    char ubuf[16];
} gtok;

typedef struct {
    const char *p, *end;
} glex;

void g_lex_init(glex *lx, const char *p, const char *end);
void g_next(glex *lx, gtok *t);
/* the next token that is not white space or a comment */
void g_next_nows(glex *lx, gtok *t);
/* skip a block or function's contents up to its matching close; p after the opener */
const char *g_skip_block(const char *p, const char *end, int close);

/* ---- Small helpers ---- */
int g_lower(int c);
int g_ieq(const char *a, size_t an, const char *b);   /* a (an bytes) equals b, ASCII case aside */
int g_ieqz(const char *a, const char *b);              /* both ended with 0 */
int g_ws(int c);
uint32_t g_hash(const char *s, size_t n, int fold);
int32_t g_mul_div(int32_t a, int32_t b, int32_t c);    /* a * b / c, saturated */

/* ---- Values ---- */
enum {
    V_NONE = 0, V_KW, V_INHERIT, V_INITIAL, V_UNSET, V_ABS, V_EM, V_REM, V_EX, V_CH, V_PCT, V_VW,
    V_VH, V_VMIN, V_VMAX, V_NUM, V_INT, V_COLOUR, V_CURRENT, V_TEXT, V_URL, V_FUNC, V_CALC, V_EXT
};

#define D_IMPORTANT 1u

typedef struct {
    uint16_t prop;
    uint8_t flags;
    uint8_t unit;               /* V_... */
    int32_t v;                  /* keyword, units, thousandths, colour, a text's offset, a calc's index */
    int32_t w;                  /* a text's length; a keyword's second part */
} gdecl;

/* calc(): a sum of these, each in thousandths but abs (units) */
typedef struct {
    int32_t abs, em, rem, pct, vw, vh, vmin, vmax;
} gcalc;

/* Where declarations go as they are parsed */
typedef struct {
    gdecl *d;
    size_t n, cap;
    gcalc *calc;
    size_t ncalc, calc_cap;
    int fixed;                  /* d and calc may not grow (a style attribute's, in the engine's scratch) */
    int oom;
    const char *base;           /* what texts' offsets are from */
} gsink;


/* One declaration (prop: value) into sink as longhands; 0 when it is not
 * valid (nothing added). */
int g_parse_declaration(gfcss_engine *e, gsink *sink, const char *name, size_t name_n,
                        const char *value, const char *value_end, int important);
/* A declaration list (a block's or a style attribute's) */
void g_parse_declarations(gfcss_engine *e, gsink *sink, const char *p, const char *end);
int g_colour_token(const gtok *t, glex *lx, uint32_t *argb, int *current);
int g_prop_inherited(int p);

/* ---- Selectors ---- */
enum {
    SS_TAG = 1, SS_ID, SS_CLASS, SS_ATTR, SS_STATE, SS_ROOT, SS_EMPTY, SS_FIRST_CHILD, SS_LAST_CHILD,
    SS_ONLY_CHILD, SS_NTH_CHILD, SS_NTH_LAST_CHILD, SS_FIRST_OF_TYPE, SS_LAST_OF_TYPE, SS_ONLY_OF_TYPE,
    SS_NTH_OF_TYPE, SS_NTH_LAST_OF_TYPE, SS_NOT, SS_NOTSTATE
};
enum { A_EXISTS = 0, A_EQ, A_INCLUDES, A_DASH, A_PREFIX, A_SUFFIX, A_SUBSTR };

typedef struct {
    uint8_t type;
    uint8_t op;                 /* attribute: A_...; SS_NOTSTATE: unused */
    uint8_t icase;              /* attribute: the i flag */
    uint8_t pad;
    uint32_t name;              /* offset into the pool (tag, id, class, attribute name) */
    uint32_t value;             /* attribute value's offset */
    int32_t a, b;               /* nth: a n + b; :not: first compound, count; state: bit */
} gsimple;

typedef struct {
    uint32_t first;             /* its simple selectors */
    uint16_t count;
    uint8_t comb;               /* how it joins the compound on its left: ' ', '>', '+', '~'; 0: none */
    uint8_t pad;
} gcompound;

typedef struct {
    uint32_t first;             /* its compounds, left to right */
    uint16_t count;
    uint8_t pseudo;             /* GFCSS_PSEUDO_... */
    uint8_t pad;
    uint32_t spec;              /* ids << 16 | classes << 8 | types */
    uint32_t rule;
} gselector;

typedef struct {
    uint32_t decl_first, decl_count;
    int32_t media;              /* its @media list; -1: none */
} grule;

/* A media query list: queries, each of conditions */
typedef struct {
    int32_t parent;             /* the @media it is inside; -1 */
    uint32_t first, count;      /* its queries */
    int ok;                     /* as evaluated (this and its parents) */
} gmedia;

enum { MT_ALL = 0, MT_SCREEN, MT_PRINT, MT_OTHER };
enum { MF_MIN_WIDTH = 1, MF_MAX_WIDTH, MF_MIN_HEIGHT, MF_MAX_HEIGHT, MF_WIDTH, MF_HEIGHT, MF_PORTRAIT,
       MF_LANDSCAPE, MF_FALSE, MF_TRUE };

typedef struct {
    uint8_t not_, type;
    uint8_t nfeat;
    uint8_t pad;
    uint8_t feat[6];
    int32_t val[6];             /* units */
} gquery;

typedef struct {
    uint32_t url;               /* pool offset */
    int32_t media;
    gfcss_sheet *sheet;
} gimport;

struct gfcss_sheet {
    gfcss_engine *engine;
    gfcss_sheet *next;          /* the engine's list */
    gfcss_sheet *owner;         /* the sheet that imports it; NULL: the engine's */
    int origin;
    char *text;                 /* every text parsed into it */
    size_t text_n, text_cap;
    char *pool;                 /* names, ended with 0 */
    size_t pool_n, pool_cap;
    grule *rules;
    size_t nrules, rules_cap;
    gselector *sels;
    size_t nsels, sels_cap;
    gcompound *comps;
    size_t ncomps, comps_cap;
    gsimple *simples;
    size_t nsimples, simples_cap;
    gsink decls;
    gmedia *media;
    size_t nmedia, media_cap;
    gquery *queries;
    size_t nqueries, queries_cap;
    gimport *imports;
    int nimports, imports_cap;
    int rules_seen;             /* a style rule came: later @imports are left out */
    uint32_t states;
    int has_pseudo;
    int ok;                     /* its import's media hold (and its owner's) */
};

typedef struct {
    uint32_t key;               /* kind << 30 ^ hash */
    const gfcss_sheet *sheet;
    uint32_t sel;
    uint32_t order;
} gentry;

typedef struct {
    const gdecl *d;
    const gfcss_sheet *sheet;   /* NULL: the style attribute's */
    uint32_t hi, lo;            /* priority */
} gwin;

struct gfcss_engine {
    gfcss_sheet *sheets;
    struct { char name[24]; uint32_t bit; } states[G_MAX_STATES];
    int nstates;
    struct { char name[32]; unsigned flags; uint32_t initial; gfcss_ext_fn fn; void *userdata; } ext[GFCSS_MAX_EXT];
    int next;
    int media_type;
    ol_unit vw, vh;
    ol_unit rem;
    /* the index of every sheet's selectors, by their last compound's id, class or type */
    int dirty;
    gentry *entries;
    size_t nentries, entries_cap;
    uint32_t *bucket;           /* nbuckets + 1 starts into entries */
    uint32_t nbuckets;
    size_t first_universal;     /* entries from here have no id, class or type */
    int any_pseudo;
    /* compute's scratch, kept between calls */
    gwin win[G_PROPS];
    uint16_t touched[G_PROPS];
    int ntouched;
    gsink inline_sink;
    gfcss_computed initial;     /* the initial values, the caller's properties' with them */
    int initial_next;
    gfcss_computed parent_tmp, out_tmp;     /* for a caller's smaller gfcss_computed */
    void *vscratch;             /* a declaration's value tokens (gfcss_values.c) */
};

/* sheet arrays */
int g_grow(void **p, size_t *cap, size_t need, size_t size);
uint32_t g_pool_add(gfcss_sheet *s, const char *str, size_t n, int fold);
int g_parse_selectors(gfcss_sheet *s, const char *p, const char *end, uint32_t rule);
int g_parse_media_list(gfcss_sheet *s, const char *p, const char *end, int32_t parent);
int g_media_eval(const gfcss_engine *e, const gfcss_sheet *s, int32_t m);
void g_media_update(gfcss_engine *e);
void g_index_dirty(gfcss_engine *e);

/* values */
int32_t g_calc_resolve(const gcalc *c, ol_unit fs, ol_unit rem, ol_unit vw, ol_unit vh, int32_t *pct);

#endif
