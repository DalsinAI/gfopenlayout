# GFOpenLayout native module builds (N2)

`build-native.sh [OUT]` builds the portable GFOpenLayout, GFCSS and OLUI
cores as PIC shared ELF modules:

```
OUT/
├── arm64/default/gfopenlayout.library   # ELF e_machine 183
└── x86_64/default/gfopenlayout.library  # ELF e_machine 62
```

The files deliberately have the same name as the 68k library. Generate each
folder's Manifest with AmiCradle's `scripts/gfos_manifest.py`, after its 68k
folder, so the native entry takes the 68k module's RomTag name and version.
Build products and Manifests are outputs and are not committed here.

There is no native-loader or descriptor ABI in this change. The modules export
only the existing public GFOpenLayout, GFCSS and OLUI C APIs; private helpers
have hidden ELF visibility. Amiga library lifecycle, RastPort glue and oldraw
remain in the 68k-side products until that loader contract exists.

Theme selection matches `build-library.sh`: leave `OGT_ROOT` unset for the
no-theme reader, or point it at GFOpenGadTools to build `ui/olui_theme.c` with
`$OGT_ROOT/lib/ogt_theme.c`.

Defaults are `x86_64-linux-gnu-gcc` for x86-64 and
`aarch64-linux-gnu-gcc` for ARM64. Override them with `X86_64_CC` and
`ARM64_CC`.

Run the platform-tree, ELF-machine and public-symbol test with:

```
python3 -m unittest -v tests.test_native_modules
```
