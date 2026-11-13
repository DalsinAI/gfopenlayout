#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
AR=${AR:-"$STOVE/prefix/bin/m68k-amigaos-ar"}
OUT=${OUT:-"$HERE/build-amiga"}

# GFCSS (the CSS engine) and GFOpenLayout's documents styled through it go in with GFOpenLayout
CSS="gfcss_parse gfcss_values gfcss_select openlayout_css"

mkdir -p "$OUT"
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror -pedantic \
  -I"$HERE/include" -c "$HERE/src/openlayout.c" -o "$OUT/openlayout.o"
CSS_OBJ=""
for f in $CSS; do
  "$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror \
    -pedantic -I"$HERE/include" -c "$HERE/src/$f.c" -o "$OUT/$f.o"
  CSS_OBJ="$CSS_OBJ $OUT/$f.o"
done
"$AR" rcs "$OUT/libopenlayout.a" "$OUT/openlayout.o" $CSS_OBJ
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror -pedantic -noixemul \
  -I"$HERE/include" "$HERE/tests/test_layout.c" "$OUT/libopenlayout.a" \
  -o "$OUT/LayoutTest"
# 0.6: the document tests too (LayoutDocTest: pages, frames, text round them, the sort checked against 0.4's),
# from a copy of GFOpenLayout built with OL_CHECK_SORT
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror \
  -pedantic -DOL_CHECK_SORT -I"$HERE/include" -c "$HERE/src/openlayout.c" -o "$OUT/openlayout-check.o"
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror \
  -pedantic -noixemul -I"$HERE/include" "$HERE/tests/test_document.c" "$OUT/openlayout-check.o" $CSS_OBJ -o "$OUT/LayoutDocTest"
# GFCSS's tests (LayoutCSSTest)
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror \
  -pedantic -noixemul -I"$HERE/include" "$HERE/tests/test_css.c" "$OUT/libopenlayout.a" -o "$OUT/LayoutCSSTest"
# GFOpenLayout drawing (include/gfopenlayout_draw.h): libgfopenlayout_draw.a, what Workbench wave 1's modules draw
# with instead of graphics.library, and DrawTest (tests/test_draw.c on m68k). No floating point, so it links into
# soft- and hard-float programs alike.
mkdir -p "$OUT/include"
cp "$HERE/include/gfopenlayout_draw.h" "$OUT/include/"
for f in draw/oldraw amiga/oldraw_amiga; do
  "$CC" -m68040 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror \
    -noixemul -DOLDRAW_AMIGA -I"$HERE/include" -I"$HERE/draw" -c "$HERE/$f.c" -o "$OUT/$(basename "$f").o"
done
"$AR" rcs "$OUT/libgfopenlayout_draw.a" "$OUT/oldraw.o" "$OUT/oldraw_amiga.o"
"$CC" -m68040 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror \
  -noixemul -I"$HERE/include" -I"$HERE/draw" "$HERE/tests/test_draw.c" "$HERE/draw/oldraw.c" -o "$OUT/DrawTest"
echo "$OUT/libgfopenlayout_draw.a ($(wc -c < "$OUT/libgfopenlayout_draw.a") bytes)"
# 0.5: gfopenlayout.library and its tests (LayoutLibTest, LayoutLibStress) too, for the lab (Cradle builds with
# this script alone)
OUT="$OUT/library" STOVE="$STOVE" CC="$CC" sh "$HERE/build-library.sh"
echo "$OUT/libopenlayout.a ($(wc -c < "$OUT/libopenlayout.a") bytes)"
echo "$OUT/LayoutTest ($(wc -c < "$OUT/LayoutTest") bytes)"
echo "$OUT/LayoutDocTest ($(wc -c < "$OUT/LayoutDocTest") bytes)"
echo "$OUT/LayoutCSSTest ($(wc -c < "$OUT/LayoutCSSTest") bytes)"
sha256sum "$OUT/libopenlayout.a" "$OUT/LayoutTest" "$OUT/LayoutDocTest" "$OUT/LayoutCSSTest"
