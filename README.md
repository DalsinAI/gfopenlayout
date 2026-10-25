# OpenLayout

OpenLayout is the small semantic layout and display-list engine shared by the Open Amiga applications.

It is intentionally independent of OpenWrite document formats and HTML parsing. Consumers provide a semantic tree and computed styles; OpenLayout provides deterministic geometry, stable object IDs, hit-testing and a renderer-neutral display list.

## First light

The 0.1 foundation includes:

- exact units shared by CSS pixels, print points and OpenWrite twips;
- stable semantic IDs;
- block/inline flow and word wrapping;
- text measurement callback;
- image nodes;
- background rectangles;
- display list;
- semantic bounds;
- role/name lookup;
- hit testing;
- headless host tests;
- AmigaOS 3.x 68040 + FPU cross-build: it needs a 68040 with an FPU (a full 68040, not an LC or EC040). The browser stack stays hard float for now.

## Documents (0.2)

0.2 adds what a word processor, a presentation program and desktop
publishing need, on the same tree and display list:

- pages: the galley cut between lines, never inside a line, a picture, a
  table row, a frame or a kept-together block, with page breaks before
  and keep-with-next; each display op carries its page;
- grid tables: column widths, column spans, every cell as tall as its row,
  borders;
- frames placed out of the flow (text boxes, pictures, slides' shapes);
  in 0.3 a frame can be anchored to its page (`box.anchor`): placed from
  the page's edge, its margins or its paragraph on the page the paragraph
  starts on, behind the text or in front of it, without moving the page's
  breaks (a cover's whole-page picture and its title boxes);
- alignment (start, centre, end, justified) and one baseline a line, so
  mixed sizes and pictures line up;
- tab stops: a block's own, then every half inch; a first line that
  hangs stops at the left edge, so a list's text lines up;
- a font number for each run, passed to the measure callback and the
  renderer;
- the caret and the character under a point, for editing;
- drawing at the display's resolution: an `ol_scale` (dots per inch and
  zoom, or a page fitted to a window) turns units into pixels with one
  multiply and shift, edges rounded so touching boxes touch, and
  `ol_draw_page` hands a renderer only the ops that cross the area it is
  drawing, already in pixels. Text is measured for layout at full
  resolution and drawn with a font at the scaled size, so nothing is
  drawn into a big bitmap and scaled down.

`tests/test_document.c` covers each of them.

## Build and test

Host:

```sh
make test
```

Amiga static target:

```sh
./build-amiga.sh
```

Resident Amiga library and ABI test:

```sh
./build-library.sh
```

The resident build produces `build-amiga/lib/openlayout.library` plus the public `libraries/`, `proto/` and `inline/` headers. It uses a process-free Exec allocation runtime so documents owned by the resident library do not depend on a caller's libc startup. The current ABI is `openlayout.library` 3.0. Version 2 added the node inspection vectors required by shared renderers and automation; version 3 adds the document calls (pages, tables, frames, tab stops, the caret, the display scale and `OL_DrawPage`) after them, so programs built for version 2 keep working. A program that uses the document calls opens `OPENLAYOUTLIB_DOCUMENT_VERSION` (3).

The OS 3.2.3 / AC090 68040 qualification opens the library from `LIBS:` and runs both the normal ABI test and a 48-node stress test covering allocation growth, metadata, image nodes, layout and display-list access.

The Amiga builds use the AmigaChrome os32-gcc16 stove by default.

## Geometry

- 1 point = 80 OpenLayout units
- 1 CSS pixel = 60 units
- 1 twip = 4 units
- 1 inch = 5760 units

See `DESIGN.md` and `docs/LIGHTWEIGHT_BROWSER.md`.

## Licence

MIT. Copyright (c) 2026 AmigaChrome Team.
