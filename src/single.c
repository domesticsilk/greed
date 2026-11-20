#include "single.h"

#include "links.h"
#include "x11.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

static char lock_path[PATH_MAX];
static char handoff_path[PATH_MAX];
static int lock_fd = -1;

static void paths_init(void) {
    if (lock_path[0]) return;
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir) dir = "/tmp";
    snprintf(lock_path, sizeof lock_path, "%s/greed-%d.lock", dir, (int)getuid());
    snprintf(handoff_path, sizeof handoff_path, "%s/greed-%d.handoff", dir, (int)getuid());
}

static void write_lock(void) {
    if (lock_fd < 0) return;
    char buf[256];
    const char *display = getenv("DISPLAY");
    int n = snprintf(buf, sizeof buf, "%d\n0\n%s\n", (int)getpid(), display ? display : "");
    if (n <= 0) return;
    if (ftruncate(lock_fd, 0) == 0) {
        ssize_t ignored = pwrite(lock_fd, buf, (size_t)n, 0);
        (void)ignored;
    }
}

bool single_acquire(void) {
    paths_init();
    lock_fd = open(lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_fd < 0) return true;   /* no runtime dir: run anyway, single-instance is best effort */
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        close(lock_fd);
        lock_fd = -1;
        return false;
    }
    write_lock();
    return true;
}

void single_set_window(unsigned long win) {
    if (lock_fd < 0) return;
    char buf[256];
    const char *display = getenv("DISPLAY");
    int n = snprintf(buf, sizeof buf, "%d\n%lu\n%s\n", (int)getpid(), win,
                     display ? display : "");
    if (n <= 0) return;
    if (ftruncate(lock_fd, 0) == 0) {
        ssize_t ignored = pwrite(lock_fd, buf, (size_t)n, 0);
        (void)ignored;
    }
}

static bool peer(unsigned long *win, char *display, size_t cap) {
    paths_init();
    int fd = open(lock_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    char buf[512];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return false;
    buf[n] = 0;
    int pid = 0;
    unsigned long w = 0;
    char disp[128] = {0};
    if (sscanf(buf, "%d\n%lu\n%127[^\n]", &pid, &w, disp) < 2) return false;
    if (pid <= 0 || (kill((pid_t)pid, 0) != 0 && errno != EPERM)) return false;
    if (win) *win = w;
    if (display && cap) snprintf(display, cap, "%s", disp);
    return true;
}

bool single_focus(void) {
    unsigned long win = 0;
    char display[128] = {0};
    if (!peer(&win, display, sizeof display) || !win) return false;
    const char *cur = getenv("DISPLAY");
    if (cur && display[0] && strcmp(cur, display)) return false;
    return x11_activate_window(win);
}

bool single_handoff(const char *links) {
    paths_init();
    bool focused = single_focus();
    if (!links || !*links) return focused;
    int fd = open(handoff_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return focused;
    if (flock(fd, LOCK_EX) == 0) {
        size_t len = strlen(links);
        bool ok = write(fd, links, len) == (ssize_t)len;
        if (ok && (len == 0 || links[len - 1] != '\n')) ok = write(fd, "\n", 1) == 1;
        flock(fd, LOCK_UN);
        close(fd);
        return focused || ok;
    }
    close(fd);
    return focused;
}

char *single_take_handoff(void) {
    paths_init();
    int fd = open(handoff_path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return NULL;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return NULL;
    }
    char *text = malloc(GREED_LINKS_MAX);
    ssize_t n = text ? read(fd, text, GREED_LINKS_MAX - 1) : -1;
    if (ftruncate(fd, 0) != 0) { /* best effort */ }
    flock(fd, LOCK_UN);
    close(fd);
    if (!text || n <= 0) {
        free(text);
        return NULL;
    }
    text[n] = 0;
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) text[--n] = 0;
    return text;
}

void single_release(void) {
    if (lock_fd >= 0) {
        close(lock_fd);
        lock_fd = -1;
    }
}
