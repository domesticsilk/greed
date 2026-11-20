#include "x11.h"

#include "appicon.h"

#include <X11/Xatom.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Motif window hints, the portable way to ask a window manager for no frame. */
typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
} MotifHints;

#define MWM_HINTS_DECORATIONS (1UL << 1)

static void send_root_message(GreedFrame *f, const char *type, long a, long b, long c, long d,
                              long e) {
    if (!f->dpy) return;
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = f->win;
    ev.xclient.message_type = XInternAtom(f->dpy, type, False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = a;
    ev.xclient.data.l[1] = b;
    ev.xclient.data.l[2] = c;
    ev.xclient.data.l[3] = d;
    ev.xclient.data.l[4] = e;
    XSendEvent(f->dpy, DefaultRootWindow(f->dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(f->dpy);
}

bool frame_init(GreedFrame *f, void *native_window) {
    memset(f, 0, sizeof *f);
    f->dpy = XOpenDisplay(NULL);
    if (!f->dpy) return false;
    f->win = (Window)(uintptr_t)native_window;
    f->screen = DefaultScreen(f->dpy);
    return f->win != 0;
}

void frame_shutdown(GreedFrame *f) {
    if (f->dpy) XCloseDisplay(f->dpy);
    f->dpy = NULL;
}

void frame_make_frameless(GreedFrame *f) {
    if (!f->dpy) return;
    MotifHints hints;
    memset(&hints, 0, sizeof hints);
    hints.flags = MWM_HINTS_DECORATIONS;
    hints.decorations = 0;
    Atom atom = XInternAtom(f->dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(f->dpy, f->win, atom, atom, 32, PropModeReplace, (unsigned char *)&hints, 5);
    atom = XInternAtom(f->dpy, "_NET_WM_ICON", False);
    XChangeProperty(f->dpy, f->win, atom, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)greed_icon,
                    (int)(sizeof greed_icon / sizeof greed_icon[0]));
    XFlush(f->dpy);
}

static void begin_interactive(GreedFrame *f, int direction) {
    if (!f->dpy) return;
    Window root, child;
    int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
    unsigned mask = 0;
    XQueryPointer(f->dpy, f->win, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
    /* _NET_WM_MOVERESIZE: x, y, direction (8 = move, 0..7 = edges), button, source */
    send_root_message(f, "_NET_WM_MOVERESIZE", root_x, root_y, direction, 1, 1);
}

void frame_begin_move(GreedFrame *f) { begin_interactive(f, 8); }

void frame_begin_resize(GreedFrame *f, int direction) { begin_interactive(f, direction); }

void frame_minimize(GreedFrame *f) {
    if (f->dpy) XIconifyWindow(f->dpy, f->win, f->screen);
}

bool frame_is_maximized(GreedFrame *f) {
    if (!f->dpy) return false;
    Atom state = XInternAtom(f->dpy, "_NET_WM_STATE", False);
    Atom vert = XInternAtom(f->dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    Atom horz = XInternAtom(f->dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom type = None;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char *data = NULL;
    if (XGetWindowProperty(f->dpy, f->win, state, 0, 16, False, XA_ATOM, &type, &format, &count,
                           &after, &data) != Success)
        return false;
    bool vert_on = false, horz_on = false;
    for (unsigned long i = 0; i < count; i++) {
        Atom a = ((Atom *)data)[i];
        if (a == vert) vert_on = true;
        if (a == horz) horz_on = true;
    }
    if (data) XFree(data);
    return vert_on || horz_on;
}

void frame_toggle_maximize(GreedFrame *f) {
    if (!f->dpy) return;
    Atom vert = XInternAtom(f->dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    Atom horz = XInternAtom(f->dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    /* Ask for the state we want instead of toggling: a stale property or a WM
     * that already dropped one axis then makes a toggle add it again. Source 1
     * is an application request; some WMs ignore anything else. */
    long action = frame_is_maximized(f) ? 0 : 1;
    send_root_message(f, "_NET_WM_STATE", action, (long)vert, (long)horz, 1, 0);
    XSync(f->dpy, False);
}

bool x11_activate_window(unsigned long win) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return false;
    Window w = (Window)win;
    XMapRaised(dpy, w);
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2; /* source: pager, accepted by stricter WMs */
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
    XCloseDisplay(dpy);
    return true;
}
