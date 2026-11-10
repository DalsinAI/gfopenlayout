CC ?= cc
AR ?= ar
CFLAGS ?= -O2
WARN = -std=c99 -Wall -Wextra -Werror -pedantic
BUILD = build

.PHONY: all test clean

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

all: $(BUILD)/libopenlayout.a $(BUILD)/test_layout $(BUILD)/test_document $(BUILD)/test_rows $(BUILD)/test_grid $(BUILD)/test_web $(BUILD)/test_ui \
     $(BUILD)/test_css $(BUILD)/test_ui_css $(BUILD)/test_desktop $(BUILD)/test_gtk

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

$(BUILD)/test_css: tests/test_css.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_css.c $(BUILD)/libopenlayout.a -o $@

test: $(BUILD)/test_layout $(BUILD)/test_document $(BUILD)/test_rows $(BUILD)/test_grid $(BUILD)/test_web $(BUILD)/test_ui $(BUILD)/test_css \
      $(BUILD)/test_ui_css $(BUILD)/test_desktop $(BUILD)/test_gtk
	./$(BUILD)/test_layout
	./$(BUILD)/test_document
	./$(BUILD)/test_rows
	./$(BUILD)/test_grid
	./$(BUILD)/test_web
	./$(BUILD)/test_ui
	./$(BUILD)/test_css
	./$(BUILD)/test_ui_css
	./$(BUILD)/test_desktop
	./$(BUILD)/test_gtk
	python3 tools/gen-library.py --check

clean:
	rm -rf $(BUILD)
