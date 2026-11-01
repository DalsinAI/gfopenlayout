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

all: $(BUILD)/libopenlayout.a $(BUILD)/test_layout $(BUILD)/test_document $(BUILD)/test_rows $(BUILD)/test_ui \
     $(BUILD)/test_css

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

$(BUILD)/test_ui: tests/test_ui.c $(UI_SRC) ui/olui_internal.h include/gfopenlayout_ui.h $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) -std=c99 -Wall -Wextra -Werror -Iinclude -Iui $(UI_INC) tests/test_ui.c $(UI_SRC) $(BUILD)/libopenlayout.a -o $@

$(BUILD)/test_css: tests/test_css.c $(BUILD)/libopenlayout.a
	$(CC) $(CFLAGS) $(WARN) -Iinclude tests/test_css.c $(BUILD)/libopenlayout.a -o $@

test: $(BUILD)/test_layout $(BUILD)/test_document $(BUILD)/test_rows $(BUILD)/test_ui $(BUILD)/test_css
	./$(BUILD)/test_layout
	./$(BUILD)/test_document
	./$(BUILD)/test_rows
	./$(BUILD)/test_ui
	./$(BUILD)/test_css

clean:
	rm -rf $(BUILD)
