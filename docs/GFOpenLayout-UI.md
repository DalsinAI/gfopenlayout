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
| `position`, `left`, `top` (0.4) | `absolute` or `fixed`: out of the flow at `left` and `top` from the parent's content corner; `relative`: in the flow, in a layer |
| `z-index` (0.4) | A whole number: a positioned element's layer among its parent's children, for drawing and the pointer |
| `opacity` (0.4) | 0 to 1: each op's `alpha`, multiplied down what the element holds |
| `text-shadow` (0.4) | Up to four shadows, each drawn as the text again under it, moved and in its colour (the blur isn't drawn) |
| `outline` (0.4) | A border round the border box, over what the element holds |
| `background-image`, `background-repeat`, `background-position` (0.4) | `url(N)`: the program's picture N (or a name given a number with `olui_image_name`), an `OLUI_DRAW_IMAGE` op in the padding box over the background; its repeat and its place (`center` is 500 of 1000) go to the drawer |

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
transforms) is not done, and a rule using it is left out, as a
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

**A strip of a window (library 11).** A program that draws most of its
window itself, such as the browser with its page, gives olui only a strip:
`olui_amiga_strip(a, x, y, w, h)` from the window's inside, `w` or `h` 0 to
its edge, so the strip follows the window's size (`0, 0, 0, 40` is a
toolbar 40 pixels tall); `y` below 0 counts from the bottom, so `0, -20, 0,
0` is a status bar that stays at the bottom. Called after the first layout
it lays out again at once. Both calls return 1, and 0 from a version 10
library, where olui would still have the whole window: a program that
needs them checks. The buffer is then the strip's size, not the
window's. Pop-ups (a select's list, a menu, a tooltip) are drawn inside the
strip, so a strip one line tall has no room for them: open those in a
window of the program's own. olui takes a press only inside the strip and keeps it until the
button is let go, takes the hover off when the pointer leaves, and gives
`IDCMP_REFRESHWINDOW` and `IDCMP_NEWSIZE` back as `OLUI_AMIGA_NONE` after
drawing the strip, for the program's own refresh and layout.
`olui_amiga_keys(a, OLUI_AMIGA_KEYS_FOCUS)` gives olui the keys only while
one of its widgets has the focus; a press outside the strip takes the
focus away and draws the strip without it. Tab can't then bring the focus
back (the keys are the program's): a click does, or the program's
`olui_focus`. In a strip, IntuiTicks are always given back, for the
program too, and what a tick changed in the strip (a tooltip, a held
arrow button, the cursor) is drawn first, so the strip stays up to date
even in a program that draws only after `OLUI_AMIGA_USED`. A window opened without `IDCMP_VANILLAKEY`, to have every key
as a RAWKEY, works too: the glue maps the keys to characters with
keymap.library's `MapRawKey`.

## Desktops and drawers (0.4)

0.4 (in `gfopenlayout.library` 11) has what a desktop and its drawer
windows need, so that OpenBench's icons can be elements styled by CSS
rather than a widget of their own. `tests/test_desktop.c` tests it.

**Positions and layers.** `position: absolute` (or `fixed`) takes an
element out of the flow to its `left` and `top` from its parent's content
corner, as wide as its `width` or, with none, its content (a label's
text). `right` and `bottom` aren't used here: the styling doesn't know
the parent's size. `z-index` puts a positioned element (`relative` too)
in a layer among its parent's children, for drawing and for the pointer
alike: those below 0 first, then those in the flow, then those at 0 or
`auto`, then those above 0, each layer in the markup's order. Each
element is a layer of its own, so what it holds stays in its layer.

**The program's elements.** `olui_add(ui, parent, markup, err, len)`
makes markup into elements at the end of `parent`'s children (its
`<style>` blocks go to the window's sheet) and returns the first;
`olui_remove(ui, el)` takes one away with what it holds.
`olui_set_style(ui, el, decls)` gives it a new style attribute, and
`olui_set_place(ui, el, x, y)` puts a positioned element at a place in
pixels (a grid's cell, a drag's end) until `OLUI_PLACE_CSS` gives it back
to its CSS. `olui_hit(ui, x, y)` is the topmost element under a point,
layers counted; `olui_parent`, `olui_first_child` and `olui_next` walk the
elements.

```c
olui_el *icon = olui_add(ui, olui_find(ui, "desk"),
    "<div class=icon style='position:absolute;left:8px;top:40px'>"
    "<img image=3 width=48 height=48><span>Work</span></div>", err, sizeof err);
olui_set_place(ui, icon, 8, 120);           /* the grid's next cell */
```

**The look.** `opacity` sets every op's `alpha` (0 to 255), multiplied
down what the element holds, so a dragged icon's picture and label fade
together. `text-shadow` draws the text again under itself for each shadow
(four, a pixel each way, make an outline that reads on any backdrop).
`outline` is a box round the border box over what the element holds (a
selected icon's ring). `background-image: url(N)` is an `OLUI_DRAW_IMAGE`
op of the program's picture N over the element's background, in its
padding box, with its repeat and place for the drawer: a desktop's
backdrop is `window{background-image:url(1)}`. `olui_image_name(ui,
"backdrop", 1)` lets the CSS say `url(backdrop)`. The Amiga glue leaves out
what has `alpha` 0 and draws the rest whole: it doesn't blend yet.

**Selecting and dragging.** Four attributes say what an element does
with the pointer, so an icon needs no widget of its own:

| Attribute | What it does |
| --- | --- |
| `selectable` | A click selects it alone (`:selected`), Shift with a click adds it or takes it away; pressed when selected with others, it is selected alone when let go, unless it was dragged. A double-click is `OLUI_ACTIVATE` |
| `draggable` | Pressed and moved 3 pixels, it is dragged: `OLUI_DRAG_START`, `OLUI_DRAG_MOVE` as the pointer moves, `OLUI_DROP` when let go, `OLUI_DRAG_CANCEL` on Escape. A selected one takes the others selected in its scope with it (`olui_drag_item`). What is dragged is `:dragging`, drawn on top of everything, moved with the pointer (no layout while it moves), and the pointer finds what is under it |
| `droppable` | A drop target: the event's `item`, and `:drop-target` while something is over it |
| `marquee` | A scope (a desktop, a drawer's contents): a press on its space drags a band (the made element `band` under it, `band{...}`) that selects what it touches, Shift adding to what was selected; without Shift a click there selects nothing |

A selectable element's scope is the nearest `marquee` it is in, or its
parent. `OLUI_SELECTION` says a scope's selection changed (`el` the scope,
`item` the element clicked, `index` how many are selected). The events
have the pointer (`x`, `y`), the qualifiers held and, for a drag, how far
it has gone (`dx`, `dy`). Nothing moves by itself when dropped: the
program moves what was dragged, with `olui_set_place` by `dx` and `dy`, or
`olui_remove` and `olui_add` into another window. `olui_mouse_qual` is
`olui_mouse` with the qualifiers (the Amiga glue gives them);
`olui_set_selected`, `olui_is_selected`, `olui_next_selected` and
`olui_select_all` are the program's way to the selection.

```c
static void dropped(olui *ui, const olui_event *ev, void *data)
{
    olui_el *d;
    int i;
    for (i = 0; (d = olui_drag_item(ui, i)) != NULL; ++i) {
        olui_rect r, p;
        olui_box(ui, d, &r);
        olui_box(ui, olui_parent(d), &p);
        olui_set_place(ui, d, r.x - p.x + ev->dx, r.y - p.y + ev->dy);
    }
}
olui_on(ui, "desk", OLUI_DROP, dropped, NULL);
```

**Long lists.** `<vlist columns="200 60 120">` holds no elements: the
program says how many rows it has and gives each cell as it is drawn
(`olui_vlist_set(ui, el, rows, fn, data)`, `fn(data, row, column,
&image)`), so a drawer of 5,000 files costs only the rows that show. A row
is styled as the made element `row` under it (`row:selected`); a cell may
have a picture before its text. A click selects a row, Shift with a click
adds or takes away (`OLUI_SELECT`, `index` the row), a double-click or
Return is `OLUI_ACTIVATE`; the arrows, PageUp, PageDown, Home, End and the
wheel move and scroll. The selection is a bit a row
(`olui_vlist_selected`, `olui_vlist_select`, `olui_vlist_current`).

**Context menus.** `<menu id=m><option value=open>Open</option>...</menu>`
is never shown in the window. `olui_menu_open(ui, menu, x, y)` shows it at
a point as a select's pop-up list is shown, with the same keys and
pointer, and choosing an option sends `OLUI_ACTIVATE` on the menu. The
menu button (`OLUI_MOUSE_MENU`; on the Amiga the right button in a window
with `WFLG_RMBTRAP`) sends `OLUI_CONTEXT` for the element under the
pointer and opens the menu its `contextmenu` attribute (or that of one it
is in) names. The right button's release doesn't choose; a click does.

**Several windows.** `olui_new_sharing(ui)` is a new UI for another window
of the same program (a desktop's drawers) that shares `ui`'s stylesheet and
picture names, so the theme and the program's CSS are read once; CSS added
through either restyles both. `olui_hit` on the other window's UI finds a
drop target there.

**Backdrops.** A desktop's backdrop window is an ordinary window to the
glue: its root (`window{background-image:url(1)}`) draws the program's
picture in the window's box, the icons over it, and only what changed is
drawn again.

## Toolkits over the UI (0.5)

0.5 (in `gfopenlayout.library` 12) has two calls a toolkit that puts its
own widgets in olui windows needs (gfopenwx, the wxWidgets port):

- `olui_repaint(ui, el, part)`: `el` is drawn again at the next `olui_draw`,
  all of it (`part` NULL) or the part of it in `part`, in the window's
  pixels (cut to the element and to the box it scrolls in). For a picture the
  program draws (an `img`) that changed: nothing else is laid out or drawn.
- `olui_popup(ui)`: the pop-up open over the window (a select's list, a
  menu), or NULL. While there is one the program gives olui the pointer
  wherever it is, even over the program's own widgets, so the pop-up gets
  the click that closes it.

## GFOpenLayout 0.8

What the UI needed in GFOpenLayout itself, for any program:

- `OL_BOX_ROW`: a box's children side by side, as a CSS flex row;
- the box style's `width`, `height`, `grow`, `gap` and `align`;
- a box with a height shares what its children leave among those that grow.

`gfopenlayout.library` is 8.0 for these, with no new calls: a program sets
`ol_box_style.size` to `sizeof(ol_box_style)` and opens version 8
(`OPENLAYOUTLIB_ROW_VERSION`). `tests/test_rows.c` tests them.

From `gfopenlayout.library` 10 the UI itself is in the library: every
`olui_` call is an `OLUI_` one (`olui_set_text` is `OLUI_SetText`), and a
program written to this API links `libgfopenlayout_link.a` instead of
`libolui.a` to use it (README, "One library").

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

Host tests: `make test`, or `make test OGT_ROOT=<gfopengadtools>` with the
theme reader; `tests/test_ui.c` runs the widgets, the keyboard and the
events, and with `OLUI_THEME_FILE` set, a theme; `tests/test_ui_css.c` the
CSS GFCSS gives the UI. `tests/ui_render.py` draws
`test_ui`'s ops as a picture, to look at a window on the host.

Amiga: `OGT_ROOT=<gfopengadtools> SPECS=<SDK>/Lib/m68k-soft-m68040.specs
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
- Menu bars (Intuition's are the program's); a context menu's separators and submenus (`optgroup`
  gives a labelled part).
- A vlist's columns are fixed widths (no header, no dragging them), its rows one line, and a drag
  from it is the program's.
- Dragging between windows: the program's. The drag events have the pointer even outside the
  window (the Amiga sends it while the button is held), and `olui_hit` on the other window's `olui`
  finds the target there.
- A box that scrolls scrolls down only (no `overflow-x`), and its bar is not
  dragged.
- An `img` directly in a `row` or `column` keeps no place: put it in a line (a `div`, a `button`).
- `gfcss_*` on its own in the library: the UI and documents reach GFCSS inside it.
- The Amiga glue blending: it draws an op whose `alpha` is above 0 whole, and an `rgba()`
  background opaque. A text shadow's blur.
- `right`, `bottom` and `position: relative`'s offsets; a positioned element's containing block is
  its parent, positioned or not.
- Laying out only what changed: a change that can move things (text, items, a style that changes a
  size, an item opened or closed) lays the whole window out. A hover, press, focus, check or selection
  that changes only how widgets look is drawn again only where it changed, with no layout.
