/* GFOpenGTK on the host: GTK 1.2 code as Glade wrote it for LinBolo (a
 * name dialog run in a gtk_main of its own, check and radio buttons, a
 * table of fields) made into olui windows, the user's input through olui
 * coming back as GTK's signals, and glib's lists and timeouts.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <stdio.h>
#include <string.h>

#include "gfopenlayout_ui.h"

static int checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL line=%d expr=%s\n", __LINE__, #x); } } while (0)

/* ---- The name dialog, as LinBolo's dialogsetname.c has it ------------------------------ */

static GtkWidget *name_dialog, *name_entry;
static char chosen[32];
static int destroyed, key_presses, unrefs;

static void counted_unref(gpointer w)
{
    ++unrefs;
    gtk_widget_unref(GTK_WIDGET(w));
}

static gboolean name_ok(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)widget;
    (void)event;
    (void)user_data;
    strncpy(chosen, gtk_entry_get_text(GTK_ENTRY(name_entry)), sizeof(chosen) - 1);
    gtk_grab_remove(name_dialog);
    gtk_widget_destroy(name_dialog);
    gtk_main_quit();
    return FALSE;
}

static gboolean name_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    ++key_presses;
    if (event->keyval == 65293) name_ok(name_dialog, NULL, NULL);
    return FALSE;
}

static void name_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    ++destroyed;
}

static GtkWidget *name_create(void)
{
    GtkWidget *d, *vbox1, *label1, *hbox1, *button1;

    d = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_object_set_data(GTK_OBJECT(d), "dialogSetName", d);
    gtk_container_set_border_width(GTK_CONTAINER(d), 10);
    gtk_window_set_title(GTK_WINDOW(d), "Enter Player Name");
    gtk_window_set_position(GTK_WINDOW(d), GTK_WIN_POS_CENTER);
    gtk_window_set_modal(GTK_WINDOW(d), TRUE);
    gtk_window_set_policy(GTK_WINDOW(d), FALSE, FALSE, FALSE);

    vbox1 = gtk_vbox_new(FALSE, 0);
    gtk_widget_ref(vbox1);
    gtk_object_set_data_full(GTK_OBJECT(d), "vbox1", vbox1, (GtkDestroyNotify)counted_unref);
    gtk_widget_show(vbox1);
    gtk_container_add(GTK_CONTAINER(d), vbox1);

    label1 = gtk_label_new("Enter the new player name for your tank");
    gtk_widget_ref(label1);
    gtk_object_set_data_full(GTK_OBJECT(d), "label1", label1, (GtkDestroyNotify)counted_unref);
    gtk_widget_show(label1);
    gtk_box_pack_start(GTK_BOX(vbox1), label1, FALSE, FALSE, 0);
    gtk_label_set_justify(GTK_LABEL(label1), GTK_JUSTIFY_LEFT);

    hbox1 = gtk_hbox_new(FALSE, 0);
    gtk_widget_ref(hbox1);
    gtk_object_set_data_full(GTK_OBJECT(d), "hbox1", hbox1, (GtkDestroyNotify)counted_unref);
    gtk_widget_show(hbox1);
    gtk_box_pack_start(GTK_BOX(vbox1), hbox1, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox1), 10);

    name_entry = gtk_entry_new();
    gtk_widget_ref(name_entry);
    gtk_object_set_data_full(GTK_OBJECT(d), "idc_setnametxt", name_entry, (GtkDestroyNotify)counted_unref);
    gtk_widget_show(name_entry);
    gtk_box_pack_start(GTK_BOX(hbox1), name_entry, TRUE, TRUE, 0);

    button1 = gtk_button_new_with_label("OK");
    gtk_widget_ref(button1);
    gtk_object_set_data_full(GTK_OBJECT(d), "button1", button1, (GtkDestroyNotify)counted_unref);
    gtk_widget_show(button1);
    gtk_box_pack_start(GTK_BOX(hbox1), button1, TRUE, TRUE, 0);

    gtk_signal_connect(GTK_OBJECT(button1), "clicked", GTK_SIGNAL_FUNC(name_ok), 0);
    gtk_signal_connect(GTK_OBJECT(name_entry), "key-press-event", GTK_SIGNAL_FUNC(name_key), 0);
    gtk_signal_connect(GTK_OBJECT(d), "destroy", GTK_SIGNAL_FUNC(name_destroyed), 0);
    gtk_entry_set_text(GTK_ENTRY(name_entry), "Player");
    return d;
}

/* The user, from a timeout once the dialog's gtk_main is running */
static int script_runs, main_level_seen;
static gboolean script(gpointer data)
{
    (void)data;
    ++script_runs;
    main_level_seen = (int)gtk_main_level();
    gtk_widget_grab_focus(name_entry);
    gtk_editable_select_region(GTK_EDITABLE(name_entry), 0, -1);
    gtkc_key(name_dialog, GDK_BackSpace, 0);
    gtkc_type(name_entry, "Dale");
    gtkc_key(name_dialog, GDK_Return, 0);
    return FALSE;
}

static void test_name_dialog(void)
{
    gint w = 0, h = 0;
    const char *markup;

    name_dialog = name_create();
    CHECK(gtkc_window_olui(name_dialog) == NULL);
    gtk_widget_show(name_dialog);
    CHECK(gtkc_window_olui(name_dialog) != NULL);
    CHECK(name_dialog->window != NULL);
    CHECK(GTK_WIDGET_REALIZED(name_dialog));
    markup = gtkc_window_markup(name_dialog);
    CHECK(markup && strstr(markup, "<input") && strstr(markup, ">OK</button>"));
    CHECK(markup && strstr(markup, "Enter the new player name"));
    CHECK(gtkc_window_size(name_dialog, &w, &h));
    CHECK(w >= 200 && w < 640);
    CHECK(h >= 40 && h < 300);
    CHECK(gtk_grab_get_current() == name_dialog);
    CHECK(!strcmp(olui_get_text(gtkc_widget_el(name_entry)), "Player"));

    g_timeout_add(50, script, NULL);
    gtk_main();
    CHECK(script_runs == 1);
    CHECK(main_level_seen == 1);
    CHECK(gtk_main_level() == 0);
    CHECK(!strcmp(chosen, "Dale"));
    CHECK(key_presses == 6);        /* BackSpace, D, a, l, e, Return */
    CHECK(destroyed == 1);
    CHECK(unrefs == 5);
    CHECK(gtk_grab_get_current() == NULL);
}

/* ---- Check and radio buttons, a label, sensitivity ------------------------------------------ */

static int toggles[4], clicks;
static void on_toggled(GtkWidget *w, gpointer data)
{
    (void)w;
    ++toggles[GPOINTER_TO_INT(data)];
}

static void on_clicked(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    ++clicks;
}

static void test_buttons(void)
{
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL), *vbox = gtk_vbox_new(FALSE, 4);
    GtkWidget *check = gtk_check_button_new_with_label("Sound");
    GtkWidget *r1 = gtk_radio_button_new_with_label(NULL, "Easy");
    GtkWidget *r2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(r1)), "Hard");
    GtkWidget *r3 = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(r1), "Mad");
    GtkWidget *label = gtk_label_new("Ready");
    GtkWidget *go = gtk_button_new_with_label("Go");
    olui *ui;

    gtk_container_add(GTK_CONTAINER(win), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), r1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), r2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), r3, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), go, FALSE, FALSE, 0);
    g_signal_connect(check, "toggled", G_CALLBACK(on_toggled), GINT_TO_POINTER(0));
    g_signal_connect(r1, "toggled", G_CALLBACK(on_toggled), GINT_TO_POINTER(1));
    g_signal_connect(r2, "toggled", G_CALLBACK(on_toggled), GINT_TO_POINTER(2));
    g_signal_connect(r3, "toggled", G_CALLBACK(on_toggled), GINT_TO_POINTER(3));
    g_signal_connect(go, "clicked", G_CALLBACK(on_clicked), NULL);
    CHECK(g_slist_length(gtk_radio_button_group(GTK_RADIO_BUTTON(r2))) == 3);
    CHECK(GTK_TOGGLE_BUTTON(r1)->active && !GTK_TOGGLE_BUTTON(r2)->active);
    gtk_widget_show_all(win);
    ui = gtkc_window_olui(win);
    CHECK(ui != NULL);

    /* the user ticks the box, picks Hard */
    CHECK(gtkc_click(check));
    CHECK(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)));
    CHECK(toggles[0] == 1);
    CHECK(gtkc_click(r2));
    CHECK(GTK_TOGGLE_BUTTON(r2)->active && !GTK_TOGGLE_BUTTON(r1)->active);
    CHECK(toggles[1] == 1 && toggles[2] == 1 && toggles[3] == 0);
    CHECK(olui_get_checked(gtkc_widget_el(r2)) && !olui_get_checked(gtkc_widget_el(r1)));

    /* the program picks Mad: the olui radio follows */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r3), TRUE);
    CHECK(olui_get_checked(gtkc_widget_el(r3)) && !olui_get_checked(gtkc_widget_el(r2)));
    CHECK(toggles[2] == 2 && toggles[3] == 1);
    gtk_toggle_button_set_state(GTK_TOGGLE_BUTTON(check), FALSE);    /* 1.2's name */
    CHECK(!olui_get_checked(gtkc_widget_el(check)) && toggles[0] == 2);

    /* a label's and a button's text, the button's through GTK_BIN()->child */
    gtk_label_set_text(GTK_LABEL(label), "Set");
    CHECK(!strcmp(olui_get_text(gtkc_widget_el(label)), "Set"));
    gtk_label_set_text(GTK_LABEL(GTK_BIN(go)->child), "Start");
    CHECK(!strcmp(olui_get_text(gtkc_widget_el(go)), "Start"));
    {
        gchar *s;
        gtk_label_get(GTK_LABEL(GTK_BIN(go)->child), &s);
        CHECK(!strcmp(s, "Start"));
    }

    /* pressed; then insensitive, it isn't */
    CHECK(gtkc_click(go));
    CHECK(clicks == 1);
    gtk_widget_set_sensitive(vbox, FALSE);
    CHECK(!GTK_WIDGET_IS_SENSITIVE(go));
    gtkc_click(go);
    CHECK(clicks == 1);
    gtk_widget_set_sensitive(vbox, TRUE);
    CHECK(gtkc_click(go));
    CHECK(clicks == 2);

    /* pack_end puts it at the bottom */
    {
        olui_rect rl, rg;
        gint w, h;
        CHECK(olui_box(ui, gtkc_widget_el(label), &rl));
        CHECK(olui_box(ui, gtkc_widget_el(go), &rg));
        CHECK(gtkc_window_size(win, &w, &h));
        CHECK(rg.y >= rl.y + rl.h);
        CHECK(rg.y + rg.h <= h);
    }

    /* hidden, it has no box; shown, it has */
    {
        olui_rect r;
        gtk_widget_hide(label);
        gtkc_flush();
        CHECK(!olui_box(ui, gtkc_widget_el(label), &r));
        gtk_widget_show(label);
        gtkc_flush();
        CHECK(olui_box(ui, gtkc_widget_el(label), &r));
    }
    gtk_widget_destroy(win);
}

/* ---- A window changed while it is open ------------------------------------------------------ */

static GtkWidget *grow_box;
static void add_row(GtkWidget *w, gpointer data)
{
    GtkWidget *l = gtk_label_new("A new row");
    (void)w;
    gtk_box_pack_start(GTK_BOX(grow_box), l, FALSE, FALSE, 0);
    gtk_widget_show(l);
    *(GtkWidget **)data = l;
}

static gboolean keep_open(GtkWidget *w, GdkEvent *e, gpointer data)
{
    (void)w;
    (void)e;
    ++*(int *)data;
    return TRUE;
}

static void test_changes(void)
{
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL), *more = gtk_button_new_with_label("More"), *added = NULL;
    int asked = 0, d = 0;
    guint id;
    grow_box = gtk_vbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(win), grow_box);
    gtk_box_pack_start(GTK_BOX(grow_box), more, FALSE, FALSE, 0);
    gtk_signal_connect(GTK_OBJECT(more), "clicked", GTK_SIGNAL_FUNC(add_row), &added);
    gtk_signal_connect(GTK_OBJECT(win), "destroy", GTK_SIGNAL_FUNC(name_destroyed), NULL);
    id = gtk_signal_connect(GTK_OBJECT(win), "delete_event", GTK_SIGNAL_FUNC(keep_open), &asked);
    gtk_widget_show_all(win);
    CHECK(gtkc_click(more));
    CHECK(added != NULL);
    CHECK(gtkc_widget_el(added) != NULL);       /* built again after the handler */
    CHECK(strstr(gtkc_window_markup(win), "A new row") != NULL);
    CHECK(gtkc_click(more));                    /* the button still works after the build */
    CHECK(strstr(strstr(gtkc_window_markup(win), "A new row") + 1, "A new row") != NULL);

    /* the close gadget: kept open while delete_event says so, then destroyed */
    d = destroyed;
    gtkc_window_close(win);
    CHECK(asked == 1 && destroyed == d);
    CHECK(gtkc_window_olui(win) != NULL);
    gtk_signal_disconnect(GTK_OBJECT(win), id);
    gtkc_window_close(win);
    CHECK(asked == 1 && destroyed == d + 1);
}

/* ---- A table of fields: its columns line up ------------------------------------------------- */

static void test_table(void)
{
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL), *table = gtk_table_new(2, 2, FALSE);
    GtkWidget *l1 = gtk_label_new("Name:"), *l2 = gtk_label_new("Tracker address:");
    GtkWidget *e1 = gtk_entry_new(), *e2 = gtk_entry_new_with_max_length(8);
    olui_rect a, b, c;
    olui *ui;
    gtk_container_add(GTK_CONTAINER(win), table);
    gtk_table_attach(GTK_TABLE(table), l1, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(GTK_TABLE(table), e1, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(GTK_TABLE(table), l2, 0, 1, 1, 2, GTK_FILL, 0, 0, 0);
    gtk_table_attach(GTK_TABLE(table), e2, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_misc_set_alignment(GTK_MISC(l1), 0, 0.5);
    gtk_widget_show_all(win);
    ui = gtkc_window_olui(win);
    CHECK(olui_box(ui, gtkc_widget_el(e1), &a));
    CHECK(olui_box(ui, gtkc_widget_el(e2), &b));
    CHECK(olui_box(ui, gtkc_widget_el(l2), &c));
    CHECK(a.x == b.x && a.w == b.w);            /* the fields line up */
    CHECK(b.y > a.y);
    CHECK(a.x >= c.x + c.w);                    /* the labels' column is as wide as its widest */
    CHECK(strstr(gtkc_window_markup(win), "maxlength=8") != NULL);
    gtkc_type(e2, "123456789");
    CHECK(!strcmp(gtk_entry_get_text(GTK_ENTRY(e2)), "12345678"));
    gtk_widget_destroy(win);
}

/* ---- A modal dialog keeps the others' input ----------------------------------------------- */

static void test_modal(void)
{
    GtkWidget *main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL), *b = gtk_button_new_with_label("Main");
    GtkWidget *dlg = gtk_dialog_new(), *ok = gtk_button_new_with_label("OK");
    int before;
    gtk_container_add(GTK_CONTAINER(main_win), b);
    g_signal_connect(b, "clicked", G_CALLBACK(on_clicked), NULL);
    gtk_widget_show_all(main_win);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), gtk_label_new("Sure?"), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->action_area), ok, TRUE, TRUE, 0);
    g_signal_connect_swapped(ok, "clicked", G_CALLBACK(gtk_widget_destroy), dlg);
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_widget_show_all(dlg);
    CHECK(strstr(gtkc_window_markup(dlg), "Sure?") != NULL);
    before = clicks;
    CHECK(!gtkc_click(b));
    CHECK(clicks == before);
    CHECK(gtkc_click(ok));                      /* destroys the dialog from its own handler */
    CHECK(gtk_grab_get_current() == NULL);
    CHECK(gtkc_click(b));
    CHECK(clicks == before + 1);
    gtk_widget_destroy(main_win);
}

/* ---- Signals' odds and ends ------------------------------------------------------------------ */

static int stopped_first, stopped_second;
static void stopper(GtkWidget *w, gpointer data)
{
    (void)data;
    ++stopped_first;
    gtk_signal_emit_stop_by_name(GTK_OBJECT(w), "clicked");
}

static void second(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    ++stopped_second;
}

static void test_signals(void)
{
    GtkWidget *b = gtk_button_new_with_label("x");
    guint id;
    gtk_signal_connect(GTK_OBJECT(b), "clicked", GTK_SIGNAL_FUNC(stopper), NULL);
    id = gtk_signal_connect(GTK_OBJECT(b), "clicked", GTK_SIGNAL_FUNC(second), NULL);
    gtk_signal_emit_by_name(GTK_OBJECT(b), "clicked");
    CHECK(stopped_first == 1 && stopped_second == 0);
    gtk_signal_disconnect(GTK_OBJECT(b), id);
    gtk_object_set_data(GTK_OBJECT(b), "k", GINT_TO_POINTER(7));
    CHECK(GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(b), "k")) == 7);
    gtk_object_remove_data(GTK_OBJECT(b), "k");
    CHECK(gtk_object_get_data(GTK_OBJECT(b), "k") == NULL);
    gtk_widget_destroy(b);
    CHECK(!strcmp(gdk_keyval_name(GDK_Return), "Return"));
    CHECK(!strcmp(gdk_keyval_name('q'), "q"));
    CHECK(gdk_keyval_to_upper('q') == 'Q');
    CHECK(gdk_keyval_from_name("F1") == GDK_F1);
}

/* ---- glib ------------------------------------------------------------------------------------ */

static int ticks;
static gboolean tick(gpointer data)
{
    (void)data;
    return ++ticks < 3;
}

static gint never(gpointer data)
{
    (void)data;
    ++ticks;
    return TRUE;
}

static void test_glib(void)
{
    GList *l = NULL;
    GSList *s = NULL;
    gchar *t;
    guint32 t0;
    guint id;
    l = g_list_append(l, "b");
    l = g_list_prepend(l, "a");
    l = g_list_append(l, "c");
    CHECK(g_list_length(l) == 3 && !strcmp(g_list_nth_data(l, 2), "c"));
    l = g_list_remove(l, g_list_nth_data(l, 1));
    CHECK(g_list_length(l) == 2 && !strcmp(l->next->data, "c") && l->next->prev == l);
    l = g_list_reverse(l);
    CHECK(!strcmp(l->data, "c"));
    g_list_free(l);
    s = g_slist_append(s, "x");
    s = g_slist_prepend(s, "w");
    CHECK(g_slist_index(s, g_slist_nth_data(s, 1)) == 1 && g_slist_length(s) == 2);
    g_slist_free(s);
    t = g_strdup_printf("%s %d", "tank", 3);
    CHECK(!strcmp(t, "tank 3"));
    g_free(t);
    t = g_strconcat("a", "b", "c", NULL);
    CHECK(!strcmp(t, "abc"));
    g_free(t);
    CHECK(g_strcasecmp("Bolo", "bOLO") == 0);

    /* a timeout three times, at its interval, in the loop's own time */
    t0 = gtkc_now();
    g_timeout_add(100, tick, NULL);
    while (ticks < 3) g_main_iteration(TRUE);
    CHECK(ticks == 3);
    CHECK(gtkc_now() - t0 == 300);
    g_main_iteration(FALSE);
    CHECK(ticks == 3);
    id = gtk_timeout_add(10, never, NULL);
    gtk_timeout_remove(id);
    g_main_iteration(FALSE);
    CHECK(ticks == 3);
}

/* ---- What a handler takes away (run under AddressSanitizer to see these fail) ---------------- */

static GtkWidget *life_win;
static int life_runs;

static void destroy_life_win(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    gtk_widget_destroy(life_win);
}

static gint nested_loop(gpointer data)
{
    (void)data;
    if (++life_runs > 3) return FALSE;      /* run again from inside itself: this would count on */
    gtk_main_iteration_do(FALSE);
    return FALSE;
}

static gboolean key_destroys(GtkWidget *w, GdkEventKey *e, gpointer data)
{
    (void)e;
    (void)data;
    gtk_widget_destroy(w);
    return TRUE;
}

static void toggled_destroys(GtkWidget *w, gpointer data)
{
    (void)data;
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) gtk_widget_destroy(w);
}

static void test_lifetimes(void)
{
    GtkWidget *win, *box, *panel, *label, *b, *r1, *r2, *entry;

    /* a child's "destroy" closes the window */
    life_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    box = gtk_table_new(1, 1, FALSE);
    gtk_container_add(GTK_CONTAINER(life_win), box);
    gtk_widget_show_all(life_win);
    gtkc_flush();
    gtk_signal_connect(GTK_OBJECT(box), "destroy", GTK_SIGNAL_FUNC(destroy_life_win), NULL);
    gtk_widget_destroy(box);
    gtkc_flush();
    CHECK(1);

    /* a loop run from a timeout doesn't run that timeout again */
    life_runs = 0;
    gtk_timeout_add(0, nested_loop, NULL);
    gtk_main_iteration_do(FALSE);
    CHECK(life_runs == 1);

    /* a panel out and back: what it holds has no element from before */
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    box = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);
    panel = gtk_vbox_new(FALSE, 0);
    label = gtk_label_new("x");
    gtk_container_add(GTK_CONTAINER(panel), label);
    gtk_box_pack_start_defaults(GTK_BOX(box), panel);
    gtk_widget_show_all(win);
    gtkc_flush();
    gtk_widget_ref(panel);
    gtk_container_remove(GTK_CONTAINER(box), panel);
    gtkc_flush();
    gtk_box_pack_start_defaults(GTK_BOX(box), panel);
    gtk_widget_unref(panel);
    gtk_label_set_text(GTK_LABEL(label), "new");
    gtkc_flush();
    {
        gchar *now = NULL;
        gtk_label_get(GTK_LABEL(label), &now);
        CHECK(now && !strcmp(now, "new"));
    }

    /* the last reference to something with the grab */
    b = gtk_button_new_with_label("b");
    gtk_box_pack_start_defaults(GTK_BOX(box), b);
    gtk_widget_show(b);
    gtkc_flush();
    gtk_grab_add(b);
    gtk_container_remove(GTK_CONTAINER(box), b);
    gtkc_key(win, 'x', 0);
    CHECK(1);

    /* a radio button destroyed as it is turned off */
    r1 = gtk_radio_button_new_with_label(NULL, "one");
    r2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(r1)), "two");
    gtk_box_pack_start_defaults(GTK_BOX(box), r1);
    gtk_box_pack_start_defaults(GTK_BOX(box), r2);
    gtk_widget_show_all(win);
    gtkc_flush();
    gtk_signal_connect(GTK_OBJECT(r1), "toggled", GTK_SIGNAL_FUNC(toggled_destroys), NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r2), TRUE);
    CHECK(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r2)));
    gtkc_flush();

    /* a key destroys the window part way through what is typed */
    entry = gtk_entry_new();
    gtk_box_pack_start_defaults(GTK_BOX(box), entry);
    gtk_widget_show(entry);
    gtkc_flush();
    gtk_signal_connect(GTK_OBJECT(win), "key_press_event", GTK_SIGNAL_FUNC(key_destroys), NULL);
    CHECK(gtkc_type(entry, "abc"));
    gtkc_flush();
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    test_lifetimes();
    test_name_dialog();
    test_buttons();
    test_changes();
    test_table();
    test_modal();
    test_signals();
    test_glib();
    printf("GFOPENGTK %d checks, %d failed\n", checks, failed);
    printf("GFOPENGTK %s version=%s\n", failed ? "FAIL" : "PASS", GFOPENGTK_VERSION);
    return failed ? 20 : 0;
}
