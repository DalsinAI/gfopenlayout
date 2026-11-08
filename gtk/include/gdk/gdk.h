/* GFOpenGTK's GDK: the events GTK hands its signal handlers (keys, the
 * buttons, focus, a window's close and size) and the key names. There is no
 * drawing here: a GTK program's own drawing (a GtkDrawingArea) goes to the
 * Amiga's RastPort through GFOpenLayout UI's own-widget hook (phase 3).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFOPENGTK_GDK_H
#define GFOPENGTK_GDK_H

#include <glib.h>
#include <gdk/gdkkeysyms.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _GdkWindow GdkWindow;   /* a realized widget's place on the screen (never looked into) */
typedef struct _GdkColormap GdkColormap;
typedef struct _GdkPixmap GdkPixmap;
typedef struct _GdkBitmap GdkBitmap;

typedef struct {
    gint x, y, width, height;
} GdkRectangle;

typedef enum {
    GDK_NOTHING = -1,
    GDK_DELETE = 0,
    GDK_DESTROY = 1,
    GDK_EXPOSE = 2,
    GDK_MOTION_NOTIFY = 3,
    GDK_BUTTON_PRESS = 4,
    GDK_2BUTTON_PRESS = 5,
    GDK_3BUTTON_PRESS = 6,
    GDK_BUTTON_RELEASE = 7,
    GDK_KEY_PRESS = 8,
    GDK_KEY_RELEASE = 9,
    GDK_ENTER_NOTIFY = 10,
    GDK_LEAVE_NOTIFY = 11,
    GDK_FOCUS_CHANGE = 12,
    GDK_CONFIGURE = 13,
    GDK_MAP = 14,
    GDK_UNMAP = 15
} GdkEventType;

typedef enum {
    GDK_SHIFT_MASK = 1 << 0,
    GDK_LOCK_MASK = 1 << 1,
    GDK_CONTROL_MASK = 1 << 2,
    GDK_MOD1_MASK = 1 << 3,     /* Alt */
    GDK_MOD4_MASK = 1 << 6,     /* the Amiga key, where a program asks */
    GDK_BUTTON1_MASK = 1 << 8,
    GDK_BUTTON2_MASK = 1 << 9,
    GDK_BUTTON3_MASK = 1 << 10
} GdkModifierType;

typedef enum {
    GDK_EXPOSURE_MASK = 1 << 1,
    GDK_POINTER_MOTION_MASK = 1 << 2,
    GDK_POINTER_MOTION_HINT_MASK = 1 << 3,
    GDK_BUTTON_MOTION_MASK = 1 << 4,
    GDK_BUTTON_PRESS_MASK = 1 << 8,
    GDK_BUTTON_RELEASE_MASK = 1 << 9,
    GDK_KEY_PRESS_MASK = 1 << 10,
    GDK_KEY_RELEASE_MASK = 1 << 11,
    GDK_FOCUS_CHANGE_MASK = 1 << 14,
    GDK_STRUCTURE_MASK = 1 << 15,
    GDK_ALL_EVENTS_MASK = 0x3ffffe
} GdkEventMask;

typedef struct {
    GdkEventType type;
    GdkWindow *window;
    gint8 send_event;
} GdkEventAny;

typedef struct {
    GdkEventType type;
    GdkWindow *window;
    gint8 send_event;
    guint32 time;
    guint state;                /* GdkModifierType */
    guint keyval;               /* GDK_... (gdkkeysyms.h) */
    gint length;
    gchar *string;              /* the character as text ("" for keys that are none) */
} GdkEventKey;

typedef struct {
    GdkEventType type;
    GdkWindow *window;
    gint8 send_event;
    guint32 time;
    gdouble x, y;
    gdouble *axes;
    guint state;
    guint button;
    gdouble x_root, y_root;
} GdkEventButton;

typedef struct {
    GdkEventType type;
    GdkWindow *window;
    gint8 send_event;
    guint32 time;
    gdouble x, y;
    gdouble *axes;
    guint state;
    gint16 is_hint;
    gdouble x_root, y_root;
} GdkEventMotion;

typedef struct {
    GdkEventType type;
    GdkWindow *window;
    gint8 send_event;
    gint16 in;
} GdkEventFocus;

typedef struct {
    GdkEventType type;
    GdkWindow *window;
    gint8 send_event;
    gint x, y, width, height;
} GdkEventConfigure;

typedef struct {
    GdkEventType type;
    GdkWindow *window;
    gint8 send_event;
    GdkRectangle area;
    gint count;
} GdkEventExpose;

typedef union _GdkEvent {
    GdkEventType type;
    GdkEventAny any;
    GdkEventKey key;
    GdkEventButton button;
    GdkEventMotion motion;
    GdkEventFocus focus_change;
    GdkEventConfigure configure;
    GdkEventExpose expose;
} GdkEvent;

/* A key's name ("Return", "a", "F1") and back; NULL / GDK_VoidSymbol when unknown. */
gchar *gdk_keyval_name(guint keyval);
guint gdk_keyval_from_name(const gchar *name);
guint gdk_keyval_to_upper(guint keyval);
guint gdk_keyval_to_lower(guint keyval);

/* One task drives the UI here: these do nothing. */
#define gdk_threads_init() ((void)0)
#define gdk_threads_enter() ((void)0)
#define gdk_threads_leave() ((void)0)
#define GDK_THREADS_ENTER() ((void)0)
#define GDK_THREADS_LEAVE() ((void)0)
#define gdk_flush() ((void)0)
void gdk_beep(void);

#ifdef __cplusplus
}
#endif
#endif
