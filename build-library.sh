#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}

mkdir -p "$OUT/include/libraries" "$OUT/include/inline" "$OUT/include/proto" "$OUT/lib" "$OUT/tests"
cp "$HERE/include/gfopenlayout.h" "$OUT/include/"
cp "$HERE/include/libraries/gfopenlayout.h" "$OUT/include/libraries/"
cp "$HERE/include/inline/gfopenlayout.h" "$OUT/include/inline/"
cp "$HERE/include/proto/gfopenlayout.h" "$OUT/include/proto/"

"$CC" -m68040 -m68881 -mcrt=nix20 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -fomit-frame-pointer -fno-toplevel-reorder \
  -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter -nostartfiles \
  -I"$HERE/include" -o "$OUT/lib/gfopenlayout.library" \
  "$HERE/library/openlayout_lib.c" "$HERE/library/openlayout_runtime.c" "$HERE/src/openlayout.c" \
  -lamiga -lgcc -Wl,-Map,"$OUT/lib/gfopenlayout.library.map"

echo "$OUT/lib/gfopenlayout.library ($(wc -c < "$OUT/lib/gfopenlayout.library") bytes)"
"$P/bin/m68k-amigaos-nm" -u "$OUT/lib/gfopenlayout.library"

"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -Wall -Wextra -Werror -noixemul \
  -I"$HERE/include" -o "$OUT/tests/LayoutLibTest" "$HERE/tests/test_library.c"
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -Wall -Wextra -Werror -noixemul \
  -I"$HERE/include" -o "$OUT/tests/LayoutLibStress" "$HERE/tests/test_library_stress.c"
echo "$OUT/tests/LayoutLibTest ($(wc -c < "$OUT/tests/LayoutLibTest") bytes)"
echo "$OUT/tests/LayoutLibStress ($(wc -c < "$OUT/tests/LayoutLibStress") bytes)"
sha256sum "$OUT/lib/gfopenlayout.library" "$OUT/tests/LayoutLibTest" "$OUT/tests/LayoutLibStress"
