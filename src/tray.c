#include "tray.h"

#include "appicon.h"
#include "config.h"
#include "rpc.h"
#include "single.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define TRAY_SIZE 24
#define TRAY_POLL_SECONDS 2
#define TRAY_DOCK_TIMEOUT 10

void tray_spawn(void) {
    const char *display = getenv("DISPLAY");
    if (!display || !*display) return;
    pid_t pid = fork();
    if (pid != 0) return;
    setsid();
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
    execl("/proc/self/exe", "greed", "tray", (char *)NULL);
    _exit(127);
}

static bool tray_lock(void) {
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir) dir = "/tmp";
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/greed-%d-tray.lock", dir, (int)getuid());
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return true;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return false;
    }
    return true; /* the open fd holds the lock for the process lifetime */
}

static bool tray_counts(Rpc *rpc, unsigned long *active, unsigned long *waiting) {
    struct json_object *v = rpc_call(rpc, "aria2.getGlobalStat", NULL);
    if (!v) return false;
    struct json_object *o = json_object_object_get(v, "numActive");
    *active = o ? strtoul(json_object_get_string(o), NULL, 10) : 0;
    o = json_object_object_get(v, "numWaiting");
    *waiting = o ? strtoul(json_object_get_string(o), NULL, 10) : 0;
    json_object_put(v);
    return true;
}

static void set_title(Display *dpy, Window win, const char *title) {
    XStoreName(dpy, win, title);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom net = XInternAtom(dpy, "_NET_WM_NAME", False);
    XChangeProperty(dpy, win, net, utf8, 8, PropModeReplace,
                    (const unsigned char *)title, (int)strlen(title));
}

static void draw_icon(Display *dpy, Window win, int w, int h, bool shape) {
    if (w <= 0 || h <= 0) return;
    int screen = DefaultScreen(dpy);
    XImage *img = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen),
                               ZPixmap, 0, NULL, (unsigned)w, (unsigned)h, 32, 0);
    if (!img) return;
    if (!img->data) {
        img->data = calloc((size_t)img->bytes_per_line * (size_t)h, 1);
        if (!img->data) {
            XDestroyImage(img);
            return;
        }
    } else {
        memset(img->data, 0, (size_t)img->bytes_per_line * (size_t)h);
    }

    const unsigned long *src = greed_icon + 2 + 128 * 128 + 2; /* the 64x64 icon */
    Pixmap mask = None;
    GC mgc = NULL;
    if (shape) {
        mask = XCreatePixmap(dpy, win, (unsigned)w, (unsigned)h, 1);
        mgc = XCreateGC(dpy, mask, 0, NULL);
        XSetForeground(dpy, mgc, 0);
        XFillRectangle(dpy, mask, mgc, 0, 0, (unsigned)w, (unsigned)h);
        XSetForeground(dpy, mgc, 1);
    }
    for (int y = 0; y < h; y++) {
        int sy = y * 64 / h;
        for (int x = 0; x < w; x++) {
            int sx = x * 64 / w;
            unsigned long p = src[sy * 64 + sx];
            XPutPixel(img, x, y, p & 0xffffff);
            if (shape && ((p >> 24) & 0xff) >= 128) XDrawPoint(dpy, mask, mgc, x, y);
        }
    }

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XPutImage(dpy, win, gc, img, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
    if (shape) {
        XShapeCombineMask(dpy, win, ShapeBounding, 0, 0, mask, ShapeSet);
        XFreeGC(dpy, mgc);
        XFreePixmap(dpy, mask);
    }
    XFreeGC(dpy, gc);
    XDestroyImage(img);
}

static void open_gui(void) {
    pid_t pid = fork();
    if (pid != 0) return;
    setsid();
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
    execl("/proc/self/exe", "greed", (char *)NULL);
    _exit(127);
}

int tray_run(void) {
    const char *display = getenv("DISPLAY");
    if (!display || !*display) return 0;
    if (!tray_lock()) return 0;

    Config cfg;
    config_load(&cfg);
    Rpc rpc;
    rpc_init(&rpc, cfg.rpc_port);
    unsigned long active = 0, waiting = 0;
    if (!tray_counts(&rpc, &active, &waiting) || active + waiting == 0) return 0;

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 0;
    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    char selection_name[64];
    snprintf(selection_name, sizeof selection_name, "_NET_SYSTEM_TRAY_S%d", screen);
    Atom selection = XInternAtom(dpy, selection_name, False);
    Window owner = XGetSelectionOwner(dpy, selection);
    if (owner == None) {
        XCloseDisplay(dpy);
        return 0;
    }

    int shape_event = 0, shape_error = 0;
    bool shape = XShapeQueryExtension(dpy, &shape_event, &shape_error);

    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof attrs);
    attrs.background_pixel = 0;
    attrs.event_mask = ButtonReleaseMask | ExposureMask | StructureNotifyMask;
    Window win = XCreateWindow(dpy, root, 0, 0, TRAY_SIZE, TRAY_SIZE, 0, CopyFromParent,
                               InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &attrs);
    set_title(dpy, win, "Greed");
    Atom xembed_info = XInternAtom(dpy, "_XEMBED_INFO", False);
    long embed[2] = {0, 1}; /* version 0, XEMBED_MAPPED */
    XChangeProperty(dpy, win, xembed_info, xembed_info, 32, PropModeReplace,
                    (const unsigned char *)embed, 2);
    Atom icon = XInternAtom(dpy, "_NET_WM_ICON", False);
    XChangeProperty(dpy, win, icon, XA_CARDINAL, 32, PropModeReplace,
                    (const unsigned char *)greed_icon,
                    (int)(sizeof greed_icon / sizeof greed_icon[0]));

    Atom opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = owner;
    ev.xclient.message_type = opcode;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = 0; /* SYSTEM_TRAY_REQUEST_DOCK */
    ev.xclient.data.l[2] = (long)win;
    XSendEvent(dpy, owner, False, NoEventMask, &ev);
    XFlush(dpy);

    time_t started = time(NULL);
    time_t last_poll = 0;
    bool docked = false;
    for (;;) {
        while (XPending(dpy)) {
            XEvent e;
            XNextEvent(dpy, &e);
            switch (e.type) {
            case Expose:
                if (e.xexpose.window == win && e.xexpose.count == 0) {
                    Window child;
                    int x, y;
                    unsigned w, h, bw, depth;
                    if (XGetGeometry(dpy, win, &child, &x, &y, &w, &h, &bw, &depth))
                        draw_icon(dpy, win, (int)w, (int)h, shape);
                }
                break;
            case ConfigureNotify:
                if (e.xconfigure.window == win)
                    draw_icon(dpy, win, e.xconfigure.width, e.xconfigure.height, shape);
                break;
            case ReparentNotify:
                if (e.xreparent.window == win) docked = true;
                break;
            case MapNotify:
                if (e.xmap.window == win) docked = true;
                break;
            case ButtonRelease:
                if (e.xbutton.window == win && e.xbutton.button == Button1) open_gui();
                break;
            case DestroyNotify:
                if (e.xdestroywindow.window == win) {
                    XCloseDisplay(dpy);
                    return 0;
                }
                break;
            }
        }

        time_t now = time(NULL);
        if (now - last_poll >= TRAY_POLL_SECONDS) {
            last_poll = now;
            if (single_running()) break;
            if (!tray_counts(&rpc, &active, &waiting) || active + waiting == 0) break;
            char title[64];
            unsigned long count = active + waiting;
            snprintf(title, sizeof title, "Greed - %lu download%s", count,
                     count == 1 ? "" : "s");
            set_title(dpy, win, title);
        }
        if (!docked && now - started >= TRAY_DOCK_TIMEOUT) break;

        fd_set fds;
        FD_ZERO(&fds);
        int xfd = ConnectionNumber(dpy);
        FD_SET(xfd, &fds);
        struct timeval tv = {1, 0};
        select(xfd + 1, &fds, NULL, NULL, &tv);
    }

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
