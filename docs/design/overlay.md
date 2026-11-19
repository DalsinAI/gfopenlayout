# GFOpenLayout Overlay: pictures drawn into a window from outside the 68k
- Team: Team 4, owner: Team 4 (Dale, 19 Nov 2026 17:25: "it will be important across the entire OS, call it Overlay"; Team 4 leads it)
- Status: proposal; the name is agreed
- Lives in: DalsinAI/gfopenlayout `library/layers/` (the compositor, layers.library's composited mode), to come. First user: DalsinAI/amigabasiliskii's Amiga front end
- Last changed: 19 Nov 2026

## What it is

An **overlay** is a box in a window whose pixels come from something other than a program drawing through graphics.library: the host (a guest core's frames over GCOR), a GL context (virgl), a video decoder, a camera. The overlay always knows where it is on the display:
- which screen it's on, and that screen's format;
- its box, in the window and on the screen;
- what covers it.

Whoever feeds it writes pictures at their own size and format. The compositor puts them on the screen: it scales, converts the format, and clips to what's visible. The 68k never copies or draws a pixel.

It replaces what each user does by hand today:
- **Guest cores** (Basilisk II now, DOSBox, MAME, a UAE later): the front end fetches every frame into its own memory (GCOR `FRAMEGET`) and draws it with `WritePixelArray`. That's 1.2 MB a frame through the 68k at 640 x 480 in 32 bits.
- **GL windows:** `ORTG_PresentVirgl` marks the whole layer, a special case of the same thing.
- **Video and SDL:** each finds its own way into a window.

## Why it lives in GFOpenLayout

The compositor is there (`library/layers/layers_comp.c`, since 13 Nov 2026). It already knows each layer's place, stack, opacity and shadow, and builds the screen from the layers' surfaces with GFOpenGPU. An overlay is one more surface it composes.

**What doesn't work, found on 19 Nov:** writing the frame into the screen's bitmap at the window's place, as Basilisk's first try did (amigabasiliskii `team4/frames-rtg-20261119`). On a composited screen, the next compose paints over it, and the window's one ClipRect is `obscured`. It only works on classic screens.

## How it works

### A composited screen (Workbench, public RTG screens of 16 or 32 bits)

- **Its own surface:** an overlay has a surface of its own, at the producer's size and format, in video RAM or the card part like a layer's. It is composed right after its layer, cut to the layer's visible part:
  - a COPY when it's at its own size;
  - a scaled COMPOSITE otherwise (the shadows already stretch).

  So a 640 x 480 guest in an 800 x 600 window costs the 68k nothing.
- **Damage:** a producer marks what it changed (`DamageOverlay`) and the compositor composes it, the same way it handles a drawing call's mark: once drawing pauses, and within 8 ms.
- **Moving, covering, translucency, shadows and dragging** all work, because the compositor already handles them for layers.

### A classic screen (a game's own screen, FULLSCREEN, 8-bit, AGA)

- **Visible rectangles:** the overlay gives its producer the list of visible rectangles in the screen's bitmap, plus a version number that changes when they do. A host producer writes only there.
- **The fallback:** an overlay on a screen with no matching format, or with no GFOpenGPU, falls back to `WritePixelArray` and `ScalePixelArray` on the 68k. A producer never has to know which case it's in.

### The calls (layers.library, proposed)

| Call | What |
|---|---|
| `ov = OpenOverlay(layer, tags)` | `OVA_Box` (in the layer), `OVA_Width` and `OVA_Height` (the picture's), `OVA_Format` (ARGB32, RGB565, CLUT8 with `OVA_Palette`) |
| `OverlayInfo(ov, &info)` | where to write: the surface's address, bytes per row and format, or the classic visible rectangles and their version |
| `DamageOverlay(ov, rect)` | the producer changed this; NULL for all of it |
| `SetOverlayAttrs(ov, tags)` | a new box, size or format (a guest changed mode) |
| `CloseOverlay(ov)` | |

A signal (`OVA_Signal`) tells the producer when `OverlayInfo`'s answer changes: the window moved to another screen, the screen went classic, or the surface moved.

### The host side (GCOR, the CPU cores board)

- **v1:**
  - The front end opens an overlay at the guest's size.
  - It gives the board the overlay's address, bytes per row and format through GCOR `FRAME`, unchanged since ABI 2.
  - On `GEV_FRAME` it writes `FRAMEGET`, so the host copies the frame straight into the surface.
  - Then it calls `DamageOverlay`. The 68k's whole cost per frame is two register writes and one call.
- **Later:**
  - The board writes into the overlay on its own and marks it from the host, so the front end is no longer woken per frame.
  - The guest's frame buffer becomes the overlay's surface, so nothing is copied at all.

## Users, in order

1. **The Basilisk II front end.** It replaces the 68k drawing path; the window, SCALE and FULLSCREEN all go through the overlay.
2. **DOSBox, then MAME,** on the same GCOR path (GFOpenEmu's core module gets `gfemu_core_overlay`).
3. **`ORTG_PresentVirgl`** becomes an overlay: GL windows, OpenGL games.
4. **GFOpenMediaPlayer's video,** and SDL's window surfaces.

## Steps

1. **This design** (lodged in gfosdesign).
2. **The overlay in `layers_comp.c`:** its own surface, composed after its layer, COPY and scaled COMPOSITE, `DamageOverlay`. Tested by `tests/CompTest.c` with a moving pattern written by the 68k, with no host needed.
3. **Classic screens:** visible rectangles, and the 68k fallback.
4. **Basilisk II on it,** in a lab: frames per second and the 68k's time per frame, before and after.
5. **gfemu_core_overlay** for every guest core.
