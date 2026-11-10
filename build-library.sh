#!/bin/sh
# gfopenlayout.library 15: GFOpenLayout, GFCSS, GFOpenLayout UI and its
# Intuition glue in one library, with the os32 stove; libgfopenlayout_link.a,
# the C API through it; and the tests, the UI's linked through the library.
#   OGT_ROOT=.../amicradle-guest/libraries/opengadtools ./build-library.sh
# Without OGT_ROOT the library has no GFOpenGadTools theme reader
# (OLUI_ThemeCss and OLUI_AmigaTheme say so). SPECS (the GFOpen SDK's
# Lib/m68k-soft-m68040.specs) builds the programs linked through the library
# soft float, as build-ui-amiga.sh does: libnix's printf has FPU code
# otherwise. The library itself has no floating point at all. OPT (default
# -O2) is the library's optimisation: the GFOpenKick ROM's build is -Os
# (Dale, 2 Nov 2026: 170 KB, 88 KB packed; at -O2 it doesn't fit).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}

OGT_ROOT=${OGT_ROOT:-}
OPT=${OPT:--O2}
SPECS=${SPECS:-}
SPECFLAG=""
[ -n "$SPECS" ] && SPECFLAG="-specs=$SPECS"
mkdir -p "$OUT/include/libraries" "$OUT/include/inline" "$OUT/include/proto" "$OUT/lib" "$OUT/tests" "$OUT/obj"
python3 "$HERE/tools/gen-library.py" --check
cp "$HERE/include/gfopenlayout.h" "$HERE/include/gfopenlayout_css.h" "$HERE/include/gfopenlayout_ui.h" \
   "$HERE/include/gfopenlayout_ui_amiga.h" "$OUT/include/"
cp "$HERE/include/libraries/gfopenlayout.h" "$OUT/include/libraries/"
cp "$HERE/include/inline/gfopenlayout.h" "$OUT/include/inline/"
cp "$HERE/include/proto/gfopenlayout.h" "$OUT/include/proto/"

if [ -n "$OGT_ROOT" ]; then
  THEME="$HERE/ui/olui_theme.c $OGT_ROOT/lib/ogt_theme.c"
  THEME_INC="-I$OGT_ROOT/lib"
else
  THEME="$HERE/ui/olui_theme_none.c"
  THEME_INC=""
fi
# shellcheck disable=SC2086
"$CC" -m68040 -m68881 -mcrt=nix20 $OPT -fno-delete-null-pointer-checks -fno-late-combine-instructions -fomit-frame-pointer -fno-toplevel-reorder \
  -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -nostartfiles \
  -I"$HERE/include" -I"$HERE/ui" $THEME_INC -o "$OUT/lib/gfopenlayout.library" \
  "$HERE/library/openlayout_lib.c" "$HERE/library/openlayout_runtime.c" "$HERE/library/openlayout_runtime_text.c" \
  "$HERE/src/openlayout.c" \
  "$HERE/src/gfcss_parse.c" "$HERE/src/gfcss_values.c" "$HERE/src/gfcss_select.c" "$HERE/src/openlayout_css.c" \
  "$HERE/ui/olui.c" "$HERE/ui/olui_css.c" "$HERE/ui/olui_markup.c" $THEME "$HERE/amiga/olui_amiga.c" \
  -lamiga -lgcc -Wl,-Map,"$OUT/lib/gfopenlayout.library.map"

echo "$OUT/lib/gfopenlayout.library ($(wc -c < "$OUT/lib/gfopenlayout.library") bytes)"
"$P/bin/m68k-amigaos-nm" -u "$OUT/lib/gfopenlayout.library"

"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -Wall -Wextra -Werror -noixemul \
  -I"$HERE/include" -o "$OUT/tests/LayoutLibTest" "$HERE/tests/test_library.c"
"$CC" -m68040 -m68881 -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -Wall -Wextra -Werror -noixemul \
  -I"$HERE/include" -o "$OUT/tests/LayoutLibStress" "$HERE/tests/test_library_stress.c"
# the C API through the library, and the UI's tests and demo linked with it
LINKF="-m68040 $SPECFLAG -noixemul -O2 -fno-delete-null-pointer-checks -fno-late-combine-instructions -Wall -Wextra -Werror -I$HERE/include"
# shellcheck disable=SC2086
"$CC" $LINKF -c "$HERE/library/link/gfopenlayout_link.c" -o "$OUT/obj/gfopenlayout_link.o"
# shellcheck disable=SC2086
"$CC" $LINKF -c "$HERE/library/link/gfopenlayout_link_base.c" -o "$OUT/obj/gfopenlayout_link_base.o"
rm -f "$OUT/lib/libgfopenlayout_link.a"
"$P/bin/m68k-amigaos-ar" rcs "$OUT/lib/libgfopenlayout_link.a" "$OUT/obj/gfopenlayout_link.o" "$OUT/obj/gfopenlayout_link_base.o"
for t in test_ui:OLUITestLib test_ui_css:OLUICSSTestLib; do
  # shellcheck disable=SC2086
  "$CC" $LINKF -DOLUI_LINK_LIBRARY -s -o "$OUT/tests/${t#*:}" "$HERE/tests/${t%%:*}.c" -L"$OUT/lib" -lgfopenlayout_link
done
# shellcheck disable=SC2086
"$CC" $LINKF -s -o "$OUT/tests/OLUIDemoLib" "$HERE/examples/OLUIDemo.c" -L"$OUT/lib" -lgfopenlayout_link
# shellcheck disable=SC2086
"$CC" $LINKF -s -o "$OUT/tests/OLUIOwnTest" "$HERE/examples/OLUIOwnTest.c" -L"$OUT/lib" -lgfopenlayout_link
for f in "$OUT/lib/libgfopenlayout_link.a" "$OUT/tests/OLUITestLib" "$OUT/tests/OLUICSSTestLib" "$OUT/tests/OLUIDemoLib" \
  "$OUT/tests/OLUIOwnTest"; do
  echo "$f ($(wc -c < "$f") bytes)"
done
echo "$OUT/tests/LayoutLibTest ($(wc -c < "$OUT/tests/LayoutLibTest") bytes)"
echo "$OUT/tests/LayoutLibStress ($(wc -c < "$OUT/tests/LayoutLibStress") bytes)"
sha256sum "$OUT/lib/gfopenlayout.library" "$OUT/tests/LayoutLibTest" "$OUT/tests/LayoutLibStress" \
  "$OUT/tests/OLUITestLib" "$OUT/tests/OLUICSSTestLib" "$OUT/tests/OLUIDemoLib" "$OUT/tests/OLUIOwnTest"
