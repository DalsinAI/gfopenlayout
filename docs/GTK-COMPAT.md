# GFOpenGTK: GTK on GFOpenLayout UI

GFOpenGTK (`gtk/`, version 0.1) is GTK's API on GFOpenLayout UI, so an
old Linux game or program whose windows are GTK (dialogs, menus, settings)
can be carried to AmigaOS 3.x by relinking it, not by writing its interface
again. Each GTK window is an olui window: its widgets are made into olui
markup when it is shown, laid out by GFOpenLayout, drawn in the
GFOpenGadTools theme, and olui's events come back as GTK's signals.

```c
#include <gtk/gtk.h>                    /* gtk/include: GFOpenGTK's, not the system's */

GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL), *b = gtk_button_new_with_label("Hello");
gtk_container_add(GTK_CONTAINER(win), b);
gtk_signal_connect(GTK_OBJECT(b), "clicked", GTK_SIGNAL_FUNC(hello), NULL);
gtk_widget_show_all(win);
gtk_main();
```

It came from LinBolo, WinBolo's Linux client (WinBolo 1.15,
`winbolo/src/gui/linux/`): its main window and its seventeen dialogs are all
GTK 1.2, written by Glade, so the game couldn't be ported without either a
new interface or this.

## What LinBolo needs

LinBolo's interface is 18,000 lines of C in 30 files; the dialogs are Glade's
code. Counted from its sources, the GTK calls by how often they are made:

| Calls | Times | What they are |
| --- | --- | --- |
| `gtk_widget_show`, `gtk_widget_ref`, `gtk_widget_unref`, `gtk_object_set_data_full` | 410-430 each | Glade's way: every widget referenced and hung on its window by name |
| `gtk_signal_connect` | 164 | `activate` 68 (menu items), `clicked` 46, `key_press_event` 18, `delete_event` 15, a few `show`, `destroy`, `select`, `focus_in/out_event`, `configure_event`, `expose_event`, `button_press_event`, `motion_notify_event` |
| `gtk_box_pack_start` / `_end`, `gtk_hbox_new`, `gtk_vbox_new` | 146 / 19, 39, 20 | Boxes |
| `gtk_label_new`, `gtk_label_set_text`, `gtk_label_set_justify`, `gtk_misc_set_alignment`, `gtk_misc_set_padding` | 145, 105, 50, 66, 20 | Labels, many changed while open (the game's status) |
| `gtk_widget_set_sensitive` | 136 | |
| `gtk_table_attach`, `gtk_table_new` | 132, 9 | Tables of labels and fields |
| `gtk_container_add`, `gtk_container_set_border_width` | 105, 27 | |
| `gtk_check_menu_item_*`, `gtk_radio_menu_item_*`, `gtk_menu_item_*`, `gtk_menu_new`, `gtk_menu_bar_new`, `gtk_widget_add_accelerator` | about 200 | The main window's menus, with ticks, radio items and keys |
| `gtk_button_new_with_label`, `gtk_check_button_new_with_label`, `gtk_radio_button_*`, `gtk_toggle_button_*` | 43, 13, 24, 57 | |
| `gtk_entry_new`, `_set_text`, `_get_text`, `_set_visibility`, `_new_with_max_length` | 26, 29, 15, 3, 1 | |
| `gtk_window_new`, `_set_title`, `_set_policy`, `_set_position`, `_set_modal`, `gtk_grab_add/remove`, `gtk_main`, `gtk_main_quit` | 17, 17, 17, 11, 9, 23, 12, 17 | Each dialog in a `gtk_main` of its own, ended by `gtk_main_quit` |
| `gtk_timeout_add/remove`, `g_main_iteration` | 13 | The game's own ticks |
| `gtk_list_*`, `gtk_frame_new`, `gtk_file_selection_*`, `gtk_dialog_new`, `gtk_pixmap_new` | 1-7 each | The game finder's list, the map chooser |
| `gtk_drawing_area_new` | 1 | The game's view, which SDL draws into (`SDL_WINDOWID`) |
| glib | small | `g_free`, `g_strdup_printf`, `g_strconcat`, `g_slist_*`, `g_get_home_dir`, `g_get_tmp_dir`; `g_module_*` for the AI "brains" |
| GDK | small | `gdk_keyval_name`, `gdk_keyval_to_upper` (the key setup), `gdk_threads_enter/leave`, an icon from XPM |

What it reads directly: `GTK_BIN(w)->child` (a button's or menu item's
label), `GTK_TOGGLE_BUTTON(w)->active`, `GTK_DIALOG(d)->vbox` and
`->action_area`, `widget->window` (to give SDL the X window).

So a GTK for LinBolo is mostly: boxes, tables, labels, buttons, check and
radio buttons, entries, menus with ticks, modal windows each with its own
`gtk_main`, signals, timeouts, and the drawing area SDL draws in.

## Decisions

- **GTK 1.2's API, with 2.x's names beside it.** LinBolo and its era are
  1.2: `gtk_signal_connect`, `GTK_SIGNAL_FUNC`, `gtk_window_set_policy`,
  `gtk_toggle_button_set_state`, `gtk_label_get`, `gtk_entry_get_text`
  returning `gchar *`. 2.x's spelling of the same calls costs only macros
  (`g_signal_connect`, `G_CALLBACK`, `g_signal_connect_swapped`,
  `G_OBJECT`, `g_object_set_data`, `gtk_window_set_resizable`,
  `gtk_widget_set_size_request`, `gtk_label_get_text`,
  `gtk_radio_button_get_group`), so both are there. `GTK_MAJOR_VERSION` is
  1, or 2 with `-DGTKC_GTK2`. GObject's type system, properties and
  `GtkBuilder` are not.
- **One struct for every widget.** `GTK_BOX(w)`, `GTK_LABEL(w)` and the
  rest are casts; the fields programs read (`parent`, `window`, `child`,
  `active`, `vbox`, `action_area`, `flags`, `ref_count`) are in it, and the
  rest is GFOpenGTK's own behind it. There is no type checking: a GTK program
  that works on Linux casts correctly already.
- **Made into markup, not into olui elements one by one.** GTK builds a
  tree before showing it, and Glade's code adds every widget before the
  window is shown. GFOpenGTK keeps the tree, and when the window is shown
  writes it as olui markup (`gtkc_window_markup` shows it) and loads it.
  After that, changes of state (a label's text, a tick, sensitive, hidden)
  go straight to the element with `olui_set_*`; changes of shape (a widget
  added or taken out, a box's spacing) build the window again, outside olui's
  handlers, with the entries' texts, the ticks and the focus kept.
- **glib is the part programs use.** Types, `g_malloc` (which ends the
  program when memory runs out, as glib does), strings, `GList`, `GSList`,
  messages, `g_get_home_dir` (PROGDIR: when there is no `$HOME`) and
  `g_get_tmp_dir` (T:), and the main loop's timeouts and idle functions.
  Not GHashTable, GString, GArray, GModule, GIOChannel or threads
  (`g_thread_init` and `gdk_threads_enter/leave` do nothing: one task drives
  the interface).
- **No floating point.** GFOpenLayout and the UI use none, so a program can
  be built soft float and run with or without an FPU. GTK takes alignments
  as `gfloat`; GFOpenGTK reads their bits with integers.
- **Here, in gfopenlayout, for now.** See the end.

## How GTK's widgets become olui's

| GTK | olui | Notes |
| --- | --- | --- |
| `GtkWindow`, `gtk_dialog_new` | the window: a `column` with the window's border as padding; a dialog's `vbox`, a separator and its `action_area` (a homogeneous `row`) | `set_title`, `set_default_size`/`set_usize`, `set_position` (centre, mouse), `set_modal`, `set_policy`/`set_resizable` (the size gadget) |
| `GtkVBox`, `GtkHBox` | `column`, `row` with `gap` for the spacing | `expand` (or homogeneous) is `flex:1`; `padding` is margins along the box; `pack_end` children go after a `spacer`, last packed first; `fill=FALSE` is taken as TRUE |
| `GtkTable` | a `column` of `row`s | Each column is as wide as its widest cell, or, when a cell in it has `GTK_EXPAND`, shares the room left with the other growing columns, so the columns line up; a cell over several columns is as wide as they are; a cell over several rows is in its top one; `yoptions` `GTK_EXPAND` makes its row grow |
| `GtkLabel`, `GtkMisc` | `label` | `xalign` is `text-align` (left below 0.25, right above 0.75); new lines are `<br>`; `misc_set_padding` is padding |
| `GtkButton` | `button` | Its label is a real `GtkLabel` (`GTK_BIN(b)->child`), the button's text |
| `GtkToggleButton` | `button` with `checked` (`button:checked`) | `toggled`, then `clicked` |
| `GtkCheckButton` | `checkbox` | |
| `GtkRadioButton` | `radio` with a `name` per group | The group is a `GSList` every member has, as GTK's; one turned on turns the other off, each sending `toggled` |
| `GtkEntry` | `input` | `type=password` for `set_visibility(FALSE)`, `maxlength`, `readonly`; the text is kept in the widget as it is typed |
| `GtkFrame` | `fieldset` and `legend` | |
| `GtkHSeparator`, `GtkVSeparator` | `hr`, a bordered `div` | |
| `GtkMenuBar`, `GtkMenu`, `GtkMenuItem`, `GtkCheckMenuItem`, `GtkRadioMenuItem` | *phase 2*: Intuition's own menus (GadTools `NewMenu`) on the window, `activate` from `IDCMP_MENUPICK`, ticks as `CHECKIT`/`MENUTOGGLE`, radio items as `MUTUALEXCLUDE`, accelerators as the Amiga key's | olui has no menu bar ("Not yet" in GFOpenLayout-UI.md), and Amiga programs' menus are Intuition's anyway |
| `GtkCombo`, `GtkOptionMenu` | *phase 2*: `select` | |
| `GtkCList`, `GtkList` | *phase 2*: `vlist` with `columns`, or `list`/`item` | The game finder's list |
| `GtkNotebook` | *phase 2*: `tabs`/`tab` | |
| `GtkSpinButton`, `GtkHScale`, `GtkProgressBar`, `GtkText` | *phase 2*: `spinner`, `slider`, `progress`, `textarea` | |
| `GtkFileSelection` | *phase 3*: asl.library's file requester, in a modal loop as GTK's dialog is | |
| `GtkDrawingArea`, GDK drawing, `GtkPixmap` | *phase 3*: a `div` given to `olui_amiga_own`, the program drawing in the window's RastPort; `expose_event`, `configure_event`, the mouse's events | And `widget->window` for a program (as LinBolo) that gives it to SDL: the Amiga's SDL takes a window |

## Signals

`gtk_signal_connect(object, "name", fn, data)` (and `_after`, `_object`,
`_object_after`; 2.x's `g_signal_connect`, `_after`, `_swapped`) keeps a
handler per widget; `-` and `_` in names are the same. Handlers are called
in the order connected, `_after` ones last. Signals named `..._event` are
called as `gboolean fn(GtkWidget *, GdkEvent *, gpointer)`, and TRUE stops
them; the rest as `void fn(GtkWidget *, gpointer)`. `gtk_signal_disconnect`,
`_handler_block`/`_unblock`, `gtk_signal_emit_by_name` (signals with no
arguments) and `gtk_signal_emit_stop_by_name` work, disconnecting from
inside a handler too.

One `olui_on(ui, NULL, 0, ...)` per window takes olui's events and sends:

| olui | GTK |
| --- | --- |
| `OLUI_CLICK` on a button | `clicked` (a toggle button: `toggled`, then `clicked`) |
| `OLUI_CHANGE` on a checkbox or radio | `toggled` (on each one that changed), then `clicked` |
| `OLUI_CHANGE` on an input | the entry's text kept; `changed` |
| `OLUI_SUBMIT` on an input | `activate` |
| a key | `key_press_event` on the window, then on the widget with the keyboard (as GTK sends it), with `keyval` as GDK's keysyms; only when no handler returns TRUE does olui have it |
| the close gadget | `delete_event`; destroyed unless a handler returns TRUE |
| destroyed | `destroy`, what it holds destroyed, its data's destroy functions called (Glade's `gtk_widget_unref`s), its window closed |

Widgets are referenced as GTK's are: made floating, sunk by the container
they go into, `gtk_widget_ref`/`unref` and `set_data_full` as Glade uses
them, freed when the last reference goes; a window is held by the list of
windows until destroyed. A widget destroyed from inside one of olui's
handlers (a dialog's OK button destroying the dialog) has its window closed
once olui is out of its call.

## The main loop

`gtk_main` runs rounds of: the timeouts due, the windows' input, the
windows built again and drawn as handlers asked, and, when nothing else
happened, the idle functions; then it waits for the next timeout or input.
`gtk_main_quit` ends the innermost `gtk_main`, so a dialog run in a
`gtk_main` of its own from a handler (LinBolo's every dialog) works: while it
runs, the window whose handler started it is left alone (its messages wait
in its port) and the others go on. `gtk_grab_add` (or a modal window) keeps
the other windows' mouse and keys from them (a click there beeps).
`gtk_main_iteration`, `gtk_events_pending`, glib's `g_main_iteration`,
`g_main_pending`, `g_timeout_add`, `g_idle_add`, `g_source_remove` and
1.2's `gtk_timeout_add`, `gtk_idle_add` are all the one loop.

**The Amiga** (`gtk/gtkc_amiga.c`): each window is an Intuition window on
the default public screen with GFOpenLayout UI's glue drawing it (smart
refresh, the theme GFOpenPrefs Look chose), opened at the size its contents
want (measured with the screen's font, then the window made that size). The
loop `Wait()`s on every window's port, timer.device for the next timeout and
Ctrl-C (`gtk_main_quit`). The clock is `DateStamp`'s (20 ms).

**The host** (`gtk/gtkc_host.c`): no screen; each window an olui laid out at
its size and drawn to nothing, for `tests/test_gtk.c`. Time is the loop's
own: it moves on only while the loop waits, by as long as it waits, so a
timeout of a second runs at once and the test is the same each time.

GFOpenGTK's own calls (`gtk/include/gtk/gtkc.h`): `gtkc_add_css` (CSS for
every window, after the theme), `gtkc_window_olui` and `gtkc_widget_el`
(olui under a window and a widget), `gtkc_window_markup`,
`gtkc_window_size`, and input as the user gives it through the same path,
for tests and programs that drive a window: `gtkc_key`, `gtkc_click`,
`gtkc_type`, `gtkc_window_close`, `gtkc_flush`, `gtkc_now`.

## Not done

- Menus, combo and option menus, lists, notebooks, spin buttons, scales,
  progress bars, text views: phase 2.
- Drawing areas and GDK's drawing, pixmaps and XPM, the file selection,
  colour and font dialogs: phase 3.
- `fill=FALSE` in a box (the child fills its share), a label's `yalign`, a
  table's cell over several rows (in its top row), a table's columns sized
  to their cells when the window is narrower than they want.
- A window's width is worked out from its widgets' texts with about the
  screen font's width a character before it is open, and its height from
  olui's layout; GTK's own size requests and allocations
  (`gtk_widget_size_request`, `widget->allocation`) are not there.
- Accelerators, accel groups, `gtk_widget_add_accelerator`: phase 2 with
  the menus.
- Styles, rc files and themes of GTK's: the look is the GFOpenGadTools
  theme's, and a program's CSS through `gtkc_add_css`.
- Selections and the clipboard between programs; drag and drop.
- GObject, `GtkType`, `GtkArg`, `gtk_object_get/set` by argument name,
  signals of the program's own (`gtk_signal_new`).
- glib's hash tables, strings, arrays, modules, I/O channels and threads.

## Phases

1. **This: the frame.** `gtk_init`, windows and dialogs, boxes, tables,
   labels, buttons, toggle, check and radio buttons, entries, frames,
   separators, signals, object data and references as Glade uses them,
   `gtk_main` nested, grabs, timeouts and idle functions, glib's lists and
   strings, GDK's key names. Host tests (`tests/test_gtk.c`, 90 checks,
   clean under AddressSanitizer), and the 68k build (`build-gtk-amiga.sh`:
   `libgfopengtk.a`, `GtkHello`, `GtkTest`).
2. **The rest of LinBolo's dialogs.** Menu bars and menus on Intuition's
   menus (`activate`, check and radio items, the Amiga key's accelerators),
   `GtkCList`/`GtkList` on `vlist`/`list`, option menus and combos on
   `select`, notebooks on `tabs`, spin buttons, scales, progress bars. Then
   LinBolo's 17 dialogs built against it on the host, each looked at with
   `tests/ui_render.py`.
3. **The game's window.** `GtkDrawingArea` on `olui_amiga_own` with its
   events, `widget->window` handed to the Amiga's SDL, GDK's pixmaps from
   XPM for the icons, the file selection on asl.library. LinBolo running on
   the 68040 in the lab.
4. **Others.** The next GTK program (a GNOME 1 game, Sylpheed, gFTP) finds
   what is missing, and that is done next; GLib's hash table and GString
   come with the first that needs them.

## Size

On the 68040 (`-O2`, measured 7 November 2026): GFOpenGTK's code is 31 KB
(its core 24.5 KB, glib 2.9 KB, the Intuition loop 3.5 KB), over the UI
and GFOpenLayout; `GtkHello` is 297 KB with everything linked in, stripped.
A widget is under 300 bytes in GFOpenGTK, besides its element in olui.

## Building

Host: `make test` builds and runs `build/test_gtk` with the rest. A
program includes `<gtk/gtk.h>` with `-Igtk/include` and links `gtk/gtk.c`,
`gtk/glib.c`, a backend and the UI.

Amiga: `SPECS=<SDK>/Lib/m68k-soft-m68040.specs ./build-gtk-amiga.sh` builds
`build-amiga/gtk/libgfopengtk.a` (GFOpenLayout, GFCSS, the UI, its Intuition
glue, GFOpenGTK and the Intuition loop), `GtkHello` and `GtkTest` with
GFOpenAmigaGCC 16.2 (`TOOLCHAIN`, `NDK` and `OGT_ROOT`, for the theme,
have defaults). A program is built with `-Igtk/include -Igtk -Iinclude` and
links `libgfopengtk.a`. Neither has been run on an Amiga yet.

## Where it lives

In gfopenlayout for now, beside the UI it is made of: it changes with the
UI's markup and calls, and the tests run with the UI's. When phase 2 makes it
big enough to have users of its own (LinBolo, then others), it moves to a
repository of its own, `gfopengtk`, linking gfopenlayout's `libolui.a` (or
`gfopenlayout.library` through `libgfopenlayout_link.a`), as gfopenwx, the
wxWidgets port, does.
