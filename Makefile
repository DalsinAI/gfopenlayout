CC ?= cc
AR ?= ar
CFLAGS ?= -O2
WARN = -std=c99 -Wall -Wextra -Werror -pedantic
BUILD = build

.PHONY: all test clean probe_oi

# GFOpenLayout UI: with OGT_ROOT (GFOpenGadTools), its theme reader is built in
OGT_ROOT ?=
ifneq ($(OGT_ROOT),)
UI_THEME = ui/olui_theme.c $(OGT_ROOT)/lib/ogt_theme.c
UI_INC = -I$(OGT_ROOT)/lib
else
UI_THEME = ui/olui_theme_none.c
UI_INC =
endif
UI_SRC = ui/olui.c ui/olui_css.c ui/olui_markup.c $(UI_THEME)

# GFCSS, the CSS engine, and GFOpenLayout's documents styled through it
CSS_SRC = src/gfcss_parse.c src/gfcss_values.c src/gfcss_select.c src/openlayout_css.c
CSS_OBJ = $(CSS_SRC:src/%.c=$(BUILD)/%.o)
CSS_HDR = include/gfopenlayout.h include/gfopenlayout_css.h src/gfcss_internal.h src/openlayout_css.h

# GFOpenGTK: GTK 1.2 / 2.x's API on the UI (gtk/), with the host's window-less backend
GTK_SRC = gtk/gtk.c gtk/glib.c gtk/gtkc_host.c
GTK_HDR = gtk/include/glib.h gtk/include/gtk/gtk.h gtk/include/gtk/gtkc.h gtk/include/gdk/gdk.h \
          gtk/include/gdk/gdkkeysyms.h gtk/gtkc_internal.h

all: $(BUILD)/libopenlayout.a $(BUILD)/test_layout $(BUILD)/test_document $(BUILD)/test_rows $(BUILD)/test_grid $(BUILD)/test_samesize $(BUILD)/test_web $(BUILD)/test_ui \
	 $(BUILD)/test_css $(BUILD)/test_ui_css $(BUILD)/test_desktop $(BUILD)/test_gtk $(BUILD)/test_draw $(BUILD)/test_about $(BUILD)/test_core_api $(BUILD)/test_ogpu_accel

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/openlayout.o: src/openlayout.c include/gfopenlayout.h src/openlayout_css.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -DOL_CHECK_SORT -Iinclude -c src/openlayout.c -o $@

$(BUILD)/%.o: src/%.c $(CSS_HDR) | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -c $< -o $@

$(BUILD)/libopenlayout.a: $(BUILD)/openlayout.o $(CSS_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/test_layout: tests/test_layout.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_layout.c $(BUILD)/libopenlayout.a -o $@

$(BUILD)/test_document: tests/test_document.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_document.c $(BUILD)/libopenlayout.a -o $@

$(BUILD)/test_rows: tests/test_rows.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_rows.c $(BUILD)/libopenlayout.a -o $@

$(BUILD)/test_grid: tests/test_grid.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_grid.c $(BUILD)/libopenlayout.a -o $@

$(BUILD)/test_samesize: tests/test_samesize.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_samesize.c $(BUILD)/libopenlayout.a -o $@

$(BUILD)/test_web: tests/test_web.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_web.c $(BUILD)/libopenlayout.a -o $@

$(BUILD)/test_ui: tests/test_ui.c $(UI_SRC) ui/olui_internal.h include/gfopenlayout_ui.h $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) -std=c99 -Wall -Wextra -Werror -Iinclude -Iui $(UI_INC) tests/test_ui.c $(UI_SRC) $(BUILD)/libopenlayout.a -o $@

# GFOpenLayout UI's CSS through GFCSS
$(BUILD)/test_ui_css: tests/test_ui_css.c $(UI_SRC) ui/olui_internal.h include/gfopenlayout_ui.h $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) -std=c99 -Wall -Wextra -Werror -Iinclude -Iui $(UI_INC) tests/test_ui_css.c $(UI_SRC) $(BUILD)/libopenlayout.a -o $@

# 0.4: what a desktop and its drawers need (OpenBench)
$(BUILD)/test_desktop: tests/test_desktop.c $(UI_SRC) ui/olui_internal.h include/gfopenlayout_ui.h $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) -std=c99 -Wall -Wextra -Werror -Iinclude -Iui $(UI_INC) tests/test_desktop.c $(UI_SRC) $(BUILD)/libopenlayout.a -o $@

$(BUILD)/test_gtk: tests/test_gtk.c $(GTK_SRC) $(GTK_HDR) $(UI_SRC) ui/olui_internal.h include/gfopenlayout_ui.h $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) -std=c99 -Wall -Wextra -Werror -Igtk/include -Igtk -Iinclude -Iui $(UI_INC) tests/test_gtk.c $(GTK_SRC) $(UI_SRC) $(BUILD)/libopenlayout.a -o $@

# GFOpenLayout drawing (gfopenlayout_draw.h): what wave 1's modules draw with, on a host target of pixels
$(BUILD)/test_about: tests/test_about.c about/olabout.c draw/oldraw.c include/gfopenlayout_about.h include/gfopenlayout_draw.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -Idraw tests/test_about.c about/olabout.c draw/oldraw.c -lm -o $@

$(BUILD)/test_draw: tests/test_draw.c draw/oldraw.c draw/oldraw_internal.h include/gfopenlayout_draw.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -Idraw tests/test_draw.c draw/oldraw.c -o $@

$(BUILD)/test_css: tests/test_css.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_css.c $(BUILD)/libopenlayout.a -o $@

test: $(BUILD)/test_layout $(BUILD)/test_document $(BUILD)/test_rows $(BUILD)/test_grid $(BUILD)/test_samesize $(BUILD)/test_web $(BUILD)/test_ui $(BUILD)/test_css \
	  $(BUILD)/test_ui_css $(BUILD)/test_desktop $(BUILD)/test_gtk $(BUILD)/test_draw $(BUILD)/test_about $(BUILD)/test_core_api $(BUILD)/test_ogpu_accel
	./$(BUILD)/test_layout
	./$(BUILD)/test_document
	./$(BUILD)/test_rows
	./$(BUILD)/test_grid
	./$(BUILD)/test_samesize
	./$(BUILD)/test_web
	./$(BUILD)/test_ui
	./$(BUILD)/test_css
	./$(BUILD)/test_ui_css
	./$(BUILD)/test_desktop
	./$(BUILD)/test_gtk
	./$(BUILD)/test_draw
	./$(BUILD)/test_about
	./$(BUILD)/test_core_api
	./$(BUILD)/test_ogpu_accel
	python3 tools/gen-library.py --check
	python3 -B -m unittest -v tests.test_native_modules

# The ROM core's API (gfopenlayout_core.h, wave 2 review rule 2): the header alone in C89 and C99, and in C++
$(BUILD)/test_core_api: tests/test_core_api.c include/gfopenlayout_core.h | $(BUILD)
	$(CC) -std=c89 -pedantic -Wall -Wextra -Werror -Iinclude -fsyntax-only tests/test_core_api.c
	if command -v c++ >/dev/null; then printf '#include "gfopenlayout_core.h"\nint main(){return olc_oi()!=0;}\n' | \
	  c++ -x c++ -Wall -Wextra -Werror -Iinclude -fsyntax-only -; fi
	$(CC) $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -Werror -Iinclude tests/test_core_api.c -o $@

# olc_oi()'s entries against openintuition's real env tables, with its m68k types (its NDK size checks need
# 32-bit pointers): make probe_oi OI_DIR=<amigachrome-guest>/libraries/openintuition [OI_CC=m68k-linux-gnu-gcc]
OI_CC ?= m68k-linux-gnu-gcc
probe_oi:
	@test -n "$(OI_DIR)" || { echo "probe_oi: set OI_DIR to openintuition's source"; exit 1; }
	$(OI_CC) -std=c99 -Wall -Wextra -Werror -Iinclude -I$(OI_DIR) -fsyntax-only tests/probe_oi_types.c
	$(OI_CC) -DOU_NATIVE -std=c99 -Wall -Wextra -Werror -Iinclude -I$(OI_DIR) -fsyntax-only tests/probe_oi_types.c
	@echo "probe_oi: 18 hooks match openintuition's types"

# Row N1's GFOpenGPU back end: the host test supplies the four Amiga calls and inspects the exact stream.
$(BUILD)/test_ogpu_accel: tests/test_ogpu_accel.c core/olc_ogpu_accel.c core/olc_ogpu_accel.h include/gfopenlayout_core.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -Icore tests/test_ogpu_accel.c core/olc_ogpu_accel.c -o $@

clean:
	rm -rf $(BUILD)
