/* GFCSS: matching selectors against the caller's elements, the cascade,
 * computed values, and GFOpenLayout's styles from them.
 *
 * Every sheet's selectors are indexed by their last compound's id, else a
 * class, else its type, so an element is matched only against the rules
 * that could match it: those under its id, each of its classes, its type,
 * and the few with none. The winning declaration of each property is kept
 * as the rules are matched (origin and !important, specificity, order),
 * so nothing is sorted and nothing is allocated for an element.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfcss_internal.h"

/* ---- The index --------------------------------------------------------------- */

void g_index_dirty(gfcss_engine *e)
{
    if (e) e->dirty = 1;
}

enum { K_UNIVERSAL = 0, K_ID = 1, K_CLASS = 2, K_TAG = 3 };

static uint32_t make_key(int kind, uint32_t h)
{
    return (uint32_t)kind << 30 | (h & 0x3fffffffu);
}

static uint32_t bucket_of(const gfcss_engine *e, uint32_t key)
{
    uint32_t h = key * 2654435761u;
    return (h ^ (h >> 16)) & (e->nbuckets - 1);
}

static uint32_t sel_key(const gfcss_sheet *s, const gselector *sel)
{
    const gcompound *c = &s->comps[sel->first + sel->count - 1];
    uint32_t k, cls = 0, tag = 0;
    int have_cls = 0, have_tag = 0;
    for (k = 0; k < c->count; ++k) {
        const gsimple *ss = &s->simples[c->first + k];
        const char *nm = s->pool + ss->name;
        if (ss->type == SS_ID) return make_key(K_ID, g_hash(nm, strlen(nm), 0));
        if (ss->type == SS_CLASS && !have_cls) {
            cls = make_key(K_CLASS, g_hash(nm, strlen(nm), 0));
            have_cls = 1;
        }
        if (ss->type == SS_TAG && !have_tag) {
            tag = make_key(K_TAG, g_hash(nm, strlen(nm), 1));
            have_tag = 1;
        }
    }
    if (have_cls) return cls;
    if (have_tag) return tag;
    return make_key(K_UNIVERSAL, 0);
}

static int count_sheet(const gfcss_sheet *s, size_t *n)
{
    int i;
    for (i = 0; i < s->nimports; ++i)
        if (s->imports[i].sheet) count_sheet(s->imports[i].sheet, n);
    *n += s->nsels;
    return 1;
}

/* entries in cascade order: a sheet's imports, then its own rules */
static void fill_sheet(gfcss_engine *e, const gfcss_sheet *s, uint32_t *order)
{
    size_t k;
    int i;
    uint32_t base;
    for (i = 0; i < s->nimports; ++i)
        if (s->imports[i].sheet) fill_sheet(e, s->imports[i].sheet, order);
    base = *order;
    for (k = 0; k < s->nsels; ++k) {
        gentry *en = &e->entries[e->nentries++];
        en->key = sel_key(s, &s->sels[k]);
        en->sheet = s;
        en->sel = (uint32_t)k;
        en->order = base + s->sels[k].rule;
    }
    *order = base + (uint32_t)s->nrules;
    if (s->has_pseudo) e->any_pseudo = 1;
}

static int build_index(gfcss_engine *e)
{
    size_t n = 0, i, nk = 0;
    const gfcss_sheet *s;
    gentry *sorted;
    uint32_t order = 0, nb = 16, *at;
    for (s = e->sheets; s; s = s->next) count_sheet(s, &n);
    if (!g_grow((void **)&e->entries, &e->entries_cap, n ? n : 1, sizeof(gentry))) return 0;
    e->nentries = 0;
    e->any_pseudo = 0;
    for (s = e->sheets; s; s = s->next) fill_sheet(e, s, &order);
    while (nb < n) nb *= 2;
    free(e->bucket);
    e->bucket = (uint32_t *)calloc((size_t)nb + 2, sizeof(uint32_t));
    sorted = (gentry *)malloc((n ? n : 1) * sizeof(gentry));
    if (!e->bucket || !sorted) {
        free(sorted);
        free(e->bucket);
        e->bucket = NULL;
        return 0;
    }
    e->nbuckets = nb;
    /* counting sort: the universal ones last, the rest by bucket */
    for (i = 0; i < n; ++i) {
        if (e->entries[i].key >> 30 == K_UNIVERSAL) continue;
        e->bucket[bucket_of(e, e->entries[i].key) + 1]++;
        ++nk;
    }
    for (i = 0; i < nb; ++i) e->bucket[i + 1] += e->bucket[i];
    at = (uint32_t *)malloc((size_t)nb * sizeof(uint32_t));
    if (!at) {
        free(sorted);
        return 0;
    }
    memcpy(at, e->bucket, (size_t)nb * sizeof(uint32_t));
    e->first_universal = nk;
    for (i = 0; i < n; ++i) {
        const gentry *en = &e->entries[i];
        if (en->key >> 30 == K_UNIVERSAL) sorted[nk++] = *en;
        else sorted[at[bucket_of(e, en->key)]++] = *en;
    }
    free(at);
    memcpy(e->entries, sorted, n * sizeof(gentry));
    free(sorted);
    g_media_update(e);
    e->dirty = 0;
    return 1;
}

/* ---- Matching -------------------------------------------------------------- */

typedef struct {
    gfcss_engine *e;
    const gfcss_element_ops *ops;
    void *ud;
} gm;

static int has_class(const char *list, const char *c)
{
    size_t n = strlen(c);
    const char *p = list;
    if (!p || !n) return 0;
    while (*p) {
        while (*p && g_ws((unsigned char)*p)) ++p;
        if (!*p) break;
        size_t k = 0;
        while (k < n && p[k] == c[k]) ++k;
        if (k == n && (p[n] == 0 || g_ws((unsigned char)p[n]))) return 1;
        while (*p && !g_ws((unsigned char)*p)) ++p;
    }
    return 0;
}

static int child_index(const gm *m, const void *el, const gfcss_info *in)
{
    int n = 1;
    if (in->index > 0) return in->index;
    if (m->ops->index) return m->ops->index(m->ud, el);
    for (el = m->ops->prev(m->ud, el); el; el = m->ops->prev(m->ud, el)) ++n;
    return n;
}

/* how many after it; -1: not known */
static int after_count(const gm *m, const void *el, const gfcss_info *in)
{
    int n = 0;
    if (in->count > 0 && in->index > 0) return in->count - in->index;
    if (!m->ops->next) {
        if (in->count > 0) return in->count - child_index(m, el, in);
        return -1;
    }
    for (el = m->ops->next(m->ud, el); el; el = m->ops->next(m->ud, el)) ++n;
    return n;
}

static int same_tag(const char *a, const char *b)
{
    if (!a || !b) return 0;
    return g_ieqz(a, b);
}

/* of its type before (dir 0) or after (dir 1) it */
static int type_count(const gm *m, const void *el, const char *tag, int dir)
{
    int n = 0;
    const void *(*step)(void *, const void *) = dir ? m->ops->next : m->ops->prev;
    if (!step) return -1;
    for (el = step(m->ud, el); el; el = step(m->ud, el)) {
        gfcss_info si;
        memset(&si, 0, sizeof(si));
        m->ops->info(m->ud, el, &si);
        if (same_tag(si.tag, tag)) ++n;
    }
    return n;
}

static int nth(int32_t a, int32_t b, int n)
{
    if (a == 0) return n == b;
    return (n - b) / a >= 0 && (n - b) % a == 0;
}

static int attr_matches(const gsimple *ss, const char *v, const char *want)
{
    size_t n = strlen(want), vn;
    size_t k;
    if (!v) return 0;
    vn = strlen(v);
#define CEQ(x, y) (ss->icase ? g_lower((unsigned char)(x)) == g_lower((unsigned char)(y)) : (x) == (y))
    switch (ss->op) {
    case A_EXISTS: return 1;
    case A_EQ:
        if (vn != n) return 0;
        for (k = 0; k < n; ++k) if (!CEQ(v[k], want[k])) return 0;
        return 1;
    case A_PREFIX:
        if (!n || vn < n) return 0;
        for (k = 0; k < n; ++k) if (!CEQ(v[k], want[k])) return 0;
        return 1;
    case A_SUFFIX:
        if (!n || vn < n) return 0;
        for (k = 0; k < n; ++k) if (!CEQ(v[vn - n + k], want[k])) return 0;
        return 1;
    case A_DASH:
        if (vn < n) return 0;
        for (k = 0; k < n; ++k) if (!CEQ(v[k], want[k])) return 0;
        return vn == n || v[n] == '-';
    case A_SUBSTR: {
        size_t s;
        if (!n || vn < n) return 0;
        for (s = 0; s + n <= vn; ++s) {
            for (k = 0; k < n && CEQ(v[s + k], want[k]); ++k) {}
            if (k == n) return 1;
        }
        return 0;
    }
    case A_INCLUDES: {
        const char *p = v;
        if (!n) return 0;
        for (k = 0; k < n; ++k) if (g_ws((unsigned char)want[k])) return 0;
        while (*p) {
            size_t w = 0;
            while (*p && g_ws((unsigned char)*p)) ++p;
            while (p[w] && !g_ws((unsigned char)p[w])) ++w;
            if (w == n) {
                for (k = 0; k < n && CEQ(p[k], want[k]); ++k) {}
                if (k == n) return 1;
            }
            p += w;
        }
        return 0;
    }
    default: return 0;
    }
#undef CEQ
}

static int match_compound(const gm *m, const gfcss_sheet *s, const gcompound *c, const void *el,
                          const gfcss_info *in)
{
    uint32_t k;
    for (k = 0; k < c->count; ++k) {
        const gsimple *ss = &s->simples[c->first + k];
        const char *nm = s->pool + ss->name;
        int idx, aft;
        switch (ss->type) {
        case SS_TAG: if (!same_tag(in->tag, nm)) return 0; break;
        case SS_ID: if (!in->id || strcmp(in->id, nm)) return 0; break;
        case SS_CLASS: if (!has_class(in->classes, nm)) return 0; break;
        case SS_ATTR:
            if (!m->ops->attr || !attr_matches(ss, m->ops->attr(m->ud, el, nm), s->pool + ss->value)) return 0;
            break;
        case SS_STATE: if ((in->state & (uint32_t)ss->a) != (uint32_t)ss->a) return 0; break;
        case SS_ROOT: if (m->ops->parent(m->ud, el)) return 0; break;
        case SS_EMPTY: if (!in->empty) return 0; break;
        case SS_FIRST_CHILD: if (child_index(m, el, in) != 1) return 0; break;
        case SS_LAST_CHILD: if (after_count(m, el, in) != 0) return 0; break;
        case SS_ONLY_CHILD: if (child_index(m, el, in) != 1 || after_count(m, el, in) != 0) return 0; break;
        case SS_NTH_CHILD: if (!nth(ss->a, ss->b, child_index(m, el, in))) return 0; break;
        case SS_NTH_LAST_CHILD:
            aft = after_count(m, el, in);
            if (aft < 0 || !nth(ss->a, ss->b, aft + 1)) return 0;
            break;
        case SS_FIRST_OF_TYPE: if (type_count(m, el, in->tag, 0) != 0) return 0; break;
        case SS_LAST_OF_TYPE: if (type_count(m, el, in->tag, 1) != 0) return 0; break;
        case SS_ONLY_OF_TYPE:
            if (type_count(m, el, in->tag, 0) != 0 || type_count(m, el, in->tag, 1) != 0) return 0;
            break;
        case SS_NTH_OF_TYPE:
            idx = type_count(m, el, in->tag, 0);
            if (!nth(ss->a, ss->b, idx + 1)) return 0;
            break;
        case SS_NTH_LAST_OF_TYPE:
            aft = type_count(m, el, in->tag, 1);
            if (aft < 0 || !nth(ss->a, ss->b, aft + 1)) return 0;
            break;
        case SS_NOT: {
            int32_t j;
            for (j = 0; j < ss->b; ++j)
                if (match_compound(m, s, &s->comps[ss->a + j], el, in)) return 0;
            break;
        }
        default: return 0;
        }
    }
    return 1;
}

static int match_from(const gm *m, const gfcss_sheet *s, const gselector *sel, int k, const void *el,
                      const gfcss_info *known)
{
    const gcompound *c = &s->comps[sel->first + (uint32_t)k];
    gfcss_info in;
    const void *o;
    if (!known) {
        memset(&in, 0, sizeof(in));
        m->ops->info(m->ud, el, &in);
        known = &in;
    }
    if (!match_compound(m, s, c, el, known)) return 0;
    if (k == 0) return 1;
    switch (c->comb) {
    case '>':
        o = m->ops->parent(m->ud, el);
        return o && match_from(m, s, sel, k - 1, o, NULL);
    case '+':
        o = m->ops->prev(m->ud, el);
        return o && match_from(m, s, sel, k - 1, o, NULL);
    case '~':
        for (o = m->ops->prev(m->ud, el); o; o = m->ops->prev(m->ud, o))
            if (match_from(m, s, sel, k - 1, o, NULL)) return 1;
        return 0;
    default:
        for (o = m->ops->parent(m->ud, el); o; o = m->ops->parent(m->ud, o))
            if (match_from(m, s, sel, k - 1, o, NULL)) return 1;
        return 0;
    }
}

/* ---- The cascade ---------------------------------------------------------- */

static void win_reset(gfcss_engine *e)
{
    int i;
    for (i = 0; i < e->ntouched; ++i) e->win[e->touched[i]].d = NULL;
    e->ntouched = 0;
}

static void offer(gfcss_engine *e, const gdecl *d, const gfcss_sheet *s, uint32_t hi, uint32_t lo)
{
    gwin *w;
    if (d->prop >= G_PROPS) return;
    w = &e->win[d->prop];
    if (!w->d) {
        e->touched[e->ntouched++] = d->prop;
    } else if (hi < w->hi || (hi == w->hi && lo < w->lo)) {
        return;
    }
    w->d = d;
    w->sheet = s;
    w->hi = hi;
    w->lo = lo;
}

static uint32_t tier(int origin, int important, int inline_style)
{
    if (!important) return inline_style ? 3u : (uint32_t)origin;
    if (inline_style) return 5u;
    return origin == GFCSS_ORIGIN_AUTHOR ? 4u : origin == GFCSS_ORIGIN_USER ? 6u : 7u;
}

static int try_entry(gfcss_engine *e, const gm *m, const gentry *en, int pseudo, const void *el,
                     const gfcss_info *in)
{
    const gfcss_sheet *s = en->sheet;
    const gselector *sel = &s->sels[en->sel];
    const grule *r = &s->rules[sel->rule];
    uint32_t k;
    if (sel->pseudo != pseudo || !s->ok) return 0;
    if (r->media >= 0 && !s->media[r->media].ok) return 0;
    if (!match_from(m, s, sel, sel->count - 1, el, in)) return 0;
    for (k = 0; k < r->decl_count; ++k) {
        const gdecl *d = &s->decls.d[r->decl_first + k];
        offer(e, d, s, tier(s->origin, d->flags & D_IMPORTANT, 0) << 24 | sel->spec, en->order);
    }
    return 1;
}

static int try_bucket(gfcss_engine *e, const gm *m, uint32_t key, int pseudo, const void *el,
                      const gfcss_info *in)
{
    uint32_t b = bucket_of(e, key), i;
    int hit = 0;
    for (i = e->bucket[b]; i < e->bucket[b + 1]; ++i)
        if (e->entries[i].key == key) hit |= try_entry(e, m, &e->entries[i], pseudo, el, in);
    return hit;
}

/* ---- Computed values ------------------------------------------------------ */

static void len_set(gfcss_length *l, int kind, ol_unit units, int32_t pct)
{
    l->kind = kind;
    l->units = units;
    l->pct = pct;
}

void gfcss_computed_init(gfcss_computed *cs)
{
    int k;
    if (!cs) return;
    memset(cs, 0, sizeof(*cs));
    cs->size = sizeof(*cs);
    cs->display = GFCSS_DISPLAY_INLINE;
    cs->z_auto = 1;
    len_set(&cs->width, GFCSS_LEN_AUTO, 0, 0);
    len_set(&cs->height, GFCSS_LEN_AUTO, 0, 0);
    len_set(&cs->min_width, GFCSS_LEN_AUTO, 0, 0);
    len_set(&cs->min_height, GFCSS_LEN_AUTO, 0, 0);
    len_set(&cs->max_width, GFCSS_LEN_NONE, 0, 0);
    len_set(&cs->max_height, GFCSS_LEN_NONE, 0, 0);
    for (k = 0; k < 4; ++k) cs->border_color[k] = 0xff000000u;
    cs->outline_color = 0xff000000u;
    cs->color = 0xff000000u;
    cs->opacity = 1000;
    cs->font_size = OL_CSSPX(16);
    cs->font_weight = 400;
    cs->line_height_kind = GFCSS_LINE_NORMAL;
    cs->flex_shrink = 1000;
    len_set(&cs->flex_basis, GFCSS_LEN_AUTO, 0, 0);
    len_set(&cs->row_gap, GFCSS_LEN_NORMAL, 0, 0);
    len_set(&cs->column_gap, GFCSS_LEN_NORMAL, 0, 0);
    cs->content.kind = GFCSS_TEXT_NORMAL;
    for (k = 0; k < 4; ++k) len_set(&cs->inset[k], GFCSS_LEN_AUTO, 0, 0);
}

int gfcss_is_set(const gfcss_computed *cs, int p)
{
    if (!cs || p < 0 || p >= G_PROPS) return 0;
    return (cs->set[p >> 5] >> (p & 31)) & 1u;
}

static void mark_set(gfcss_computed *cs, int p)
{
    cs->set[p >> 5] |= 1u << (p & 31);
}

/* one property's value from src (the parent's for inherit, the initial ones) */
static void copy_prop(gfcss_computed *d, const gfcss_computed *s, int p)
{
    int k;
    if (p >= GFCSS_P_COUNT) {
        d->ext[p - GFCSS_P_COUNT] = s->ext[p - GFCSS_P_COUNT];
        return;
    }
    switch (p) {
    case GFCSS_P_DISPLAY: d->display = s->display; break;
    case GFCSS_P_POSITION: d->position = s->position; break;
    case GFCSS_P_FLOAT: d->floating = s->floating; break;
    case GFCSS_P_CLEAR: d->clear = s->clear; break;
    case GFCSS_P_Z_INDEX: d->z_index = s->z_index; d->z_auto = s->z_auto; break;
    case GFCSS_P_VISIBILITY: d->visibility = s->visibility; break;
    case GFCSS_P_OVERFLOW_X: d->overflow_x = s->overflow_x; break;
    case GFCSS_P_OVERFLOW_Y: d->overflow_y = s->overflow_y; break;
    case GFCSS_P_BOX_SIZING: d->box_sizing = s->box_sizing; break;
    case GFCSS_P_WIDTH: d->width = s->width; break;
    case GFCSS_P_HEIGHT: d->height = s->height; break;
    case GFCSS_P_MIN_WIDTH: d->min_width = s->min_width; break;
    case GFCSS_P_MIN_HEIGHT: d->min_height = s->min_height; break;
    case GFCSS_P_MAX_WIDTH: d->max_width = s->max_width; break;
    case GFCSS_P_MAX_HEIGHT: d->max_height = s->max_height; break;
    case GFCSS_P_MARGIN_TOP: case GFCSS_P_MARGIN_RIGHT: case GFCSS_P_MARGIN_BOTTOM: case GFCSS_P_MARGIN_LEFT:
        k = p - GFCSS_P_MARGIN_TOP; d->margin[k] = s->margin[k]; break;
    case GFCSS_P_PADDING_TOP: case GFCSS_P_PADDING_RIGHT: case GFCSS_P_PADDING_BOTTOM: case GFCSS_P_PADDING_LEFT:
        k = p - GFCSS_P_PADDING_TOP; d->padding[k] = s->padding[k]; break;
    case GFCSS_P_BORDER_TOP_WIDTH: case GFCSS_P_BORDER_RIGHT_WIDTH: case GFCSS_P_BORDER_BOTTOM_WIDTH:
    case GFCSS_P_BORDER_LEFT_WIDTH:
        k = p - GFCSS_P_BORDER_TOP_WIDTH; d->border_width[k] = s->border_width[k]; break;
    case GFCSS_P_BORDER_TOP_STYLE: case GFCSS_P_BORDER_RIGHT_STYLE: case GFCSS_P_BORDER_BOTTOM_STYLE:
    case GFCSS_P_BORDER_LEFT_STYLE:
        k = p - GFCSS_P_BORDER_TOP_STYLE; d->border_style[k] = s->border_style[k]; break;
    case GFCSS_P_BORDER_TOP_COLOR: case GFCSS_P_BORDER_RIGHT_COLOR: case GFCSS_P_BORDER_BOTTOM_COLOR:
    case GFCSS_P_BORDER_LEFT_COLOR:
        k = p - GFCSS_P_BORDER_TOP_COLOR; d->border_color[k] = s->border_color[k]; break;
    case GFCSS_P_BORDER_TOP_LEFT_RADIUS: case GFCSS_P_BORDER_TOP_RIGHT_RADIUS:
    case GFCSS_P_BORDER_BOTTOM_RIGHT_RADIUS: case GFCSS_P_BORDER_BOTTOM_LEFT_RADIUS:
        k = p - GFCSS_P_BORDER_TOP_LEFT_RADIUS; d->border_radius[k] = s->border_radius[k]; break;
    case GFCSS_P_OUTLINE_WIDTH: d->outline_width = s->outline_width; break;
    case GFCSS_P_OUTLINE_STYLE: d->outline_style = s->outline_style; break;
    case GFCSS_P_OUTLINE_COLOR: d->outline_color = s->outline_color; break;
    case GFCSS_P_COLOR: d->color = s->color; break;
    case GFCSS_P_BACKGROUND_COLOR: d->background_color = s->background_color; break;
    case GFCSS_P_BACKGROUND_IMAGE: d->background_image = s->background_image; break;
    case GFCSS_P_BACKGROUND_REPEAT: d->background_repeat = s->background_repeat; break;
    case GFCSS_P_BACKGROUND_POSITION_X: d->background_position[0] = s->background_position[0]; break;
    case GFCSS_P_BACKGROUND_POSITION_Y: d->background_position[1] = s->background_position[1]; break;
    case GFCSS_P_OPACITY: d->opacity = s->opacity; break;
    case GFCSS_P_FONT_FAMILY: d->font_family = s->font_family; break;
    case GFCSS_P_FONT_GENERIC: d->font_generic = s->font_generic; break;
    case GFCSS_P_FONT_SIZE: d->font_size = s->font_size; break;
    case GFCSS_P_FONT_WEIGHT: d->font_weight = s->font_weight; break;
    case GFCSS_P_FONT_STYLE: d->font_style = s->font_style; break;
    case GFCSS_P_FONT_VARIANT: d->font_variant = s->font_variant; break;
    case GFCSS_P_LINE_HEIGHT: d->line_height_kind = s->line_height_kind; d->line_height = s->line_height; break;
    case GFCSS_P_TEXT_ALIGN: d->text_align = s->text_align; break;
    case GFCSS_P_TEXT_INDENT: d->text_indent = s->text_indent; break;
    case GFCSS_P_TEXT_DECORATION_LINE: d->text_decoration = s->text_decoration; break;
    case GFCSS_P_TEXT_TRANSFORM: d->text_transform = s->text_transform; break;
    case GFCSS_P_WHITE_SPACE: d->white_space = s->white_space; break;
    case GFCSS_P_VERTICAL_ALIGN:
        d->vertical_align = s->vertical_align;
        d->vertical_align_length = s->vertical_align_length;
        break;
    case GFCSS_P_LIST_STYLE_TYPE: d->list_style_type = s->list_style_type; break;
    case GFCSS_P_LIST_STYLE_POSITION: d->list_style_position = s->list_style_position; break;
    case GFCSS_P_LIST_STYLE_IMAGE: d->list_style_image = s->list_style_image; break;
    case GFCSS_P_LETTER_SPACING: d->letter_spacing = s->letter_spacing; break;
    case GFCSS_P_WORD_SPACING: d->word_spacing = s->word_spacing; break;
    case GFCSS_P_CURSOR: d->cursor = s->cursor; break;
    case GFCSS_P_FLEX_DIRECTION: d->flex_direction = s->flex_direction; break;
    case GFCSS_P_FLEX_WRAP: d->flex_wrap = s->flex_wrap; break;
    case GFCSS_P_FLEX_GROW: d->flex_grow = s->flex_grow; break;
    case GFCSS_P_FLEX_SHRINK: d->flex_shrink = s->flex_shrink; break;
    case GFCSS_P_FLEX_BASIS: d->flex_basis = s->flex_basis; break;
    case GFCSS_P_ROW_GAP: d->row_gap = s->row_gap; break;
    case GFCSS_P_COLUMN_GAP: d->column_gap = s->column_gap; break;
    case GFCSS_P_ALIGN_ITEMS: d->align_items = s->align_items; break;
    case GFCSS_P_JUSTIFY_CONTENT: d->justify_content = s->justify_content; break;
    case GFCSS_P_BORDER_COLLAPSE: d->border_collapse = s->border_collapse; break;
    case GFCSS_P_BORDER_SPACING_H: d->border_spacing[0] = s->border_spacing[0]; break;
    case GFCSS_P_BORDER_SPACING_V: d->border_spacing[1] = s->border_spacing[1]; break;
    case GFCSS_P_CAPTION_SIDE: d->caption_side = s->caption_side; break;
    case GFCSS_P_CONTENT: d->content = s->content; break;
    case GFCSS_P_TOP: case GFCSS_P_RIGHT: case GFCSS_P_BOTTOM: case GFCSS_P_LEFT:
        k = p - GFCSS_P_TOP; d->inset[k] = s->inset[k]; break;
    case GFCSS_P_TEXT_SHADOW:
        d->text_shadows = s->text_shadows;
        memcpy(d->text_shadow, s->text_shadow, sizeof(d->text_shadow));
        break;
    default: break;
    }
}

static int prop_inherited(const gfcss_engine *e, int p)
{
    if (p >= GFCSS_P_COUNT) return (e->ext[p - GFCSS_P_COUNT].flags & GFCSS_INHERITED) != 0;
    return g_prop_inherited(p);
}

static void initial_of(const gfcss_engine *e, gfcss_computed *cs)
{
    int k;
    if (e->initial.size && e->initial_next == e->next && cs != &e->initial) {
        memcpy(cs, &e->initial, sizeof(*cs));
        return;
    }
    gfcss_computed_init(cs);
    for (k = 0; k < e->next; ++k) cs->ext[k] = e->ext[k].initial;
}

/* What a style is resolved against */
typedef struct {
    ol_unit fs;                 /* the element's font size (its parent's, for font-size) */
    ol_unit rem;
    const gfcss_engine *e;
    const char *base;           /* a text's offsets are from here */
    const gcalc *calc;
} gres;

static int resolve_len(const gres *r, const gdecl *d, gfcss_length *out)
{
    int32_t pct = 0;
    ol_unit vw = r->e->vw, vh = r->e->vh;
    switch (d->unit) {
    case V_KW:
        len_set(out, d->v == 1 ? GFCSS_LEN_AUTO : d->v == 2 ? GFCSS_LEN_NONE : d->v == 3 ? GFCSS_LEN_NORMAL
                     : GFCSS_LEN_CONTENT, 0, 0);
        return 1;
    case V_ABS: len_set(out, GFCSS_LEN, d->v, 0); return 1;
    case V_EM: len_set(out, GFCSS_LEN, g_mul_div(d->v, r->fs, 1000), 0); return 1;
    case V_EX: case V_CH: len_set(out, GFCSS_LEN, g_mul_div(d->v, r->fs, 2000), 0); return 1;
    case V_REM: len_set(out, GFCSS_LEN, g_mul_div(d->v, r->rem, 1000), 0); return 1;
    case V_PCT: len_set(out, GFCSS_LEN, 0, d->v); return 1;
    case V_VW: len_set(out, GFCSS_LEN, g_mul_div(d->v, vw, 100000), 0); return 1;
    case V_VH: len_set(out, GFCSS_LEN, g_mul_div(d->v, vh, 100000), 0); return 1;
    case V_VMIN: len_set(out, GFCSS_LEN, g_mul_div(d->v, vw < vh ? vw : vh, 100000), 0); return 1;
    case V_VMAX: len_set(out, GFCSS_LEN, g_mul_div(d->v, vw > vh ? vw : vh, 100000), 0); return 1;
    case V_CALC:
        out->kind = GFCSS_LEN;
        out->units = g_calc_resolve(&r->calc[d->v], r->fs, r->rem, vw, vh, &pct);
        out->pct = pct;
        return 1;
    default: return 0;
    }
}

/* a length that can't be a percentage: units */
static ol_unit resolve_units(const gres *r, const gdecl *d)
{
    gfcss_length l;
    if (!resolve_len(r, d, &l) || l.kind != GFCSS_LEN) return 0;
    return l.units;
}

static gfcss_text text_of(const gres *r, const gdecl *d, int kind)
{
    gfcss_text t;
    t.kind = kind;
    t.text = r->base + d->v;
    t.length = (uint32_t)d->w;
    return t;
}

static int32_t bolder(int32_t w, int up)
{
    if (up) return w < 350 ? 400 : w < 550 ? 700 : w < 900 ? 900 : w;
    return w < 550 ? 100 : w < 750 ? 400 : 700;
}

enum { CUR_BORDER = 15u, CUR_OUTLINE = 16u };

/* one declared value on cs; cur: which colours are currentColor */
static void apply_value(const gres *r, gfcss_computed *cs, const gfcss_computed *parent, int p, const gdecl *d,
                        unsigned *cur)
{
    int k;
    gfcss_length l;
    switch (p) {
    case GFCSS_P_DISPLAY: cs->display = d->v; break;
    case GFCSS_P_POSITION: cs->position = d->v; break;
    case GFCSS_P_FLOAT: cs->floating = d->v; break;
    case GFCSS_P_CLEAR: cs->clear = d->v; break;
    case GFCSS_P_Z_INDEX:
        cs->z_auto = d->unit == V_KW;
        cs->z_index = d->unit == V_KW ? 0 : d->v;
        break;
    case GFCSS_P_VISIBILITY: cs->visibility = d->v; break;
    case GFCSS_P_OVERFLOW_X: cs->overflow_x = d->v; break;
    case GFCSS_P_OVERFLOW_Y: cs->overflow_y = d->v; break;
    case GFCSS_P_BOX_SIZING: cs->box_sizing = d->v; break;
    case GFCSS_P_WIDTH: resolve_len(r, d, &cs->width); break;
    case GFCSS_P_HEIGHT: resolve_len(r, d, &cs->height); break;
    case GFCSS_P_MIN_WIDTH: resolve_len(r, d, &cs->min_width); break;
    case GFCSS_P_MIN_HEIGHT: resolve_len(r, d, &cs->min_height); break;
    case GFCSS_P_MAX_WIDTH: resolve_len(r, d, &cs->max_width); break;
    case GFCSS_P_MAX_HEIGHT: resolve_len(r, d, &cs->max_height); break;
    case GFCSS_P_MARGIN_TOP: case GFCSS_P_MARGIN_RIGHT: case GFCSS_P_MARGIN_BOTTOM: case GFCSS_P_MARGIN_LEFT:
        resolve_len(r, d, &cs->margin[p - GFCSS_P_MARGIN_TOP]);
        break;
    case GFCSS_P_PADDING_TOP: case GFCSS_P_PADDING_RIGHT: case GFCSS_P_PADDING_BOTTOM: case GFCSS_P_PADDING_LEFT:
        resolve_len(r, d, &cs->padding[p - GFCSS_P_PADDING_TOP]);
        break;
    case GFCSS_P_BORDER_TOP_WIDTH: case GFCSS_P_BORDER_RIGHT_WIDTH: case GFCSS_P_BORDER_BOTTOM_WIDTH:
    case GFCSS_P_BORDER_LEFT_WIDTH:
        cs->border_width[p - GFCSS_P_BORDER_TOP_WIDTH] = resolve_units(r, d);
        break;
    case GFCSS_P_BORDER_TOP_STYLE: case GFCSS_P_BORDER_RIGHT_STYLE: case GFCSS_P_BORDER_BOTTOM_STYLE:
    case GFCSS_P_BORDER_LEFT_STYLE:
        cs->border_style[p - GFCSS_P_BORDER_TOP_STYLE] = d->v;
        break;
    case GFCSS_P_BORDER_TOP_COLOR: case GFCSS_P_BORDER_RIGHT_COLOR: case GFCSS_P_BORDER_BOTTOM_COLOR:
    case GFCSS_P_BORDER_LEFT_COLOR:
        k = p - GFCSS_P_BORDER_TOP_COLOR;
        if (d->unit == V_CURRENT) *cur |= 1u << k;
        else {
            *cur &= ~(1u << k);
            cs->border_color[k] = (uint32_t)d->v;
        }
        break;
    case GFCSS_P_BORDER_TOP_LEFT_RADIUS: case GFCSS_P_BORDER_TOP_RIGHT_RADIUS:
    case GFCSS_P_BORDER_BOTTOM_RIGHT_RADIUS: case GFCSS_P_BORDER_BOTTOM_LEFT_RADIUS:
        resolve_len(r, d, &cs->border_radius[p - GFCSS_P_BORDER_TOP_LEFT_RADIUS]);
        break;
    case GFCSS_P_OUTLINE_WIDTH: cs->outline_width = resolve_units(r, d); break;
    case GFCSS_P_OUTLINE_STYLE: cs->outline_style = d->v; break;
    case GFCSS_P_OUTLINE_COLOR:
        if (d->unit == V_CURRENT) *cur |= CUR_OUTLINE;
        else {
            *cur &= ~CUR_OUTLINE;
            cs->outline_color = (uint32_t)d->v;
        }
        break;
    case GFCSS_P_COLOR:
        /* currentColor in color is the parent's */
        cs->color = d->unit == V_CURRENT ? (parent ? parent->color : 0xff000000u) : (uint32_t)d->v;
        break;
    case GFCSS_P_BACKGROUND_COLOR:
        cs->background_color = d->unit == V_CURRENT ? cs->color : (uint32_t)d->v;
        break;
    case GFCSS_P_BACKGROUND_IMAGE: case GFCSS_P_LIST_STYLE_IMAGE: {
        gfcss_text t;
        if (d->unit == V_URL) t = text_of(r, d, GFCSS_TEXT_URL);
        else if (d->unit == V_FUNC) t = text_of(r, d, GFCSS_TEXT_FUNCTION);
        else {
            t.kind = GFCSS_TEXT_NONE;
            t.length = 0;
            t.text = NULL;
        }
        if (p == GFCSS_P_BACKGROUND_IMAGE) cs->background_image = t;
        else cs->list_style_image = t;
        break;
    }
    case GFCSS_P_BACKGROUND_REPEAT: cs->background_repeat = d->v; break;
    case GFCSS_P_BACKGROUND_POSITION_X: resolve_len(r, d, &cs->background_position[0]); break;
    case GFCSS_P_BACKGROUND_POSITION_Y: resolve_len(r, d, &cs->background_position[1]); break;
    case GFCSS_P_OPACITY: cs->opacity = d->v; break;
    case GFCSS_P_FONT_FAMILY: cs->font_family = text_of(r, d, GFCSS_TEXT_STRING); break;
    case GFCSS_P_FONT_GENERIC: cs->font_generic = d->v; break;
    case GFCSS_P_FONT_WEIGHT:
        if (d->unit == V_KW) cs->font_weight = bolder(parent ? parent->font_weight : 400, d->v == -1);
        else cs->font_weight = d->v;
        break;
    case GFCSS_P_FONT_STYLE: cs->font_style = d->v; break;
    case GFCSS_P_FONT_VARIANT: cs->font_variant = d->v; break;
    case GFCSS_P_LINE_HEIGHT:
        if (d->unit == V_KW) {
            cs->line_height_kind = GFCSS_LINE_NORMAL;
            cs->line_height = 0;
        } else if (d->unit == V_NUM) {
            cs->line_height_kind = GFCSS_LINE_NUMBER;
            cs->line_height = d->v;
        } else if (resolve_len(r, d, &l)) {
            cs->line_height_kind = GFCSS_LINE_LENGTH;
            cs->line_height = l.units + g_mul_div(l.pct, r->fs, 100000);
        }
        break;
    case GFCSS_P_TEXT_ALIGN: cs->text_align = d->v; break;
    case GFCSS_P_TEXT_INDENT: resolve_len(r, d, &cs->text_indent); break;
    case GFCSS_P_TEXT_DECORATION_LINE: cs->text_decoration = (uint32_t)d->v; break;
    case GFCSS_P_TEXT_TRANSFORM: cs->text_transform = d->v; break;
    case GFCSS_P_WHITE_SPACE: cs->white_space = d->v; break;
    case GFCSS_P_VERTICAL_ALIGN:
        if (d->unit == V_KW) {
            cs->vertical_align = d->v;
            len_set(&cs->vertical_align_length, GFCSS_LEN, 0, 0);
        } else {
            cs->vertical_align = GFCSS_VALIGN_LENGTH;
            resolve_len(r, d, &cs->vertical_align_length);
        }
        break;
    case GFCSS_P_LIST_STYLE_TYPE: cs->list_style_type = d->v; break;
    case GFCSS_P_LIST_STYLE_POSITION: cs->list_style_position = d->v; break;
    case GFCSS_P_LETTER_SPACING: case GFCSS_P_WORD_SPACING: {
        ol_unit u = 0;
        if (resolve_len(r, d, &l) && l.kind == GFCSS_LEN) u = l.units;   /* word-spacing %: left out */
        if (p == GFCSS_P_LETTER_SPACING) cs->letter_spacing = u;
        else cs->word_spacing = u;
        break;
    }
    case GFCSS_P_CURSOR: cs->cursor = d->v; break;
    case GFCSS_P_FLEX_DIRECTION: cs->flex_direction = d->v; break;
    case GFCSS_P_FLEX_WRAP: cs->flex_wrap = d->v; break;
    case GFCSS_P_FLEX_GROW: cs->flex_grow = d->v; break;
    case GFCSS_P_FLEX_SHRINK: cs->flex_shrink = d->v; break;
    case GFCSS_P_FLEX_BASIS: resolve_len(r, d, &cs->flex_basis); break;
    case GFCSS_P_ROW_GAP: resolve_len(r, d, &cs->row_gap); break;
    case GFCSS_P_COLUMN_GAP: resolve_len(r, d, &cs->column_gap); break;
    case GFCSS_P_ALIGN_ITEMS: cs->align_items = d->v; break;
    case GFCSS_P_JUSTIFY_CONTENT: cs->justify_content = d->v; break;
    case GFCSS_P_BORDER_COLLAPSE: cs->border_collapse = d->v; break;
    case GFCSS_P_BORDER_SPACING_H: cs->border_spacing[0] = resolve_units(r, d); break;
    case GFCSS_P_BORDER_SPACING_V: cs->border_spacing[1] = resolve_units(r, d); break;
    case GFCSS_P_CAPTION_SIDE: cs->caption_side = d->v; break;
    case GFCSS_P_TOP: case GFCSS_P_RIGHT: case GFCSS_P_BOTTOM: case GFCSS_P_LEFT:
        resolve_len(r, d, &cs->inset[p - GFCSS_P_TOP]);
        break;
    case GFCSS_P_TEXT_SHADOW:
        /* its lengths of the font size, currentColor the colour (both are worked out first) */
        cs->text_shadows = d->unit == V_KW ? 0
                         : g_shadows(r->base + d->v, (size_t)d->w, r->fs, r->rem, cs->color, cs->text_shadow,
                                     GFCSS_MAX_SHADOWS);
        break;
    case GFCSS_P_CONTENT:
        if (d->unit == V_TEXT) cs->content = text_of(r, d, GFCSS_TEXT_STRING);
        else {
            cs->content.kind = d->v;
            cs->content.text = NULL;
            cs->content.length = 0;
        }
        break;
    default:
        if (p >= GFCSS_P_COUNT && d->unit == V_EXT) cs->ext[p - GFCSS_P_COUNT] = (uint32_t)d->v;
        break;
    }
}

static ol_unit font_size_of(const gres *r, const gdecl *d, ol_unit pfs, int *ok)
{
    gfcss_length l;
    gres pr = *r;
    *ok = 1;
    if (d->unit == V_KW) return d->v == -1 ? (ol_unit)((int64_t)pfs * 5 / 6) : (ol_unit)((int64_t)pfs * 6 / 5);
    pr.fs = pfs;
    if (!resolve_len(&pr, d, &l) || l.kind != GFCSS_LEN) {
        *ok = 0;
        return pfs;
    }
    l.units += g_mul_div(l.pct, pfs, 100000);
    if (l.units < 0) *ok = 0;
    return l.units;
}

/* The winners onto cs. cur says which colours are currentColor (resolved at the end). */
static void resolve(gfcss_engine *e, gfcss_computed *cs, const gfcss_computed *parent, int root, unsigned cur)
{
    const gfcss_computed *init = &e->initial;
    gres r;
    int i, order[3] = { GFCSS_P_FONT_SIZE, GFCSS_P_COLOR, -1 };
    memset(&r, 0, sizeof(r));
    r.e = e;
    r.rem = root ? OL_CSSPX(16) : e->rem;
    /* font-size, then color, then the rest: em and currentColor are of them */
    for (i = 0; i < 2; ++i) {
        int p = order[i];
        gwin *w = &e->win[p];
        if (!w->d) continue;
        r.base = w->sheet ? w->sheet->text : e->inline_sink.base;
        r.calc = w->sheet ? w->sheet->decls.calc : e->inline_sink.calc;
        mark_set(cs, p);
        if (w->d->unit == V_INHERIT || w->d->unit == V_UNSET) copy_prop(cs, parent ? parent : init, p);
        else if (w->d->unit == V_INITIAL) copy_prop(cs, init, p);
        else if (p == GFCSS_P_FONT_SIZE) {
            int ok;
            ol_unit fs = font_size_of(&r, w->d, parent ? parent->font_size : init->font_size, &ok);
            if (ok) cs->font_size = fs;
        } else {
            apply_value(&r, cs, parent, p, w->d, &cur);
        }
    }
    if (root) e->rem = cs->font_size;
    r.fs = cs->font_size;
    r.rem = e->rem;
    for (i = 0; i < e->ntouched; ++i) {
        int p = e->touched[i];
        gwin *w = &e->win[p];
        const gdecl *d = w->d;
        if (p == GFCSS_P_FONT_SIZE || p == GFCSS_P_COLOR) continue;
        mark_set(cs, p);
        r.base = w->sheet ? w->sheet->text : e->inline_sink.base;
        r.calc = w->sheet ? w->sheet->decls.calc : e->inline_sink.calc;
        if (d->unit == V_INHERIT || (d->unit == V_UNSET && prop_inherited(e, p))) {
            copy_prop(cs, parent ? parent : init, p);
            if (p >= GFCSS_P_BORDER_TOP_COLOR && p <= GFCSS_P_BORDER_LEFT_COLOR)
                cur &= ~(1u << (p - GFCSS_P_BORDER_TOP_COLOR));
            if (p == GFCSS_P_OUTLINE_COLOR) cur &= ~CUR_OUTLINE;
        } else if (d->unit == V_INITIAL || d->unit == V_UNSET) {
            copy_prop(cs, init, p);
            if (p >= GFCSS_P_BORDER_TOP_COLOR && p <= GFCSS_P_BORDER_LEFT_COLOR)
                cur |= 1u << (p - GFCSS_P_BORDER_TOP_COLOR);
            if (p == GFCSS_P_OUTLINE_COLOR) cur |= CUR_OUTLINE;
        } else {
            apply_value(&r, cs, parent, p, d, &cur);
        }
    }
    for (i = 0; i < 4; ++i) {
        if (cur & (1u << i)) cs->border_color[i] = cs->color;
        if (cs->border_style[i] == GFCSS_BORDER_NONE || cs->border_style[i] == GFCSS_BORDER_HIDDEN)
            cs->border_width[i] = 0;
    }
    if (cur & CUR_OUTLINE) cs->outline_color = cs->color;
    if (cs->outline_style == GFCSS_BORDER_NONE) cs->outline_width = 0;
}

/* the inherited properties, field by field (an element's every style starts with these) */
static void inherit_from(const gfcss_engine *e, gfcss_computed *cs, const gfcss_computed *parent)
{
    int k;
    cs->visibility = parent->visibility;
    cs->color = parent->color;
    cs->font_generic = parent->font_generic;
    cs->font_family = parent->font_family;
    cs->font_size = parent->font_size;
    cs->font_weight = parent->font_weight;
    cs->font_style = parent->font_style;
    cs->font_variant = parent->font_variant;
    cs->line_height_kind = parent->line_height_kind;
    cs->line_height = parent->line_height;
    cs->text_align = parent->text_align;
    cs->text_indent = parent->text_indent;
    cs->text_transform = parent->text_transform;
    cs->white_space = parent->white_space;
    cs->list_style_type = parent->list_style_type;
    cs->list_style_position = parent->list_style_position;
    cs->list_style_image = parent->list_style_image;
    cs->letter_spacing = parent->letter_spacing;
    cs->word_spacing = parent->word_spacing;
    cs->cursor = parent->cursor;
    cs->border_collapse = parent->border_collapse;
    cs->border_spacing[0] = parent->border_spacing[0];
    cs->border_spacing[1] = parent->border_spacing[1];
    cs->caption_side = parent->caption_side;
    cs->text_shadows = parent->text_shadows;
    memcpy(cs->text_shadow, parent->text_shadow, sizeof(cs->text_shadow));
    for (k = 0; k < e->next; ++k)
        if (e->ext[k].flags & GFCSS_INHERITED) cs->ext[k] = parent->ext[k];
}

/* the parent as a whole gfcss_computed (a smaller one has the initial values after its size) */
static const gfcss_computed *whole(gfcss_engine *e, const gfcss_computed *parent)
{
    if (!parent || !parent->size || parent->size >= sizeof(gfcss_computed)) return parent;
    initial_of(e, &e->parent_tmp);
    memcpy(&e->parent_tmp, parent, parent->size);
    e->parent_tmp.size = sizeof(gfcss_computed);
    return &e->parent_tmp;
}

static void hand_back(gfcss_engine *e, gfcss_computed *out, const gfcss_computed *cs)
{
    uint32_t size = out->size;
    if (cs == out) return;
    (void)e;
    memcpy(out, cs, size);
    out->size = size;
}

static int prepare(gfcss_engine *e)
{
    if (e->dirty && !build_index(e)) return 0;
    if (e->initial_next != e->next || !e->initial.size) {
        e->initial.size = 0;
        initial_of(e, &e->initial);
        e->initial_next = e->next;
    }
    return 1;
}

int gfcss_compute(gfcss_engine *e, const gfcss_element_ops *ops, void *userdata, const void *el,
                  int pseudo, const gfcss_computed *parent, gfcss_computed *out)
{
    gm m;
    gfcss_info in;
    gfcss_computed *cs;
    size_t i;
    int root, hit = 0;
    unsigned cur = CUR_BORDER | CUR_OUTLINE;
    if (!e || !ops || !ops->info || !ops->parent || !ops->prev || !out) return 0;
    if (!prepare(e)) return 0;
    if (pseudo && !e->any_pseudo) return 0;
    cs = out->size && out->size < sizeof(gfcss_computed) ? &e->out_tmp : out;
    parent = whole(e, parent);
    m.e = e;
    m.ops = ops;
    m.ud = userdata;
    memset(&in, 0, sizeof(in));
    ops->info(userdata, el, &in);
    root = !pseudo && !ops->parent(userdata, el);
    win_reset(e);

    /* the rules that could match: by its id, its classes, its type, and those with none */
    if (e->nentries) {
        if (in.id && in.id[0]) hit |= try_bucket(e, &m, make_key(K_ID, g_hash(in.id, strlen(in.id), 0)), pseudo, el, &in);
        if (in.classes) {
            const char *p = in.classes;
            while (*p) {
                const char *s;
                while (*p && g_ws((unsigned char)*p)) ++p;
                s = p;
                while (*p && !g_ws((unsigned char)*p)) ++p;
                if (p > s) hit |= try_bucket(e, &m, make_key(K_CLASS, g_hash(s, (size_t)(p - s), 0)), pseudo, el, &in);
            }
        }
        if (in.tag && in.tag[0])
            hit |= try_bucket(e, &m, make_key(K_TAG, g_hash(in.tag, strlen(in.tag), 1)), pseudo, el, &in);
        for (i = e->first_universal; i < e->nentries; ++i) hit |= try_entry(e, &m, &e->entries[i], pseudo, el, &in);
    }
    /* its style attribute: after every rule (the third tier, or the fifth when important) */
    if (!pseudo && in.style && in.style[0]) {
        gsink *s = &e->inline_sink;
        s->n = 0;
        s->ncalc = 0;
        s->oom = 0;
        s->base = in.style;
        g_parse_declarations(e, s, in.style, in.style + strlen(in.style));
        for (i = 0; i < s->n; ++i)
            offer(e, &s->d[i], NULL, tier(GFCSS_ORIGIN_AUTHOR, s->d[i].flags & D_IMPORTANT, 1) << 24, (uint32_t)i);
    }

    initial_of(e, cs);
    if (parent) inherit_from(e, cs, parent);
    resolve(e, cs, parent, root, cur);
    hand_back(e, out, cs);
    if (pseudo) {
        if (!hit) return 0;
        if ((pseudo == GFCSS_PSEUDO_BEFORE || pseudo == GFCSS_PSEUDO_AFTER) &&
            (cs->content.kind != GFCSS_TEXT_STRING || cs->display == GFCSS_DISPLAY_NONE))
            return 0;
    }
    return 1;
}

void gfcss_inherit(gfcss_engine *e, const gfcss_computed *parent, gfcss_computed *out)
{
    gfcss_computed *cs;
    if (!e || !out) return;
    cs = out->size && out->size < sizeof(gfcss_computed) ? &e->out_tmp : out;
    parent = whole(e, parent);
    initial_of(e, cs);
    if (parent) inherit_from(e, cs, parent);
    hand_back(e, out, cs);
}

int gfcss_apply_declarations(gfcss_engine *e, const char *decls, size_t length,
                             const gfcss_computed *parent, gfcss_computed *cs)
{
    gsink *s;
    size_t i;
    gfcss_computed *t;
    if (!e || !cs) return 0;
    if (!decls || !length) return 1;
    if (!prepare(e)) return 0;
    parent = whole(e, parent);
    t = cs->size && cs->size < sizeof(gfcss_computed) ? &e->out_tmp : cs;
    if (t != cs) {
        initial_of(e, t);
        memcpy(t, cs, cs->size);
        t->size = sizeof(gfcss_computed);
    }
    win_reset(e);
    s = &e->inline_sink;
    s->n = 0;
    s->ncalc = 0;
    s->oom = 0;
    s->base = decls;
    g_parse_declarations(e, s, decls, decls + length);
    if (s->oom) return 0;
    for (i = 0; i < s->n; ++i)
        offer(e, &s->d[i], NULL, tier(GFCSS_ORIGIN_AUTHOR, s->d[i].flags & D_IMPORTANT, 1) << 24, (uint32_t)i);
    resolve(e, t, parent, 0, 0);
    hand_back(e, cs, t);
    return 1;
}

ol_unit gfcss_resolve(const gfcss_length *len, ol_unit of)
{
    if (!len || len->kind != GFCSS_LEN) return 0;
    return len->units + g_mul_div(len->pct, of, 100000);
}

/* ---- GFOpenLayout's styles -------------------------------------------------- */

void gfcss_to_ol(const gfcss_computed *cs, ol_unit cw, ol_style *st, ol_box_style *box)
{
    int k, widest = -1;
    ol_unit bw[4], pad_lr, border_lr;
    ol_style_init(st);
    ol_box_style_init(box);
    if (!cs) return;
    switch (cs->display) {
    case GFCSS_DISPLAY_NONE: st->display = OL_DISPLAY_NONE; break;
    case GFCSS_DISPLAY_INLINE: st->display = OL_DISPLAY_INLINE; break;
    case GFCSS_DISPLAY_INLINE_BLOCK: case GFCSS_DISPLAY_INLINE_TABLE:
#ifdef OL_HAS_WEB_BOXES
        st->display = OL_DISPLAY_INLINE_BLOCK;
#else
        st->display = OL_DISPLAY_INLINE;
#endif
        break;
    case GFCSS_DISPLAY_INLINE_FLEX:
#ifdef OL_HAS_WEB_BOXES
        st->display = OL_DISPLAY_INLINE_BLOCK;
#else
        st->display = OL_DISPLAY_BLOCK;
#endif
        break;
    default: st->display = OL_DISPLAY_BLOCK; break;
    }
    if ((cs->display == GFCSS_DISPLAY_FLEX || cs->display == GFCSS_DISPLAY_INLINE_FLEX) &&
        (cs->flex_direction == GFCSS_ROW || cs->flex_direction == GFCSS_ROW_REVERSE))
        box->flags |= OL_BOX_ROW;
    switch (cs->text_align) {
    case GFCSS_ALIGN_CENTER: st->text_align = OL_ALIGN_CENTER; break;
    case GFCSS_ALIGN_END: case GFCSS_ALIGN_RIGHT: st->text_align = OL_ALIGN_END; break;
    case GFCSS_ALIGN_JUSTIFY: st->text_align = OL_ALIGN_JUSTIFY; break;
    default: st->text_align = OL_ALIGN_START; break;
    }
    if (box->flags & OL_BOX_ROW) {
        /* a row with room to spare puts its children at its text_align, or shares it out
         * between them */
        if (cs->justify_content == GFCSS_FLEX_SPACE_BETWEEN) box->flags |= OL_BOX_SPACE_BETWEEN;
        else if (cs->justify_content == GFCSS_FLEX_SPACE_AROUND) box->flags |= OL_BOX_SPACE_AROUND;
        else if (cs->justify_content == GFCSS_FLEX_SPACE_EVENLY) box->flags |= OL_BOX_SPACE_EVENLY;
        if (cs->justify_content == GFCSS_FLEX_CENTER) st->text_align = OL_ALIGN_CENTER;
        else if (cs->justify_content == GFCSS_FLEX_END || cs->justify_content == GFCSS_FLEX_RIGHT)
            st->text_align = OL_ALIGN_END;
        else if (cs->justify_content != GFCSS_FLEX_NORMAL) st->text_align = OL_ALIGN_START;
    } else if (cs->display == GFCSS_DISPLAY_FLEX || cs->display == GFCSS_DISPLAY_INLINE_FLEX) {
        /* 0.19: a column of a height with room to spare: its children down it at its justify-content */
        if (cs->justify_content == GFCSS_FLEX_CENTER) box->flags |= OL_BOX_PACK_CENTRE;
        else if (cs->justify_content == GFCSS_FLEX_END) box->flags |= OL_BOX_PACK_END;
        else if (cs->justify_content == GFCSS_FLEX_SPACE_BETWEEN) box->flags |= OL_BOX_SPACE_BETWEEN;
        else if (cs->justify_content == GFCSS_FLEX_SPACE_AROUND) box->flags |= OL_BOX_SPACE_AROUND;
        else if (cs->justify_content == GFCSS_FLEX_SPACE_EVENLY) box->flags |= OL_BOX_SPACE_EVENLY;
    }
    st->margin_top = gfcss_resolve(&cs->margin[0], cw);
    st->margin_right = gfcss_resolve(&cs->margin[1], cw);
    st->margin_bottom = gfcss_resolve(&cs->margin[2], cw);
    st->margin_left = gfcss_resolve(&cs->margin[3], cw);
    st->padding_top = gfcss_resolve(&cs->padding[0], cw);
    st->padding_right = gfcss_resolve(&cs->padding[1], cw);
    st->padding_bottom = gfcss_resolve(&cs->padding[2], cw);
    st->padding_left = gfcss_resolve(&cs->padding[3], cw);
    st->font_size = cs->font_size;
    switch (cs->line_height_kind) {
    case GFCSS_LINE_NUMBER: st->line_height = g_mul_div(cs->font_size, cs->line_height, 1000); break;
    case GFCSS_LINE_LENGTH: st->line_height = cs->line_height; break;
    default: st->line_height = g_mul_div(cs->font_size, GFCSS_LINE_NORMAL_MILLI, 1000); break;
    }
    st->foreground = cs->color;
    st->background = cs->background_color;
    st->text_flags = 0;
    if (cs->font_weight >= 600) st->text_flags |= OL_TEXT_BOLD;
    if (cs->font_style != GFCSS_FONT_NORMAL) st->text_flags |= OL_TEXT_ITALIC;
    if (cs->text_decoration & GFCSS_UNDERLINE) st->text_flags |= OL_TEXT_UNDERLINE;
    if (cs->text_decoration & GFCSS_LINE_THROUGH) st->text_flags |= OL_TEXT_STRIKE;
    if (cs->font_generic == GFCSS_FONT_MONOSPACE) st->text_flags |= OL_TEXT_MONO;
    if (cs->vertical_align == GFCSS_VALIGN_SUPER) st->text_flags |= OL_TEXT_SUPER;
    if (cs->vertical_align == GFCSS_VALIGN_SUB) st->text_flags |= OL_TEXT_SUB;

    box->text_indent = gfcss_resolve(&cs->text_indent, cw);
    /* borders: each side's; without web boxes the widest side's, on the sides that have one */
    box->border_sides = 0;
    for (k = 0; k < 4; ++k) {
        bw[k] = cs->border_width[k];
        if (bw[k] > 0) {
            static const uint32_t side[4] = { OL_BORDER_TOP, OL_BORDER_RIGHT, OL_BORDER_BOTTOM, OL_BORDER_LEFT };
            box->border_sides |= side[k];
            if (widest < 0 || bw[k] > bw[widest]) widest = k;
        }
    }
    if (widest >= 0) {
        box->border_width = bw[widest];
        box->border_argb = cs->border_color[widest];
    } else {
        box->border_sides = OL_BORDER_ALL;
    }
#ifdef OL_HAS_WEB_BOXES
    for (k = 0; k < 4; ++k) {
        box->border_widths[k] = bw[k];
        box->border_argbs[k] = cs->border_color[k];
    }
    if (widest >= 0) box->flags |= OL_BOX_SIDE_BORDERS;
#endif
    /* widths are the border box's, as GFOpenLayout's box.width is */
    pad_lr = st->padding_left + st->padding_right;
    border_lr = bw[1] + bw[3];
    if (cs->width.kind == GFCSS_LEN) {
        ol_unit extra = cs->box_sizing == GFCSS_BORDER_BOX ? 0 : pad_lr + border_lr;
#ifdef OL_HAS_WEB_BOXES
        /* a percentage alone, of the border box: GFOpenLayout works it out of the block it is in */
        if (cs->width.pct > 0 && cs->width.units == 0 && extra == 0) box->width_permille = cs->width.pct / 100;
        else box->width = gfcss_resolve(&cs->width, cw) + extra;
#else
        box->width = gfcss_resolve(&cs->width, cw) + extra;
#endif
        if (box->width < 0) box->width = 0;
    }
    if (cs->height.kind == GFCSS_LEN && !cs->height.pct) {
        ol_unit extra = cs->box_sizing == GFCSS_BORDER_BOX ? 0
                      : st->padding_top + st->padding_bottom + bw[0] + bw[2];
        box->height = cs->height.units + extra;
    }
#ifdef OL_HAS_WEB_BOXES
    {
        /* the least and the most, of the border box as GFOpenLayout's are */
        ol_unit extra = cs->box_sizing == GFCSS_BORDER_BOX ? 0 : pad_lr + border_lr;
        if (cs->min_width.kind == GFCSS_LEN) box->min_width = gfcss_resolve(&cs->min_width, cw) + extra;
        if (cs->max_width.kind == GFCSS_LEN) {
            if (cs->max_width.pct > 0 && cs->max_width.units == 0 && extra == 0)
                box->max_width_permille = cs->max_width.pct / 100;
            else if ((box->max_width = gfcss_resolve(&cs->max_width, cw) + extra) <= 0)
                box->max_width = 1;     /* max-width 0: as narrow as can be (0 would be none) */
        }
    }
    switch (cs->vertical_align) {
    case GFCSS_VALIGN_TOP: case GFCSS_VALIGN_TEXT_TOP: box->valign = OL_VALIGN_TOP; break;
    case GFCSS_VALIGN_MIDDLE: box->valign = OL_VALIGN_MIDDLE; break;
    case GFCSS_VALIGN_BOTTOM: case GFCSS_VALIGN_TEXT_BOTTOM: box->valign = OL_VALIGN_BOTTOM; break;
    default: box->valign = OL_VALIGN_BASELINE; break;
    }
    if (cs->margin[1].kind == GFCSS_LEN_AUTO) box->flags |= OL_BOX_AUTO_RIGHT;
    if (cs->margin[3].kind == GFCSS_LEN_AUTO) box->flags |= OL_BOX_AUTO_LEFT;
#else
    /* without web boxes: a width narrower than its block, with both margins auto, is centred here */
    if (cs->margin[1].kind == GFCSS_LEN_AUTO && cs->margin[3].kind == GFCSS_LEN_AUTO && box->width > 0 &&
        box->width < cw) {
        st->margin_left = st->margin_right = (cw - box->width) / 2;
    } else if (cs->margin[3].kind == GFCSS_LEN_AUTO && box->width > 0 && box->width < cw) {
        st->margin_left = cw - box->width - st->margin_right;
    }
    if (cs->max_width.kind == GFCSS_LEN) {
        ol_unit mx = gfcss_resolve(&cs->max_width, cw) +
                     (cs->box_sizing == GFCSS_BORDER_BOX ? 0 : pad_lr + border_lr);
        if (mx > 0 && (box->width == 0 ? cw - st->margin_left - st->margin_right > mx : box->width > mx))
            box->width = mx;
    }
    if (cs->min_width.kind == GFCSS_LEN) {
        ol_unit mn = gfcss_resolve(&cs->min_width, cw) +
                     (cs->box_sizing == GFCSS_BORDER_BOX ? 0 : pad_lr + border_lr);
        if (box->width > 0 && box->width < mn) box->width = mn;
    }
#endif
    /* rows: their children's share and the gap between them */
    box->grow = cs->flex_grow / 1000;
    if (box->flags & OL_BOX_ROW) box->gap = gfcss_resolve(&cs->column_gap, cw);
    else box->gap = gfcss_resolve(&cs->row_gap, cw);
    switch (cs->align_items) {
    case GFCSS_FLEX_CENTER: box->align = OL_CROSS_CENTRE; break;
    case GFCSS_FLEX_START: box->align = OL_CROSS_START; break;
    case GFCSS_FLEX_END: box->align = OL_CROSS_END; break;
    default: box->align = OL_CROSS_STRETCH; break;
    }
    /* 1.1: a box placed out of the flow (absolute or fixed): a GFOpenLayout
     * frame at left and top from its containing block's content corner, as
     * wide as its width (or what left and right leave of cw, or its
     * content), as tall as its height or its content. right alone places a
     * box with a width from cw's right; bottom is left out (the height of
     * the block it is in is not known here). */
    if ((cs->position == GFCSS_POSITION_ABSOLUTE || cs->position == GFCSS_POSITION_FIXED) &&
        (!cs->size || cs->size >= sizeof(gfcss_computed))) {
        ol_unit l = gfcss_resolve(&cs->inset[GFCSS_LEFT], cw), r = gfcss_resolve(&cs->inset[GFCSS_RIGHT], cw);
        ol_unit w = box->width;
        int has_l = cs->inset[GFCSS_LEFT].kind == GFCSS_LEN, has_r = cs->inset[GFCSS_RIGHT].kind == GFCSS_LEN;
        if (w <= 0 && has_l && has_r) w = cw - l - r;
        if (w <= 0 && !(has_l && has_r)) {
#ifdef OL_HAS_FIT_CONTENT
            w = 0;                      /* as wide as its content: GFOpenLayout works it out */
            box->flags |= OL_BOX_FIT_CONTENT;
#else
            w = cw - (has_l ? l : 0);
#endif
        } else if (w < OL_CSSPX(1)) {
            w = OL_CSSPX(1);
        }
        box->flags |= OL_BOX_ABSOLUTE;
        box->frame.x = has_l ? l : has_r ? cw - r - w : 0;
        box->frame.y = cs->inset[GFCSS_TOP].kind == GFCSS_LEN ? gfcss_resolve(&cs->inset[GFCSS_TOP], cw) : 0;
        box->frame.width = w;
        box->frame.height = box->height > 0 ? box->height : 0;
    }
}
