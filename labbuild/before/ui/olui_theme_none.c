/* OpenLayout UI built without OpenGadTools' theme reader (no OGT_ROOT):
 * olui_theme_css says so, and the built-in look stands.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "olui_internal.h"

#include <stdio.h>

char *olui_theme_css(const char *theme_text, int dark, char *err, size_t errlen)
{
    (void)theme_text;
    (void)dark;
    if (err && errlen) snprintf(err, errlen, "built without OpenGadTools' theme reader (OGT_ROOT)");
    return NULL;
}
