#!/bin/sh
# Build GFOpenLayout's portable core as the N2 host-native modules.
# The platform loader is deliberately not defined here: it is a later change.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$HERE/build-native/gfosmodules"}
X86_64_CC=${X86_64_CC:-x86_64-linux-gnu-gcc}
ARM64_CC=${ARM64_CC:-aarch64-linux-gnu-gcc}
OGT_ROOT=${OGT_ROOT:-}

if [ -n "$OGT_ROOT" ]; then
    THEME="$HERE/ui/olui_theme.c $OGT_ROOT/lib/ogt_theme.c"
    THEME_INC="-I$OGT_ROOT/lib"
else
    THEME="$HERE/ui/olui_theme_none.c"
    THEME_INC=""
fi

SOURCES="
$HERE/src/openlayout.c
$HERE/src/gfcss_parse.c
$HERE/src/gfcss_values.c
$HERE/src/gfcss_select.c
$HERE/src/openlayout_css.c
$HERE/ui/olui.c
$HERE/ui/olui_css.c
$HERE/ui/olui_markup.c
$THEME
"

build_one()
{
    platform=$1
    compiler=$2
    folder="$OUT/$platform/default"
    module="$folder/gfopenlayout.library"
    temporary="$module.gfos-new"
    mkdir -p "$folder"
    # shellcheck disable=SC2086
    "$compiler" -std=c99 -O2 -fPIC -fno-ident -fvisibility=hidden -shared \
        -include "$HERE/gfosmodules/native_exports.h" \
        -Wl,--build-id=none -Wl,-z,defs -Wl,--no-undefined \
        -Wl,--version-script="$HERE/gfosmodules/gfopenlayout.map" \
        -Wall -Wextra -Werror -I"$HERE/include" -I"$HERE/ui" $THEME_INC \
        -o "$temporary" $SOURCES
    mv "$temporary" "$module"
    printf '%s (%s bytes)\n' "$module" "$(wc -c < "$module")"
}

build_one x86_64 "$X86_64_CC"
build_one arm64 "$ARM64_CC"
