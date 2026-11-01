/* GFCSS: the tokenizer (a subset of CSS Syntax 3), stylesheets and their
 * at-rules, selectors and media queries. What can't be read is skipped as
 * CSS says: a bad declaration up to its ';', a rule whose selector list
 * has one we don't know as a whole, an at-rule we don't know with its
 * block.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "gfcss_internal.h"

/* ---- Small helpers ---------------------------------------------------------- */

int g_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int g_ws(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

int g_ieq(const char *a, size_t an, const char *b)
{
    size_t i;
    for (i = 0; i < an; ++i)
        if (!b[i] || g_lower((unsigned char)a[i]) != g_lower((unsigned char)b[i])) return 0;
    return b[an] == 0;
}

int g_ieqz(const char *a, const char *b)
{
    while (*a && g_lower((unsigned char)*a) == g_lower((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

uint32_t g_hash(const char *s, size_t n, int fold)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < n; ++i) {
        h ^= (uint32_t)(fold ? g_lower((unsigned char)s[i]) : (unsigned char)s[i]);
        h *= 16777619u;
    }
    return h;
}

int32_t g_mul_div(int32_t a, int32_t b, int32_t c)
{
    int64_t v;
    if (c == 0) return 0;
    v = (int64_t)a * b / c;
    if (v > G_SAT) return G_SAT;
    if (v < -G_SAT) return -G_SAT;
    return (int32_t)v;
}

int g_grow(void **p, size_t *cap, size_t need, size_t size)
{
    size_t n;
    void *q;
    if (need <= *cap) return 1;
    n = *cap ? *cap * 2 : 16;
    while (n < need) n *= 2;
    q = realloc(*p, n * size);
    if (!q) return 0;
    *p = q;
    *cap = n;
    return 1;
}

/* ---- Tokens ------------------------------------------------------------------ */

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = g_lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int name_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

static int name_char(int c)
{
    return name_start(c) || (c >= '0' && c <= '9') || c == '-';
}

static int valid_escape(const char *p, const char *end)
{
    return p < end && *p == '\\' && (p + 1 >= end || p[1] != '\n');
}

static int starts_ident(const char *p, const char *end)
{
    if (p >= end) return 0;
    if (*p == '-') {
        if (p + 1 >= end) return 0;
        return name_start((unsigned char)p[1]) || p[1] == '-' || valid_escape(p + 1, end);
    }
    return name_start((unsigned char)*p) || valid_escape(p, end);
}

static int starts_number(const char *p, const char *end)
{
    if (p >= end) return 0;
    if (*p == '+' || *p == '-') ++p;
    if (p >= end) return 0;
    if (*p >= '0' && *p <= '9') return 1;
    return *p == '.' && p + 1 < end && p[1] >= '0' && p[1] <= '9';
}

static size_t put_utf8(char *out, size_t room, uint32_t cp)
{
    char b[4];
    size_t n, i;
    if (cp == 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xc0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else if (cp < 0x10000) {
        b[0] = (char)(0xe0 | cp >> 12); b[1] = (char)(0x80 | ((cp >> 6) & 63)); b[2] = (char)(0x80 | (cp & 63));
        n = 3;
    } else {
        b[0] = (char)(0xf0 | cp >> 18); b[1] = (char)(0x80 | ((cp >> 12) & 63));
        b[2] = (char)(0x80 | ((cp >> 6) & 63)); b[3] = (char)(0x80 | (cp & 63));
        n = 4;
    }
    for (i = 0; i < n && i < room; ++i) out[i] = b[i];
    return n;
}

/* An escape at p (the backslash): its code point; *after past it */
static uint32_t escape_at(const char *p, const char *end, const char **after)
{
    uint32_t cp = 0;
    int n = 0;
    ++p;
    if (p >= end) { *after = p; return 0xfffd; }
    while (n < 6 && p < end && hexv((unsigned char)*p) >= 0) {
        cp = cp * 16 + (uint32_t)hexv((unsigned char)*p++);
        ++n;
    }
    if (n) {
        if (p < end && *p == '\r' && p + 1 < end && p[1] == '\n') p += 2;
        else if (p < end && g_ws((unsigned char)*p)) ++p;
        *after = p;
        return cp;
    }
    /* any other character stands for itself: a UTF-8 sequence is copied whole by the caller */
    *after = p + 1;
    return (unsigned char)*p;
}

size_t gfcss_unescape(const char *s, size_t length, char *out, size_t size)
{
    const char *p = s, *end = s + length;
    size_t n = 0;
    while (p < end) {
        if (*p == '\\') {
            const char *after;
            uint32_t cp;
            if (p + 1 < end && p[1] == '\n') { p += 2; continue; }      /* an escaped newline: nothing */
            if (p + 1 < end && p[1] == '\r') { p += (p + 2 < end && p[2] == '\n') ? 3 : 2; continue; }
            cp = escape_at(p, end, &after);
            if (cp >= 0x80 && after == p + 2) {
                /* a backslash before a UTF-8 lead byte: the character as it is */
                p = after - 1;
                if (n < size) out[n] = *p;
                ++n;
                ++p;
                continue;
            }
            n += put_utf8(out + (n < size ? n : size), n < size ? size - n : 0, cp);
            p = after;
            continue;
        }
        if (n < size) out[n] = *p;
        ++n;
        ++p;
    }
    if (size) out[n < size ? n : size - 1] = 0;
    return n;
}

size_t gfcss_text_copy(const gfcss_text *t, char *out, size_t size)
{
    if (!t || !t->text) {
        if (size) out[0] = 0;
        return 0;
    }
    return gfcss_unescape(t->text, t->length, out, size);
}

/* A name at p into buf (decoded); *endp after it */
static void consume_name(const char *p, const char *end, const char **endp, char *buf, size_t size,
                         const char **s, size_t *n)
{
    const char *start = p;
    int esc = 0;
    while (p < end) {
        if (name_char((unsigned char)*p)) ++p;
        else if (valid_escape(p, end)) {
            esc = 1;
            escape_at(p, end, &p);
        } else break;
    }
    *endp = p;
    if (!esc) {
        *s = start;
        *n = (size_t)(p - start);
    } else {
        size_t k = gfcss_unescape(start, (size_t)(p - start), buf, size);
        *s = buf;
        *n = k < size ? k : size - 1;
    }
}

static const char *consume_number(const char *p, const char *end, int32_t *out, int *is_int)
{
    int64_t mant = 0;           /* in ten-thousandths */
    int neg = 0, fd = 0, exp = 0, eneg = 0;
    *is_int = 1;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    while (p < end && *p >= '0' && *p <= '9') {
        if (mant < (int64_t)1 << 50) mant = mant * 10 + (*p - '0');
        ++p;
    }
    mant *= 10000;
    if (p + 1 < end && *p == '.' && p[1] >= '0' && p[1] <= '9') {
        int64_t scale = 1000;
        *is_int = 0;
        ++p;
        while (p < end && *p >= '0' && *p <= '9') {
            if (fd < 4) {
                mant += (*p - '0') * scale;
                scale /= 10;
            }
            ++fd;
            ++p;
        }
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        if (q < end && (*q == '+' || *q == '-')) { eneg = *q == '-'; ++q; }
        if (q < end && *q >= '0' && *q <= '9') {
            *is_int = 0;
            while (q < end && *q >= '0' && *q <= '9') {
                if (exp < 100) exp = exp * 10 + (*q - '0');
                ++q;
            }
            p = q;
            while (exp-- > 0) {
                if (eneg) mant /= 10;
                else if (mant < (int64_t)1 << 55) mant *= 10;
            }
        }
    }
    mant = (mant + 5) / 10;     /* to thousandths */
    if (mant > G_SAT) mant = G_SAT;
    *out = (int32_t)(neg ? -mant : mant);
    return p;
}

void g_lex_init(glex *lx, const char *p, const char *end)
{
    lx->p = p;
    lx->end = end;
}

static void url_token(glex *lx, gtok *t, const char *p)
{
    const char *end = lx->end;
    while (p < end && g_ws((unsigned char)*p)) ++p;
    t->s = p;
    t->esc = 0;
    while (p < end) {
        int c = (unsigned char)*p;
        if (c == ')') {
            t->n = (size_t)(p - t->s);
            lx->p = p + 1;
            t->type = T_URL;
            return;
        }
        if (g_ws(c)) {
            const char *q = p;
            while (q < end && g_ws((unsigned char)*q)) ++q;
            if (q >= end || *q == ')') {
                t->n = (size_t)(p - t->s);
                lx->p = q < end ? q + 1 : q;
                t->type = T_URL;
                return;
            }
            break;
        }
        if (c == '"' || c == '\'' || c == '(' || c < 0x20 || c == 0x7f) break;
        if (c == '\\') {
            if (!valid_escape(p, end)) break;
            t->esc = 1;
            escape_at(p, end, &p);
            continue;
        }
        ++p;
    }
    if (p >= end) {
        t->n = (size_t)(p - t->s);
        lx->p = p;
        t->type = T_URL;
        return;
    }
    /* a bad url: up to its ')' */
    while (p < end && *p != ')') {
        if (valid_escape(p, end)) escape_at(p, end, &p);
        else ++p;
    }
    lx->p = p < end ? p + 1 : p;
    t->type = T_BADURL;
}

void g_next(glex *lx, gtok *t)
{
    const char *p = lx->p, *end = lx->end;
    int c;
again:
    t->start = p;
    t->s = p;
    t->n = 0;
    t->esc = 0;
    t->num = 0;
    t->is_int = 0;
    t->hash_id = 0;
    t->unit = NULL;
    t->unit_n = 0;
    t->delim = 0;
    if (p >= end) {
        t->type = T_EOF;
        t->stop = p;
        lx->p = p;
        return;
    }
    c = (unsigned char)*p;
    if (c == '/' && p + 1 < end && p[1] == '*') {
        const char *q = p + 2;
        while (q + 1 < end && !(q[0] == '*' && q[1] == '/')) ++q;
        p = q + 1 < end ? q + 2 : end;
        goto again;
    }
    if (g_ws(c)) {
        while (p < end && g_ws((unsigned char)*p)) ++p;
        t->type = T_WS;
    } else if (c == '"' || c == '\'') {
        const char *q = p + 1;
        t->type = T_STRING;
        t->s = q;
        while (q < end && *q != c) {
            if (*q == '\n') { t->type = T_BADSTR; break; }
            if (*q == '\\') {
                t->esc = 1;
                q += q + 1 < end ? 2 : 1;
                continue;
            }
            ++q;
        }
        t->n = (size_t)(q - t->s);
        p = q < end && *q == c ? q + 1 : q;
    } else if (c == '#') {
        if (p + 1 < end && (name_char((unsigned char)p[1]) || valid_escape(p + 1, end))) {
            t->type = T_HASH;
            t->hash_id = starts_ident(p + 1, end);
            consume_name(p + 1, end, &p, t->buf, sizeof(t->buf), &t->s, &t->n);
        } else {
            t->type = T_DELIM;
            t->delim = '#';
            ++p;
        }
    } else if ((c >= '0' && c <= '9') || ((c == '+' || c == '-' || c == '.') && starts_number(p, end))) {
        p = consume_number(p, end, &t->num, &t->is_int);
        if (starts_ident(p, end)) {
            t->type = T_DIM;
            consume_name(p, end, &p, t->ubuf, sizeof(t->ubuf), &t->unit, &t->unit_n);
        } else if (p < end && *p == '%') {
            t->type = T_PERCENT;
            ++p;
        } else {
            t->type = T_NUMBER;
        }
    } else if (c == '-' && p + 2 < end && p[1] == '-' && p[2] == '>') {
        t->type = T_CDC;
        p += 3;
    } else if (c == '<' && p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {
        t->type = T_CDO;
        p += 4;
    } else if (c == '@') {
        if (starts_ident(p + 1, end)) {
            t->type = T_AT;
            consume_name(p + 1, end, &p, t->buf, sizeof(t->buf), &t->s, &t->n);
        } else {
            t->type = T_DELIM;
            t->delim = '@';
            ++p;
        }
    } else if (starts_ident(p, end)) {
        consume_name(p, end, &p, t->buf, sizeof(t->buf), &t->s, &t->n);
        if (p < end && *p == '(') {
            ++p;
            if (g_ieq(t->s, t->n, "url")) {
                const char *q = p;
                while (q < end && g_ws((unsigned char)*q)) ++q;
                if (q >= end || (*q != '"' && *q != '\'')) {
                    url_token(lx, t, p);
                    t->stop = lx->p;
                    return;
                }
            }
            t->type = T_FUNC;
        } else {
            t->type = T_IDENT;
        }
    } else {
        ++p;
        switch (c) {
        case ':': t->type = T_COLON; break;
        case ';': t->type = T_SEMI; break;
        case ',': t->type = T_COMMA; break;
        case '[': t->type = T_LBRACK; break;
        case ']': t->type = T_RBRACK; break;
        case '(': t->type = T_LPAREN; break;
        case ')': t->type = T_RPAREN; break;
        case '{': t->type = T_LBRACE; break;
        case '}': t->type = T_RBRACE; break;
        default:
            t->type = T_DELIM;
            t->delim = (unsigned)c;
            break;
        }
    }
    t->stop = p;
    lx->p = p;
}

void g_next_nows(glex *lx, gtok *t)
{
    do g_next(lx, t); while (t->type == T_WS);
}

/* Past a block's contents and its close (the opener already read). Past
 * 32 levels the kinds of bracket are no longer told apart, only counted. */
const char *g_skip_block(const char *p, const char *end, int close)
{
    glex lx;
    gtok t;
    int stack[32], depth = 0;
    long deep = 0;
    stack[0] = close;
    g_lex_init(&lx, p, end);
    for (;;) {
        g_next(&lx, &t);
        if (t.type == T_EOF) return lx.p;
        if (t.type == T_LBRACE || t.type == T_LBRACK || t.type == T_LPAREN || t.type == T_FUNC) {
            int want = t.type == T_LBRACE ? T_RBRACE : t.type == T_LBRACK ? T_RBRACK : T_RPAREN;
            if (deep || depth + 1 >= 32) ++deep;
            else stack[++depth] = want;
        } else if (deep) {
            if (t.type == T_RBRACE || t.type == T_RBRACK || t.type == T_RPAREN) --deep;
        } else if (t.type == stack[depth]) {
            if (depth == 0) return lx.p;
            --depth;
        }
    }
}

/* ---- Pool --------------------------------------------------------------------- */

uint32_t g_pool_add(gfcss_sheet *s, const char *str, size_t n, int fold)
{
    uint32_t at;
    size_t i;
    if (!g_grow((void **)&s->pool, &s->pool_cap, s->pool_n + n + 1, 1)) {
        s->decls.oom = 1;
        return 0;
    }
    at = (uint32_t)s->pool_n;
    for (i = 0; i < n; ++i) s->pool[at + i] = (char)(fold ? g_lower((unsigned char)str[i]) : str[i]);
    s->pool[at + n] = 0;
    s->pool_n += n + 1;
    return at;
}

/* ---- Selectors ------------------------------------------------------------------ */

#define MAX_SIMPLE 24
#define MAX_PENDING 8

typedef struct {
    gfcss_sheet *s;
    glex lx;
    gtok t;                     /* the current token */
    int bad;
    uint32_t states;
    int pseudo;                 /* a pseudo-element seen: nothing may follow it */
    int depth;                  /* :not()s inside :not()s */
} gsel_parse;

typedef struct {
    uint32_t simple;            /* the :not's simple selector */
    const char *p, *end;        /* its argument */
} gpending;

static void sp_next(gsel_parse *sp)
{
    g_next(&sp->lx, &sp->t);
}

static uint32_t spec_add(uint32_t a, uint32_t b)
{
    uint32_t ids = (a >> 16) + (b >> 16), cls = ((a >> 8) & 255) + ((b >> 8) & 255), ty = (a & 255) + (b & 255);
    if (ids > 255) ids = 255;
    if (cls > 255) cls = 255;
    if (ty > 255) ty = 255;
    return ids << 16 | cls << 8 | ty;
}

/* "2n+1", "odd", "-n+3", "5" (spaces taken out, lower case) */
static int parse_nth(const char *p, const char *end, int32_t *a, int32_t *b)
{
    char s[32];
    size_t n = 0;
    const char *q;
    int32_t sign = 1, v = 0;
    int digits = 0;
    for (q = p; q < end; ++q) {
        if (g_ws((unsigned char)*q)) continue;
        if (n + 1 >= sizeof(s)) return 0;
        s[n++] = (char)g_lower((unsigned char)*q);
    }
    s[n] = 0;
    if (!strcmp(s, "odd")) { *a = 2; *b = 1; return 1; }
    if (!strcmp(s, "even")) { *a = 2; *b = 0; return 1; }
    q = s;
    if (*q == '+' || *q == '-') sign = *q++ == '-' ? -1 : 1;
    while (*q >= '0' && *q <= '9') {
        if (v < 100000) v = v * 10 + (*q - '0');
        ++q;
        ++digits;
    }
    if (*q == 'n') {
        *a = sign * (digits ? v : 1);
        ++q;
        *b = 0;
        if (!*q) return 1;
        if (*q != '+' && *q != '-') return 0;
        sign = *q++ == '-' ? -1 : 1;
        v = 0;
        digits = 0;
        while (*q >= '0' && *q <= '9') {
            if (v < 100000) v = v * 10 + (*q - '0');
            ++q;
            ++digits;
        }
        if (!digits || *q) return 0;
        *b = sign * v;
        return 1;
    }
    if (!digits || *q) return 0;
    *a = 0;
    *b = sign * v;
    return 1;
}

static uint32_t state_bit(const gfcss_engine *e, const char *s, size_t n)
{
    static const struct { const char *name; uint32_t bit; } std[] = {
        { "hover", GFCSS_STATE_HOVER }, { "active", GFCSS_STATE_ACTIVE }, { "focus", GFCSS_STATE_FOCUS },
        { "checked", GFCSS_STATE_CHECKED }, { "disabled", GFCSS_STATE_DISABLED },
        { "enabled", GFCSS_STATE_ENABLED }, { "link", GFCSS_STATE_LINK }, { "visited", GFCSS_STATE_VISITED }
    };
    size_t i;
    for (i = 0; i < sizeof(std) / sizeof(std[0]); ++i)
        if (g_ieq(s, n, std[i].name)) return std[i].bit;
    for (i = 0; i < (size_t)e->nstates; ++i)
        if (g_ieq(s, n, e->states[i].name)) return e->states[i].bit;
    return 0;
}

static int pseudo_element(const char *s, size_t n)
{
    if (g_ieq(s, n, "before")) return GFCSS_PSEUDO_BEFORE;
    if (g_ieq(s, n, "after")) return GFCSS_PSEUDO_AFTER;
    if (g_ieq(s, n, "first-line")) return GFCSS_PSEUDO_FIRST_LINE;
    if (g_ieq(s, n, "first-letter")) return GFCSS_PSEUDO_FIRST_LETTER;
    return 0;
}

static int add_simple(gsimple *tmp, int *n, const gsimple *ss)
{
    if (*n >= MAX_SIMPLE) return 0;
    tmp[(*n)++] = *ss;
    return 1;
}

/* An attribute selector, after its '[' */
static int parse_attr(gsel_parse *sp, gsimple *ss)
{
    gfcss_sheet *s = sp->s;
    char val[256];
    memset(ss, 0, sizeof(*ss));
    ss->type = SS_ATTR;
    g_next_nows(&sp->lx, &sp->t);
    if (sp->t.type != T_IDENT) return 0;
    ss->name = g_pool_add(s, sp->t.s, sp->t.n, 1);
    g_next_nows(&sp->lx, &sp->t);
    if (sp->t.type == T_RBRACK) {
        ss->op = A_EXISTS;
        return 1;
    }
    if (sp->t.type != T_DELIM) return 0;
    switch (sp->t.delim) {
    case '=': ss->op = A_EQ; break;
    case '~': ss->op = A_INCLUDES; break;
    case '|': ss->op = A_DASH; break;
    case '^': ss->op = A_PREFIX; break;
    case '$': ss->op = A_SUFFIX; break;
    case '*': ss->op = A_SUBSTR; break;
    default: return 0;
    }
    if (ss->op != A_EQ) {
        g_next(&sp->lx, &sp->t);
        if (sp->t.type != T_DELIM || sp->t.delim != '=') return 0;
    }
    g_next_nows(&sp->lx, &sp->t);
    if (sp->t.type == T_IDENT) {
        ss->value = g_pool_add(s, sp->t.s, sp->t.n, 0);
    } else if (sp->t.type == T_STRING) {
        size_t n = gfcss_unescape(sp->t.s, sp->t.n, val, sizeof(val));
        ss->value = g_pool_add(s, val, n < sizeof(val) ? n : sizeof(val) - 1, 0);
    } else {
        return 0;
    }
    g_next_nows(&sp->lx, &sp->t);
    if (sp->t.type == T_IDENT && (g_ieq(sp->t.s, sp->t.n, "i") || g_ieq(sp->t.s, sp->t.n, "s"))) {
        ss->icase = g_lower((unsigned char)sp->t.s[0]) == 'i';
        g_next_nows(&sp->lx, &sp->t);
    }
    return sp->t.type == T_RBRACK;
}

/* A function's argument: from after its '(' to its ')' */
static int func_arg(gsel_parse *sp, const char **p, const char **end)
{
    *p = sp->lx.p;
    *end = g_skip_block(sp->lx.p, sp->lx.end, T_RPAREN);
    if (*end <= *p || (*end)[-1] != ')') return 0;
    --*end;
    sp->lx.p = *end + 1;
    return 1;
}

/* One compound selector at sp->t; appends it and its simples. 0: not one we read */
static int parse_compound(gsel_parse *sp, uint32_t *spec, gpending *pend, int *npend, int in_not)
{
    gfcss_sheet *s = sp->s;
    gsimple tmp[MAX_SIMPLE], ss;
    int n = 0, any = 0, i;
    gcompound *c;
    if (sp->pseudo) return 0;
    if (sp->t.type == T_IDENT) {
        memset(&ss, 0, sizeof(ss));
        ss.type = SS_TAG;
        ss.name = g_pool_add(s, sp->t.s, sp->t.n, 1);
        add_simple(tmp, &n, &ss);
        *spec = spec_add(*spec, 1);
        any = 1;
        sp_next(sp);
    } else if (sp->t.type == T_DELIM && sp->t.delim == '*') {
        any = 1;
        sp_next(sp);
    }
    if (sp->t.type == T_DELIM && sp->t.delim == '|') return 0;     /* namespaces: not done */
    for (;;) {
        memset(&ss, 0, sizeof(ss));
        if (sp->t.type == T_HASH && sp->t.hash_id) {
            if (sp->pseudo) return 0;
            ss.type = SS_ID;
            ss.name = g_pool_add(s, sp->t.s, sp->t.n, 0);
            *spec = spec_add(*spec, 1u << 16);
        } else if (sp->t.type == T_DELIM && sp->t.delim == '.') {
            sp_next(sp);
            if (sp->t.type != T_IDENT || sp->pseudo) return 0;
            ss.type = SS_CLASS;
            ss.name = g_pool_add(s, sp->t.s, sp->t.n, 0);
            *spec = spec_add(*spec, 1u << 8);
        } else if (sp->t.type == T_LBRACK) {
            if (sp->pseudo || !parse_attr(sp, &ss)) return 0;
            *spec = spec_add(*spec, 1u << 8);
        } else if (sp->t.type == T_COLON) {
            int element;
            sp_next(sp);
            element = sp->t.type == T_COLON;
            if (element) sp_next(sp);
            if (sp->pseudo) return 0;
            if (sp->t.type == T_IDENT) {
                const char *nm = sp->t.s;
                size_t nn = sp->t.n;
                int pe = pseudo_element(nm, nn);
                uint32_t bit;
                /* ::before, and the four that may be written with one colon */
                if (element || pe) {
                    if (!pe || in_not) return 0;
                    sp->pseudo = pe;
                    *spec = spec_add(*spec, 1);
                    any = 1;
                    sp_next(sp);
                    continue;
                }
                if ((bit = state_bit(s->engine, nm, nn)) != 0) {
                    ss.type = SS_STATE;
                    ss.a = (int32_t)bit;
                    sp->states |= bit;
                    if (bit == GFCSS_STATE_ENABLED) sp->states |= GFCSS_STATE_DISABLED;
                } else if (g_ieq(nm, nn, "root")) ss.type = SS_ROOT;
                else if (g_ieq(nm, nn, "empty")) ss.type = SS_EMPTY;
                else if (g_ieq(nm, nn, "first-child")) ss.type = SS_FIRST_CHILD;
                else if (g_ieq(nm, nn, "last-child")) ss.type = SS_LAST_CHILD;
                else if (g_ieq(nm, nn, "only-child")) ss.type = SS_ONLY_CHILD;
                else if (g_ieq(nm, nn, "first-of-type")) ss.type = SS_FIRST_OF_TYPE;
                else if (g_ieq(nm, nn, "last-of-type")) ss.type = SS_LAST_OF_TYPE;
                else if (g_ieq(nm, nn, "only-of-type")) ss.type = SS_ONLY_OF_TYPE;
                else return 0;
                *spec = spec_add(*spec, 1u << 8);
            } else if (sp->t.type == T_FUNC && !element) {
                const char *nm = sp->t.s, *ap, *ae;
                size_t nn = sp->t.n;
                if (!func_arg(sp, &ap, &ae)) return 0;
                if (g_ieq(nm, nn, "not")) {
                    ss.type = SS_NOT;
                    if (*npend >= MAX_PENDING) return 0;
                    pend[*npend].simple = 0;            /* set when appended */
                    pend[*npend].p = ap;
                    pend[*npend].end = ae;
                    ss.a = -1 - *npend;                 /* which pending it is, until then */
                    ++*npend;
                } else {
                    if (g_ieq(nm, nn, "nth-child")) ss.type = SS_NTH_CHILD;
                    else if (g_ieq(nm, nn, "nth-last-child")) ss.type = SS_NTH_LAST_CHILD;
                    else if (g_ieq(nm, nn, "nth-of-type")) ss.type = SS_NTH_OF_TYPE;
                    else if (g_ieq(nm, nn, "nth-last-of-type")) ss.type = SS_NTH_LAST_OF_TYPE;
                    else return 0;
                    if (!parse_nth(ap, ae, &ss.a, &ss.b)) return 0;
                    *spec = spec_add(*spec, 1u << 8);
                }
                if (!add_simple(tmp, &n, &ss)) return 0;
                any = 1;
                sp_next(sp);
                continue;
            } else {
                return 0;
            }
        } else {
            break;
        }
        if (!add_simple(tmp, &n, &ss)) return 0;
        any = 1;
        sp_next(sp);
    }
    if (!any) return 0;
    if (!g_grow((void **)&s->comps, &s->comps_cap, s->ncomps + 1, sizeof(gcompound)) ||
        !g_grow((void **)&s->simples, &s->simples_cap, s->nsimples + (size_t)n, sizeof(gsimple))) {
        s->decls.oom = 1;
        return 0;
    }
    c = &s->comps[s->ncomps++];
    c->first = (uint32_t)s->nsimples;
    c->count = (uint16_t)n;
    c->comb = 0;
    c->pad = 0;
    for (i = 0; i < n; ++i) {
        if (tmp[i].type == SS_NOT) pend[-1 - tmp[i].a].simple = (uint32_t)s->nsimples;
        s->simples[s->nsimples++] = tmp[i];
    }
    return 1;
}

static int resolve_pending(gsel_parse *sp, gpending *pend, int npend, uint32_t *spec);

/* :not()'s argument: compound selectors with commas; its specificity the most specific's */
static int parse_not(gsel_parse *outer, gpending *pd, uint32_t *spec)
{
    gsel_parse sp;
    gpending pend[MAX_PENDING];
    int npend = 0, count = 0;
    uint32_t first = (uint32_t)outer->s->ncomps, best = 0, nested = 0;
    if (outer->depth >= 4) return 0;
    memset(&sp, 0, sizeof(sp));
    sp.s = outer->s;
    sp.depth = outer->depth + 1;
    g_lex_init(&sp.lx, pd->p, pd->end);
    g_next_nows(&sp.lx, &sp.t);
    for (;;) {
        uint32_t one = 0;
        if (!parse_compound(&sp, &one, pend, &npend, 1)) return 0;
        ++count;
        if (one > best) best = one;
        if (sp.t.type == T_WS) g_next_nows(&sp.lx, &sp.t);
        if (sp.t.type == T_EOF) break;
        if (sp.t.type != T_COMMA) return 0;
        g_next_nows(&sp.lx, &sp.t);
    }
    /* its compounds are side by side; :not()s inside them come after */
    if (!resolve_pending(&sp, pend, npend, &nested)) return 0;
    outer->states |= sp.states;
    outer->s->simples[pd->simple].a = (int32_t)first;
    outer->s->simples[pd->simple].b = count;
    *spec = spec_add(*spec, spec_add(best, nested));
    return 1;
}

static int resolve_pending(gsel_parse *sp, gpending *pend, int npend, uint32_t *spec)
{
    int i;
    for (i = 0; i < npend; ++i)
        if (!parse_not(sp, &pend[i], spec)) return 0;
    return 1;
}

int g_parse_selectors(gfcss_sheet *s, const char *p, const char *end, uint32_t rule)
{
    gsel_parse sp;
    size_t nsels = s->nsels, ncomps = s->ncomps, nsimples = s->nsimples, npool = s->pool_n, i;
    memset(&sp, 0, sizeof(sp));
    sp.s = s;
    g_lex_init(&sp.lx, p, end);
    g_next_nows(&sp.lx, &sp.t);
    for (;;) {
        gselector sel;
        gpending pend[MAX_PENDING];
        int npend = 0, nc = 1;
        uint32_t spec = 0, first = (uint32_t)s->ncomps;
        sp.pseudo = 0;
        /* each compound appends one compound; :not() arguments are appended once they are
           all in, so a selector's compounds are side by side */
        if (!parse_compound(&sp, &spec, pend, &npend, 0)) goto bad;
        for (;;) {
            int comb = 0;
            size_t before;
            if (sp.t.type == T_WS) {
                comb = ' ';
                g_next_nows(&sp.lx, &sp.t);
            }
            if (sp.t.type == T_DELIM && (sp.t.delim == '>' || sp.t.delim == '+' || sp.t.delim == '~')) {
                comb = (int)sp.t.delim;
                g_next_nows(&sp.lx, &sp.t);
            } else if (sp.t.type == T_COMMA || sp.t.type == T_EOF) {
                break;
            }
            if (!comb || sp.t.type == T_COMMA || sp.t.type == T_EOF || sp.pseudo || nc >= 16) goto bad;
            before = s->ncomps;
            if (!parse_compound(&sp, &spec, pend, &npend, 0)) goto bad;
            s->comps[before].comb = (uint8_t)comb;
            ++nc;
        }
        sel.first = first;
        sel.count = (uint16_t)nc;
        if (!resolve_pending(&sp, pend, npend, &spec)) goto bad;
        sel.pseudo = (uint8_t)sp.pseudo;
        sel.pad = 0;
        sel.spec = spec;
        sel.rule = rule;
        if (!g_grow((void **)&s->sels, &s->sels_cap, s->nsels + 1, sizeof(gselector))) {
            s->decls.oom = 1;
            goto bad;
        }
        s->sels[s->nsels++] = sel;
        if (sp.t.type == T_EOF) break;
        g_next_nows(&sp.lx, &sp.t);
        if (sp.t.type == T_EOF) goto bad;       /* a list ending in a comma */
    }
    s->states |= sp.states;
    for (i = nsels; i < s->nsels; ++i)
        if (s->sels[i].pseudo) s->has_pseudo = 1;
    return 1;
bad:
    /* one selector we don't read: the whole rule goes, as CSS says */
    s->nsels = nsels;
    s->ncomps = ncomps;
    s->nsimples = nsimples;
    s->pool_n = npool;
    return 0;
}

/* ---- Media queries ------------------------------------------------------------- */

/* a length in a media feature, in units (em and rem as 16 px) */
static int media_length(const gtok *t, int32_t *out)
{
    static const struct { const char *u; int32_t num, den; } units[] = {
        { "px", 60, 1 }, { "pt", 80, 1 }, { "pc", 960, 1 }, { "in", 5760, 1 }, { "cm", 576000, 254 },
        { "mm", 57600, 254 }, { "em", 960, 1 }, { "rem", 960, 1 }
    };
    size_t i;
    if (t->type == T_NUMBER && t->num == 0) { *out = 0; return 1; }
    if (t->type != T_DIM) return 0;
    for (i = 0; i < sizeof(units) / sizeof(units[0]); ++i)
        if (g_ieq(t->unit, t->unit_n, units[i].u)) {
            *out = g_mul_div(t->num, units[i].num, units[i].den * 1000);
            return 1;
        }
    return 0;
}

static void media_feature(gquery *q, int f, int32_t v)
{
    if (q->nfeat < 6) {
        q->feat[q->nfeat] = (uint8_t)f;
        q->val[q->nfeat++] = v;
    } else {
        q->feat[5] = MF_FALSE;
    }
}

/* One (feature) after its '(' */
static void parse_feature(glex *lx, gquery *q)
{
    gtok t;
    const char *close = g_skip_block(lx->p, lx->end, T_RPAREN);
    glex in;
    char name[24];
    size_t n;
    int f = 0;
    int32_t v = 0;
    g_lex_init(&in, lx->p, close > lx->p && close[-1] == ')' ? close - 1 : close);
    lx->p = close;
    g_next_nows(&in, &t);
    if (t.type != T_IDENT) { media_feature(q, MF_FALSE, 0); return; }
    n = t.n < sizeof(name) - 1 ? t.n : sizeof(name) - 1;
    memcpy(name, t.s, n);
    name[n] = 0;
    g_next_nows(&in, &t);
    if (t.type == T_EOF) {
        /* (color), (width): true when there is such a thing */
        media_feature(q, g_ieqz(name, "color") || g_ieqz(name, "width") || g_ieqz(name, "height") ? MF_TRUE
                      : MF_FALSE, 0);
        return;
    }
    if (t.type != T_COLON) { media_feature(q, MF_FALSE, 0); return; }
    g_next_nows(&in, &t);
    if (g_ieqz(name, "orientation")) {
        if (t.type == T_IDENT && g_ieq(t.s, t.n, "portrait")) f = MF_PORTRAIT;
        else if (t.type == T_IDENT && g_ieq(t.s, t.n, "landscape")) f = MF_LANDSCAPE;
        else f = MF_FALSE;
        media_feature(q, f, 0);
        return;
    }
    if (g_ieqz(name, "min-width")) f = MF_MIN_WIDTH;
    else if (g_ieqz(name, "max-width")) f = MF_MAX_WIDTH;
    else if (g_ieqz(name, "min-height")) f = MF_MIN_HEIGHT;
    else if (g_ieqz(name, "max-height")) f = MF_MAX_HEIGHT;
    else if (g_ieqz(name, "width")) f = MF_WIDTH;
    else if (g_ieqz(name, "height")) f = MF_HEIGHT;
    if (!f || !media_length(&t, &v)) {
        media_feature(q, MF_FALSE, 0);
        return;
    }
    g_next_nows(&in, &t);
    media_feature(q, t.type == T_EOF ? f : MF_FALSE, v);
}

int g_parse_media_list(gfcss_sheet *s, const char *p, const char *end, int32_t parent)
{
    glex lx;
    gtok t;
    gmedia m;
    m.parent = parent;
    m.first = (uint32_t)s->nqueries;
    m.count = 0;
    m.ok = 1;
    g_lex_init(&lx, p, end);
    g_next_nows(&lx, &t);
    while (t.type != T_EOF) {
        gquery q;
        int state = 0;          /* 0: at its start; 1: after the type or a feature (and may follow) */
        memset(&q, 0, sizeof(q));
        q.type = MT_ALL;
        while (t.type != T_EOF && t.type != T_COMMA) {
            if (t.type == T_IDENT && state == 0 && g_ieq(t.s, t.n, "not") && !q.not_) {
                q.not_ = 1;
            } else if (t.type == T_IDENT && state == 0 && g_ieq(t.s, t.n, "only")) {
                /* nothing: only hides a query from old browsers */
            } else if (t.type == T_IDENT && state == 0) {
                if (g_ieq(t.s, t.n, "all")) q.type = MT_ALL;
                else if (g_ieq(t.s, t.n, "screen")) q.type = MT_SCREEN;
                else if (g_ieq(t.s, t.n, "print")) q.type = MT_PRINT;
                else q.type = MT_OTHER;
                state = 1;
            } else if (t.type == T_IDENT && state == 1 && g_ieq(t.s, t.n, "and")) {
                state = 2;
            } else if (t.type == T_LPAREN && (state == 0 || state == 2)) {
                parse_feature(&lx, &q);
                state = 1;
            } else {
                /* not a query we read: it is "not all" */
                media_feature(&q, MF_FALSE, 0);
                q.not_ = 0;
                while (t.type != T_EOF && t.type != T_COMMA) {
                    if (t.type == T_LPAREN || t.type == T_FUNC) lx.p = g_skip_block(lx.p, lx.end, T_RPAREN);
                    g_next(&lx, &t);
                }
                break;
            }
            g_next_nows(&lx, &t);
        }
        if (state == 2) media_feature(&q, MF_FALSE, 0);    /* "screen and" */
        if (!g_grow((void **)&s->queries, &s->queries_cap, s->nqueries + 1, sizeof(gquery))) {
            s->decls.oom = 1;
            return -1;
        }
        s->queries[s->nqueries++] = q;
        ++m.count;
        if (t.type == T_COMMA) g_next_nows(&lx, &t);
    }
    if (!g_grow((void **)&s->media, &s->media_cap, s->nmedia + 1, sizeof(gmedia))) {
        s->decls.oom = 1;
        return -1;
    }
    s->media[s->nmedia] = m;
    return (int)s->nmedia++;
}

static int query_holds(const gfcss_engine *e, const gquery *q)
{
    int ok = 1, i;
    if (q->type == MT_OTHER) ok = 0;
    else if (q->type == MT_SCREEN) ok = e->media_type == GFCSS_MEDIA_SCREEN;
    else if (q->type == MT_PRINT) ok = e->media_type == GFCSS_MEDIA_PRINT;
    for (i = 0; ok && i < q->nfeat; ++i) {
        int32_t v = q->val[i];
        switch (q->feat[i]) {
        case MF_MIN_WIDTH: ok = e->vw > 0 && e->vw >= v; break;
        case MF_MAX_WIDTH: ok = e->vw > 0 && e->vw <= v; break;
        case MF_MIN_HEIGHT: ok = e->vh > 0 && e->vh >= v; break;
        case MF_MAX_HEIGHT: ok = e->vh > 0 && e->vh <= v; break;
        case MF_WIDTH: ok = e->vw > 0 && e->vw == v; break;
        case MF_HEIGHT: ok = e->vh > 0 && e->vh == v; break;
        case MF_PORTRAIT: ok = e->vw > 0 && e->vh > 0 && e->vh >= e->vw; break;
        case MF_LANDSCAPE: ok = e->vw > 0 && e->vh > 0 && e->vw > e->vh; break;
        case MF_TRUE: break;
        default: ok = 0; break;
        }
    }
    if (q->nfeat && q->feat[q->nfeat - 1] == MF_FALSE) return 0;   /* unreadable: false even with not */
    return q->not_ ? !ok : ok;
}

int g_media_eval(const gfcss_engine *e, const gfcss_sheet *s, int32_t m)
{
    const gmedia *md;
    uint32_t i;
    if (m < 0 || (size_t)m >= s->nmedia) return 1;
    md = &s->media[m];
    if (md->count == 0) return g_media_eval(e, s, md->parent);
    for (i = 0; i < md->count; ++i)
        if (query_holds(e, &s->queries[md->first + i])) return g_media_eval(e, s, md->parent);
    return 0;
}

static void media_update_sheet(gfcss_engine *e, gfcss_sheet *s, int owner_ok)
{
    size_t i;
    int k;
    s->ok = owner_ok;
    for (i = 0; i < s->nmedia; ++i) s->media[i].ok = g_media_eval(e, s, (int32_t)i);
    for (k = 0; k < s->nimports; ++k)
        if (s->imports[k].sheet)
            media_update_sheet(e, s->imports[k].sheet, owner_ok && g_media_eval(e, s, s->imports[k].media));
}

void g_media_update(gfcss_engine *e)
{
    gfcss_sheet *s;
    for (s = e->sheets; s; s = s->next) media_update_sheet(e, s, 1);
}

/* ---- @supports --------------------------------------------------------------------- */

static int supports_cond(gfcss_engine *e, glex *lx, gtok *t, int depth);

/* ( condition ) or ( declaration ), after the '(' */
static int supports_in_parens(gfcss_engine *e, glex *lx, int depth)
{
    const char *start = lx->p, *close = g_skip_block(lx->p, lx->end, T_RPAREN);
    const char *end = close > start && close[-1] == ')' ? close - 1 : close;
    glex in;
    gtok t;
    lx->p = close;
    g_lex_init(&in, start, end);
    g_next_nows(&in, &t);
    if (t.type == T_IDENT && !(g_ieq(t.s, t.n, "not"))) {
        glex save = in;
        gtok name = t;
        g_next_nows(&in, &t);
        if (t.type == T_COLON) {
            gdecl d[24];
            gcalc c[4];
            gsink sink;
            memset(&sink, 0, sizeof(sink));
            sink.d = d;
            sink.cap = 24;
            sink.calc = c;
            sink.calc_cap = 4;
            sink.fixed = 1;
            sink.base = start;
            return g_parse_declaration(e, &sink, name.s, name.n, in.p, end, 0);
        }
        in = save;
        t = name;
    }
    return supports_cond(e, &in, &t, depth + 1);
}

static int supports_cond(gfcss_engine *e, glex *lx, gtok *t, int depth)
{
    int v, op = 0;
    if (depth > 16) return 0;
    if (t->type == T_IDENT && g_ieq(t->s, t->n, "not")) {
        g_next_nows(lx, t);
        if (t->type != T_LPAREN) return 0;
        v = !supports_in_parens(e, lx, depth);
        g_next_nows(lx, t);
        return t->type == T_EOF || t->type == T_RPAREN ? v : 0;
    }
    if (t->type != T_LPAREN) return 0;
    v = supports_in_parens(e, lx, depth);
    for (;;) {
        int w;
        g_next_nows(lx, t);
        if (t->type == T_EOF) return v;
        if (t->type != T_IDENT) return 0;
        if (g_ieq(t->s, t->n, "and")) { if (op == 2) return 0; op = 1; }
        else if (g_ieq(t->s, t->n, "or")) { if (op == 1) return 0; op = 2; }
        else return 0;
        g_next_nows(lx, t);
        if (t->type != T_LPAREN) return 0;
        w = supports_in_parens(e, lx, depth);
        v = op == 1 ? v && w : v || w;
    }
}

static int supports(gfcss_engine *e, const char *p, const char *end)
{
    glex lx;
    gtok t;
    g_lex_init(&lx, p, end);
    g_next_nows(&lx, &t);
    return supports_cond(e, &lx, &t, 0);
}

/* ---- Declarations ----------------------------------------------------------------- */

/* name: value up to ';' or the end, !important taken off */
void g_parse_declarations(gfcss_engine *e, gsink *sink, const char *p, const char *end)
{
    glex lx;
    gtok t;
    g_lex_init(&lx, p, end);
    for (;;) {
        g_next_nows(&lx, &t);
        if (t.type == T_EOF) return;
        if (t.type == T_SEMI) continue;
        if (t.type == T_IDENT) {
            gtok name = t;
            char nbuf[64];
            const char *vstart, *bang = NULL, *last_end = NULL;
            int important = 0, after_bang = 0;
            if (name.s == t.buf) {
                memcpy(nbuf, t.buf, sizeof(nbuf));
                name.s = nbuf;
            }
            g_next_nows(&lx, &t);
            if (t.type != T_COLON) goto skip;
            vstart = lx.p;
            /* the value: to ';' at the top level; the last two tokens may be "! important" */
            for (;;) {
                const char *at = lx.p;
                g_next(&lx, &t);
                if (t.type == T_EOF || t.type == T_SEMI) {
                    lx.p = t.type == T_SEMI ? lx.p : lx.p;
                    if (t.type == T_SEMI) {
                        /* the value ends before the ';' */
                        if (!last_end) last_end = at;
                    }
                    break;
                }
                if (t.type == T_WS) continue;
                if (t.type == T_DELIM && t.delim == '!') {
                    bang = t.start;
                    after_bang = 1;
                    last_end = t.stop;
                    continue;
                }
                if (after_bang == 1 && t.type == T_IDENT && g_ieq(t.s, t.n, "important")) {
                    after_bang = 2;
                    last_end = t.stop;
                    continue;
                }
                after_bang = 0;
                bang = NULL;
                if (t.type == T_LBRACE || t.type == T_LBRACK || t.type == T_LPAREN || t.type == T_FUNC)
                    lx.p = g_skip_block(lx.p, lx.end, t.type == T_LBRACE ? T_RBRACE
                                                     : t.type == T_LBRACK ? T_RBRACK : T_RPAREN);
                last_end = lx.p;
            }
            if (after_bang == 2 && bang) {
                important = 1;
                last_end = bang;
            } else if (after_bang) {
                continue;       /* a '!' that isn't !important: the declaration is bad */
            }
            if (!last_end) last_end = vstart;
            g_parse_declaration(e, sink, name.s, name.n, vstart, last_end, important);
            if (t.type == T_EOF) return;
            continue;
        }
        if (t.type == T_AT) {
            /* an at-rule in a block: skipped with its block or up to its ';' */
            for (;;) {
                g_next(&lx, &t);
                if (t.type == T_EOF || t.type == T_SEMI) break;
                if (t.type == T_LBRACE) {
                    lx.p = g_skip_block(lx.p, lx.end, T_RBRACE);
                    break;
                }
                if (t.type == T_LBRACK || t.type == T_LPAREN || t.type == T_FUNC)
                    lx.p = g_skip_block(lx.p, lx.end, t.type == T_LBRACK ? T_RBRACK : T_RPAREN);
            }
            continue;
        }
    skip:
        /* not a declaration: up to the next ';' */
        while (t.type != T_EOF && t.type != T_SEMI) {
            if (t.type == T_LBRACE || t.type == T_LBRACK || t.type == T_LPAREN || t.type == T_FUNC)
                lx.p = g_skip_block(lx.p, lx.end, t.type == T_LBRACE ? T_RBRACE
                                                 : t.type == T_LBRACK ? T_RBRACK : T_RPAREN);
            g_next(&lx, &t);
        }
        if (t.type == T_EOF) return;
    }
}

/* ---- Stylesheets ------------------------------------------------------------------- */

static int add_import(gfcss_sheet *s, const char *p, const char *end)
{
    glex lx;
    gtok t;
    char url[512];
    size_t n;
    gimport im;
    g_lex_init(&lx, p, end);
    g_next_nows(&lx, &t);
    if (t.type == T_FUNC && g_ieq(t.s, t.n, "url")) {
        g_next_nows(&lx, &t);
        if (t.type != T_STRING) return 0;
        n = gfcss_unescape(t.s, t.n, url, sizeof(url));
        g_next_nows(&lx, &t);
        if (t.type != T_RPAREN) return 0;
    } else if (t.type == T_STRING || t.type == T_URL) {
        n = gfcss_unescape(t.s, t.n, url, sizeof(url));
    } else {
        return 0;
    }
    if (n >= sizeof(url)) n = sizeof(url) - 1;
    memset(&im, 0, sizeof(im));
    im.url = g_pool_add(s, url, n, 0);
    im.media = g_parse_media_list(s, lx.p, end, -1);
    if (s->decls.oom) return 0;
    if (s->nimports == s->imports_cap) {
        int cap = s->imports_cap ? s->imports_cap * 2 : 4;
        gimport *q = (gimport *)realloc(s->imports, (size_t)cap * sizeof(gimport));
        if (!q) { s->decls.oom = 1; return 0; }
        s->imports = q;
        s->imports_cap = cap;
    }
    s->imports[s->nimports++] = im;
    return 1;
}

/* A list of rules from p to end; media: the @media they are in (-1: none) */
static void parse_rules(gfcss_sheet *s, const char *p, const char *end, int32_t media, int top)
{
    glex lx;
    gtok t;
    g_lex_init(&lx, p, end);
    for (;;) {
        const char *start;
        g_next(&lx, &t);
        if (s->decls.oom) return;
        if (t.type == T_EOF) return;
        if (t.type == T_WS || (top && (t.type == T_CDO || t.type == T_CDC))) continue;
        if (t.type == T_AT) {
            char name[32];
            size_t nn = t.n < sizeof(name) - 1 ? t.n : sizeof(name) - 1;
            const char *prelude = lx.p, *pend;
            int block = 0;
            memcpy(name, t.s, nn);
            name[nn] = 0;
            /* the prelude: to '{' or ';' */
            for (;;) {
                pend = lx.p;
                g_next(&lx, &t);
                if (t.type == T_EOF || t.type == T_SEMI) break;
                if (t.type == T_LBRACE) { block = 1; break; }
                if (t.type == T_LBRACK || t.type == T_LPAREN || t.type == T_FUNC)
                    lx.p = g_skip_block(lx.p, lx.end, t.type == T_LBRACK ? T_RBRACK : T_RPAREN);
            }
            if (block) {
                const char *bstart = lx.p, *bend = g_skip_block(lx.p, lx.end, T_RBRACE);
                const char *inner_end = bend > bstart && bend[-1] == '}' ? bend - 1 : bend;
                lx.p = bend;
                if (g_ieqz(name, "media")) {
                    int32_t m = g_parse_media_list(s, prelude, pend, media);
                    if (m >= 0) parse_rules(s, bstart, inner_end, m, 0);
                    s->rules_seen = 1;
                } else if (g_ieqz(name, "supports")) {
                    if (supports(s->engine, prelude, pend)) parse_rules(s, bstart, inner_end, media, 0);
                    s->rules_seen = 1;
                }
                /* @font-face, @keyframes, @page and the rest: skipped, block and all */
            } else if (g_ieqz(name, "import") && top && !s->rules_seen && media < 0) {
                add_import(s, prelude, pend);
            }
            /* @charset, @namespace: nothing to do */
            continue;
        }
        /* a style rule: its selectors up to '{', then its block */
        start = t.start;
        while (t.type != T_LBRACE && t.type != T_EOF) {
            if (t.type == T_LBRACK || t.type == T_LPAREN || t.type == T_FUNC)
                lx.p = g_skip_block(lx.p, lx.end, t.type == T_LBRACK ? T_RBRACK : T_RPAREN);
            g_next(&lx, &t);
        }
        if (t.type == T_EOF) return;    /* no block: nothing */
        s->rules_seen = 1;
        {
            const char *sel_end = t.start, *bstart = lx.p, *bend = g_skip_block(lx.p, lx.end, T_RBRACE);
            const char *inner_end = bend > bstart && bend[-1] == '}' ? bend - 1 : bend;
            uint32_t r = (uint32_t)s->nrules;
            lx.p = bend;
            if (!g_grow((void **)&s->rules, &s->rules_cap, s->nrules + 1, sizeof(grule))) {
                s->decls.oom = 1;
                return;
            }
            if (!g_parse_selectors(s, start, sel_end, r)) continue;
            s->rules[r].decl_first = (uint32_t)s->decls.n;
            s->rules[r].media = media;
            s->decls.base = s->text;
            g_parse_declarations(s->engine, &s->decls, bstart, inner_end);
            s->rules[r].decl_count = (uint32_t)(s->decls.n - s->rules[r].decl_first);
            s->nrules++;
        }
    }
}

int gfcss_sheet_parse(gfcss_sheet *s, const char *css, size_t length)
{
    size_t at;
    if (!s) return 0;
    if (!css || !length) return 1;
    if (!g_grow((void **)&s->text, &s->text_cap, s->text_n + length + 1, 1)) return 0;
    at = s->text_n;
    memcpy(s->text + at, css, length);
    s->text_n += length;
    s->text[s->text_n] = 0;
    s->decls.oom = 0;
    parse_rules(s, s->text + at, s->text + at + length, -1, 1);
    g_index_dirty(s->engine);
    return !s->decls.oom;
}

size_t gfcss_sheet_rules(const gfcss_sheet *s)
{
    return s ? s->nsels : 0;
}

uint32_t gfcss_sheet_states(const gfcss_sheet *s)
{
    uint32_t st;
    int i;
    if (!s) return 0;
    st = s->states;
    for (i = 0; i < s->nimports; ++i) st |= gfcss_sheet_states(s->imports[i].sheet);
    return st;
}

uint32_t gfcss_states(const gfcss_engine *e)
{
    const gfcss_sheet *s;
    uint32_t st = 0;
    if (!e) return 0;
    for (s = e->sheets; s; s = s->next) st |= gfcss_sheet_states(s);
    return st;
}

int gfcss_sheet_import_count(const gfcss_sheet *s)
{
    return s ? s->nimports : 0;
}

const char *gfcss_sheet_import_url(const gfcss_sheet *s, int i)
{
    if (!s || i < 0 || i >= s->nimports) return NULL;
    return s->pool + s->imports[i].url;
}

int gfcss_sheet_import_applies(const gfcss_sheet *s, int i)
{
    if (!s || i < 0 || i >= s->nimports) return 0;
    return g_media_eval(s->engine, s, s->imports[i].media);
}

static gfcss_sheet *sheet_alloc(gfcss_engine *e, int origin)
{
    gfcss_sheet *s = (gfcss_sheet *)calloc(1, sizeof(gfcss_sheet));
    if (!s) return NULL;
    s->engine = e;
    s->origin = origin;
    s->ok = 1;
    return s;
}

gfcss_sheet *gfcss_sheet_import(gfcss_sheet *s, int i)
{
    if (!s || i < 0 || i >= s->nimports) return NULL;
    if (!s->imports[i].sheet) {
        gfcss_sheet *c = sheet_alloc(s->engine, s->origin);
        if (!c) return NULL;
        c->owner = s;
        s->imports[i].sheet = c;
        g_index_dirty(s->engine);
    }
    return s->imports[i].sheet;
}

gfcss_sheet *gfcss_sheet_new(gfcss_engine *e, int origin)
{
    gfcss_sheet *s, **p;
    if (!e) return NULL;
    if (!(s = sheet_alloc(e, origin))) return NULL;
    for (p = &e->sheets; *p; p = &(*p)->next) {}
    *p = s;
    g_index_dirty(e);
    return s;
}

static void sheet_release(gfcss_sheet *s)
{
    int i;
    for (i = 0; i < s->nimports; ++i)
        if (s->imports[i].sheet) sheet_release(s->imports[i].sheet);
    free(s->text);
    free(s->pool);
    free(s->rules);
    free(s->sels);
    free(s->comps);
    free(s->simples);
    free(s->decls.d);
    free(s->decls.calc);
    free(s->media);
    free(s->queries);
    free(s->imports);
    free(s);
}

void gfcss_sheet_free(gfcss_sheet *s)
{
    gfcss_engine *e;
    if (!s) return;
    e = s->engine;
    if (s->owner) {
        int i;
        for (i = 0; i < s->owner->nimports; ++i)
            if (s->owner->imports[i].sheet == s) s->owner->imports[i].sheet = NULL;
    } else {
        gfcss_sheet **p;
        for (p = &e->sheets; *p; p = &(*p)->next)
            if (*p == s) {
                *p = s->next;
                break;
            }
    }
    sheet_release(s);
    g_index_dirty(e);
}

/* ---- The engine ---------------------------------------------------------------------- */

gfcss_engine *gfcss_new(void)
{
    gfcss_engine *e = (gfcss_engine *)calloc(1, sizeof(gfcss_engine));
    if (!e) return NULL;
    e->rem = OL_CSSPX(16);
    e->dirty = 1;
    return e;
}

void gfcss_free(gfcss_engine *e)
{
    if (!e) return;
    while (e->sheets) {
        gfcss_sheet *s = e->sheets;
        e->sheets = s->next;
        sheet_release(s);
    }
    free(e->entries);
    free(e->bucket);
    free(e->inline_sink.d);
    free(e->inline_sink.calc);
    free(e->vscratch);
    free(e);
}

int gfcss_add_state(gfcss_engine *e, const char *name, uint32_t bit)
{
    size_t n;
    if (!e || !name || !bit || e->nstates >= G_MAX_STATES) return 0;
    n = strlen(name);
    if (n >= sizeof(e->states[0].name)) return 0;
    memcpy(e->states[e->nstates].name, name, n + 1);
    e->states[e->nstates++].bit = bit;
    return 1;
}

int gfcss_add_property(gfcss_engine *e, const char *name, unsigned flags, uint32_t initial,
                       gfcss_ext_fn parse, void *userdata)
{
    size_t n;
    int i;
    if (!e || !name || !parse || e->next >= GFCSS_MAX_EXT) return -1;
    n = strlen(name);
    if (n >= sizeof(e->ext[0].name)) return -1;
    i = e->next++;
    for (n = 0; name[n]; ++n) e->ext[i].name[n] = (char)g_lower((unsigned char)name[n]);
    e->ext[i].name[n] = 0;
    e->ext[i].flags = flags;
    e->ext[i].initial = initial;
    e->ext[i].fn = parse;
    e->ext[i].userdata = userdata;
    return i;
}

void gfcss_set_media(gfcss_engine *e, int media, ol_unit width, ol_unit height)
{
    if (!e) return;
    e->media_type = media;
    e->vw = width > 0 ? width : 0;
    e->vh = height > 0 ? height : 0;
    g_media_update(e);
}
