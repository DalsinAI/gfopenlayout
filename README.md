# GFOpenLayout

GFOpenLayout is the small semantic layout and display-list engine shared by the Open Amiga applications.

It is intentionally independent of GFOpenWrite document formats and HTML parsing. Consumers provide a semantic tree and computed styles; GFOpenLayout provides deterministic geometry, stable object IDs, hit-testing and a renderer-neutral display list.

## First light

The 0.1 foundation includes:

- exact units shared by CSS pixels, print points and GFOpenWrite twips;
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
  in 0.4 a frame can be anchored to its page (`box.anchor`): placed from
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

## Tab stops that align (0.3)

`ol_node_set_tab_stops` gives a block's stops with their alignment and
leader: a right stop ends the text after its tab at the stop, a centred
one centres it there, a decimal one puts its first `.` there. A stop may
be past the line's end, as a contents page's page numbers often are. The
leader (dots, hyphens, underscores, middle dots or a heavy rule) is in the
tab's text op, `OL_TEXT_LEADER_OF(op->u.text.style.text_flags)`, for the
renderer to draw across the op's bounds. `ol_node_set_tabs` still sets
left stops without leaders.

## Rows, and programs' windows (0.8)

`OL_BOX_ROW` lays a box's children side by side, as a CSS flex row: each
as wide as its `box.width` or its content, sharing the room left by
`box.grow` (or giving room up in proportion when there is too little),
`box.gap` apart, as tall as the row or at its top, middle or bottom
(`box.align`). A box with a `box.height` shares what its children leave
below them among those that grow. `gfopenlayout.library` 8.0 lays these out,
with no new calls (`OPENLAYOUTLIB_ROW_VERSION`).

On them sits **GFOpenLayout UI** (`include/gfopenlayout_ui.h`, `ui/`, `amiga/`):
a program's window described in HTML and CSS, laid out by GFOpenLayout,
drawn in the GFOpenGadTools theme, with its events in the program's C
functions. Buttons, checkboxes, radio buttons, fields, lists and trees,
tabs and progress bars; and since 0.3 what a word processor's windows and
a web browser's forms need: drop-downs and list boxes (`select`), text of
many lines (`textarea`), password and number fields, group boxes
(`fieldset`), labels that work their controls, Tab's order (`tabindex`),
boxes that scroll (`overflow:auto`) and tooltips (`title`, through
`olui_tick`). [docs/GFOpenLayout-UI.md](docs/GFOpenLayout-UI.md)
describes it; `examples/OLUIDemo.c` is a whole program.

## Web boxes (0.9)

0.9 adds the boxes a web browser and a word processor need, with seven more
fields at the end of `ol_box_style`, four flags and a display value
(`OL_HAS_WEB_BOXES`):

- per-side borders: with `OL_BOX_SIDE_BORDERS` each side has its own width
  and colour (`border_widths` and `border_argbs`: top, right, bottom,
  left; a side 0 wide has none). They take room as CSS borders do: the
  padding and content are inside them, and each is a rule along its side,
  drawn within the box (`ol_draw_page`'s box for it is that side's strip).
  `border_width`, `border_argb` and `border_sides` work as before, on the
  box's edges and taking no room;
- widths in thousandths: `width_permille` of the containing block's content
  width (a cell's: the table's; a row's child's: the row's; an
  inline-block's: the line's) when there is no `width`, and the least and
  most a box is wide, `min_width`, `max_width` and `max_width_permille`
  (the narrower of the two). The least wins over the most, as in CSS, and
  only the least may make a block wider than its containing block. A
  document's own blocks now take their `width` too (0.8 gave them the
  galley's width);
- auto side margins: `OL_BOX_AUTO_LEFT` puts a block narrower than its
  containing block at its right, and with `OL_BOX_AUTO_RIGHT` in its
  middle, as `margin: 0 auto` does;
- inline-blocks: `OL_DISPLAY_INLINE_BLOCK` puts a box in the line like a
  picture, laid out as a block: as wide as it asks, or as its content up
  to the line's width. It stands on the baseline by its bottom, or at the
  line's top, middle or bottom (`valign`), and goes to the next line, as a
  word does, when it does not fit. It is laid out twice, once for its
  height and once where its line puts it;
- table cells: without the table's column widths, a cell's `width`,
  `width_permille` or `min_width` gives its column's (the widest of a
  column's cells that span it alone; the others share what is left), and
  `valign` puts its content at the top, middle or bottom of a taller row;
- repeated table headers: rows with `OL_BOX_REPEAT_HEADER` from a table's
  first row on are drawn again at the top of every page the table goes on
  to, the rest of the page below them, and the header never stands alone
  at a page's foot. A header as high as a page or higher is not repeated.
  The copies are ops of the header's own nodes, so a renderer draws them
  as it draws the header; `ol_hit_test_page` finds nothing over them.

All of it is whole numbers: a thousandth is worked out with 32 bit
divisions, which a 68040 does itself. `tests/test_web.c` covers each of
them. `gfopenlayout.library` 9.0 lays these out, with no new calls
(`OPENLAYOUTLIB_WEB_VERSION`).

## Build and test

Host:

```sh
make test                                   # GFOpenLayout, and the UI without a theme reader
make test OGT_ROOT=<opengadtools>           # the UI with GFOpenGadTools' theme reader
```

GFOpenLayout UI for the Amiga (`libolui.a`, `OLUIDemo`, `OLUITest`, `OLRowsTest`), soft float:

```sh
OGT_ROOT=<opengadtools> SPECS=<SDK>/Lib/m68k-soft-m68040.specs ./build-ui-amiga.sh
```

Amiga static target:

```sh
./build-amiga.sh
```

A program that lays a document out again after each change (an editor: a new `ol_document` each time) can
give `ol_document_reserve` the last layout's `ol_display_count` and a little more (0.5): the display list then
takes one block of about that size before the nodes' small ones, and doesn't grow by doubling. Pages are
sorted where the list is, not copied into a second one. On an Amiga, libnix's malloc kept the doubled and
copied blocks of a 30-page document (3 MB) in its pools after every relayout.

Text goes round a frame anchored to its page when the frame's `ol_box_style` has a `wrap` (0.6):
`OL_WRAP_SQUARE` puts the body's lines beside it, on either side with room (at least `OL_WRAP_MIN`),
`OL_WRAP_TOP_BOTTOM` above and below it, both `wrap_gap` from its edges. A frame's zone is worked out where
its paragraph starts, on the page that paragraph's first line is on (from the pages cut so far), so the
lines after it wrap round it in the same pass; a frame placed above its paragraph, or one that moves the
page it is on, takes another pass (at most `OL_WRAP_PASSES` in all). A document with nothing to wrap round
is laid out once, as before. Lines in table cells and in frames do not wrap. `ol_frame_place` says where an
anchored frame went (its page and rectangle), for an editor to find the text box under the pointer.

A block with `OL_BOX_EXACT_LINES` (0.7) has every line of its text exactly its style's `line_height` high, a word
processor's exact line spacing: taller text stands on the line's bottom and runs over the line above. Without it,
a line is as tall as its tallest run's `line_height` or text, whichever is more ("at least").

Resident Amiga library and ABI test:

```sh
./build-library.sh
```

The resident build produces `build-amiga/lib/gfopenlayout.library` plus the public `libraries/`, `proto/` and `inline/` headers. It uses a process-free Exec allocation runtime so documents owned by the resident library do not depend on a caller's libc startup. The current ABI is `gfopenlayout.library` 9.0. Version 2 added the node inspection vectors required by shared renderers and automation; version 3 adds the document calls (pages, tables, frames, tab stops, the caret, the display scale and `OL_DrawPage`) after them, so programs built for version 2 keep working; version 4 adds `OL_NodeSetTabStops` after those; version 5 places frames anchored to their pages (`box.anchor`, GFOpenLayout 0.4) with no new calls; version 6 adds `OL_DocumentReserve` (GFOpenLayout 0.5); version 7 wraps text round anchored frames (`box.wrap`, GFOpenLayout 0.6) and adds `OL_FramePlace` (where a frame went); revision 7.1 lays a block's lines out exactly its line height with `OL_BOX_EXACT_LINES` (GFOpenLayout 0.7), a flag 7.0 ignores; version 8 lays out rows and the box style's width, height, grow, gap and align (GFOpenLayout 0.8), with no new calls; version 9 lays out web boxes (GFOpenLayout 0.9): per-side borders, widths in thousandths with their least and most, auto side margins, inline-blocks, cells' vertical alignment and repeated table headers, again with no new calls. A program that uses the document calls opens `OPENLAYOUTLIB_DOCUMENT_VERSION` (3), one that sets aligned tab stops `OPENLAYOUTLIB_TABSTOP_VERSION` (4), one that anchors frames `OPENLAYOUTLIB_ANCHOR_VERSION` (5), one that reserves the display list's room `OPENLAYOUTLIB_RESERVE_VERSION` (6), one that wraps text round frames `OPENLAYOUTLIB_WRAP_VERSION` (7), one that lays out rows `OPENLAYOUTLIB_ROW_VERSION` (8), and one that uses web boxes `OPENLAYOUTLIB_WEB_VERSION` (9). The library's `OL_BoxStyleInit` fills only the `ol_box_style` that programs built for 3 and 4 have (`OL_BOX_STYLE_SIZE_0_3` bytes), so it never writes past theirs; a program that anchors a frame then sets `size` to `sizeof(ol_box_style)` and `anchor` (and `wrap` and `wrap_gap`, 0.8's fields and 0.9's). A box style of an older size has the fields after it 0, so a program built for 8 has no side borders, widths in thousandths or `valign` (`OL_BOX_STYLE_SIZE_0_8`).

The OS 3.2.3 / AC090 68040 qualification opens the library from `LIBS:` and runs both the normal ABI test and a 48-node stress test covering allocation growth, metadata, image nodes, layout and display-list access.

The Amiga builds use the AmiCradle os32-gcc16 stove by default.

## Geometry

- 1 point = 80 GFOpenLayout units
- 1 CSS pixel = 60 units
- 1 twip = 4 units
- 1 inch = 5760 units

See `DESIGN.md` and `docs/LIGHTWEIGHT_BROWSER.md`.

## Licence

MIT. Copyright (c) 2026 AmiCradle Team.
