#include "links.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n > m && !strcmp(s + n - m, suffix);
}

bool greed_link_arg(const char *s) {
    if (!s || !*s) return false;
    if (!strncmp(s, "magnet:", 7)) return true;
    if (strstr(s, "://")) return true;
    return has_suffix(s, ".torrent") || has_suffix(s, ".metalink") || has_suffix(s, ".meta4");
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* file://host/path with %XX escapes to a local path; other arguments are copied. */
static void to_path(const char *s, char *out, size_t cap) {
    size_t o = 0;
    if (strncmp(s, "file://", 7)) {
        snprintf(out, cap, "%s", s);
        return;
    }
    const char *p = s + 7;
    while (*p && *p != '/') p++;
    for (; *p && o + 1 < cap; p++) {
        int hi, lo;
        if (*p == '%' && p[1] && p[2] && (hi = hex_value(p[1])) >= 0 &&
            (lo = hex_value(p[2])) >= 0) {
            out[o++] = (char)(hi * 16 + lo);
            p += 2;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = 0;
}

char *greed_join_links(int argc, char **argv) {
    char *out = malloc(GREED_LINKS_MAX);
    if (!out) return NULL;
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < argc; i++) {
        char path[2048];
        to_path(argv[i], path, sizeof path);
        if (!path[0]) continue;
        size_t len = strlen(path);
        if (used + (used ? 1 : 0) + len + 1 > GREED_LINKS_MAX) break;
        if (used) out[used++] = '\n';
        memcpy(out + used, path, len);
        used += len;
        out[used] = 0;
    }
    if (!used) {
        free(out);
        return NULL;
    }
    return out;
}
