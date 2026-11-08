/* Made by tools/gen-library.py from the headers: don't edit. gfopenlayout.library
 * version 10's calls, each the C call with its arguments from their registers.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <gfopenlayout_ui.h>
#include <gfopenlayout_ui_amiga.h>

/* ---- GFCSS in documents ---- */

static int OL_NodeSetTag(REG(a0, ol_node * node), REG(a1, const char * tag), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_set_tag(node, tag);
}

static const char * OL_NodeTag(REG(a0, const ol_node * node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_tag(node);
}

static int OL_NodeSetAttr(REG(a0, ol_node * node), REG(a1, const char * name), REG(a2, const char * value), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_set_attr(node, name, value);
}

static const char * OL_NodeAttr(REG(a0, const ol_node * node), REG(a1, const char * name), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_attr(node, name);
}

static void OL_NodeSetCssState(REG(a0, ol_node * node), REG(d0, uint32_t states), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_node_set_css_state(node, states);
}

static uint32_t OL_NodeCssState(REG(a0, const ol_node * node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_css_state(node);
}

static int OL_NodeIsGenerated(REG(a0, const ol_node * node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_is_generated(node);
}

static const ol_node * OL_NodeFirstChild(REG(a0, const ol_node * node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_first_child(node);
}

static const ol_node * OL_NodeNextSibling(REG(a0, const ol_node * node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_next_sibling(node);
}

static int OL_DocumentAddStylesheet(REG(a0, ol_document * doc), REG(a1, const char * css), REG(d0, size_t length), REG(d1, int origin), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_document_add_stylesheet(doc, css, length, origin);
}

static void OL_DocumentSetViewport(REG(a0, ol_document * doc), REG(d0, ol_unit width), REG(d1, ol_unit height), REG(d2, int media), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_document_set_viewport(doc, width, height, media);
}

static int OL_DocumentApplyStyles(REG(a0, ol_document * doc), REG(d0, ol_unit containing_width), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_document_apply_styles(doc, containing_width);
}

static int OL_DocumentImportCount(REG(a0, const ol_document * doc), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_document_import_count(doc);
}

static const char * OL_DocumentImportUrl(REG(a0, const ol_document * doc), REG(d0, int index), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_document_import_url(doc, index);
}

static int OL_DocumentAddImport(REG(a0, ol_document * doc), REG(d0, int index), REG(a1, const char * css), REG(d1, size_t length), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_document_add_import(doc, index, css, length);
}

/* ---- GFOpenLayout UI ---- */

static olui * OLUI_New(REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_new();
}

static void OLUI_Free(REG(a0, olui * ui), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_free(ui);
}

static void OLUI_SetMeasure(REG(a0, olui * ui), REG(a1, ol_measure_text_fn measure), REG(a2, void * userdata), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_measure(ui, measure, userdata);
}

static int OLUI_AddCss(REG(a0, olui * ui), REG(a1, const char * css), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_add_css(ui, css);
}

static int OLUI_Load(REG(a0, olui * ui), REG(a1, const char * markup), REG(a2, const char * css), REG(a3, char * err), REG(d0, size_t errlen), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_load(ui, markup, css, err, errlen);
}

static int OLUI_On(REG(a0, olui * ui), REG(a1, const char * id), REG(d0, int type), REG(a2, olui_handler fn), REG(a3, void * userdata), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_on(ui, id, type, fn, userdata);
}

static void OLUI_SetZoom(REG(a0, olui * ui), REG(d0, int percent), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_zoom(ui, percent);
}

static int OLUI_Layout(REG(a0, olui * ui), REG(d0, int width), REG(d1, int height), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_layout(ui, width, height);
}

static void OLUI_Draw(REG(a0, olui * ui), REG(a1, const olui_rect * area), REG(a2, olui_draw_fn draw), REG(a3, void * userdata), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_draw(ui, area, draw, userdata);
}

static int OLUI_Damaged(REG(a0, const olui * ui), REG(a1, olui_rect * area), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_damaged(ui, area);
}

static int OLUI_Mouse(REG(a0, olui * ui), REG(d0, int x), REG(d1, int y), REG(d2, int what), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_mouse(ui, x, y, what);
}

static int OLUI_Key(REG(a0, olui * ui), REG(d0, unsigned code), REG(d1, unsigned qualifiers), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_key(ui, code, qualifiers);
}

static int OLUI_Tick(REG(a0, olui * ui), REG(d0, unsigned ms), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_tick(ui, ms);
}

static olui_el * OLUI_Find(REG(a0, olui * ui), REG(a1, const char * id), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_find(ui, id);
}

static const char * OLUI_Id(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_id(el);
}

static const char * OLUI_Tag(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_tag(el);
}

static const char * OLUI_GetText(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_get_text(el);
}

static int OLUI_SetText(REG(a0, olui * ui), REG(a1, olui_el * el), REG(a2, const char * utf8), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_set_text(ui, el, utf8);
}

static const char * OLUI_GetValue(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_get_value(el);
}

static int OLUI_GetChecked(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_get_checked(el);
}

static void OLUI_SetChecked(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int checked), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_checked(ui, el, checked);
}

static void OLUI_SetDisabled(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int disabled), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_disabled(ui, el, disabled);
}

static void OLUI_SetHidden(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int hidden), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_hidden(ui, el, hidden);
}

static void OLUI_SetProgress(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int value), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_progress(ui, el, value);
}

static int OLUI_SliderValue(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_slider_value(el);
}

static void OLUI_SetSlider(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int value), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_slider(ui, el, value);
}

static int OLUI_Box(REG(a0, olui * ui), REG(a1, const olui_el * el), REG(a2, olui_rect * r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_box(ui, el, r);
}

static void OLUI_Focus(REG(a0, olui * ui), REG(a1, olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_focus(ui, el);
}

static olui_el * OLUI_Focused(REG(a0, const olui * ui), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_focused(ui);
}

static int32_t OLUI_GetScroll(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_get_scroll(el);
}

static void OLUI_SetScroll(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int32_t y), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_scroll(ui, el, y);
}

static int OLUI_SelectIndex(REG(a0, const olui_el * select), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_select_index(select);
}

static void OLUI_SelectSet(REG(a0, olui * ui), REG(a1, olui_el * select), REG(d0, int index), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_select_set(ui, select, index);
}

static int OLUI_SelectCount(REG(a0, const olui_el * select), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_select_count(select);
}

static const char * OLUI_SelectOptionValue(REG(a0, const olui_el * select), REG(d0, int index), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_select_option_value(select, index);
}

static const char * OLUI_SelectOptionText(REG(a0, const olui_el * select), REG(d0, int index), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_select_option_text(select, index);
}

static olui_el * OLUI_SelectAdd(REG(a0, olui * ui), REG(a1, olui_el * select), REG(a2, const char * text), REG(a3, const char * value), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_select_add(ui, select, text, value);
}

static void OLUI_SelectClear(REG(a0, olui * ui), REG(a1, olui_el * select), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_select_clear(ui, select);
}

static int OLUI_SelectIsOpen(REG(a0, const olui * ui), REG(a1, const olui_el * select), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_select_is_open(ui, select);
}

static void OLUI_SelectOpen(REG(a0, olui * ui), REG(a1, olui_el * select), REG(d0, int open), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_select_open(ui, select, open);
}

static int OLUI_NumberValue(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_number_value(el);
}

static int OLUI_NumberDecimals(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_number_decimals(el);
}

static void OLUI_SetNumber(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int value), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_number(ui, el, value);
}

static int OLUI_GetSelection(REG(a0, const olui_el * el), REG(a1, size_t * from), REG(a2, size_t * to), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_get_selection(el, from, to);
}

static void OLUI_SetSelection(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, size_t from), REG(d1, size_t to), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_selection(ui, el, from, to);
}

static int OLUI_InsertText(REG(a0, olui * ui), REG(a1, olui_el * el), REG(a2, const char * utf8), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_insert_text(ui, el, utf8);
}

static int OLUI_Edit(REG(a0, olui * ui), REG(d0, int what), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_edit(ui, what);
}

static olui_el * OLUI_ListAdd(REG(a0, olui * ui), REG(a1, olui_el * list), REG(a2, olui_el * parent), REG(a3, const char * text), REG(d0, const char * value), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_list_add(ui, list, parent, text, value);
}

static void OLUI_ListClear(REG(a0, olui * ui), REG(a1, olui_el * list), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_list_clear(ui, list);
}

static olui_el * OLUI_ListSelected(REG(a0, const olui_el * list), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_list_selected(list);
}

static void OLUI_ListSelect(REG(a0, olui * ui), REG(a1, olui_el * list), REG(a2, olui_el * item), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_list_select(ui, list, item);
}

static void OLUI_ItemOpen(REG(a0, olui * ui), REG(a1, olui_el * item), REG(d0, int open), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_item_open(ui, item, open);
}

static int OLUI_ItemIsOpen(REG(a0, const olui_el * item), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_item_is_open(item);
}

static int OLUI_TabsCurrent(REG(a0, const olui_el * tabs), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_tabs_current(tabs);
}

static void OLUI_TabsSelect(REG(a0, olui * ui), REG(a1, olui_el * tabs), REG(d0, int index), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_tabs_select(ui, tabs, index);
}

static void OLUI_SetUser(REG(a0, olui_el * el), REG(a1, void * user), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_user(el, user);
}

static void * OLUI_User(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_user(el);
}

static char * OLUI_ThemeCss(REG(a0, const char * theme_text), REG(d0, int dark), REG(a1, char * err), REG(d1, size_t errlen), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_theme_css(theme_text, dark, err, errlen);
}

static void OLUI_FreeCss(REG(a0, char * css), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_free_css(css);
}

/* ---- GFOpenLayout UI's Amiga glue ---- */

static ULONG OLUI_AmigaIdcmp(REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_amiga_idcmp();
}

static olui_amiga * OLUI_AmigaNew(REG(a0, olui * ui), REG(a1, struct Window * win), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_amiga_new(ui, win);
}

static void OLUI_AmigaFree(REG(a0, olui_amiga * a), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_amiga_free(a);
}

static int OLUI_AmigaTheme(REG(a0, olui_amiga * a), REG(a1, const char * path), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_amiga_theme(a, path);
}

static int OLUI_AmigaOwn(REG(a0, olui_amiga * a), REG(a1, const olui_el * el), REG(a2, olui_amiga_own_fn fn), REG(a3, void * userdata), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_amiga_own(a, el, fn, userdata);
}

static void OLUI_AmigaImages(REG(a0, olui_amiga * a), REG(a1, olui_amiga_image_fn fn), REG(a2, void * userdata), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_amiga_images(a, fn, userdata);
}

static void OLUI_AmigaFonts(REG(a0, olui_amiga * a), REG(a1, const char * font), REG(a2, const char * mono), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_amiga_fonts(a, font, mono);
}

static int OLUI_AmigaLayout(REG(a0, olui_amiga * a), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_amiga_layout(a);
}

static void OLUI_AmigaDraw(REG(a0, olui_amiga * a), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_amiga_draw(a);
}

static int OLUI_AmigaEvent(REG(a0, olui_amiga * a), REG(a1, struct IntuiMessage * msg), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_amiga_event(a, msg);
}

/* ---- what a desktop needs (GFOpenLayout UI 0.4) ---- */

static olui_el * OLUI_Add(REG(a0, olui * ui), REG(a1, olui_el * parent), REG(a2, const char * markup), REG(a3, char * err), REG(d0, size_t errlen), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_add(ui, parent, markup, err, errlen);
}

static int OLUI_Remove(REG(a0, olui * ui), REG(a1, olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_remove(ui, el);
}

static int OLUI_SetStyle(REG(a0, olui * ui), REG(a1, olui_el * el), REG(a2, const char * decls), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_set_style(ui, el, decls);
}

static void OLUI_SetPlace(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int32_t x), REG(d1, int32_t y), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_place(ui, el, x, y);
}

static olui_el * OLUI_Hit(REG(a0, olui * ui), REG(d0, int x), REG(d1, int y), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_hit(ui, x, y);
}

static olui_el * OLUI_Parent(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_parent(el);
}

static olui_el * OLUI_FirstChild(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_first_child(el);
}

static olui_el * OLUI_Next(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_next(el);
}

static int OLUI_ImageName(REG(a0, olui * ui), REG(a1, const char * name), REG(d0, int image), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_image_name(ui, name, image);
}

static int OLUI_MouseQual(REG(a0, olui * ui), REG(d0, int x), REG(d1, int y), REG(d2, int what), REG(d3, unsigned qualifiers), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_mouse_qual(ui, x, y, what, qualifiers);
}

static void OLUI_SetSelected(REG(a0, olui * ui), REG(a1, olui_el * el), REG(d0, int selected), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_set_selected(ui, el, selected);
}

static int OLUI_IsSelected(REG(a0, const olui_el * el), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_is_selected(el);
}

static olui_el * OLUI_NextSelected(REG(a0, olui * ui), REG(a1, const olui_el * scope), REG(a2, const olui_el * after), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_next_selected(ui, scope, after);
}

static int OLUI_SelectAll(REG(a0, olui * ui), REG(a1, olui_el * scope), REG(d0, int selected), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_select_all(ui, scope, selected);
}

static olui_el * OLUI_DragItem(REG(a0, const olui * ui), REG(d0, int index), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_drag_item(ui, index);
}

static void OLUI_VlistSet(REG(a0, olui * ui), REG(a1, olui_el * vlist), REG(d0, int rows), REG(a2, olui_row_fn fn), REG(a3, void * userdata), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_vlist_set(ui, vlist, rows, fn, userdata);
}

static int OLUI_VlistRows(REG(a0, const olui_el * vlist), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_vlist_rows(vlist);
}

static int OLUI_VlistSelected(REG(a0, const olui_el * vlist), REG(d0, int row), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_vlist_selected(vlist, row);
}

static void OLUI_VlistSelect(REG(a0, olui * ui), REG(a1, olui_el * vlist), REG(d0, int row), REG(d1, int selected), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_vlist_select(ui, vlist, row, selected);
}

static int OLUI_VlistCurrent(REG(a0, const olui_el * vlist), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_vlist_current(vlist);
}

static void OLUI_VlistSetCurrent(REG(a0, olui * ui), REG(a1, olui_el * vlist), REG(d0, int row), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_vlist_set_current(ui, vlist, row);
}

static int OLUI_MenuOpen(REG(a0, olui * ui), REG(a1, olui_el * menu), REG(d0, int x), REG(d1, int y), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_menu_open(ui, menu, x, y);
}

static olui * OLUI_NewSharing(REG(a0, olui * from), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_new_sharing(from);
}

/* ---- the Amiga glue in a strip of a window ---- */

static int OLUI_AmigaStrip(REG(a0, olui_amiga * a), REG(d0, int x), REG(d1, int y), REG(d2, int w), REG(d3, int h), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_amiga_strip(a, x, y, w, h);
}

static int OLUI_AmigaKeys(REG(a0, olui_amiga * a), REG(d0, int keys), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_amiga_keys(a, keys);
}

/* ---- what a toolkit over the UI needs (GFOpenLayout UI 0.5) ---- */

static void OLUI_Repaint(REG(a0, olui * ui), REG(a1, olui_el * el), REG(a2, const olui_rect * part), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    olui_repaint(ui, el, part);
}

static olui_el * OLUI_Popup(REG(a0, const olui * ui), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return olui_popup(ui);
}

/* ---- document drawing, image crops and clipping ---- */

static void OL_ShapeStyleInit(REG(a0, ol_shape_style * style), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_shape_style_init(style);
}

static int OL_NodeSetShape(REG(a0, ol_node * node), REG(a1, const ol_shape_style * style), REG(d0, int count), REG(a2, const ol_point * points), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_set_shape(node, style, count, points);
}

static int OL_NodeSetImageCrop(REG(a0, ol_node * node), REG(a1, const ol_image_crop * crop), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_set_image_crop(node, crop);
}

/* the last call's bias: lib_vectors is checked against it */
#define OL_V10_LAST_BIAS 0x3f6

#define OL_V10_VECTORS \
    (APTR)OL_NodeSetTag, (APTR)OL_NodeTag, (APTR)OL_NodeSetAttr, \
    (APTR)OL_NodeAttr, (APTR)OL_NodeSetCssState, (APTR)OL_NodeCssState, \
    (APTR)OL_NodeIsGenerated, (APTR)OL_NodeFirstChild, (APTR)OL_NodeNextSibling, \
    (APTR)OL_DocumentAddStylesheet, (APTR)OL_DocumentSetViewport, (APTR)OL_DocumentApplyStyles, \
    (APTR)OL_DocumentImportCount, (APTR)OL_DocumentImportUrl, (APTR)OL_DocumentAddImport, \
    (APTR)OLUI_New, (APTR)OLUI_Free, (APTR)OLUI_SetMeasure, \
    (APTR)OLUI_AddCss, (APTR)OLUI_Load, (APTR)OLUI_On, \
    (APTR)OLUI_SetZoom, (APTR)OLUI_Layout, (APTR)OLUI_Draw, \
    (APTR)OLUI_Damaged, (APTR)OLUI_Mouse, (APTR)OLUI_Key, \
    (APTR)OLUI_Tick, (APTR)OLUI_Find, (APTR)OLUI_Id, \
    (APTR)OLUI_Tag, (APTR)OLUI_GetText, (APTR)OLUI_SetText, \
    (APTR)OLUI_GetValue, (APTR)OLUI_GetChecked, (APTR)OLUI_SetChecked, \
    (APTR)OLUI_SetDisabled, (APTR)OLUI_SetHidden, (APTR)OLUI_SetProgress, \
    (APTR)OLUI_SliderValue, (APTR)OLUI_SetSlider, (APTR)OLUI_Box, \
    (APTR)OLUI_Focus, (APTR)OLUI_Focused, (APTR)OLUI_GetScroll, \
    (APTR)OLUI_SetScroll, (APTR)OLUI_SelectIndex, (APTR)OLUI_SelectSet, \
    (APTR)OLUI_SelectCount, (APTR)OLUI_SelectOptionValue, (APTR)OLUI_SelectOptionText, \
    (APTR)OLUI_SelectAdd, (APTR)OLUI_SelectClear, (APTR)OLUI_SelectIsOpen, \
    (APTR)OLUI_SelectOpen, (APTR)OLUI_NumberValue, (APTR)OLUI_NumberDecimals, \
    (APTR)OLUI_SetNumber, (APTR)OLUI_GetSelection, (APTR)OLUI_SetSelection, \
    (APTR)OLUI_InsertText, (APTR)OLUI_Edit, (APTR)OLUI_ListAdd, \
    (APTR)OLUI_ListClear, (APTR)OLUI_ListSelected, (APTR)OLUI_ListSelect, \
    (APTR)OLUI_ItemOpen, (APTR)OLUI_ItemIsOpen, (APTR)OLUI_TabsCurrent, \
    (APTR)OLUI_TabsSelect, (APTR)OLUI_SetUser, (APTR)OLUI_User, \
    (APTR)OLUI_ThemeCss, (APTR)OLUI_FreeCss, (APTR)OLUI_AmigaIdcmp, \
    (APTR)OLUI_AmigaNew, (APTR)OLUI_AmigaFree, (APTR)OLUI_AmigaTheme, \
    (APTR)OLUI_AmigaOwn, (APTR)OLUI_AmigaImages, (APTR)OLUI_AmigaFonts, \
    (APTR)OLUI_AmigaLayout, (APTR)OLUI_AmigaDraw, (APTR)OLUI_AmigaEvent, \
    (APTR)OLUI_Add, (APTR)OLUI_Remove, (APTR)OLUI_SetStyle, \
    (APTR)OLUI_SetPlace, (APTR)OLUI_Hit, (APTR)OLUI_Parent, \
    (APTR)OLUI_FirstChild, (APTR)OLUI_Next, (APTR)OLUI_ImageName, \
    (APTR)OLUI_MouseQual, (APTR)OLUI_SetSelected, (APTR)OLUI_IsSelected, \
    (APTR)OLUI_NextSelected, (APTR)OLUI_SelectAll, (APTR)OLUI_DragItem, \
    (APTR)OLUI_VlistSet, (APTR)OLUI_VlistRows, (APTR)OLUI_VlistSelected, \
    (APTR)OLUI_VlistSelect, (APTR)OLUI_VlistCurrent, (APTR)OLUI_VlistSetCurrent, \
    (APTR)OLUI_MenuOpen, (APTR)OLUI_NewSharing, (APTR)OLUI_AmigaStrip, \
    (APTR)OLUI_AmigaKeys, (APTR)OLUI_Repaint, (APTR)OLUI_Popup, \
    (APTR)OL_ShapeStyleInit, (APTR)OL_NodeSetShape, (APTR)OL_NodeSetImageCrop
