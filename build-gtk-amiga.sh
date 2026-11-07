#!/bin/sh
# GFOpenGTK for AmigaOS 3.x on the 68040: libgfopengtk.a (GFOpenLayout and
# GFCSS, the UI and its Intuition glue, and GTK's API over them), GtkHello
# (examples/GtkHello.c, a GTK 1.2 program unchanged) and GtkTest
# (tests/test_gtk.c on the window-less backend, the host test on the 68040).
# Soft float, as build-ui-amiga.sh: give SPECS the GFOpen SDK's specs file.
#   SPECS=.../m68k-soft-m68040.specs ./build-gtk-amiga.sh
# OGT_ROOT (GFOpenGadTools, for its theme reader) is optional: without it the
# windows take olui's built-in look.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOLCHAIN=${TOOLCHAIN:-"$HOME/CradleLab/toolchain/openamigagcc-16.2.0-open4-x86_64-linux"}
CC=${CC:-"$TOOLCHAIN/bin/m68k-amigaos-gcc"}
AR=${AR:-"$TOOLCHAIN/bin/m68k-amigaos-ar"}
NDK=${NDK:-"$HOME/CradleLab/gfopensdk/OpenNDK/Include_H"}
OUT=${OUT:-"$HERE/build-amiga/gtk"}
OGT_ROOT=${OGT_ROOT:-"$HOME/CradleLab/amicradle-guest/libraries/opengadtools"}
SPECS=${SPECS:-}
SPECFLAG=""
[ -n "$SPECS" ] && SPECFLAG="-specs=$SPECS"

THEME="ui/olui_theme_none.c"
THEME_INC=""
if [ -f "$OGT_ROOT/lib/ogt_theme.c" ]; then
  THEME="ui/olui_theme.c"
  THEME_INC="-I$OGT_ROOT/lib"
fi

mkdir -p "$OUT/obj"
FLAGS="-m68040 $SPECFLAG -noixemul -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=gnu99 -Wall -Wextra -Werror \
  -I$HERE/gtk/include -I$HERE/gtk -I$HERE/include -I$HERE/ui $THEME_INC -I$NDK"
rm -f "$OUT"/obj/*.o
for f in src/openlayout.c src/gfcss_parse.c src/gfcss_values.c src/gfcss_select.c src/openlayout_css.c \
         ui/olui.c ui/olui_css.c ui/olui_markup.c $THEME amiga/olui_amiga.c \
         gtk/gtk.c gtk/glib.c; do
  "$CC" $FLAGS -c "$HERE/$f" -o "$OUT/obj/$(basename "$f" .c).o"
done
[ -n "$THEME_INC" ] && "$CC" $FLAGS -Wno-sign-compare -c "$OGT_ROOT/lib/ogt_theme.c" -o "$OUT/obj/ogt_theme.o"
"$CC" $FLAGS -c "$HERE/gtk/gtkc_amiga.c" -o "$OUT/gtkc_amiga.o"
"$CC" $FLAGS -c "$HERE/gtk/gtkc_host.c" -o "$OUT/gtkc_host.o"
rm -f "$OUT/libgfopengtk.a"
"$AR" rcs "$OUT/libgfopengtk.a" "$OUT"/obj/*.o "$OUT/gtkc_amiga.o"
"$CC" $FLAGS -s -o "$OUT/GtkHello" "$HERE/examples/GtkHello.c" "$OUT/libgfopengtk.a"
# the host test with the window-less backend in place of Intuition's
"$CC" $FLAGS -s -o "$OUT/GtkTest" "$HERE/tests/test_gtk.c" "$OUT/gtkc_host.o" "$OUT"/obj/*.o
for f in "$OUT/libgfopengtk.a" "$OUT/GtkHello" "$OUT/GtkTest"; do
  echo "$f ($(wc -c < "$f") bytes)"
done
sha256sum "$OUT/GtkHello" "$OUT/GtkTest"
