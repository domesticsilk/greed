#include "ytdlp.h"

#include "market.h"
#include "rpc.h"

#include <json-c/json.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char title[256];
    char url[2048];
    char ext[16];
    char headers[12][512];
    int header_count;
} Resolved;

static const char *jstr(struct json_object *v, const char *key) {
    struct json_object *o = NULL;
    if (!v || !json_object_object_get_ex(v, key, &o)) return NULL;
    return json_object_get_string(o);
}

static double jdouble(struct json_object *v, const char *key) {
    struct json_object *o = NULL;
    if (!v || !json_object_object_get_ex(v, key, &o)) return 0;
    if (json_object_is_type(o, json_type_double) || json_object_is_type(o, json_type_int))
        return json_object_get_double(o);
    const char *s = json_object_get_string(o);
    return s ? strtod(s, NULL) : 0;
}

static void copy_headers(struct json_object *v, Resolved *out) {
    struct json_object *h = NULL;
    if (!v || !json_object_object_get_ex(v, "http_headers", &h) ||
        !json_object_is_type(h, json_type_object))
        return;
    json_object_object_foreach(h, key, val) {
        if (out->header_count >= 12) break;
        const char *s = json_object_get_string(val);
        if (!s || !*s) continue;
        snprintf(out->headers[out->header_count++], sizeof out->headers[0], "%s: %s", key, s);
    }
}

static void safe_name(const char *title, const char *ext, char *out, size_t cap) {
    char base[180];
    size_t o = 0;
    if (!title) title = "";
    for (const char *p = title; *p && o + 1 < sizeof base; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c < 32)
            c = '_';
        base[o++] = (char)c;
    }
    while (o && (base[o - 1] == ' ' || base[o - 1] == '.')) o--;
    base[o] = 0;
    if (!o) snprintf(base, sizeof base, "video");
    if (ext && *ext) snprintf(out, cap, "%s.%s", base, ext);
    else snprintf(out, cap, "%s", base);
}

static bool resolve(const char *url, Resolved *out, char *err, size_t cap) {
    const char *bin = market_tool_path("yt-dlp", "yt-dlp");
    if (!bin) {
        snprintf(err, cap, "yt-dlp is not installed.");
        return false;
    }

    int fds[2];
    if (pipe(fds) != 0) {
        snprintf(err, cap, "Could not start yt-dlp.");
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        snprintf(err, cap, "Could not start yt-dlp.");
        return false;
    }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        alarm(90);
        execl(bin, bin, "-J", "--no-playlist", "--no-warnings", "--socket-timeout", "15",
              "--", url, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);

    char *data = NULL;
    size_t len = 0, size = 0;
    char chunk[8192];
    ssize_t got;
    while ((got = read(fds[0], chunk, sizeof chunk)) > 0) {
        if (len + (size_t)got > 16 * 1024 * 1024) break;
        if (len + (size_t)got + 1 > size) {
            size_t next = size ? size * 2 : 65536;
            while (next < len + (size_t)got + 1) next *= 2;
            char *p = realloc(data, next);
            if (!p) break;
            data = p;
            size = next;
        }
        memcpy(data + len, chunk, (size_t)got);
        len += (size_t)got;
    }
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (data) data[len] = 0;

    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0 || !data || !len) {
        free(data);
        snprintf(err, cap, "yt-dlp could not read this link.");
        return false;
    }

    struct json_object *root = json_tokener_parse(data);
    free(data);
    if (!root) {
        snprintf(err, cap, "yt-dlp sent an unexpected reply.");
        return false;
    }

    const char *title = jstr(root, "title");
    const char *root_url = jstr(root, "url");
    const char *root_ext = jstr(root, "ext");
    struct json_object *formats = NULL;
    int best = -1;
    double best_score = -1;

    if (json_object_object_get_ex(root, "formats", &formats) &&
        json_object_is_type(formats, json_type_array)) {
        int n = (int)json_object_array_length(formats);
        for (int i = 0; i < n; i++) {
            struct json_object *f = json_object_array_get_idx(formats, i);
            const char *v = jstr(f, "vcodec");
            const char *a = jstr(f, "acodec");
            const char *u = jstr(f, "url");
            if (!u || !u[0] || !v || !a || !strcmp(v, "none") || !strcmp(a, "none"))
                continue;
            double score = jdouble(f, "height") + jdouble(f, "tbr") / 1000.0;
            const char *ext = jstr(f, "ext");
            if (ext && !strcmp(ext, "mp4")) score += 1000;
            if (score > best_score) {
                best_score = score;
                best = i;
            }
        }
    }

    struct json_object *chosen = best >= 0 ? json_object_array_get_idx(formats, best) : NULL;
    const char *url_out = chosen ? jstr(chosen, "url") : root_url;
    const char *ext = chosen ? jstr(chosen, "ext") : root_ext;
    if (!url_out || !url_out[0]) {
        json_object_put(root);
        snprintf(err, cap, "This link has no single-file stream. Try another video.");
        return false;
    }

    snprintf(out->title, sizeof out->title, "%s", title ? title : "video");
    snprintf(out->url, sizeof out->url, "%s", url_out);
    snprintf(out->ext, sizeof out->ext, "%s", ext && *ext ? ext : "mp4");
    copy_headers(chosen ? chosen : root, out);
    json_object_put(root);
    return true;
}

static bool add_to_aria2(int port, const Resolved *r, const char *dir, char *err, size_t cap) {
    Rpc rpc;
    rpc_init(&rpc, port);
    struct json_object *opts = json_object_new_object();
    json_object_object_add(opts, "dir", json_object_new_string(dir));
    char name[256];
    safe_name(r->title, r->ext, name, sizeof name);
    json_object_object_add(opts, "out", json_object_new_string(name));
    if (r->header_count) {
        struct json_object *headers = json_object_new_array();
        for (int i = 0; i < r->header_count; i++)
            json_object_array_add(headers, json_object_new_string(r->headers[i]));
        json_object_object_add(opts, "header", headers);
    }
    struct json_object *params = json_object_new_array();
    struct json_object *urls = json_object_new_array();
    json_object_array_add(urls, json_object_new_string(r->url));
    json_object_array_add(params, urls);
    json_object_array_add(params, opts);
    struct json_object *res = rpc_call(&rpc, "aria2.addUri", params);
    if (!res) {
        snprintf(err, cap, "%s", rpc.error[0] ? rpc.error : "aria2 refused the download.");
        return false;
    }
    json_object_put(res);
    return true;
}

typedef struct {
    YtdlpJob *job;
    int port;
    char *text;
    char dir[1024];
} Arg;

static void *worker(void *p) {
    Arg *arg = p;
    char *copy = arg->text ? strdup(arg->text) : NULL;
    int done = 0, total = 0;
    char last_error[512] = {0};

    for (char *line = copy ? strtok(copy, "\n") : NULL; line; line = strtok(NULL, "\n")) {
        while (*line == ' ' || *line == '\t') line++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\r' || line[len - 1] == '\t'))
            line[--len] = 0;
        if (!*line) continue;
        total++;

        Resolved r = {0};
        char err[512] = {0};
        if (!resolve(line, &r, err, sizeof err) ||
            !add_to_aria2(arg->port, &r, arg->dir, err, sizeof err)) {
            snprintf(last_error, sizeof last_error, "%s", err);
            continue;
        }
        done++;
        pthread_mutex_lock(&arg->job->lock);
        arg->job->done = done;
        arg->job->total = total;
        pthread_mutex_unlock(&arg->job->lock);
    }

    pthread_mutex_lock(&arg->job->lock);
    arg->job->running = false;
    arg->job->done = done;
    arg->job->total = total;
    snprintf(arg->job->error, sizeof arg->job->error, "%s", done ? "" : last_error);
    pthread_mutex_unlock(&arg->job->lock);

    free(copy);
    free(arg->text);
    free(arg);
    return NULL;
}

void ytdlp_init(YtdlpJob *j) {
    memset(j, 0, sizeof *j);
    pthread_mutex_init(&j->lock, NULL);
}

bool ytdlp_available(void) { return market_tool_path("yt-dlp", "yt-dlp") != NULL; }

bool ytdlp_start(YtdlpJob *j, int rpc_port, const char *text, const char *dir) {
    pthread_mutex_lock(&j->lock);
    bool busy = j->running;
    if (!busy) {
        j->running = true;
        j->done = 0;
        j->total = 0;
        j->error[0] = 0;
    }
    pthread_mutex_unlock(&j->lock);
    if (busy) return false;

    Arg *arg = calloc(1, sizeof *arg);
    if (!arg) {
        pthread_mutex_lock(&j->lock);
        j->running = false;
        snprintf(j->error, sizeof j->error, "Could not start the video task.");
        pthread_mutex_unlock(&j->lock);
        return false;
    }
    arg->job = j;
    arg->port = rpc_port;
    arg->text = strdup(text ? text : "");
    snprintf(arg->dir, sizeof arg->dir, "%s", dir);
    if (!arg->text) {
        free(arg);
        pthread_mutex_lock(&j->lock);
        j->running = false;
        snprintf(j->error, sizeof j->error, "Could not start the video task.");
        pthread_mutex_unlock(&j->lock);
        return false;
    }

    pthread_t t;
    if (pthread_create(&t, NULL, worker, arg) != 0) {
        free(arg->text);
        free(arg);
        pthread_mutex_lock(&j->lock);
        j->running = false;
        snprintf(j->error, sizeof j->error, "Could not start the video task.");
        pthread_mutex_unlock(&j->lock);
        return false;
    }
    pthread_detach(t);
    return true;
}

void ytdlp_snapshot(YtdlpJob *j, bool *running, int *done, int *total, char *error,
                    size_t cap) {
    pthread_mutex_lock(&j->lock);
    if (running) *running = j->running;
    if (done) *done = j->done;
    if (total) *total = j->total;
    if (error && cap) snprintf(error, cap, "%s", j->error);
    pthread_mutex_unlock(&j->lock);
}
