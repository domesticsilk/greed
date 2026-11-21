#include "request.h"

#include <ctype.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef struct {
    char *name;
    char *value;
} HeaderLine;

static bool has_ctl(const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

/* Trims one "Name: value" line in place. Empty lines return true with no header. */
static bool parse_header(char *line, HeaderLine *h) {
    while (*line == ' ' || *line == '\t') line++;
    size_t n = strlen(line);
    while (n && (line[n - 1] == ' ' || line[n - 1] == '\t' || line[n - 1] == '\r'))
        line[--n] = 0;
    if (!*line) return true;

    char *colon = strchr(line, ':');
    if (!colon) return false;
    char *name_end = colon;
    while (name_end > line && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
    if (name_end == line) return false;
    for (char *p = line; p < name_end; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || strchr("!#$%&'*+-.^_`|~", c))) return false;
    }

    char *value = colon + 1;
    while (*value == ' ' || *value == '\t') value++;
    if (has_ctl(value)) return false;

    *name_end = 0;
    h->name = line;
    h->value = value;
    return true;
}

bool request_options_empty(const RequestOptions *o) {
    return !o || (!o->referer[0] && !o->user_agent[0] && !o->cookie[0] && !o->headers[0]);
}

void request_options_clear(RequestOptions *o) {
    if (o) memset(o, 0, sizeof *o);
}

bool request_options_validate(const RequestOptions *o, char *err, size_t cap) {
    if (!o) return true;
    if (has_ctl(o->referer) || has_ctl(o->user_agent) || has_ctl(o->cookie)) {
        snprintf(err, cap, "Request header values cannot contain control characters");
        return false;
    }
    char block[REQUEST_HEADERS_MAX];
    snprintf(block, sizeof block, "%s", o->headers);
    char *save = NULL;
    for (char *line = strtok_r(block, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        HeaderLine h;
        if (!parse_header(line, &h)) {
            snprintf(err, cap, "Invalid header \"%s\"; use Name: value", line);
            return false;
        }
    }
    return true;
}

bool request_options_has_header(const RequestOptions *o, const char *name) {
    if (!o || !name || !*name) return false;
    if (o->referer[0] && !strcasecmp(name, "referer")) return true;
    if (o->user_agent[0] && !strcasecmp(name, "user-agent")) return true;
    if (o->cookie[0] && !strcasecmp(name, "cookie")) return true;
    char block[REQUEST_HEADERS_MAX];
    snprintf(block, sizeof block, "%s", o->headers);
    char *save = NULL;
    for (char *line = strtok_r(block, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        HeaderLine h;
        if (parse_header(line, &h) && h.name && !strcasecmp(h.name, name)) return true;
    }
    return false;
}

void request_options_add_headers(struct json_object *headers, const RequestOptions *o) {
    if (!headers || !o) return;
    char line[REQUEST_HEADERS_MAX + 32];
    if (o->referer[0]) {
        snprintf(line, sizeof line, "Referer: %s", o->referer);
        json_object_array_add(headers, json_object_new_string(line));
    }
    if (o->user_agent[0]) {
        snprintf(line, sizeof line, "User-Agent: %s", o->user_agent);
        json_object_array_add(headers, json_object_new_string(line));
    }
    if (o->cookie[0]) {
        snprintf(line, sizeof line, "Cookie: %s", o->cookie);
        json_object_array_add(headers, json_object_new_string(line));
    }

    char block[REQUEST_HEADERS_MAX];
    snprintf(block, sizeof block, "%s", o->headers);
    char *save = NULL;
    for (char *raw = strtok_r(block, "\n", &save); raw; raw = strtok_r(NULL, "\n", &save)) {
        HeaderLine h;
        if (!parse_header(raw, &h) || !h.name) continue;
        if (o->referer[0] && !strcasecmp(h.name, "referer")) continue;
        if (o->user_agent[0] && !strcasecmp(h.name, "user-agent")) continue;
        if (o->cookie[0] && !strcasecmp(h.name, "cookie")) continue;
        snprintf(line, sizeof line, "%s: %s", h.name, h.value);
        json_object_array_add(headers, json_object_new_string(line));
    }
}

void request_options_apply_aria2(struct json_object *opts, const RequestOptions *o) {
    if (!opts || request_options_empty(o)) return;
    struct json_object *headers = json_object_new_array();
    request_options_add_headers(headers, o);
    if (json_object_array_length(headers))
        json_object_object_add(opts, "header", headers);
    else
        json_object_put(headers);
}

/* yt-dlp reads config files with shlex, so quote each value as one token. */
static bool quote_value(char *out, size_t cap, size_t *used, const char *s) {
    if (*used >= cap) return false;
    out[(*used)++] = '\'';
    for (; *s; s++) {
        if (*s == '\'') {
            static const char rep[] = "'\"'\"'";
            for (const char *r = rep; *r; r++) {
                if (*used >= cap) return false;
                out[(*used)++] = *r;
            }
        } else {
            if (*used >= cap) return false;
            out[(*used)++] = *s;
        }
    }
    if (*used >= cap) return false;
    out[(*used)++] = '\'';
    if (*used >= cap) return false;
    out[(*used)++] = '\n';
    return true;
}

static bool config_add(char *buf, size_t cap, size_t *used, const char *opt, const char *value) {
    int n = snprintf(buf + *used, cap - *used, "%s\n", opt);
    if (n < 0 || (size_t)n >= cap - *used) return false;
    *used += (size_t)n;
    return quote_value(buf, cap, used, value);
}

int request_options_ytdlp_config(const RequestOptions *o, char *path, size_t cap) {
    if (request_options_empty(o)) return -1;
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir) dir = "/tmp";
    char tmpl[PATH_MAX];
    snprintf(tmpl, sizeof tmpl, "%s/greed-ytdlp-XXXXXX", dir);
    int fd = mkstemp(tmpl);
    if (fd < 0) return -1;
    unlink(tmpl);

    char buf[REQUEST_HEADERS_MAX + REQUEST_COOKIE_MAX + REQUEST_REFERER_MAX +
             REQUEST_USER_AGENT_MAX + 512];
    size_t used = 0;
    bool ok = true;
    if (o->referer[0]) ok = ok && config_add(buf, sizeof buf, &used, "--referer", o->referer);
    if (o->user_agent[0])
        ok = ok && config_add(buf, sizeof buf, &used, "--user-agent", o->user_agent);
    if (o->cookie[0]) {
        char cookie[REQUEST_COOKIE_MAX + 16];
        snprintf(cookie, sizeof cookie, "Cookie: %s", o->cookie);
        ok = ok && config_add(buf, sizeof buf, &used, "--add-header", cookie);
    }

    char block[REQUEST_HEADERS_MAX];
    snprintf(block, sizeof block, "%s", o->headers);
    char *save = NULL;
    for (char *raw = strtok_r(block, "\n", &save); ok && raw;
         raw = strtok_r(NULL, "\n", &save)) {
        HeaderLine h;
        if (!parse_header(raw, &h) || !h.name) continue;
        if (o->referer[0] && !strcasecmp(h.name, "referer")) continue;
        if (o->user_agent[0] && !strcasecmp(h.name, "user-agent")) continue;
        if (o->cookie[0] && !strcasecmp(h.name, "cookie")) continue;
        char line[REQUEST_HEADERS_MAX + 4];
        snprintf(line, sizeof line, "%s: %s", h.name, h.value);
        ok = config_add(buf, sizeof buf, &used, "--add-header", line);
    }

    if (!ok) {
        close(fd);
        return -1;
    }
    size_t off = 0;
    while (off < used) {
        ssize_t n = write(fd, buf + off, used - off);
        if (n <= 0) {
            close(fd);
            return -1;
        }
        off += (size_t)n;
    }
    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);
    snprintf(path, cap, "/dev/fd/%d", fd);
    return fd;
}
