#ifndef GREED_X11_H
#define GREED_X11_H

#include <stdbool.h>
#include <X11/Xlib.h>

typedef struct {
    Display *dpy;
    Window win;
    int screen;
} GreedFrame;

bool frame_init(GreedFrame *f, void *native_window);
void frame_shutdown(GreedFrame *f);
void frame_make_frameless(GreedFrame *f);
void frame_begin_move(GreedFrame *f);
void frame_begin_resize(GreedFrame *f, int direction);
void frame_minimize(GreedFrame *f);
void frame_toggle_maximize(GreedFrame *f);
bool frame_is_maximized(GreedFrame *f);

/* Raise and focus another process's window on the current display. */
bool x11_activate_window(unsigned long win);

#endif
