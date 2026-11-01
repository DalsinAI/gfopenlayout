#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
AR=${AR:-"$STOVE/prefix/bin/m68k-amigaos-ar"}
OUT=${OUT:-"$HERE/build-amiga"}

mkdir -p "$OUT"
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror -pedantic \
  -I"$HERE/include" -c "$HERE/src/openlayout.c" -o "$OUT/openlayout.o"
"$AR" rcs "$OUT/libopenlayout.a" "$OUT/openlayout.o"
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror -pedantic -noixemul \
  -I"$HERE/include" "$HERE/tests/test_layout.c" "$OUT/libopenlayout.a" \
  -o "$OUT/LayoutTest"
# 0.6: the document tests too (LayoutDocTest: pages, frames, text round them, the sort checked against 0.4's),
# from a copy of GFOpenLayout built with OL_CHECK_SORT
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror \
  -pedantic -DOL_CHECK_SORT -I"$HERE/include" -c "$HERE/src/openlayout.c" -o "$OUT/openlayout-check.o"
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror \
  -pedantic -noixemul -I"$HERE/include" "$HERE/tests/test_document.c" "$OUT/openlayout-check.o" -o "$OUT/LayoutDocTest"
# 0.5: gfopenlayout.library and its tests (LayoutLibTest, LayoutLibStress) too, for the lab (Cradle builds with
# this script alone)
OUT="$OUT/library" STOVE="$STOVE" CC="$CC" sh "$HERE/build-library.sh"
echo "$OUT/libopenlayout.a ($(wc -c < "$OUT/libopenlayout.a") bytes)"
echo "$OUT/LayoutTest ($(wc -c < "$OUT/LayoutTest") bytes)"
echo "$OUT/LayoutDocTest ($(wc -c < "$OUT/LayoutDocTest") bytes)"
sha256sum "$OUT/libopenlayout.a" "$OUT/LayoutTest" "$OUT/LayoutDocTest"
