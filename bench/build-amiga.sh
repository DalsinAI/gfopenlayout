#!/bin/sh
# GFLayoutBench for the Amiga (Cradle's stove_build, target bench, stove os32-gcc16-open6; STOVE and OUT are set):
# bench/gflbench.c and gfopenlayout_draw.h's library (draw/oldraw.c, amiga/oldraw_amiga.c) built as the top
# build-amiga.sh builds libgfopenlayout_draw.a (-m68040 -O2, no floating point), into one program, GFLayoutBench.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$HERE/.." && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16-open6"}
CC=${CC:-"$STOVE/prefix/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$TOP/build-amiga/bench"}
FLAGS="-m68040 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -std=c99 -Wall -Wextra -Werror -noixemul"
mkdir -p "$OUT/obj"
for f in draw/oldraw amiga/oldraw_amiga; do
  "$CC" $FLAGS -DOLDRAW_AMIGA -I"$TOP/include" -I"$TOP/draw" -c "$TOP/$f.c" -o "$OUT/obj/$(basename "$f").o"
done
for f in gflbench gflbench_amiga; do
  "$CC" $FLAGS -I"$TOP/include" -I"$HERE" -c "$HERE/$f.c" -o "$OUT/obj/$f.o"
done
"$CC" $FLAGS "$OUT/obj/gflbench_amiga.o" "$OUT/obj/gflbench.o" "$OUT/obj/oldraw.o" "$OUT/obj/oldraw_amiga.o" \
  -o "$OUT/GFLayoutBench"
rm -rf "$OUT/obj"
echo "GFLayoutBench at $(git -C "$TOP" rev-parse --short HEAD 2>/dev/null || echo unknown): $OUT/GFLayoutBench ($(wc -c < "$OUT/GFLayoutBench") bytes)"
