#include "checksum.h"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <json-c/json.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define QUEUE_MAX 8

static char queue[QUEUE_MAX][PATH_MAX];
static int queue_count;
static pid_t job_pid = -1;
static int job_fd = -1;
static char job_path[PATH_MAX];
static char job_buf[256];
static int job_len;

/* The details pane asks every frame; remember the last answer until a record changes. */
static char cache_path[PATH_MAX];
static char cache_hex[65];
static int cache_state;
static bool cache_valid;

static const char *store_path(void) {
    static char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/checksums.json", config_dir());
    return path;
}

static struct json_object *store_load(void) {
    struct json_object *v = NULL;
    FILE *f = fopen(store_path(), "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size >= 0) {
            char *text = malloc((size_t)size + 1);
            if (text) {
                size_t got = fread(text, 1, (size_t)size, f);
                text[got] = 0;
                v = json_tokener_parse(text);
                free(text);
            }
        }
        fclose(f);
    }
    if (!v || !json_object_is_type(v, json_type_object)) {
        if (v) json_object_put(v);
        v = json_object_new_object();
    }
    return v;
}

static bool store_save(struct json_object *v) {
    mkdir(config_dir(), 0755);
    FILE *f = fopen(store_path(), "w");
    bool ok = false;
    if (f) {
        ok = fputs(json_object_to_json_string_ext(v, JSON_C_TO_STRING_PRETTY), f) >= 0;
        fputc('\n', f);
        fclose(f);
    }
    json_object_put(v);
    return ok;
}

static void start_next(void) {
    if (job_pid > 0 || queue_count == 0) return;
    snprintf(job_path, sizeof job_path, "%s", queue[0]);
    memmove(queue[0], queue[1], sizeof queue[0] * (size_t)(queue_count - 1));
    queue_count--;

    int fds[2];
    if (pipe(fds) != 0) return;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return;
    }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);
        execlp("sha256sum", "sha256sum", "--", job_path, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    job_pid = pid;
    job_fd = fds[0];
    job_len = 0;
}

bool checksum_queue(const char *path) {
    if (!path || !path[0]) return false;
    if (job_pid > 0 && !strcmp(job_path, path)) return false;
    for (int i = 0; i < queue_count; i++)
        if (!strcmp(queue[i], path)) return false;
    if (queue_count == QUEUE_MAX) return false;
    snprintf(queue[queue_count], PATH_MAX, "%s", path);
    queue_count++;
    return true;
}

int checksum_poll(char *path, size_t path_cap, char *hex, size_t hex_cap) {
    if (job_pid <= 0) start_next();
    if (job_pid <= 0) return 0;

    for (;;) {
        ssize_t got = read(job_fd, job_buf + job_len, sizeof job_buf - 1 - (size_t)job_len);
        if (got > 0) {
            job_len += (int)got;
            if (job_len >= (int)sizeof job_buf - 1) break;
        } else if (got == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        } else {
            break;
        }
    }

    close(job_fd);
    job_fd = -1;
    waitpid(job_pid, NULL, 0);
    job_pid = -1;
    job_buf[job_len] = 0;

    /* sha256sum prints "<64 lowercase hex>  <name>"; keep the hash, ignore the name. */
    char out[65];
    int k = 0;
    for (int i = 0; i < job_len && k < 64; i++) {
        char ch = job_buf[i];
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')) out[k++] = ch;
        else break;
    }
    out[k] = 0;

    snprintf(path, path_cap, "%s", job_path);
    snprintf(hex, hex_cap, "%s", k == 64 ? out : "");
    start_next();
    return 1;
}

int checksum_stored(const char *path, char *hex, size_t cap) {
    if (cache_valid && !strcmp(cache_path, path)) {
        snprintf(hex, cap, "%s", cache_hex);
        return cache_state;
    }
    struct json_object *v = store_load();
    struct json_object *e = NULL, *o = NULL;
    int state = 0;
    if (json_object_object_get_ex(v, path, &e)) {
        const char *h = NULL;
        if (json_object_object_get_ex(e, "sha256", &o)) h = json_object_get_string(o);
        if (h && strlen(h) == 64) {
            snprintf(hex, cap, "%s", h);
            state = 1;
            if (json_object_object_get_ex(e, "verified", &o) && !json_object_get_boolean(o))
                state = 2;
        }
    }
    json_object_put(v);
    snprintf(cache_path, sizeof cache_path, "%s", path);
    snprintf(cache_hex, sizeof cache_hex, "%s", hex);
    cache_state = state;
    cache_valid = true;
    return state;
}

bool checksum_record(const char *path, const char *hex, bool verified) {
    cache_valid = false;
    struct json_object *v = store_load();
    struct json_object *e = json_object_new_object();
    json_object_object_add(e, "sha256", json_object_new_string(hex));
    json_object_object_add(e, "verified", json_object_new_boolean(verified));
    json_object_object_add(v, path, e);
    return store_save(v);
}
