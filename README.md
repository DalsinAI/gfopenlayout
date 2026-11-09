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

## CSS: GFCSS

**GFCSS** (`include/gfopenlayout_css.h`, `src/gfcss_*.c`, in
`libopenlayout.a`) is the CSS engine GFOpenLayout UI styles its windows
with and GFOpenBrowser Lite will style pages with: stylesheets read as CSS
Syntax 3 says, `@media`, `@import` and `@supports`, every combinator and
the structural, state and `:not()` pseudo-classes, the cascade with
origins and `!important`, inheritance, and computed styles in integers
only, turned into `ol_style` and `ol_box_style` by `gfcss_to_ol`. The
caller describes its elements through a few functions; rules are indexed
by id, class and type, and nothing is allocated for an element.

GFOpenLayout's documents can be styled through it too: a node takes a
tag, attributes and states (`ol_node_set_tag`, `ol_node_set_attr`,
`ol_node_set_css_state`), a document stylesheets and a viewport
(`ol_document_add_stylesheet`, `ol_document_set_viewport`), and
`ol_document_apply_styles`, or the next `ol_layout`, styles every tagged
node. Nodes with no tag keep their styles, so nothing changes for a
document with no stylesheets. [docs/GFCSS.md](docs/GFCSS.md) describes
both; `tests/test_css.c` tests them.

## Desktops (11)

`gfopenlayout.library` 11 (GFOpenLayout UI 0.4, GFCSS 1.1) has what a
desktop and its drawers need (OpenBench on GFOpenLayout), as calls after
version 10's: positions (`position: absolute` with `left` and `top`, and
frames as wide as their content, `OL_BOX_FIT_CONTENT`), `z-index` layers
for drawing and for the pointer, and elements the program makes, places and
takes away (`olui_add`, `olui_remove`, `olui_set_style`, `olui_set_place`,
`olui_hit`); the look of icons on any backdrop (`opacity` as each draw
op's `alpha`, `text-shadow`, `outline`, `background-image: url(N)` and
`olui_image_name`); and selecting and dragging (`selectable`, `draggable`,
`droppable` and `marquee` attributes: clicks, Shift with a click, a band,
drag and drop with the dragged on top, `olui_mouse_qual`); long lists whose
rows the program gives as they are drawn (`vlist`), context menus (`menu`,
`contextmenu`, `olui_menu_open`) and windows that share one stylesheet
(`olui_new_sharing`). [docs/GFOpenLayout-UI.md](docs/GFOpenLayout-UI.md) says how;
`tests/test_desktop.c` tests it. A program opens version 11
(`OPENLAYOUTLIB_DESKTOP_VERSION`) for them; through
`libgfopenlayout_link.a` a version 11 call on version 10 does nothing and
returns 0. Version 10's calls, and the `OL_` calls of 9, are as they were:
`OL_VERSION` stays "0.9", so programs built for them are the same bytes.

## Toolkits (12)

`gfopenlayout.library` 12 (GFOpenLayout UI 0.5) adds, after version 11's,
the two calls a toolkit over the UI needs (the wxWidgets port, gfopenwx):
`olui_repaint` draws a program's picture again, all of it or a part, and
`olui_popup` gives the pop-up open over the window. A program opens version
12 (`OPENLAYOUTLIB_TOOLKIT_VERSION`) for them; through
`libgfopenlayout_link.a` a version 12 call on version 11 does nothing and
returns 0. `tests/test_ui.c` tests both.

## Shared drawing and clipping (13)

`gfopenlayout.library` 13 moves reusable Office drawing mechanics into the
layout engine instead of leaving each application to maintain its own shape,
crop and overflow code:

- `ol_node_set_shape` attaches rectangles, rounded rectangles, ellipses,
  lines, polylines or polygons to any block/frame. Fill, solid/dashed/dotted
  stroke, width, rounded corners and start/end arrows are renderer-neutral. Polygon points
  are in thousandths of the node's bounds, so resizing and page placement do
  not rewrite them;
- `ol_node_set_image_crop` carries source-image crop insets, also in
  thousandths, with the image display op;
- `OL_BOX_CLIP_CONTENT` intersects all descendants with the box. Each
  affected op carries the final clip rectangle, so renderers need no mutable
  push/pop state and a partial redraw may begin at any op.

The node owns one copy of its points and metadata; display ops only reference
that immutable copy. Drawing allocates nothing, scaling remains integer-only,
and paths are bounded by `OL_MAX_SHAPE_POINTS`. The `ol_display_op` and
`ol_device_op` extensions are after every version-12 field, and the three new
vectors are after version 12's last bias. Old programs therefore keep their
field offsets and call biases; a program using the new calls opens
`OPENLAYOUTLIB_DRAWING_VERSION` (13). `tests/test_document.c` verifies copied
paths, nested clipping, crop metadata, pixel scaling, page placement and the
version-12 structure offsets.

GFOpenLayout 0.14 appends anticlockwise rotation in tenths of a degree to the
size-versioned `ol_shape_style`. `ol_shape_path` flattens rectangles, rounded
rectangles, ellipses, lines and polygons into an allocation-free device path,
then rotates it about the supplied bounds' centre. Screen, PDF and other
backends therefore share one primitive geometry and transform implementation.
The call is the resident library's final version-14 vector; all version-13
field offsets and call biases are unchanged. A program using it opens
`OPENLAYOUTLIB_PATH_VERSION` (14).

GFOpenLayout 0.15 adds newspaper columns inside a bounded absolute text
frame. Set `ol_box_style.text_columns` to 2..16 and
`text_column_gap` to the room between them; the existing line, paragraph,
inline-image and formatting engine lays out one narrow galley and places each
frame-height stretch left to right. Each stretch has a stateless clip to its
column and content after the final column is overset rather than painted
outside the frame. A positive `frame.height` is required. The two fields are
appended after every 0.14 box field, so old callers remain byte-for-byte
compatible; a resident-library caller using them opens
`OPENLAYOUTLIB_COLUMNS_VERSION` (15).

GFOpenLayout 0.16 adds copied, box-local exclusion rectangles. A bounded
Writer or Publisher text frame calls `ol_node_set_exclusions` with square or
top-and-bottom rectangles measured from its own border box; its text and
descendant paragraphs then reuse the established version-7 wrap fitter while
sibling flows remain unchanged. Exclusions draw nothing, so the application
keeps one picture or shape object and supplies only its overlapping geometry
to each affected text frame. The call is appended after every version-14
vector (version 15 added no vector), and resident-library users open
`OPENLAYOUTLIB_EXCLUSIONS_VERSION` (16).

## GTK compatibility

GFOpenGTK (`gtk/`, 0.1) is GTK 1.2's API, with 2.x's names beside it, over
the UI, so a Linux program whose windows are GTK (dialogs, settings) is
relinked rather than given a new interface: windows, boxes, tables, labels,
buttons, check and radio buttons, entries, signals, `gtk_main` nested for
modal dialogs, timeouts, and the glib programs lean on. LinBolo (WinBolo's
Linux client) is the first it is for. [docs/GTK-COMPAT.md](docs/GTK-COMPAT.md)
says what is done and the phases to come; `tests/test_gtk.c` tests it, and
`./build-gtk-amiga.sh` builds `libgfopengtk.a` and `GtkHello` for the 68040.

## Build and test

Host:

```sh
make test                                   # GFOpenLayout, and the UI without a theme reader
make test OGT_ROOT=<gfopengadtools>           # the UI with GFOpenGadTools' theme reader
```

GFOpenLayout UI for the Amiga (`libolui.a`, `OLUIDemo`, `OLUITest`, `OLRowsTest`), soft float:

```sh
OGT_ROOT=<gfopengadtools> SPECS=<SDK>/Lib/m68k-soft-m68040.specs ./build-ui-amiga.sh
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

The resident build produces `build-amiga/lib/gfopenlayout.library` plus the public `libraries/`, `proto/` and `inline/` headers. It uses a process-free Exec allocation runtime so documents owned by the resident library do not depend on a caller's libc startup. The current ABI is `gfopenlayout.library` 16.0. Version 2 added node inspection; version 3 documents; version 4 tab stops; version 5 anchored frames; version 6 display-list reserve; version 7 frame wrapping; revision 7.1 exact lines; version 8 rows and flex-like boxes; version 9 web boxes; version 10 combined documents, GFCSS, UI and Intuition glue; versions 11 and 12 extended the UI; version 13 added shared drawing; version 14 path flattening and rotation; version 15 bounded text-frame columns; and version 16 copied local exclusions. Version 15 moves no earlier field: its two box fields follow every 0.14 field. Version 16 appends one vector and leaves every earlier bias unchanged. Callers open the capability version they use: document 3, tab stops 4, anchors 5, reserve 6, wrap 7, rows 8, web boxes 9, drawing 13, paths 14, columns 15, or exclusions 16. `OL_BoxStyleInit` through the library still fills only `OL_BOX_STYLE_SIZE_0_3` bytes, so it never writes past an older caller's structure; callers using later fields set `size` themselves. `OL_BOX_STYLE_SIZE_0_14` identifies the exact pre-column prefix.

### One library (10)

Version 10 has GFOpenLayout, GFCSS, GFOpenLayout UI and its Intuition glue in
`gfopenlayout.library` (about 243 KB), so programs share one copy instead of
each linking `libolui.a` (OLUIDemo is 22 KB through the library, 245 KB
linked). Its 84 new calls are the C API under library names, made from the
headers by `tools/gen-library.py`:

| C API | Library call |
|---|---|
| `ol_node_set_tag` … `ol_document_add_import` (GFCSS in documents) | `OL_NodeSetTag` … `OL_DocumentAddImport` |
| `olui_new` … `olui_free_css` (GFOpenLayout UI) | `OLUI_New` … `OLUI_FreeCss` |
| `olui_amiga_idcmp` … `olui_amiga_event` (the Intuition glue) | `OLUI_AmigaIdcmp` … `OLUI_AmigaEvent` |

Arguments are in registers (pointers a0 to a3, then d0 to d7), and the
generator writes the sfd, the inline calls and the library's side from the
one list, which only grows. `make test` checks they are as it would write
them, and the library's build checks its call table against the last bias.
A program written to the C API needn't change: it links
`libgfopenlayout_link.a` where it linked `libolui.a`, and that opens
`gfopenlayout.library` 10 before `main()` (or stops with RETURN_FAIL and
says so). Callbacks (measure, draw, event handlers) are the program's C
functions, called with the C calling convention; a program built with
`-fbaserel` gives them `__saveds`. The library opens dos, graphics,
intuition and layers (version 39) when it is loaded, and diskfont when it
can; without diskfont the glue uses the screen's font. Build it with
GFOpenGadTools' theme reader and soft float programs:

```sh
OGT_ROOT=<gfopengadtools> SPECS=<SDK>/Lib/m68k-soft-m68040.specs ./build-library.sh
```

That also builds `OLUITestLib`, `OLUICSSTestLib` and `OLUIDemoLib`, the UI's
tests and demo through the library. `gfcss_*` (the engine on its own) is not
in the library yet: programs reach it through documents.

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
