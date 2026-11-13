/* Compile-only: olc_oi()'s entries go into openintuition's own env tables with no cast, so the types match
 * exactly. Built with OI_DIR set to openintuition's source (amigachrome-guest libraries/openintuition);
 * -Werror makes any mismatch (incompatible-pointer-types) fail. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "boopsi_core.h"
#include "window_core.h"
#include "gfopenlayout_core.h"

void probe_oi_types(struct oi_env *env, struct oi_wenv *we);
void probe_oi_types(struct oi_env *env, struct oi_wenv *we)
{
    const olc_oi_hooks *t = olc_oi();
    env->obtain_rp = t->obtain_rp;
    env->release_rp = t->release_rp;
    env->draw_image_state = t->draw_image_state;
    env->erase_rect = t->erase_rect;
    env->fill_rect = t->fill_rect;
    env->text = t->text;
    env->text_length = t->text_length;
    env->font_height = t->font_height;
    env->polyline = t->polyline;
    env->blit_image = t->blit_image;
    env->fill_pattern = t->fill_pattern;
    env->deep = t->deep;
    env->limit_rect = t->limit_rect;
    env->limit_end = t->limit_end;
    we->set_font = t->set_font;
    we->complement = t->complement;
    we->blit_bitmap = t->blit_bitmap;
    we->scroll_raster = t->scroll_raster;
}
