/* libgfopenlayout_link.a's OpenLayoutBase: gfopenlayout.library 10 or later,
 * opened before main() and closed after it. Without it the program stops at
 * once with RETURN_FAIL and says why: a call through no library would crash.
 * The calls (gfopenlayout_link.c) take this in only when the program has no
 * OpenLayoutBase of its own; one that opens the library itself keeps doing so.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <libraries/gfopenlayout.h>
#include <inline/macros.h>

struct Library *OpenLayoutBase;

extern void exit(int);

static void __attribute__((constructor)) ol_link_open(void)
{
    struct Library *dos;
    static const char why[] = "This needs gfopenlayout.library 10 or later in LIBS:\n";
    if (OpenLayoutBase) return;
    OpenLayoutBase = OpenLibrary((CONST_STRPTR)OPENLAYOUTLIB_NAME, OPENLAYOUTLIB_UI_VERSION);
    if (OpenLayoutBase) return;
    dos = OpenLibrary((CONST_STRPTR)"dos.library", 36);
    if (dos) {
        BPTR out = LP0(0x3c, BPTR, Output, , dos);     /* Output() */
        if (out) LP3(0x30, LONG, Write, BPTR, out, d1, APTR, (APTR)why, d2, LONG, (LONG)sizeof(why) - 1, d3, , dos);
        CloseLibrary(dos);
    }
    exit(RETURN_FAIL);
}

static void __attribute__((destructor)) ol_link_close(void)
{
    if (OpenLayoutBase) CloseLibrary(OpenLayoutBase);
    OpenLayoutBase = NULL;
}
