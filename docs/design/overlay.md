# GFOpenLayout Overlay: pictures the host writes straight into a window
- Team: Team 4, owner: Team 4 (Dale, 19 Nov 2026: "it will be important across the entire OS, call it Overlay"; Team 4 leads it)
- Status: proposal, being built: layers.library 47.111 (gfopenlayout #79, gfopengfx #70)
- Lives in: DalsinAI/gfopenlayout `library/layers/layers_comp.c` (the compositor) and DalsinAI/gfopengfx `library/layers/` (layers.library's calls), with the host side on the CPU cores board (DalsinAI/gfosmotherboards). First user: DalsinAI/amigabasiliskii's Amiga front end
- Last changed: 19 Nov 2026

## What it is

An **overlay** is a box in a window whose pixels the host writes straight into the screen: a guest core's frames, GL, a video, a camera. The host scales them with the GPU or AkikoPlus.

The compositor never draws the box. It keeps a **place record** up to date and leaves a hole in the window for the box. The record says:
- where the box is on the screen;
- what of it shows;
- the screen's memory and format.

Whatever makes the pictures, an emulator for example, never sees any of this (Dale, 19 Nov): it makes frames as before.

**Moving, sizing, covering and uncovering the window only change the record.** Full screen is just the window resized to the screen: the new size reaches the hardware, which scales the picture into the new space. The 68k doesn't copy or draw a pixel, and the compositor doesn't redraw the window's contents.

## Who does what

| Who | What |
|---|---|
| The program that owns the window (Basilisk II's front end) | `OpenOverlay` on its window once, hands the record to the host (one GCOR register), `CloseOverlay` at the end. Nothing per frame |
| layers.library (the compositor) | Writes the record after every change of the stack, on composited and classic screens alike. On a composited screen it leaves the box out of what it draws of the window |
| The host (the CPU cores board, GFOpenGPU, AkikoPlus) | For each new picture: reads the record and writes the picture, scaled to the box, into each visible rectangle of the screen |
| Whatever makes the pictures (the guest's body) | Nothing new: the board takes its frames as now (`guest_api present`) |

## The place record

It sits in public memory, at the same address for as long as the overlay is open. It's big-endian, and is read as a seqlock: read `seq` and check it's even, read the record, then read `seq` again and check it's the same. If not, read it again.

| Field | |
|---|---|
| `magic`, `version` | `'OVPL'`, 1 |
| `seq` | odd while being written |
| `flags` | `SHOWN` (write into `rects`), `COMPOSITED`, `TOOMANY` (more pieces than `rects` holds) |
| `screen`, `bytes_per_row`, `screen_width`, `screen_height`, `format` | the screen's pixel 0,0, as GFOpenRTG has it. Format 1 is RGB565, 2 is ARGB32; 0 means a screen the host can't write (AGA), so the program draws itself |
| `box` | the overlay's box on the screen |
| `pic_width`, `pic_height` | the picture's size, if the program says |
| `nrects`, `rects[32]` | what of the box shows, on the screen |

## The calls (layers.library 47.111)

| Call | What |
|---|---|
| `ov = OpenOverlay(layer, tags)` | `OVA_Left`, `OVA_Top`, `OVA_Width`, `OVA_Height` (the box in the layer, default all of it); `OVA_PicWidth`, `OVA_PicHeight` |
| `place = OverlayPlace(ov)` | the record's address, for the host |
| `SetOverlayAttrs(ov, tags)` | a new box or picture size |
| `CloseOverlay(ov)` | after the host has stopped writing |

## The host side (GCOR, the CPU cores board)

- **A register, `OVERLAY`:** the front end writes the record's address for its guest, and 0 to stop.
- **On each frame from the guest's body:** the board reads the record and writes the frame, scaled to `box`, into each of `rects`, in the screen's format.
  - v1 scales on the host's CPU, nearest pixel.
  - Then GFOpenGPU (Vulkan) or AkikoPlus does the scaling and the format conversion, synced to the display so there's no tearing.
- **No event per frame** reaches the Amiga.

## Limits, and what comes after

- **A see-through window in front of the box:** it shows the box's old picture under it, because the host writes only where nothing is in front. The GPU blending the front window over the host's picture fixes it.
- **AGA and 8-bit screens:** `format` is 0 and the program draws itself, through `WritePixelArray` or `ScalePixelArray`.
- **More than one overlay in a window:** not yet; v1 allows one per layer.

## Users, in order

1. Basilisk II's front end.
2. DOSBox, then MAME, the same way (GFOpenEmu's core module gets `gfemu_core_overlay`).
3. GL windows (`ORTG_PresentVirgl`).
4. GFOpenMediaPlayer's video, through AkikoPlus.
5. SDL's window surfaces.

## Steps

1. The design: this page.
2. The record and the hole in layers.library: gfopenlayout #79 and gfopengfx #70. `tests/host-comp/test_overlay.c` passes 10/10: the hole, shown, covered, past the screen's edge, hidden, gone, and the seqlock.
3. GCOR `OVERLAY` and the host writing frames into the record's rectangles (gfosmotherboards).
4. Basilisk II's front end on it, in a lab: frames per second and the 68k's time per frame, before and after.
5. Scaling on GFOpenGPU or AkikoPlus.
