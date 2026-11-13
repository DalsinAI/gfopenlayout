/* Row N1: GFOpenGPU compositor batching. No Amiga headers are used: the mock
 * checks the exact big-endian stream and every library crossing. MIT. */
#include "olc_ogpu_accel.h"
#include <stdio.h>
#include <string.h>

enum { OP_SURFACE = 1, OP_TARGET = 2, OP_CLIP = 3, OP_FILL = 0x10, OP_COPY = 0x12, OP_COMPOSITE = 0x20 };
typedef struct mock {
    int addresses, submits, waits, closes;
    uint32_t waited, fence, words;
    unsigned char stream[4096];
} mock;
static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t xy(int x, int y) { return ((uint32_t)x & 0xffffu) << 16 | ((uint32_t)y & 0xffffu); }
static uint32_t address_of(void *ctx, const void *p)
{
    mock *m = (mock *)ctx;
    m->addresses++;
    return p == (const void *)0x3333 ? 0x00200000u : p == (const void *)0x1111 ? 0x00300000u
                                                  : p == (const void *)0x2222 ? 0x00400000u : 0;
}
static int submit(void *ctx, const void *stream, uint32_t words, uint32_t *fence)
{
    mock *m = (mock *)ctx;
    m->submits++;
    m->words = words;
    memcpy(m->stream, stream, (size_t)words * 4);
    *fence = ++m->fence;
    return 0;
}
static int wait_fence(void *ctx, uint32_t fence)
{ mock *m = (mock *)ctx; m->waits++; m->waited = fence; return 0; }
static void close_io(void *ctx) { ((mock *)ctx)->closes++; }
static int count_op(const mock *m, unsigned op)
{
    uint32_t at = 0;
    int n = 0;
    while (at < m->words) {
        uint32_t h = be32(m->stream + at * 4), len = h & 0xffffu;
        if ((h >> 16) == op) n++;
        if (!len) break;
        at += len;
    }
    return n;
}
static int has4(const mock *m, unsigned op, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t at = 0;
    while (at < m->words) {
        const unsigned char *p = m->stream + at * 4;
        uint32_t h = be32(p), len = h & 0xffffu;
        if ((h >> 16) == op && len == 4 && be32(p + 4) == a && be32(p + 8) == b && be32(p + 12) == c) return 1;
        if (!len) break;
        at += len;
    }
    return 0;
}

int main(void)
{
    mock m;
    olc_ogpu_io io;
    olc_ogpu *g;
    const olc_accel *a;
    void *b;
    uint32_t s1, s2, fence = 0;
    olc_box clips[2] = { { 0, 0, 9, 9 }, { 20, 30, 39, 49 } };
    olc_box upload = { 1, 2, 3, 4 }, src = { 2, 3, 11, 12 }, fill = { 0, 0, 100, 50 };

    memset(&m, 0, sizeof m);
    io.ctx = &m; io.address = address_of; io.submit = submit; io.wait = wait_fence; io.close = close_io;
    g = olc_ogpu_new(&io);
    CHECK(g != NULL);
    a = olc_ogpu_ops(g);
    CHECK(a && a->version == OLC_ACCEL_VERSION);
    CHECK(m.addresses == 0 && m.submits == 0 && m.waits == 0); /* boot/construct probes nothing */

    b = a->begin(a->ctx, (void *)0x1234, (void *)0x3333, 2560, OLC_FMT_XRGB32, 640, 480);
    CHECK(b != NULL && m.addresses == 1);
    CHECK(a->bind(a->ctx, b, (void *)0x1111, 256, 64, 64, OLC_FMT_XRGB32, &s1) == 0);
    CHECK(a->bind(a->ctx, b, (void *)0x2222, 128, 64, 64, OLC_FMT_RGB16, &s2) == 0 && s2 != s1);
    CHECK(a->upload(a->ctx, b, s1, &upload, 1) == 0);
    CHECK(a->clip(a->ctx, b, clips, 2) == 0);
    CHECK(a->fill(a->ctx, b, &fill, 0xff123456u) == 0);
    CHECK(a->copy(a->ctx, b, s2, &src, 40, 50) == 0);
    CHECK(a->composite(a->ctx, b, s1, &src, 60, 70, 128) == 0);
    CHECK(m.submits == 0 && m.waits == 0); /* no per-primitive crossing */
    CHECK(a->submit(a->ctx, b, &fence) == 0);
    CHECK(m.submits == 1 && fence == 1 && m.waits == 0); /* one compose, one submit/fence, no wait */
    CHECK(count_op(&m, OP_CLIP) == 2);
    CHECK(count_op(&m, OP_FILL) == 2 && count_op(&m, OP_COPY) == 2 && count_op(&m, OP_COMPOSITE) == 2);
    CHECK(count_op(&m, OP_SURFACE) >= 3 && count_op(&m, OP_TARGET) == 1);
    CHECK(has4(&m, OP_FILL, xy(0, 0), xy(101, 51), 0x00123456u));

    /* The same surface gets the same logical slot in the next compose. */
    b = a->begin(a->ctx, (void *)0x1234, (void *)0x3333, 2560, OLC_FMT_XRGB32, 640, 480);
    CHECK(b != NULL && m.addresses == 4); /* screen address plus the two first-compose surfaces */
    { uint32_t again = 0; CHECK(a->bind(a->ctx, b, (void *)0x1111, 256, 64, 64, OLC_FMT_XRGB32, &again) == 0 && again == s1); }
    CHECK(a->clip(a->ctx, b, clips, 1) == 0);
    CHECK(a->copy(a->ctx, b, s1, &src, 0, 0) == 0);
    CHECK(a->submit(a->ctx, b, &fence) == 0 && fence == 2 && m.submits == 2 && m.waits == 0);

    a->retire(a->ctx, s1);
    CHECK(m.waits == 1 && m.waited == 2); /* retirement may wait, compose never did */
    a->retire(a->ctx, s2);
    CHECK(m.waits == 2 && m.waited == 1);
    a->detach(a->ctx);
    CHECK(m.waits == 2 && m.closes == 1);
    olc_ogpu_free(g);
    CHECK(m.closes == 1);

    printf("test_ogpu_accel: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
