/* Made by tools/gen-library.py from the headers: don't edit. The C API of
 * GFOpenLayout's CSS calls, GFOpenLayout UI and its Amiga glue through
 * gfopenlayout.library 10: a program links this (libgfopenlayout_link.a) where it
 * linked libolui.a; gfopenlayout_link_base.c opens the library for it.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdlib.h>
#include <string.h>
#include <proto/gfopenlayout.h>

int ol_node_set_tag(ol_node *node, const char *tag)
{
    return OL_NodeSetTag(node, tag);
}

const char * ol_node_tag(const ol_node *node)
{
    return OL_NodeTag(node);
}

int ol_node_set_attr(ol_node *node, const char *name, const char *value)
{
    return OL_NodeSetAttr(node, name, value);
}

const char * ol_node_attr(const ol_node *node, const char *name)
{
    return OL_NodeAttr(node, name);
}

void ol_node_set_css_state(ol_node *node, uint32_t states)
{
    OL_NodeSetCssState(node, states);
}

uint32_t ol_node_css_state(const ol_node *node)
{
    return OL_NodeCssState(node);
}

int ol_node_is_generated(const ol_node *node)
{
    return OL_NodeIsGenerated(node);
}

const ol_node * ol_node_first_child(const ol_node *node)
{
    return OL_NodeFirstChild(node);
}

const ol_node * ol_node_next_sibling(const ol_node *node)
{
    return OL_NodeNextSibling(node);
}

int ol_document_add_stylesheet(ol_document *doc, const char *css, size_t length, int origin)
{
    return OL_DocumentAddStylesheet(doc, css, length, origin);
}

void ol_document_set_viewport(ol_document *doc, ol_unit width, ol_unit height, int media)
{
    OL_DocumentSetViewport(doc, width, height, media);
}

int ol_document_apply_styles(ol_document *doc, ol_unit containing_width)
{
    return OL_DocumentApplyStyles(doc, containing_width);
}

int ol_document_import_count(const ol_document *doc)
{
    return OL_DocumentImportCount(doc);
}

const char * ol_document_import_url(const ol_document *doc, int index)
{
    return OL_DocumentImportUrl(doc, index);
}

int ol_document_add_import(ol_document *doc, int index, const char *css, size_t length)
{
    return OL_DocumentAddImport(doc, index, css, length);
}

olui * olui_new(void)
{
    return OLUI_New();
}

void olui_free(olui *ui)
{
    OLUI_Free(ui);
}

void olui_set_measure(olui *ui, ol_measure_text_fn measure, void *userdata)
{
    OLUI_SetMeasure(ui, measure, userdata);
}

int olui_add_css(olui *ui, const char *css)
{
    return OLUI_AddCss(ui, css);
}

int olui_load(olui *ui, const char *markup, const char *css, char *err, size_t errlen)
{
    return OLUI_Load(ui, markup, css, err, errlen);
}

int olui_on(olui *ui, const char *id, int type, olui_handler fn, void *userdata)
{
    return OLUI_On(ui, id, type, fn, userdata);
}

void olui_set_zoom(olui *ui, int percent)
{
    OLUI_SetZoom(ui, percent);
}

int olui_layout(olui *ui, int width, int height)
{
    return OLUI_Layout(ui, width, height);
}

void olui_draw(olui *ui, const olui_rect *area, olui_draw_fn draw, void *userdata)
{
    OLUI_Draw(ui, area, draw, userdata);
}

int olui_damaged(const olui *ui, olui_rect *area)
{
    return OLUI_Damaged(ui, area);
}

int olui_mouse(olui *ui, int x, int y, int what)
{
    return OLUI_Mouse(ui, x, y, what);
}

int olui_key(olui *ui, unsigned code, unsigned qualifiers)
{
    return OLUI_Key(ui, code, qualifiers);
}

int olui_tick(olui *ui, unsigned ms)
{
    return OLUI_Tick(ui, ms);
}

olui_el * olui_find(olui *ui, const char *id)
{
    return OLUI_Find(ui, id);
}

const char * olui_id(const olui_el *el)
{
    return OLUI_Id(el);
}

const char * olui_tag(const olui_el *el)
{
    return OLUI_Tag(el);
}

const char * olui_get_text(const olui_el *el)
{
    return OLUI_GetText(el);
}

int olui_set_text(olui *ui, olui_el *el, const char *utf8)
{
    return OLUI_SetText(ui, el, utf8);
}

const char * olui_get_value(const olui_el *el)
{
    return OLUI_GetValue(el);
}

int olui_get_checked(const olui_el *el)
{
    return OLUI_GetChecked(el);
}

void olui_set_checked(olui *ui, olui_el *el, int checked)
{
    OLUI_SetChecked(ui, el, checked);
}

void olui_set_disabled(olui *ui, olui_el *el, int disabled)
{
    OLUI_SetDisabled(ui, el, disabled);
}

void olui_set_hidden(olui *ui, olui_el *el, int hidden)
{
    OLUI_SetHidden(ui, el, hidden);
}

void olui_set_progress(olui *ui, olui_el *el, int value)
{
    OLUI_SetProgress(ui, el, value);
}

int olui_slider_value(const olui_el *el)
{
    return OLUI_SliderValue(el);
}

void olui_set_slider(olui *ui, olui_el *el, int value)
{
    OLUI_SetSlider(ui, el, value);
}

int olui_box(olui *ui, const olui_el *el, olui_rect *r)
{
    return OLUI_Box(ui, el, r);
}

void olui_focus(olui *ui, olui_el *el)
{
    OLUI_Focus(ui, el);
}

olui_el * olui_focused(const olui *ui)
{
    return OLUI_Focused(ui);
}

int32_t olui_get_scroll(const olui_el *el)
{
    return OLUI_GetScroll(el);
}

void olui_set_scroll(olui *ui, olui_el *el, int32_t y)
{
    OLUI_SetScroll(ui, el, y);
}

int olui_select_index(const olui_el *select)
{
    return OLUI_SelectIndex(select);
}

void olui_select_set(olui *ui, olui_el *select, int index)
{
    OLUI_SelectSet(ui, select, index);
}

int olui_select_count(const olui_el *select)
{
    return OLUI_SelectCount(select);
}

const char * olui_select_option_value(const olui_el *select, int index)
{
    return OLUI_SelectOptionValue(select, index);
}

const char * olui_select_option_text(const olui_el *select, int index)
{
    return OLUI_SelectOptionText(select, index);
}

olui_el * olui_select_add(olui *ui, olui_el *select, const char *text, const char *value)
{
    return OLUI_SelectAdd(ui, select, text, value);
}

void olui_select_clear(olui *ui, olui_el *select)
{
    OLUI_SelectClear(ui, select);
}

int olui_select_is_open(const olui *ui, const olui_el *select)
{
    return OLUI_SelectIsOpen(ui, select);
}

void olui_select_open(olui *ui, olui_el *select, int open)
{
    OLUI_SelectOpen(ui, select, open);
}

int olui_number_value(const olui_el *el)
{
    return OLUI_NumberValue(el);
}

int olui_number_decimals(const olui_el *el)
{
    return OLUI_NumberDecimals(el);
}

void olui_set_number(olui *ui, olui_el *el, int value)
{
    OLUI_SetNumber(ui, el, value);
}

int olui_get_selection(const olui_el *el, size_t *from, size_t *to)
{
    return OLUI_GetSelection(el, from, to);
}

void olui_set_selection(olui *ui, olui_el *el, size_t from, size_t to)
{
    OLUI_SetSelection(ui, el, from, to);
}

int olui_insert_text(olui *ui, olui_el *el, const char *utf8)
{
    return OLUI_InsertText(ui, el, utf8);
}

int olui_edit(olui *ui, int what)
{
    return OLUI_Edit(ui, what);
}

olui_el * olui_list_add(olui *ui, olui_el *list, olui_el *parent, const char *text, const char *value)
{
    return OLUI_ListAdd(ui, list, parent, text, value);
}

void olui_list_clear(olui *ui, olui_el *list)
{
    OLUI_ListClear(ui, list);
}

olui_el * olui_list_selected(const olui_el *list)
{
    return OLUI_ListSelected(list);
}

void olui_list_select(olui *ui, olui_el *list, olui_el *item)
{
    OLUI_ListSelect(ui, list, item);
}

void olui_item_open(olui *ui, olui_el *item, int open)
{
    OLUI_ItemOpen(ui, item, open);
}

int olui_item_is_open(const olui_el *item)
{
    return OLUI_ItemIsOpen(item);
}

int olui_tabs_current(const olui_el *tabs)
{
    return OLUI_TabsCurrent(tabs);
}

void olui_tabs_select(olui *ui, olui_el *tabs, int index)
{
    OLUI_TabsSelect(ui, tabs, index);
}

void olui_set_user(olui_el *el, void *user)
{
    OLUI_SetUser(el, user);
}

void * olui_user(const olui_el *el)
{
    return OLUI_User(el);
}

char * olui_theme_css(const char *theme_text, int dark, char *err, size_t errlen)
{
    char *lib = OLUI_ThemeCss(theme_text, dark, err, errlen), *mine;
    size_t n;
    if (!lib) return NULL;
    n = strlen(lib) + 1;
    mine = (char *)malloc(n);
    if (mine) memcpy(mine, lib, n);
    else if (err && errlen) strncpy(err, "out of memory", errlen - 1), err[errlen - 1] = 0;
    OLUI_FreeCss(lib);
    return mine;
}

void olui_free_css(char *css)
{
    free(css);   /* olui_theme_css's copy, in the program's memory */
}

ULONG olui_amiga_idcmp(void)
{
    return OLUI_AmigaIdcmp();
}

olui_amiga * olui_amiga_new(olui *ui, struct Window *win)
{
    return OLUI_AmigaNew(ui, win);
}

void olui_amiga_free(olui_amiga *a)
{
    OLUI_AmigaFree(a);
}

int olui_amiga_theme(olui_amiga *a, const char *path)
{
    return OLUI_AmigaTheme(a, path);
}

int olui_amiga_own(olui_amiga *a, const olui_el *el, olui_amiga_own_fn fn, void *userdata)
{
    return OLUI_AmigaOwn(a, el, fn, userdata);
}

void olui_amiga_images(olui_amiga *a, olui_amiga_image_fn fn, void *userdata)
{
    OLUI_AmigaImages(a, fn, userdata);
}

void olui_amiga_fonts(olui_amiga *a, const char *font, const char *mono)
{
    OLUI_AmigaFonts(a, font, mono);
}

int olui_amiga_layout(olui_amiga *a)
{
    return OLUI_AmigaLayout(a);
}

void olui_amiga_draw(olui_amiga *a)
{
    OLUI_AmigaDraw(a);
}

int olui_amiga_event(olui_amiga *a, struct IntuiMessage *msg)
{
    return OLUI_AmigaEvent(a, msg);
}

olui_el * olui_add(olui *ui, olui_el *parent, const char *markup, char *err, size_t errlen)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_Add(ui, parent, markup, err, errlen);
}

int olui_remove(olui *ui, olui_el *el)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_Remove(ui, el);
}

int olui_set_style(olui *ui, olui_el *el, const char *decls)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_SetStyle(ui, el, decls);
}

void olui_set_place(olui *ui, olui_el *el, int32_t x, int32_t y)
{
    if (OpenLayoutBase->lib_Version < 11) return;
    OLUI_SetPlace(ui, el, x, y);
}

olui_el * olui_hit(olui *ui, int x, int y)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_Hit(ui, x, y);
}

olui_el * olui_parent(const olui_el *el)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_Parent(el);
}

olui_el * olui_first_child(const olui_el *el)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_FirstChild(el);
}

olui_el * olui_next(const olui_el *el)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_Next(el);
}

int olui_image_name(olui *ui, const char *name, int image)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_ImageName(ui, name, image);
}

int olui_mouse_qual(olui *ui, int x, int y, int what, unsigned qualifiers)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_MouseQual(ui, x, y, what, qualifiers);
}

void olui_set_selected(olui *ui, olui_el *el, int selected)
{
    if (OpenLayoutBase->lib_Version < 11) return;
    OLUI_SetSelected(ui, el, selected);
}

int olui_is_selected(const olui_el *el)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_IsSelected(el);
}

olui_el * olui_next_selected(olui *ui, const olui_el *scope, const olui_el *after)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_NextSelected(ui, scope, after);
}

int olui_select_all(olui *ui, olui_el *scope, int selected)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_SelectAll(ui, scope, selected);
}

olui_el * olui_drag_item(const olui *ui, int index)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_DragItem(ui, index);
}

void olui_vlist_set(olui *ui, olui_el *vlist, int rows, olui_row_fn fn, void *userdata)
{
    if (OpenLayoutBase->lib_Version < 11) return;
    OLUI_VlistSet(ui, vlist, rows, fn, userdata);
}

int olui_vlist_rows(const olui_el *vlist)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_VlistRows(vlist);
}

int olui_vlist_selected(const olui_el *vlist, int row)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_VlistSelected(vlist, row);
}

void olui_vlist_select(olui *ui, olui_el *vlist, int row, int selected)
{
    if (OpenLayoutBase->lib_Version < 11) return;
    OLUI_VlistSelect(ui, vlist, row, selected);
}

int olui_vlist_current(const olui_el *vlist)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_VlistCurrent(vlist);
}

void olui_vlist_set_current(olui *ui, olui_el *vlist, int row)
{
    if (OpenLayoutBase->lib_Version < 11) return;
    OLUI_VlistSetCurrent(ui, vlist, row);
}

int olui_menu_open(olui *ui, olui_el *menu, int x, int y)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_MenuOpen(ui, menu, x, y);
}

olui * olui_new_sharing(olui *from)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_NewSharing(from);
}

int olui_amiga_strip(olui_amiga *a, int x, int y, int w, int h)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_AmigaStrip(a, x, y, w, h);
}

int olui_amiga_keys(olui_amiga *a, int keys)
{
    if (OpenLayoutBase->lib_Version < 11) return 0;
    return OLUI_AmigaKeys(a, keys);
}

void olui_repaint(olui *ui, olui_el *el, const olui_rect *part)
{
    if (OpenLayoutBase->lib_Version < 12) return;
    OLUI_Repaint(ui, el, part);
}

olui_el * olui_popup(const olui *ui)
{
    if (OpenLayoutBase->lib_Version < 12) return 0;
    return OLUI_Popup(ui);
}

void ol_shape_style_init(ol_shape_style *style)
{
    if (OpenLayoutBase->lib_Version < 13) return;
    OL_ShapeStyleInit(style);
}

int ol_node_set_shape(ol_node *node, const ol_shape_style *style, int count, const ol_point *points)
{
    if (OpenLayoutBase->lib_Version < 13) return 0;
    return OL_NodeSetShape(node, style, count, points);
}

int ol_node_set_image_crop(ol_node *node, const ol_image_crop *crop)
{
    if (OpenLayoutBase->lib_Version < 13) return 0;
    return OL_NodeSetImageCrop(node, crop);
}
