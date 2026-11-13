#!/bin/sh
# The compositor's first start against the lock order, on the host:
# gfopengfx's layers_lib.c with this repository's layers_comp.c built in
# (OL_COMPOSITE), under ASan and UBSan (test_infolock.c).
# GFOpenGfx's code and test headers come from a gfopengfx checkout: GFX=,
# else ../gfopengfx beside this one, else a shallow clone of its main.
# COMP= another directory's layers_comp.c (to see the test fail on an old one).
#   sh tests/host-comp/run.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
GFX=${GFX:-$top/../gfopengfx}
if [ ! -d "$GFX/library/layers" ]; then
  GFX=$tmp/gfopengfx
  git clone -q --depth 1 https://github.com/DalsinAI/gfopengfx "$GFX"
fi
COMP=${COMP:-$top/library/layers}
cc=${HOST_CC:-cc}
# abihost's shim, with a struct Library that has lib_Version and lib_Revision (layers_comp.c reads them)
sed 's/struct Library { char pad\[24\]; APTR lib_IdString; };/struct Library { char pad[20]; UWORD lib_Version, lib_Revision; APTR lib_IdString; };/' \
  "$GFX/tests/abihost/shim.h" > "$tmp/shim.h"
grep -q lib_Revision "$tmp/shim.h" || { echo "FAIL: abihost/shim.h's struct Library has changed" >&2; exit 1; }
# shellcheck disable=SC2086
$cc -O1 -g -w -Werror=implicit-function-declaration \
  -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize-recover=all -DOL_COMPOSITE \
  -I"$here/inc" -I"$GFX/tests/abihost/inc" -I"$GFX/tests/host-ndk" -I"$GFX/tests/m68k/text-ndk" -I"$GFX/include" \
  -I"$GFX/library/layers" -I"$COMP" -include "$tmp/shim.h" -include "$here/comp_host.h" \
  -o "$tmp/t" "$here/test_infolock.c" "$GFX/library/layers/layers_lib.c" "$GFX/library/layers/layers_core.c"
# each test leaves the library state it made (a library base is never torn down): no leak check; the race test counts its own
ASAN_OPTIONS=detect_leaks=0 "$tmp/t"
