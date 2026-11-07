/* GtkHello: a GTK 1.2 program as it would be written for Linux, built
 * unchanged against GFOpenGTK for AmigaOS 3.x: a window of boxes, a label,
 * an entry, a check button and radio buttons, a timeout ticking, and a
 * modal dialog run in a gtk_main of its own.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <stdio.h>

static const char version[] = "$VER: GtkHello 0.1 (7.11.2026) AmiCradle Team";

static GtkWidget *status, *name, *loud, *clock_label;
static int seconds;

static void say_hello(GtkWidget *w, gpointer data)
{
    char text[96];
    (void)w;
    (void)data;
    snprintf(text, sizeof(text), "Hello, %s%s", gtk_entry_get_text(GTK_ENTRY(name)),
             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(loud)) ? "!!!" : ".");
    gtk_label_set_text(GTK_LABEL(status), text);
}

static void picked(GtkWidget *w, gpointer data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) gtk_label_set_text(GTK_LABEL(status), data);
}

static gint tick(gpointer data)
{
    char text[32];
    (void)data;
    snprintf(text, sizeof(text), "Open for %d s", ++seconds);
    gtk_label_set_text(GTK_LABEL(clock_label), text);
    return TRUE;
}

static GtkWidget *about;
static void about_closed(GtkWidget *w, gpointer data)
{
    (void)w;
    (void)data;
    gtk_grab_remove(about);
    gtk_widget_destroy(about);
    gtk_main_quit();
}

static gboolean about_deleted(GtkWidget *w, GdkEvent *e, gpointer data)
{
    (void)e;
    about_closed(w, data);
    return TRUE;
}

/* A modal dialog in a loop of its own, as LinBolo's dialogs are */
static void show_about(GtkWidget *w, gpointer data)
{
    GtkWidget *ok;
    (void)w;
    (void)data;
    about = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(about), "About GtkHello");
    gtk_window_set_position(GTK_WINDOW(about), GTK_WIN_POS_CENTER);
    gtk_window_set_modal(GTK_WINDOW(about), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(about)->vbox), 10);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(about)->vbox),
                       gtk_label_new("GtkHello " GFOPENGTK_VERSION "\nGTK on GFOpenLayout UI\nfor AmigaOS 3.x"),
                       TRUE, TRUE, 0);
    ok = gtk_button_new_with_label("OK");
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(about)->action_area), ok, TRUE, TRUE, 0);
    gtk_signal_connect(GTK_OBJECT(ok), "clicked", GTK_SIGNAL_FUNC(about_closed), NULL);
    gtk_signal_connect(GTK_OBJECT(about), "delete_event", GTK_SIGNAL_FUNC(about_deleted), NULL);
    gtk_widget_show_all(about);
    gtk_grab_add(about);
    gtk_main();
}

static gboolean quit(GtkWidget *w, GdkEvent *e, gpointer data)
{
    (void)w;
    (void)e;
    (void)data;
    gtk_main_quit();
    return FALSE;
}

static gboolean key(GtkWidget *w, GdkEventKey *e, gpointer data)
{
    (void)w;
    if (e->keyval == GDK_Escape) {
        quit(NULL, NULL, data);
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char **argv)
{
    GtkWidget *window, *vbox, *table, *hbox, *button, *r1, *r2;

    (void)version;
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "GtkHello");
    gtk_container_set_border_width(GTK_CONTAINER(window), 8);
    gtk_signal_connect(GTK_OBJECT(window), "delete_event", GTK_SIGNAL_FUNC(quit), NULL);
    gtk_signal_connect(GTK_OBJECT(window), "key_press_event", GTK_SIGNAL_FUNC(key), NULL);

    vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    table = gtk_table_new(2, 2, FALSE);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_row_spacings(GTK_TABLE(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);
    gtk_table_attach(GTK_TABLE(table), gtk_label_new("Your name:"), 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
    name = gtk_entry_new_with_max_length(24);
    gtk_entry_set_text(GTK_ENTRY(name), "Amiga");
    gtk_signal_connect(GTK_OBJECT(name), "activate", GTK_SIGNAL_FUNC(say_hello), NULL);
    gtk_table_attach(GTK_TABLE(table), name, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_table_attach(GTK_TABLE(table), gtk_label_new("Password:"), 0, 1, 1, 2, GTK_FILL, 0, 0, 0);
    button = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(button), FALSE);
    gtk_table_attach(GTK_TABLE(table), button, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    loud = gtk_check_button_new_with_label("Say it loudly");
    gtk_box_pack_start(GTK_BOX(vbox), loud, FALSE, FALSE, 0);

    hbox = gtk_hbox_new(TRUE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    r1 = gtk_radio_button_new_with_label(NULL, "Workbench");
    r2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(r1)), "Shell");
    gtk_signal_connect(GTK_OBJECT(r1), "toggled", GTK_SIGNAL_FUNC(picked), "Started from Workbench");
    gtk_signal_connect(GTK_OBJECT(r2), "toggled", GTK_SIGNAL_FUNC(picked), "Started from the Shell");
    gtk_box_pack_start(GTK_BOX(hbox), r1, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), r2, TRUE, TRUE, 0);

    status = gtk_label_new("Type a name and press Hello.");
    gtk_misc_set_alignment(GTK_MISC(status), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), status, TRUE, TRUE, 0);
    clock_label = gtk_label_new("Open for 0 s");
    gtk_misc_set_alignment(GTK_MISC(clock_label), 1, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), clock_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_hseparator_new(), FALSE, FALSE, 0);
    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    button = gtk_button_new_with_label("Hello");
    gtk_signal_connect(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(say_hello), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    button = gtk_button_new_with_label("About");
    gtk_signal_connect(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(show_about), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    button = gtk_button_new_with_label("Quit");
    gtk_signal_connect_object(GTK_OBJECT(button), "clicked", GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);

    gtk_timeout_add(1000, tick, NULL);
    gtk_widget_show_all(window);
    gtk_main();
    gtk_widget_destroy(window);
    return 0;
}
