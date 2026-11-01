#!/bin/sh
# Lab build only (not for master): OpenLayout UI without OpenGadTools' theme
# reader, OLUITest, and OLUIFrames from this branch and (OLUIFramesBefore)
# from before/, the UI as it was at ccff1765. Cradle runs this script alone.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LAB="$HERE/labbuild"
STOVE=${STOVE:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
AR=${AR:-"$STOVE/prefix/bin/m68k-amigaos-ar"}
OUT=${OUT:-"$HERE/build-amiga/lab"}
BASE="-m68040 -noixemul -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=gnu99 -Wall -Wextra -Werror"

build() {   # build NAME INCLUDES SOURCES-DIR
  name=$1; inc=$2; src=$3
  mkdir -p "$OUT/$name"
  for f in "$HERE/src/openlayout.c" "$src/ui/olui.c" "$src/ui/olui_css.c" "$src/ui/olui_markup.c" \
           "$src/ui/olui_theme_none.c" "$src/amiga/olui_amiga.c"; do
    "$CC" $BASE $inc -c "$f" -o "$OUT/$name/$(basename "$f" .c).o"
  done
  rm -f "$OUT/$name/libolui.a"
  "$AR" rcs "$OUT/$name/libolui.a" "$OUT/$name"/*.o
}

build after "-I$HERE/include -I$HERE/ui" "$HERE"
build before "-I$LAB/before/include -I$LAB/before/ui -I$HERE/include" "$LAB/before"
"$CC" $BASE -I"$HERE/include" -s -o "$OUT/OLUITest" "$HERE/tests/test_ui.c" "$OUT/after/libolui.a"
"$CC" $BASE -I"$HERE/include" -s -o "$OUT/OLUIFrames" "$LAB/OLUIFrames.c" "$OUT/after/libolui.a"
"$CC" $BASE -I"$LAB/before/include" -I"$HERE/include" -s -o "$OUT/OLUIFramesBefore" "$LAB/OLUIFrames.c" \
  "$OUT/before/libolui.a"
for f in "$OUT/after/libolui.a" "$OUT/OLUITest" "$OUT/OLUIFrames" "$OUT/OLUIFramesBefore"; do
  echo "$f ($(wc -c < "$f") bytes)"
done
sha256sum "$OUT/OLUITest" "$OUT/OLUIFrames" "$OUT/OLUIFramesBefore"
