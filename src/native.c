#include "native.h"

#include "browser.h"
#include "links.h"
#include "single.h"
#include "version.h"

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool read_exact(void *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(STDIN_FILENO, (char *)buf + got, len - got);
        if (n <= 0) return false;
        got += (size_t)n;
    }
    return true;
}

static void write_message(const char *json) {
    size_t len = strlen(json);
    unsigned char hdr[4] = {
        (unsigned char)(len & 0xff),
        (unsigned char)((len >> 8) & 0xff),
        (unsigned char)((len >> 16) & 0xff),
        (unsigned char)((len >> 24) & 0xff),
    };
    size_t sent = 0;
    while (sent < sizeof hdr) {
        ssize_t n = write(STDOUT_FILENO, hdr + sent, sizeof hdr - sent);
        if (n <= 0) return;
        sent += (size_t)n;
    }
    sent = 0;
    while (sent < len) {
        ssize_t n = write(STDOUT_FILENO, json + sent, len - sent);
        if (n <= 0) return;
        sent += (size_t)n;
    }
}

/* Chrome starts the host with no terminal; keep the GUI off its pipes. */
static void spawn_gui(void) {
    char exe[PATH_MAX];
    const char *app = getenv("APPIMAGE");
    const char *path = app && *app ? app : NULL;
    if (!path) {
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n <= 0) return;
        exe[n] = 0;
        path = exe;
    }
    pid_t pid = fork();
    if (pid != 0) return;
    setsid();
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) close(fd);
    }
    execl(path, path, (char *)NULL);
    _exit(1);
}

int native_host_run(void) {
    unsigned char hdr[4];
    if (!read_exact(hdr, sizeof hdr)) return 1;
    uint32_t len = (uint32_t)hdr[0] | (uint32_t)hdr[1] << 8 | (uint32_t)hdr[2] << 16 |
                   (uint32_t)hdr[3] << 24;
    if (len == 0 || len > 1024 * 1024) return 1;

    char *json = malloc((size_t)len + 1);
    if (!json) return 1;
    if (!read_exact(json, len)) {
        free(json);
        return 1;
    }
    json[len] = 0;

    char links[GREED_LINKS_MAX], error[256];
    bool ping = false;
    if (!browser_parse_message(json, links, sizeof links, &ping, error, sizeof error)) {
        char reply[512];
        snprintf(reply, sizeof reply, "{\"ok\":false,\"error\":\"%s\"}", error);
        write_message(reply);
        free(json);
        return 0;
    }
    free(json);

    if (ping) {
        write_message("{\"ok\":true,\"version\":\"" GREED_VERSION "\"}");
        return 0;
    }

    bool running = single_running();
    single_handoff(links);
    write_message("{\"ok\":true}");
    if (!running) spawn_gui();
    return 0;
}
