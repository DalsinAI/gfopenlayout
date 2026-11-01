# GFOpenLayout UI

GFOpenLayout UI (`olui`, version 0.3) lets a program describe its window in
HTML and CSS. GFOpenLayout lays the window out, the GFOpenGadTools theme draws
it, and the program's own C functions are called back when something
happens. It came from a chat window that needed a tree GadTools hasn't
got.

```c
olui *ui = olui_new();
olui_amiga *a = olui_amiga_new(ui, window);
olui_amiga_theme(a, NULL);                    /* the theme GFOpenPrefs Look chose */
olui_load(ui, "<row><input id=name style='flex:1'><button id=ok>OK</button></row>",
          "button{width:80px}", err, sizeof err);
olui_on(ui, "ok", OLUI_CLICK, ok_pressed, my_data);
olui_amiga_layout(a);
/* the event loop: olui_amiga_event(a, msg) for each IDCMP message, then olui_amiga_draw(a) */
```

0.3 has what a word processor's windows and a web browser's forms need:
drop-downs and list boxes (`select`), text of many lines (`textarea`),
password and number fields, group boxes (`fieldset`), labels that work
their controls, Tab's order (`tabindex`), boxes that scroll
(`overflow:auto`) and tooltips (`title`).

`examples/OLUIDemo.c` is a whole program with every widget in it.

## How it fits

- **GadTools stays.** Every GadTools program keeps working as it is, and
  GFOpenGadTools is still how a plain window is made. GFOpenLayout UI is the
  "GUI described once" that GFOpenGadTools' `DESIGN.md` plans as phase 6,
  for programs whose windows GadTools can't make well: trees, panes that
  share the room, pages of settings.
- **One look.** The theme file GFOpenPrefs Look chose is turned into CSS
  (`olui_theme_css`) with GFOpenGadTools' own theme reader
  (`lib/ogt_theme.c`), so these windows take the same colours, gradients
  and corners as GadTools windows. A program's CSS comes after the theme,
  so it can change any of it.
- **Drawing is GFOpenGfx.** The Amiga glue draws through graphics.library,
  which is GFOpenGfx's; boxes on a true-colour screen go through
  cybergraphics' `FillPixelArray` in exact colours, as GFOpenGadTools' look
  does. On a palette screen it uses pens, with a gradient in four bands, as
  `THEME_SPEC.md` section 6 says.
- **No floating point.** GFOpenLayout and the UI use integers only, so a
  program is built soft float (`-m68040` and the GFOpen SDK's specs file)
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
| `div`, `section`, `p`, `h1`-`h3`, `hr`, `spacer` | Boxes and text; `spacer` grows (`flex:1`) | |
| `label` | Text; a click on it works its control: the one `for` names, or the one inside it (a checkbox is ticked, a radio button chosen, a button pressed, anything else takes the keyboard) | `for` |
| `span`, `b`, `strong`, `i`, `em`, `u`, `code`, `a`, `br` | Text in a line | |
| `button` | A button: `OLUI_CLICK` | `value` |
| `checkbox` (or `input type=checkbox`) | A checkbox: `OLUI_CHANGE` | `checked`, `value` |
| `radio` (or `input type=radio`) | A radio button; one of a `name` is on: `OLUI_CHANGE` | `name`, `value`, `checked` |
| `input` | A one-line field: `OLUI_CHANGE` on each edit, `OLUI_SUBMIT` on Return | `value`, `placeholder`, `maxlength`, `readonly` |
| `input type=password` | A field drawn a bullet a character; its events carry no `value`, `olui_get_text` reads it; never copied | as `input` |
| `input type=number`, `spinner` | A number field with up and down arrow buttons: `OLUI_CHANGE` with the value in `index`. Decimals are fixed point: with `step=0.5` the value is in tenths (`olui_number_decimals`). Typed, it is held to `min` and `max` on Return or when the keyboard goes elsewhere | `value`, `min`, `max`, `step` |
| `textarea` | Text of many lines, wrapped at words, that scrolls (a bar when it is taller than its box): `OLUI_CHANGE` on each edit. Its text is as written, spaces and newlines kept | `rows`, `cols` (its size when CSS gives none), `placeholder`, `readonly`, `maxlength`, `wrap=off` |
| `select` | A drop-down: the chosen option and a mark; a click, Return or space opens its list over the window. With `size` above 1, a list box in place. `OLUI_CHANGE` with the option's number from 0 in `index`, its `value` (its text when it has none) in `value` and the option in `item` | `size` |
| `option` | One of a select's choices (`</option>` may be left out, as in HTML) | `value`, `selected`, `disabled`, `label` |
| `optgroup` | A row with its `label`, and the options in it under it | `label`, `disabled` |
| `fieldset`, `legend` | A box round a group, its legend on its top edge, the edge broken where it is | |
| `list`, `tree` | A list that scrolls; with items inside items, a tree: `OLUI_SELECT`, `OLUI_ACTIVATE`, `OLUI_EXPAND`, `OLUI_COLLAPSE` | |
| `item`, `li` | A row of a list; items inside it are its children in the tree | `value`, `open`, `selected` |
| `tabs` | Pages with a row of tab buttons: `OLUI_SELECT` | |
| `tab` | One page | `title` (its button), `value`, `selected` |
| `progress` | A bar from 0 to `max` | `max` |
| `slider` | A value from `min` to `max`, dragged or keyed: `OLUI_CHANGE` as it moves, the value in the event's `index` | `min`, `max`, `value` |
| `img` | A picture's place in a line (in a button too): the program draws it | `image` (its number), `width`, `height` |

Every tag takes `id` (how the program finds it and names its handlers),
`class`, `style`, `disabled`, `hidden`, `title` (a tooltip, but for a
tab's, which is its button's text) and `tabindex`.

## The CSS

The CSS is GFCSS's ([GFCSS.md](GFCSS.md)), the engine GFOpenBrowser Lite
shares: every selector there (type, class and id, attributes, the
descendant, child and sibling combinators, `:not()`, `:nth-child()` and
the other structural pseudo-classes), the states `:hover`, `:active`
(pressed), `:focus`, `:checked`, `:disabled`, `:enabled`, and the UI's own
`:selected` and `:open`, and CSS's cascade: `!important`, then the more
specific rule, then the later one, a `style` attribute after the rules.
Lengths are any of CSS's absolute ones, `em`, `rem` (of the window's font
size), `vw` and `vh` and `calc()`; colours are all of CSS's names,
`#rgb` to `#rrggbbaa`, `rgb()`, `rgba()`, `hsl()` and `hsla()`.
`@media screen` and `@media all` rules count (the window's size isn't
known to them: a width or height feature is false); `@media print` rules
don't.

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
| `color`, `background`, `background-color` | Colours; `background` also takes `linear-gradient()` down the box, with up to four stops |
| `border`, `border-width`, `border-color`, `border-radius` | |
| `font-size`, `font-weight`, `font-style`, `font-family` (`monospace`), `font`, `text-decoration` (`underline`, `line-through`), `text-align`, `line-height` | |
| `accent-color` | Ticks, radio dots, progress bars and a slider's filled track |
| `mark-background`, `mark-border-color` | A checkbox's and radio button's box, a slider's track (ours, not CSS's) |
| `placeholder-color` | A field's placeholder (ours) |
| `overflow` (`overflow-y`) | `auto` or `scroll`: a box with a height scrolls what it holds, with a bar when that is taller; `hidden`: cut to the box, scrolled only by the program; `visible` |

A list with `height:auto` is as tall as its row, or as what grows gives it,
and scrolls what it holds.

The new widgets' parts are styled as elements of their own: a select's
pop-up list is `popup` (under its select: `#font popup`), its rows
`option` and `optgroup` (`option:selected` is the one pointed at, and a
list box's chosen one), and a tooltip `tooltip` (under the element whose
title it is). A select with `size` is also of class `listbox`
(`select.listbox` has a list's look), and `select:open` while its list is
open. A textarea's selection is drawn in `accent-color` with its text in
`mark-background`; a number field's buttons in `mark-background`, the one
held in `accent-color`. What CSS has beyond this table (grid, floats,
positions, transforms) is not done, and a rule using it is left out, as a
browser leaves out what it doesn't know.

The UI keeps its own ways where they aren't CSS's: an element is a block
unless its CSS says otherwise; a row's children don't take its
`text-align` (its `justify-content` is put there); an element given a new
font size and no line height keeps its line height in proportion; a
border is as wide as its top side, black unless a colour is given; a
percentage (of what, the styling doesn't know) is left out. Since GFCSS,
three things differ from the UI's first CSS: `!important` counts, an
`rgba()` alpha is exact (0.5 was 141 of 255, now 128), and a line height
set on an element stays when a later rule gives it a new font size.

## Events

`olui_on(ui, id, type, function, data)` calls `function(ui, event, data)`
for the events of `type` (0: all) on the widget with that `id` (NULL: every
widget). The event has its `type`, the widget (`el`, `id`), a list's or
tabs' `item`, a `value` (a field's text, an item's or radio's `value`, else
its `id`), an `index` and `checked`. A handler may change any widget: the
window is laid out again after it returns.

A field's or textarea's text goes to and from the clipboard through the
program: `OLUI_COPY` and `OLUI_CUT` carry the text in `value` (cut takes
it out after), and on `OLUI_PASTE` the program gives the clipboard's text
with `olui_insert_text(ui, ev->el, text)`. The Amiga key with x, c and v
(Ctrl on the host, as `OLUI_QUAL_COMMAND`) and `olui_edit(ui, OLUI_COPY)`
from a menu do it. Without a selection a field's whole text is cut or
copied; a textarea's needs one.

The keyboard: Tab and Shift-Tab move between widgets, those with a
`tabindex` above 0 first (lowest first), then the rest in the markup's
order; `tabindex=-1` is left out (a click still reaches it). A widget in a
box that scrolls is scrolled into view. Return and space
press a button or a checkbox; in a field the arrows (Shift for the ends),
Backspace and Del; in a list Up, Down, Shift-Up and Shift-Down for a page,
Right and Left to open and close a tree's items, Return to activate; in the
tab buttons Left and Right; on a slider Left and Right (Up and Down) a step,
Shift-Up and Shift-Down a tenth, Shift-Left and Shift-Right the ends. A double-click on an item activates it, and the
wheel scrolls a list.

In a textarea the arrows, Home and End (of the line), PageUp and PageDown;
Shift with any of them selects; Return a new line; Backspace and Del; a
drag selects, a double-click selects a word, the Amiga key with a all of
it. In a closed select Up, Down, Home and End choose, as does a letter
(the next option starting with it); Return or space opens it. In its open
list Up, Down, Home, End, PageUp, PageDown and a letter point, Return or
space chooses, Escape closes it with nothing chosen; a click outside it
closes it too. A list box takes the same keys in place. In a number field
Up and Down a step, PageUp and PageDown ten. On the Amiga Alt with the
arrows is PageUp, PageDown, Home and End. The wheel scrolls the
textarea, list box or box that scrolls under the pointer (the innermost
that can).

## The calls

`include/gfopenlayout_ui.h` describes each one. The main ones:

| Call | Does |
| --- | --- |
| `olui_new`, `olui_free` | |
| `olui_load(ui, markup, css, err, len)` | The window's contents; `err` says the line of a mistake |
| `olui_add_css(ui, css)` | More rules, after those before |
| `olui_on(ui, id, type, fn, data)` | A handler |
| `olui_layout(ui, w, h)`, `olui_draw(ui, area, fn, data)` | For a program that draws itself (the Amiga glue does both) |
| `olui_mouse`, `olui_key` | Input, for the same |
| `olui_find(ui, id)` | A widget |
| `olui_get_text`, `olui_set_text`, `olui_get_value` | A textarea's and a password's text too; a select's `olui_get_text` is its chosen option's |
| `olui_get_checked`, `olui_set_checked`, `olui_set_disabled`, `olui_set_hidden`, `olui_set_progress`, `olui_focus` | `olui_set_checked` works on a button too, for `button:checked` (a toolbox's tool in use) |
| `olui_slider_value`, `olui_set_slider` | A slider's value |
| `olui_box(ui, el, rect)` | Where a widget is, as last laid out |
| `olui_focused` | The widget with the keyboard |
| `olui_get_scroll`, `olui_set_scroll` | A list's, textarea's, list box's or scrolling box's scroll, in pixels |
| `olui_select_index`, `olui_select_set`, `olui_select_count`, `olui_select_option_value`, `olui_select_option_text`, `olui_select_add`, `olui_select_clear`, `olui_select_is_open`, `olui_select_open` | Selects; setting the choice sends no event |
| `olui_number_value`, `olui_number_decimals`, `olui_set_number` | A number field's value, times 10 to the power of its decimals |
| `olui_get_selection`, `olui_set_selection`, `olui_insert_text`, `olui_edit` | A field's or textarea's selection (bytes of its text), text put in as typed, and cut, copy and paste |
| `olui_tick(ui, ms)` | Time going by: see below |
| `olui_list_add(ui, list, parent, text, value)`, `olui_list_clear`, `olui_list_selected`, `olui_list_select`, `olui_item_open` | Lists and trees |
| `olui_tabs_current`, `olui_tabs_select` | |
| `olui_theme_css(text, dark, err, len)` | A theme file as CSS |

## Time: olui_tick

olui has no timer of its own. A program calls `olui_tick(ui, ms)` now and
then with the milliseconds since the last call: a tooltip shows when the
pointer has rested on an element with a `title` for `OLUI_TIP_DELAY` (700
ms), and goes when the pointer moves to another element, a key is pressed
or a button clicked (it comes back only when the pointer has been on
another); a number field's arrow button held steps again after 400 ms and
then every 80. It returns 1 when something needs drawing. The Amiga glue
asks for `IDCMP_INTUITICKS` and calls it with 100 for each, about ten a
second while the window is active, so a program on the glue does nothing
more. A program that draws itself calls it from its own timer; without
calls there are no tooltips, and a held arrow steps once.

The Amiga glue (`include/gfopenlayout_ui_amiga.h`): `olui_amiga_idcmp()`,
`olui_amiga_new(ui, window)`, `olui_amiga_theme(a, path or NULL)`,
`olui_amiga_layout(a)`, `olui_amiga_event(a, msg)`, `olui_amiga_draw(a)`,
`olui_amiga_free(a)`; `olui_amiga_own(a, el, fn, data)` for a widget the
program draws itself (a box the buffer is copied around, up to four), and
`olui_amiga_fonts(a, font, mono)` for the fonts text is drawn in, and
`olui_amiga_images(a, fn, data)` for the program's pictures in `img`
elements, drawn into the window's buffer with the rest. It draws into a buffer the size of the window's
inside, a simple layer on a bitmap like the window's, and copies it to the
window in one blit, so nothing flickers.

## GFOpenLayout 0.8

What the UI needed in GFOpenLayout itself, for any program:

- `OL_BOX_ROW`: a box's children side by side, as a CSS flex row;
- the box style's `width`, `height`, `grow`, `gap` and `align`;
- a box with a height shares what its children leave among those that grow.

`gfopenlayout.library` is 8.0 for these, with no new calls: a program sets
`ol_box_style.size` to `sizeof(ol_box_style)` and opens version 8
(`OPENLAYOUTLIB_ROW_VERSION`). `tests/test_rows.c` tests them.

## Size and speed

Measured 30 October 2026, for 0.2 (0.3's widgets add to the UI's code, and
each element some 85 bytes more by its new fields; not measured on the
68040 yet):

| | |
| --- | --- |
| Code, 68040, `-Os` | GFOpenLayout 20.9 KB; the UI 30.1 KB (widgets 15.1, CSS 7.9, markup 4.3, theme 2.8) |
| Memory, 68040 | 252 bytes a node in GFOpenLayout, 340 an element in the UI, 100 a display op, and their text: a window of 300 widgets about 220 KB |
| Laying out a window of 265 widgets and a list of 100 items | 117 µs on a 2.1 GHz Xeon when only states changed (styles kept), 279 µs with every style worked out again |

Each element's style is kept until the stylesheet or its states change, so
a hover or a click styles only what changed; the layout itself is done
again in full. The 68040 hasn't been timed yet: the lab run will say.

The CSS has been GFCSS's since 1 November 2026: the figures above are from
before it. On the host a window of 401 widgets is styled and laid out from
the start in about 270 µs with it, 365 µs with the UI's first CSS
(`tests/test_ui_css.c`); its size on the 68040 is still to be measured.

## Building

Host tests: `make test`, or `make test OGT_ROOT=<opengadtools>` with the
theme reader; `tests/test_ui.c` runs the widgets, the keyboard and the
events, and with `OLUI_THEME_FILE` set, a theme; `tests/test_ui_css.c` the
CSS GFCSS gives the UI. `tests/ui_render.py` draws
`test_ui`'s ops as a picture, to look at a window on the host.

Amiga: `OGT_ROOT=<opengadtools> SPECS=<SDK>/Lib/m68k-soft-m68040.specs
./build-ui-amiga.sh` builds `libolui.a`, `OLUIDemo`, `OLUITest` and
`OLRowsTest` with the os32 stove.

A program links `libolui.a` and includes `gfopenlayout_ui.h` and
`gfopenlayout_ui_amiga.h`.

## Not yet

- A one-line field's selection is the program's (`olui_set_selection`) or
  all of it (the Amiga key with a): Shift with the arrows still goes to
  its ends, and the pointer places the caret only. A textarea selects
  with both.
- A textarea has no undo, its bar is not dragged (the wheel, the keys and
  `olui_set_scroll` scroll it), lines break at spaces only (a word longer
  than the line between its characters), and a tab is a character like
  any other.
- A select chooses one option (no `multiple`); its pop-up shows 12 rows
  at most and scrolls the rest. A select is as wide as its widest option,
  measured at each layout.
- Menus.
- A box that scrolls scrolls down only (no `overflow-x`), and its bar is not
  dragged.
- An `img` directly in a `row` or `column` keeps no place: put it in a line (a `div`, a `button`).
- A resident library: it is a link library for now.
- Laying out only what changed: a change that can move things (text, items, a style that changes a
  size, an item opened or closed) lays the whole window out. A hover, press, focus, check or selection
  that changes only how widgets look is drawn again only where it changed, with no layout.
