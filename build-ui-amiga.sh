#!/bin/sh
# GFOpenLayout UI for AmigaOS 3.x on the 68040: libolui.a (GFOpenLayout, the UI,
# GFOpenGadTools' theme reader and the Intuition glue) and OLUIDemo, with the
# os32 stove. Soft float (one binary with or without an FPU): give SPECS the
# GFOpen SDK's Lib/m68k-soft-m68040.specs. GFOpenLayout and the UI use no
# floating point at all.
#   OGT_ROOT=.../amigachrome-guest/libraries/opengadtools SPECS=.../m68k-soft-m68040.specs ./build-ui-amiga.sh
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
AR=${AR:-"$STOVE/prefix/bin/m68k-amigaos-ar"}
OUT=${OUT:-"$HERE/build-amiga/ui"}
OGT_ROOT=${OGT_ROOT:?"OGT_ROOT: GFOpenGadTools (amigachrome-guest libraries/opengadtools), for its theme reader"}
SPECS=${SPECS:-}
SPECFLAG=""
[ -n "$SPECS" ] && SPECFLAG="-specs=$SPECS"

mkdir -p "$OUT/obj"
FLAGS="-m68040 $SPECFLAG -noixemul -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=gnu99 -Wall -Wextra -Werror \
  -I$HERE/include -I$HERE/ui -I$OGT_ROOT/lib"
for f in src/openlayout.c src/gfcss_parse.c src/gfcss_values.c src/gfcss_select.c src/openlayout_css.c \
         ui/olui.c ui/olui_css.c ui/olui_markup.c ui/olui_theme.c amiga/olui_amiga.c; do
  "$CC" $FLAGS -c "$HERE/$f" -o "$OUT/obj/$(basename "$f" .c).o"
done
"$CC" $FLAGS -Wno-sign-compare -c "$OGT_ROOT/lib/ogt_theme.c" -o "$OUT/obj/ogt_theme.o"
rm -f "$OUT/libolui.a"
"$AR" rcs "$OUT/libolui.a" "$OUT"/obj/*.o
"$CC" $FLAGS -s -o "$OUT/OLUIDemo" "$HERE/examples/OLUIDemo.c" "$OUT/libolui.a"
"$CC" $FLAGS -s -o "$OUT/OLUITest" "$HERE/tests/test_ui.c" "$OUT/libolui.a"
"$CC" $FLAGS -s -o "$OUT/OLRowsTest" "$HERE/tests/test_rows.c" "$OUT/libolui.a"
for f in "$OUT/libolui.a" "$OUT/OLUIDemo" "$OUT/OLUITest" "$OUT/OLRowsTest"; do
  echo "$f ($(wc -c < "$f") bytes)"
done
sha256sum "$OUT/OLUIDemo" "$OUT/OLUITest" "$OUT/OLRowsTest"
