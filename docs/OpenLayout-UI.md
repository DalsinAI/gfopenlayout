# OpenLayout UI

OpenLayout UI (`olui`, version 0.2) lets a program describe its window in
HTML and CSS. OpenLayout lays the window out, the OpenGadTools theme draws
it, and the program's own C functions are called back when something
happens. It came from the OpenClaude window, which needed a tree that
GadTools hasn't got.

```c
olui *ui = olui_new();
olui_amiga *a = olui_amiga_new(ui, window);
olui_amiga_theme(a, NULL);                    /* the theme OpenPrefs Look chose */
olui_load(ui, "<row><input id=name style='flex:1'><button id=ok>OK</button></row>",
          "button{width:80px}", err, sizeof err);
olui_on(ui, "ok", OLUI_CLICK, ok_pressed, my_data);
olui_amiga_layout(a);
/* the event loop: olui_amiga_event(a, msg) for each IDCMP message, then olui_amiga_draw(a) */
```

`examples/OLUIDemo.c` is a whole program with every widget in it.

## How it fits

- **GadTools stays.** Every GadTools program keeps working as it is, and
  OpenGadTools is still how a plain window is made. OpenLayout UI is the
  "GUI described once" that OpenGadTools' `DESIGN.md` plans as phase 6,
  for programs whose windows GadTools can't make well: trees, panes that
  share the room, pages of settings.
- **One look.** The theme file OpenPrefs Look chose is turned into CSS
  (`olui_theme_css`) with OpenGadTools' own theme reader
  (`lib/ogt_theme.c`), so these windows take the same colours, gradients
  and corners as GadTools windows. A program's CSS comes after the theme,
  so it can change any of it.
- **Drawing is OpenGfx.** The Amiga glue draws through graphics.library,
  which is OpenGfx's; boxes on a true-colour screen go through
  cybergraphics' `FillPixelArray` in exact colours, as OpenGadTools' look
  does. On a palette screen it uses pens, with a gradient in four bands, as
  `THEME_SPEC.md` section 6 says.
- **No floating point.** OpenLayout and the UI use integers only, so a
  program is built soft float (`-m68040` and the Open SDK's specs file)
  and runs on a 68040 with or without an FPU.

## The markup

It is HTML's syntax: tags, attributes with or without quotes, `&amp;` and
the other entities, `<!-- comments -->`, and `<style>` blocks, which go to
the stylesheet. Spaces collapse as they do in HTML. The whole markup is
the window: what it holds is laid out down the window, and what grows
fills it.

| Tag | What it is | Attributes |
| --- | --- | --- |
| `row` | Its children side by side (`display:flex`) | |
| `column` | Its children one under another, `gap` apart | |
| `div`, `section`, `p`, `label`, `h1`-`h3`, `hr`, `spacer` | Boxes and text; `spacer` grows (`flex:1`) | |
| `span`, `b`, `strong`, `i`, `em`, `u`, `code`, `a`, `br` | Text in a line | |
| `button` | A button: `OLUI_CLICK` | `value` |
| `checkbox` (or `input type=checkbox`) | A checkbox: `OLUI_CHANGE` | `checked`, `value` |
| `radio` (or `input type=radio`) | A radio button; one of a `name` is on: `OLUI_CHANGE` | `name`, `value`, `checked` |
| `input` | A one-line field: `OLUI_CHANGE` on each edit, `OLUI_SUBMIT` on Return | `value`, `placeholder` |
| `list`, `tree` | A list that scrolls; with items inside items, a tree: `OLUI_SELECT`, `OLUI_ACTIVATE`, `OLUI_EXPAND`, `OLUI_COLLAPSE` | |
| `item`, `li` | A row of a list; items inside it are its children in the tree | `value`, `open`, `selected` |
| `tabs` | Pages with a row of tab buttons: `OLUI_SELECT` | |
| `tab` | One page | `title` (its button), `value`, `selected` |
| `progress` | A bar from 0 to `max` | `max` |
| `slider` | A value from `min` to `max`, dragged or keyed: `OLUI_CHANGE` as it moves, the value in the event's `index` | `min`, `max`, `value` |
| `img` | A picture's place in a line (in a button too): the program draws it | `image` (its number), `width`, `height` |

Every tag takes `id` (how the program finds it and names its handlers),
`class`, `style`, `disabled` and `hidden`.

## The CSS

Rules with type, class and id selectors, the descendant and child
combinators (`toolbar button`, `row > label`) and the states `:hover`,
`:active` (pressed), `:focus`, `:checked`, `:disabled`, `:selected` and
`:open`. The more specific rule wins, then the later one, as in CSS; a
`style` attribute comes last. Lengths are `px`, `pt` and `em`.

| Property | Values |
| --- | --- |
| `display` | `block`, `inline`, `flex`, `none` |
| `flex-direction` | `row`, `column` |
| `flex`, `flex-grow` | A whole number: its share of the room left (across a row, or down a box with a height) |
| `gap` | Between a row's or a column's children |
| `align-items` | `stretch` (the default), `start`, `center`, `end` |
| `justify-content` | `start`, `center`, `end`: where a row's children go when none grows |
| `width`, `height` | A length or `auto` |
| `margin`, `padding` and their `-top`, `-right`, `-bottom`, `-left` | Lengths |
| `color`, `background`, `background-color` | `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()`, `rgba()`, a few names; `background` also takes `linear-gradient()` down the box, with up to four stops |
| `border`, `border-width`, `border-color`, `border-radius` | |
| `font-size`, `font-weight`, `font-style`, `font-family` (`monospace`), `text-decoration` (`underline`), `text-align`, `line-height` | |
| `accent-color` | Ticks, radio dots, progress bars and a slider's filled track |
| `mark-background`, `mark-border-color` | A checkbox's and radio button's box, a slider's track (ours, not CSS's) |
| `placeholder-color` | A field's placeholder (ours) |

A list with `height:auto` is as tall as its row, or as what grows gives it,
and scrolls what it holds. What CSS has beyond this table (grid, floats,
positions, transforms) is not done, and a rule using it is left out, as a
browser leaves out what it doesn't know.

## Events

`olui_on(ui, id, type, function, data)` calls `function(ui, event, data)`
for the events of `type` (0: all) on the widget with that `id` (NULL: every
widget). The event has its `type`, the widget (`el`, `id`), a list's or
tabs' `item`, a `value` (a field's text, an item's or radio's `value`, else
its `id`), an `index` and `checked`. A handler may change any widget: the
window is laid out again after it returns.

The keyboard: Tab and Shift-Tab move between widgets; Return and space
press a button or a checkbox; in a field the arrows (Shift for the ends),
Backspace and Del; in a list Up, Down, Shift-Up and Shift-Down for a page,
Right and Left to open and close a tree's items, Return to activate; in the
tab buttons Left and Right; on a slider Left and Right (Up and Down) a step,
Shift-Up and Shift-Down a tenth, Shift-Left and Shift-Right the ends. A double-click on an item activates it, and the
wheel scrolls a list.

## The calls

`include/openlayout_ui.h` describes each one. The main ones:

| Call | Does |
| --- | --- |
| `olui_new`, `olui_free` | |
| `olui_load(ui, markup, css, err, len)` | The window's contents; `err` says the line of a mistake |
| `olui_add_css(ui, css)` | More rules, after those before |
| `olui_on(ui, id, type, fn, data)` | A handler |
| `olui_layout(ui, w, h)`, `olui_draw(ui, area, fn, data)` | For a program that draws itself (the Amiga glue does both) |
| `olui_mouse`, `olui_key` | Input, for the same |
| `olui_find(ui, id)` | A widget |
| `olui_get_text`, `olui_set_text`, `olui_get_value` | |
| `olui_get_checked`, `olui_set_checked`, `olui_set_disabled`, `olui_set_hidden`, `olui_set_progress`, `olui_focus` | `olui_set_checked` works on a button too, for `button:checked` (a toolbox's tool in use) |
| `olui_slider_value`, `olui_set_slider` | A slider's value |
| `olui_box(ui, el, rect)` | Where a widget is, as last laid out |
| `olui_list_add(ui, list, parent, text, value)`, `olui_list_clear`, `olui_list_selected`, `olui_list_select`, `olui_item_open` | Lists and trees |
| `olui_tabs_current`, `olui_tabs_select` | |
| `olui_theme_css(text, dark, err, len)` | A theme file as CSS |

The Amiga glue (`include/openlayout_ui_amiga.h`): `olui_amiga_idcmp()`,
`olui_amiga_new(ui, window)`, `olui_amiga_theme(a, path or NULL)`,
`olui_amiga_layout(a)`, `olui_amiga_event(a, msg)`, `olui_amiga_draw(a)`,
`olui_amiga_free(a)`; `olui_amiga_own(a, el, fn, data)` for a widget the
program draws itself (a box the buffer is copied around, up to four), and
`olui_amiga_fonts(a, font, mono)` for the fonts text is drawn in, and
`olui_amiga_images(a, fn, data)` for the program's pictures in `img`
elements, drawn into the window's buffer with the rest. It draws into a buffer the size of the window's
inside, a simple layer on a bitmap like the window's, and copies it to the
window in one blit, so nothing flickers.

## OpenLayout 0.8

What the UI needed in OpenLayout itself, for any program:

- `OL_BOX_ROW`: a box's children side by side, as a CSS flex row;
- the box style's `width`, `height`, `grow`, `gap` and `align`;
- a box with a height shares what its children leave among those that grow.

`openlayout.library` is 8.0 for these, with no new calls: a program sets
`ol_box_style.size` to `sizeof(ol_box_style)` and opens version 8
(`OPENLAYOUTLIB_ROW_VERSION`). `tests/test_rows.c` tests them.

## Size and speed

Measured 30 October 2026:

| | |
| --- | --- |
| Code, 68040, `-Os` | OpenLayout 20.9 KB; the UI 30.1 KB (widgets 15.1, CSS 7.9, markup 4.3, theme 2.8) |
| Memory, 68040 | 252 bytes a node in OpenLayout, 340 an element in the UI, 100 a display op, and their text: a window of 300 widgets about 220 KB |
| Laying out a window of 265 widgets and a list of 100 items | 117 µs on a 2.1 GHz Xeon when only states changed (styles kept), 279 µs with every style worked out again |

Each element's style is kept until the stylesheet or its states change, so
a hover or a click styles only what changed; the layout itself is done
again in full. The 68040 hasn't been timed yet: the lab run will say.

## Building

Host tests: `make test`, or `make test OGT_ROOT=<opengadtools>` with the
theme reader; `tests/test_ui.c` runs the widgets, the keyboard and the
events, and with `OLUI_THEME_FILE` set, a theme. `tests/ui_render.py` draws
`test_ui`'s ops as a picture, to look at a window on the host.

Amiga: `OGT_ROOT=<opengadtools> SPECS=<SDK>/Lib/m68k-soft-m68040.specs
./build-ui-amiga.sh` builds `libolui.a`, `OLUIDemo`, `OLUITest` and
`OLRowsTest` with the os32 stove.

A program links `libolui.a` and includes `openlayout_ui.h` and
`openlayout_ui_amiga.h`.

## Not yet

- Text selection in a field, the clipboard, and multi-line text editing.
- Menus, pop-up choosers and number fields.
- An `img` directly in a `row` or `column` keeps no place: put it in a line (a `div`, a `button`).
- A resident library: it is a link library for now.
- Laying out only what changed: a change that can move things (text, items, a style that changes a
  size) lays the whole window out. A hover, press or focus that changes only how widgets look is drawn
  again only where it changed, with no layout.
