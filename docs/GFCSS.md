# GFCSS

GFCSS is the CSS engine GFOpenLayout UI styles programs' windows with, and
that GFOpenBrowser Lite will style web pages with. It reads stylesheets,
matches their selectors against the caller's elements, runs the cascade
and hands back each element's computed style, in integers. It turns a
computed style into GFOpenLayout's `ol_style` and `ol_box_style`, and
GFOpenLayout's documents can be styled through it by themselves.

It is part of `libopenlayout.a` (`include/gfopenlayout_css.h`,
`src/gfcss_*.c`), plain C99 like the rest, and uses no floating point
anywhere, so it runs on a 68040 with or without an FPU. A decimal in CSS
(`1.5em`, `opacity: .5`, `37.5%`) becomes a whole number of thousandths
as it is read, and stays one.

```c
gfcss_engine *e = gfcss_new();
gfcss_sheet *ua = gfcss_sheet_new(e, GFCSS_ORIGIN_UA);
gfcss_sheet *page = gfcss_sheet_new(e, GFCSS_ORIGIN_AUTHOR);
gfcss_sheet_parse(ua, ua_css, strlen(ua_css));
gfcss_sheet_parse(page, css, strlen(css));
gfcss_set_media(e, GFCSS_MEDIA_SCREEN, OL_CSSPX(800), OL_CSSPX(600));

/* each element after its parent, the parent's computed style kept */
gfcss_computed_init(&cs);
gfcss_compute(e, &my_ops, my_data, el, GFCSS_PSEUDO_NONE, parent_cs, &cs);
gfcss_to_ol(&cs, containing_width, &style, &box);
```

## The element interface

GFCSS has no tree of its own. The caller describes its elements through
`gfcss_element_ops`, a few functions on its own element pointers:

| Function | Answers |
| --- | --- |
| `info(userdata, el, &info)` | Its tag, id, classes (separated by spaces), style attribute, states, its place among its parent's elements and how many there are (0 when not known), and whether it is empty |
| `parent(userdata, el)` | Its parent element, NULL at the root |
| `prev(userdata, el)` | The element before it under the same parent, or NULL |
| `next(userdata, el)` | The one after it (may be NULL as a function: the "last" selectors then use `info`'s count, and the `-of-type` ones that look forward match nothing) |
| `attr(userdata, el, name)` | An attribute's value, NULL when it hasn't one; it must answer for `id` and `class` too. May be NULL as a function when no selector asks for attributes |
| `index(userdata, el)` | Its place among its parent's elements, from 1, when `info` said 0: asked only when a selector needs it. May be NULL (`prev` is walked) |

The strings need only last until the call returns. Tags are matched
without regard to case; ids, classes and attribute values with it (but
for `[a=v i]`).

States are bits in `info.state`: `GFCSS_STATE_HOVER`, `ACTIVE`, `FOCUS`,
`CHECKED`, `DISABLED`, `ENABLED`, `LINK` and `VISITED`. As in a browser
the caller sets `ENABLED` on form controls that aren't disabled and
`LINK` on links not visited. `gfcss_add_state(e, "selected",
GFCSS_STATE_USER(0))` adds a pseudo-class of the caller's own (up to 16).

Nothing is allocated for an element while it is styled: the engine's
working space is made once and kept.

## Stylesheets

`gfcss_sheet_new(e, origin)` adds an empty sheet at the end of the
cascade; `gfcss_sheet_parse` adds CSS to it (the text is copied, and
calling it again goes on after what was there). Sheets belong to the
engine and go with `gfcss_free`, or alone with `gfcss_sheet_free`.

Reading follows CSS Syntax 3: comments, strings with their escapes,
`\` escapes in names (`.\31 23`, `#ma\in`), numbers with decimals and
exponents, `url()` with and without quotes, and blocks that nest. What
can't be read is skipped as CSS says: a declaration it doesn't know or
whose value is wrong, up to its `;`; a whole rule when one selector in
its list is one it doesn't know; an at-rule it doesn't know, with its
block. A block left open ends at the end of the text; a string left open
at the end of its line makes its declaration a bad one. Very deep nesting is counted, not followed,
so no input runs the stack out.

| At-rule | What GFCSS does |
| --- | --- |
| `@media` | Its rules count while its query list holds (below); `@media` inside `@media` too |
| `@import` | Collected, with its media list, for the caller to fetch (only before the sheet's first rule, as CSS says) |
| `@supports` | Evaluated once, as it is read: `(property: value)` holds when GFCSS reads that declaration; `not`, `and`, `or` and brackets; `selector()` doesn't hold |
| `@charset`, `@namespace` | Read and left |
| `@font-face`, `@keyframes`, `@page` and any other | Skipped with their block |

`@media` takes a comma list of queries, each `[not|only] type [and
(feature)]...` or `(feature) [and (feature)]...`. Types are `screen`,
`print` and `all` (any other is false); features `width`, `height`,
`min-width`, `max-width`, `min-height`, `max-height` (in px, pt, pc, in,
cm, mm, or em and rem as 16 px) and `orientation`. A query with a feature
it doesn't know is false, `not` or no `not`. Queries are evaluated
against `gfcss_set_media(e, media, width, height)`; a width or height of
0 is not known, and a feature that needs it is false. Changing the media
needs no reparsing.

`@import`: `gfcss_sheet_import_count`, `gfcss_sheet_import_url` (the URL
unescaped), `gfcss_sheet_import_applies` (its media holds now), and
`gfcss_sheet_import(sheet, i)`, a sheet standing in the import's place in
the cascade (before the importing sheet's own rules) to parse the fetched
text into.

## Selectors

| | |
| --- | --- |
| Simple | `*`, `p`, `.class`, `#id`, `[a]`, `[a=v]`, `[a~=v]`, `[a\|=v]`, `[a^=v]`, `[a$=v]`, `[a*=v]`, each with ` i` (and ` s`) |
| Combinators | descendant (a space), `>`, `+`, `~` |
| Pseudo-classes | `:first-child`, `:last-child`, `:only-child`, `:nth-child(an+b)`, `:nth-last-child()`, `:first-of-type`, `:last-of-type`, `:only-of-type`, `:nth-of-type()`, `:nth-last-of-type()` (with `odd`, `even`, `-n+3` and the rest), `:not(a, b)` of compound selectors, `:root`, `:empty`, `:link`, `:visited`, `:hover`, `:active`, `:focus`, `:checked`, `:disabled`, `:enabled`, and the caller's |
| Pseudo-elements | `::before`, `::after`, `::first-line`, `::first-letter` (and the one-colon spellings); last in a selector |
| Lists | `a, b, c` |

Specificity is CSS's: ids, then classes, attributes and pseudo-classes,
then types and pseudo-elements, each counted to 255; `:not()` counts as
its most specific argument.

The rules are indexed by their last compound's id, else one of its
classes, else its type; an element is matched only against the rules
filed under its id, each of its classes, its type, and the few filed
under none. A selector is matched from its right, and a descendant or
`~` combinator tries each ancestor or earlier sibling in turn.

## The cascade

Each property's winning declaration is kept as the rules are matched, by:

1. origin and importance, lowest first: user agent, user, author, the
   style attribute, author `!important`, style attribute `!important`,
   user `!important`, user agent `!important`;
2. specificity;
3. order: sheets in the cascade's order, an import's rules before its
   sheet's, rules in order, declarations in order.

The style attribute (`info.style`) is read as each element is styled.
Then the inherited properties come from the parent (`color`, `font-*`,
`line-height`, `text-align`, `text-indent`, `text-transform`,
`white-space`, `visibility`, `list-style-*`, `letter-spacing`,
`word-spacing`, `cursor`, `border-collapse`, `border-spacing`,
`caption-side`), the rest start from CSS's initial values, and
`inherit`, `initial` and `unset` (and `revert`, as `unset`) work on every
property and shorthand. The root element's font size is what `rem` is of.

`::before` and `::after` are styled by `gfcss_compute` with the
pseudo-element and the element's own computed style as the parent. It
returns 0 when no rule makes one, or `content` isn't a string, or it is
`display: none`.

## Values

| | |
| --- | --- |
| Lengths | `px`, `pt`, `pc`, `in`, `cm`, `mm`, `q` (to GFOpenLayout units exactly, a pixel being 60), `em`, `rem`, `ex` and `ch` (half an em: there are no font metrics here), `vw`, `vh`, `vmin`, `vmax`, `%`, and a bare `0` |
| `calc()` | `+`, `-` (with spaces round them, as CSS wants), `*` and `/` by a number, brackets and nested `calc()`, of lengths and percentages: `calc(100% - 2em)` keeps its percentage and its length apart until the layout knows what it is of |
| Colours | The 148 named colours, `transparent`, `currentColor`, `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`, `rgb()` and `rgba()` (commas or spaces, numbers or percentages, `/ alpha`), `hsl()` and `hsla()` (degrees, `turn`, `grad`, `rad`) worked out in integers |
| Numbers | thousandths, saturated at ±2,000,000 rather than wrapping round |

A unit is needed but on 0, as CSS says.

## Properties

| Property | Values kept |
| --- | --- |
| `display` | `block`, `inline`, `inline-block`, `none`, `list-item`, `flex`, `inline-flex`, `table`, `inline-table`, `table-row`, `table-cell`, `table-header-group`, `table-row-group`, `table-footer-group`, `table-column`, `table-column-group`, `table-caption`, `contents`, `flow-root` (as block); `grid` as block, `inline-grid` as inline-block |
| `position`, `float`, `clear`, `z-index` | kept for the caller; `gfcss_to_ol` makes an `absolute` or `fixed` box a GFOpenLayout frame (1.1); floats aren't laid out |
| `top`, `right`, `bottom`, `left`, `inset` (1.1) | lengths, percentages, `auto` |
| `box-sizing` | `content-box`, `border-box` |
| `width`, `height`, `min-*`, `max-*` | a length, a percentage, `auto`, `none` |
| `margin`, `padding` and their sides | lengths, percentages, `auto` (margins) |
| `border`, `border-top` and the other sides, `border-width`, `border-style`, `border-color`, and each side's longhands | widths (`thin` 1px, `medium` 3px, `thick` 5px), the ten styles, colours; a side with style `none` or `hidden` is 0 wide |
| `border-radius` and its corners | kept (the horizontal radius of an ellipse) |
| `outline` and its longhands | kept |
| `color`, `background-color` | colours |
| `background` | a colour, the first layer's `url()` or image function (a gradient is kept as written for the caller), repeat and position; size, attachment and boxes are read and left |
| `background-image`, `background-repeat`, `background-position` | |
| `opacity` | a number or a percentage, in thousandths |
| `font-family` | the first family's name, and a generic: the first family's own if it is one or a well-known one (`Courier New` is monospace, `Times` serif, `Arial` sans-serif...), else the first generic in the list |
| `font-size` | `xx-small` to `xxx-large` (9 to 48 px, medium 16), `smaller`, `larger`, lengths, percentages |
| `font-weight` | 1 to 1000, `normal`, `bold`, `bolder`, `lighter` |
| `font-style`, `font-variant` | `normal`, `italic`, `oblique`; `small-caps` |
| `font` | style, variant, weight and stretch in any order, size, `/` line height, family |
| `line-height` | `normal`, a number (kept as one, so it inherits as one), a length, a percentage |
| `text-align` | `start`, `end`, `left`, `right`, `center`, `justify` |
| `text-indent` | a length or percentage |
| `text-decoration`, `text-decoration-line` | `underline`, `line-through`, `overline`, `none`; the shorthand's colour, style and thickness are read and left |
| `text-transform`, `white-space` | all their keywords (`break-spaces` as `pre-wrap`) |
| `vertical-align` | the keywords, a length or a percentage |
| `list-style` and its longhands | the usual counter styles (others as `decimal`), `inside`, `outside`, an image |
| `letter-spacing`, `word-spacing` | `normal` (0) or a length |
| `text-shadow` (1.1) | `none`, or shadows by commas, each two or three lengths (`px` and the other absolute units, `em`, `rem`, `ex`, `ch`) and a colour before or after them; the first four kept, resolved (`gfcss_shadow`: across, down, blur, colour, `currentColor` worked out), inherited |
| `cursor` | the keywords, after any `url()`s |
| `overflow`, `overflow-x`, `overflow-y` | `visible`, `hidden`, `scroll`, `auto`, `clip` |
| `flex`, `flex-grow`, `flex-shrink`, `flex-basis`, `flex-direction`, `flex-wrap`, `flex-flow` | as CSS reads them (`flex: 2` is 2 1 0) |
| `gap`, `row-gap`, `column-gap` (and `grid-` spellings) | `normal` or a length |
| `align-items`, `justify-content` | the flex keywords |
| `border-collapse`, `border-spacing`, `caption-side` | |
| `content` | `normal`, `none`, a string |

## The computed style

`gfcss_computed` has a `size` first, so a later GFCSS can add fields at
its end: `gfcss_computed_init` sets it, and `gfcss_compute` writes no more
than `size` bytes when it is smaller. Everything in it is an integer:

- a length that depends on the layout (widths, margins, padding, gaps,
  `text-indent`, `flex-basis`, `border-radius`) is a `gfcss_length`:
  `units`, plus `pct` thousandths of a percent of what it is measured
  against, or `auto`, `none`, `normal` or `content`; `gfcss_resolve(len,
  of)` works it out;
- what doesn't depend on it is resolved: `font_size` and border widths in
  units, `line_height` as a number in thousandths or a length;
- colours are `AARRGGBB`, `currentColor` resolved;
- text from the sheet (a `url()`, an image function, a font's name,
  `content`'s string) is a `gfcss_text`: a stretch of the sheet's own
  text, as written, which `gfcss_text_copy` unescapes into UTF-8;
- `set` says which properties a declaration gave the element
  (`gfcss_is_set(cs, GFCSS_P_COLOR)`), for a caller that has defaults of
  its own.

`gfcss_inherit` makes an anonymous box's style (a run of text in an
element). `gfcss_apply_declarations` applies a declaration list on top of
a computed style, as a style attribute would be.

## GFOpenLayout's styles

`gfcss_to_ol(cs, containing_width, &style, &box)` fills an `ol_style` and
an `ol_box_style` from their init functions' values and `cs`.
Percentages are of `containing_width`. `display` becomes block, inline or
none (a flex container is a block with `OL_BOX_ROW` when its direction is
a row, and its `justify-content` is put in `text_align`, where
GFOpenLayout puts a row's children, or its space modes in
`OL_BOX_SPACE_BETWEEN`, `OL_BOX_SPACE_AROUND` and `OL_BOX_SPACE_EVENLY`); bold is a weight of 600 or more;
`OL_TEXT_MONO` is the monospace generic; `line-height: normal` is 1.2
times the font size. Widths are border-box widths, as GFOpenLayout's
`box.width` is: a content-box width has its padding and borders added.

With a GFOpenLayout that defines `OL_HAS_WEB_BOXES` (0.9), each side's
border goes in `border_widths` and `border_argbs` with
`OL_BOX_SIDE_BORDERS`, `inline-block` is `OL_DISPLAY_INLINE_BLOCK`, a
percentage width or maximum width on its own goes in `width_permille` or
`max_width_permille` for the layout to work out, `min_width` and
`max_width` are set, `vertical-align` is `valign`, and `margin: auto` is
`OL_BOX_AUTO_LEFT` and `OL_BOX_AUTO_RIGHT`. Without it, the widest side's
border is the box's border (on the sides that have one), and a box
narrower than its containing block with both side margins `auto` is
centred by its margins here.

## The caller's own properties

`gfcss_add_property(e, "accent-color", GFCSS_INHERITED, initial, parse,
userdata)` adds a property GFCSS doesn't know (up to `GFCSS_MAX_EXT`, 8).
`parse` gets the declaration's value as written and turns it into a
32-bit value, or refuses it (the declaration is then left out, as any
bad declaration is). The cascade, `inherit`, `initial` and `unset` treat
it as any other; its value is in `cs.ext[slot]`. Register properties and
pseudo-classes before parsing the sheets that use them.

## GFOpenLayout's documents

GFOpenLayout's own documents can be styled through GFCSS, with no tree
of the caller's (`gfopenlayout.h`):

```c
ol_node *p = ol_node_append(doc, body, OL_ROLE_PARAGRAPH, NULL);
ol_node_set_tag(p, "p");
ol_node_set_attr(p, "class", "intro");
ol_node_set_css_state(link, GFCSS_STATE_LINK | GFCSS_STATE_HOVER);
ol_document_add_stylesheet(doc, ua_css, strlen(ua_css), OL_ORIGIN_USER_AGENT);
ol_document_add_stylesheet(doc, css, strlen(css), OL_ORIGIN_AUTHOR);
ol_document_set_viewport(doc, OL_CSSPX(800), OL_CSSPX(600), GFCSS_MEDIA_SCREEN);
ol_layout(doc, OL_CSSPX(800), measure, NULL);       /* styled first */
```

A node with a tag is an element; `id`, `class` and `style` are ordinary
attributes. `ol_document_apply_styles(doc, width)` sets every element's
`ol_style` and `ol_box_style` through `gfcss_to_ol`, keeping what CSS
doesn't say (a font number, page breaks, frames, columns spanned).
`ol_layout` and `ol_layout_pages` do it first by themselves when the
document has stylesheets and a sheet, a tag, an attribute, a state, the
viewport, the number of nodes or the width has changed since. Along the
way:

- a text node with no tag in an element takes the element's inherited
  style, and the text decoration its element and their ancestors pass
  on;
- other nodes with no tag keep the styles set on them (a document with
  no stylesheets is laid out as it always was); elements under them
  inherit from the nearest element above;
- a flex container's children are blocks, as CSS says;
- `::before` and `::after` with a string `content` are text nodes made
  first and last in their element (`ol_node_is_generated`), kept and
  changed by the next styling;
- percentages are of the containing block's width as worked out on the
  way down (its width or the width it is given, less its margins,
  padding and borders).

The sheets must say how elements are shown: CSS's initial `display` is
`inline`, so a document needs a user-agent sheet (`div, p { display:
block }` and so on). `ol_document_import_count` and
`ol_document_import_url` list the sheets' `@import`s;
`ol_document_add_import(doc, i, css, length)` gives one its text. Until
`ol_document_set_viewport` is called, `@media` sees the width laid out
at and no height. `ol_node_first_child` and `ol_node_next_sibling` walk
the tree.

`gfopenlayout.library` doesn't have these calls yet: it will when the
library takes GFCSS in.

## GFOpenLayout UI

GFOpenLayout UI's `olui_sheet` is a GFCSS engine with one author sheet.
Its own properties (`accent-color`, `mark-background`,
`mark-border-color`, `placeholder-color`) are the caller's properties
above, `:selected` and `:open` its pseudo-classes, and a
`linear-gradient()` background comes from `background_image`. The UI
keeps its own defaults where they differ from CSS's: see
[GFOpenLayout-UI.md](GFOpenLayout-UI.md).

## Speed and size

On the host (a 2.1 GHz Xeon, `-O2`), `tests/test_css.c` parses 2,253
rules (ids, classes, compounds with `>` and descendants, `:nth-child`,
attribute selectors, `@media`, `::before`) in 1 to 2 ms and styles 2,000
elements against them in about 17 ms. GFOpenLayout UI styles and lays
out a window of 401 widgets from the start in about 270 µs
(`tests/test_ui_css.c`), where its own cascade took about 365 µs.

Code, x86-64 `-Os`: about 60 KB of code and 8 KB of tables (most of it
the values: keywords and the named colours) for the engine, 6 KB for
GFOpenLayout's documents. A rule's selector is 16 bytes, each compound 8,
each simple selector 20, each declaration (a longhand) 12. The 68040
hasn't been measured yet.

## Limits

- No namespaces (`ns|p`), `:is()`, `:where()`, `:has()`, `:lang()`,
  `:dir()`, `:target`, `:focus-within`, `:focus-visible`, `:any-link`,
  or `:nth-child(an+b of S)`: a rule using one is left out. `:not()`
  takes compound selectors (no combinators), nested four deep.
- At most 16 compounds in a selector, 24 simple selectors in a compound,
  8 `:not()`s in a selector, 48 tokens in a declaration's value (a
  function GFCSS doesn't read inside, such as a gradient, is one).
- No custom properties (`--x`, `var()`), `min()`, `max()`, `clamp()`,
  `attr()`, `counter()`, `@layer`, `@container`, `@scope`, media
  queries level 4's ranges (`width >= 600px`) or features other than
  those above.
- `calc()` is for lengths and `font-size`; a property that takes a number
  (`opacity`, `flex-grow`) takes no `calc()`.
- `ex` and `ch` are half an em; `em` in a media query is 16 px.
- No system colours, `hwb()`, `lab()`, `lch()`, `color()`.
- `content` keeps its first string; `quotes`, `counter-*` aren't read.
- `::first-line` and `::first-letter` are matched and computed: applying
  them to the text is the caller's.
- `gfcss_to_ol` places an absolute or fixed box at its `left` and `top`
  (or a box with a width at `right` from the containing width), as a
  frame from its containing block's content corner; it is as wide as its
  width, what `left` and `right` leave, or its content
  (`OL_BOX_FIT_CONTENT`). `bottom` isn't used, nor `position: relative`'s
  offsets, and a frame's containing block is its parent, positioned or not.
- `gfcss_to_ol` leaves a percentage height out (as `auto`).
- An engine works for one caller at a time: `gfcss_compute` uses the
  engine's working space.
- GFOpenLayout's documents list the `@import`s of their own sheets, not
  those of the imported sheets (GFCSS does).

## Tests

`make test` runs `tests/test_css.c`: reading and its mistakes (and 3,000
random sheets, and very deep nesting), every selector and combinator on a
small tree, specificity, origins and `!important`, inheritance and the
keywords, `@media`, `@import`, `@supports`, each unit and colour form,
the shorthands, the caller's properties and pseudo-classes, `::before`,
`gfcss_to_ol` (with and without web boxes), a smaller `gfcss_computed`, a
GFOpenLayout document styled and laid out, and the 2,000 rules over
2,000 elements. `tests/test_ui_css.c` is GFOpenLayout UI through GFCSS.
Both pass under `-fsanitize=address,undefined`. On the Amiga,
`build-amiga.sh` builds `LayoutCSSTest` and `build-ui-amiga.sh`
`OLUICSSTest`.
