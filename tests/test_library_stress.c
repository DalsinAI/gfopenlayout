#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/gfopenlayout.h>

#include <stdio.h>
#include <string.h>

struct Library *OpenLayoutBase;

#define MARK(s) do { printf("LAYOUTSTRESS stage=%s\n", (s)); fflush(stdout); } while (0)
#define CHECK(x) do { if (!(x)) {     printf("LAYOUTSTRESS FAIL line=%d expr=%s\n", __LINE__, #x);     fflush(stdout); rc = 20; goto done; } } while (0)

static ol_node *append_node(ol_document *doc, ol_node *parent, ULONG role,
                            const char *text)
{
    struct OLNodeAppend r;
    r.document = doc;
    r.parent = parent;
    r.role = role;
    r.text = (CONST_STRPTR)text;
    return OL_NodeAppend(&r);
}

static void set_style(ol_node *node, const ol_style *style)
{
    struct OLNodeStyle r;
    r.node = node;
    r.style = style;
    OL_NodeSetStyle(&r);
}

int main(void)
{
    int rc = 0, i;
    char buf[48];
    ol_document *doc = NULL;
    ol_node *root, *block, *node, *image;
    ol_style block_style, inline_style;
    struct OLNodeString sr;
    struct OLNodeImage ir;
    struct OLLayoutRequest lr;

    OpenLayoutBase = OpenLibrary((CONST_STRPTR)OPENLAYOUTLIB_NAME,
                                 OPENLAYOUTLIB_VERSION);
    CHECK(OpenLayoutBase != NULL);
    MARK("opened");

    doc = OL_DocumentNew();
    CHECK(doc != NULL);
    root = OL_DocumentRoot(doc);
    CHECK(root != NULL);
    OL_StyleInit(&block_style);
    OL_StyleInit(&inline_style);
    block_style.display = OL_DISPLAY_BLOCK;
    inline_style.display = OL_DISPLAY_INLINE;

    block = append_node(doc, root, OL_ROLE_PARAGRAPH, NULL);
    CHECK(block != NULL);
    set_style(block, &block_style);
    MARK("base");

    for (i = 0; i < 48; ++i) {
        snprintf(buf, sizeof(buf), "node-%02d ", i);
        node = append_node(doc, block, OL_ROLE_TEXT, buf);
        CHECK(node != NULL);
        set_style(node, &inline_style);
        if ((i % 7) == 0) {
            sr.node = node;
            sr.text = (CONST_STRPTR)"metadata-value";
            CHECK(OL_NodeSetValue(&sr));
            sr.text = (CONST_STRPTR)"metadata-name";
            CHECK(OL_NodeSetName(&sr));
        }
        if (i == 15) MARK("crossed-16");
        if (i == 31) MARK("crossed-32");
    }
    MARK("nodes-48");

    image = append_node(doc, block, OL_ROLE_IMAGE, NULL);
    CHECK(image != NULL);
    set_style(image, &inline_style);
    ir.node = image;
    ir.image_id = 7;
    ir.width = OL_CSSPX(80);
    ir.height = OL_CSSPX(40);
    OL_NodeSetImage(&ir);
    sr.node = image;
    sr.text = (CONST_STRPTR)"image.png";
    CHECK(OL_NodeSetValue(&sr));
    sr.text = (CONST_STRPTR)"Image";
    CHECK(OL_NodeSetName(&sr));
    MARK("image");

    lr.document = doc;
    lr.viewport_width = OL_CSSPX(320);
    lr.measure = NULL;
    lr.measure_userdata = NULL;
    CHECK(OL_Layout(&lr));
    MARK("layout");

    CHECK(OL_DisplayCount(doc) > 20);
    CHECK(OL_DocumentContentHeight(doc) > 0);
    printf("LAYOUTSTRESS PASS ops=%lu height=%ld\n",
           (unsigned long)OL_DisplayCount(doc),
           (long)(OL_DocumentContentHeight(doc) / OL_UNITS_PER_CSSPX));
    fflush(stdout);

done:
    if (doc) OL_DocumentFree(doc);
    if (OpenLayoutBase) CloseLibrary(OpenLayoutBase);
    return rc;
}
