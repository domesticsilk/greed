#include "ytdlp.h"

#include "config.h"
#include "market.h"
#include "rpc.h"

#include <json-c/json.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    char title[256];
    char base[180];
    char ext[16];
    char url[2048];
    char headers[12][512];
    int header_count;
    char audio_id[32];
    bool merge;
    char video_ext[16];
    char audio_url[2048];
    char audio_ext[16];
    char audio_headers[12][512];
    int audio_header_count;
    char note[256];
} Resolved;

typedef enum {
    STAGE_DOWNLOAD = 0,
    STAGE_MERGE,
    STAGE_DONE,
    STAGE_ERROR,
    STAGE_CANCELLED
} Stage;

typedef struct {
    bool used;
    int id;
    bool youtube;
    bool merge;
    char video_gid[32];
    char audio_gid[32];
    char video_path[PATH_MAX];
    char audio_path[PATH_MAX];
    char final_path[PATH_MAX];
    char tmp_path[PATH_MAX];
    char ext[8];
    Stage stage;
    char error[256];
    bool merge_started;
    bool restored;
    time_t last_seen;
    pid_t ffmpeg_pid;
    bool cancel;
} Job;

#define JOBS_MAX 64
static Job jobs[JOBS_MAX];
static pthread_mutex_t jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static int job_seq;

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

static void copy_headers(struct json_object *v, char headers[][512], int *count) {
    struct json_object *h = NULL;
    if (!v || !json_object_object_get_ex(v, "http_headers", &h) ||
        !json_object_is_type(h, json_type_object))
        return;
    json_object_object_foreach(h, key, val) {
        if (*count >= 12) break;
        const char *s = json_object_get_string(val);
        if (!s || !*s) continue;
        snprintf(headers[(*count)++], 512, "%s: %s", key, s);
    }
}

static void safe_base(const char *title, char *out, size_t cap) {
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
    snprintf(out, cap, "%s", base);
}

static bool is_youtube_url(const char *url) {
    if (!url) return false;
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    return !strncasecmp(p, "youtube.com/", 12) || !strncasecmp(p, "www.youtube.com/", 16) ||
           !strncasecmp(p, "m.youtube.com/", 14) || !strncasecmp(p, "music.youtube.com/", 18) ||
           !strncasecmp(p, "youtu.be/", 9);
}

static bool downloadable(struct json_object *f) {
    const char *u = jstr(f, "url");
    if (!u || !u[0]) return false;
    /* aria2 downloads one file, not a DASH or HLS manifest. */
    const char *proto = jstr(f, "protocol");
    if (proto && proto[0] &&
        (strstr(proto, "dash") || strstr(proto, "m3u8") ||
         (strncmp(proto, "http", 4) && strncmp(proto, "ftp", 3))))
        return false;
    return true;
}

static bool missing_codec(const char *c) { return c && !strcmp(c, "none"); }

static bool single_file(struct json_object *f) {
    const char *v = jstr(f, "vcodec");
    const char *a = jstr(f, "acodec");
    if (missing_codec(v) || missing_codec(a)) return false;
    return downloadable(f);
}

int ytdlp_best_format(struct json_object *formats) {
    if (!formats || !json_object_is_type(formats, json_type_array)) return -1;
    int best = -1;
    double best_score = -1;
    int n = (int)json_object_array_length(formats);
    for (int i = 0; i < n; i++) {
        struct json_object *f = json_object_array_get_idx(formats, i);
        if (!single_file(f)) continue;
        double score = jdouble(f, "height") + jdouble(f, "tbr") / 1000.0;
        const char *ext = jstr(f, "ext");
        if (ext && !strcmp(ext, "mp4")) score += 1000;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

bool ytdlp_pick_pair(struct json_object *formats, int max_height, int *video, int *audio) {
    *video = *audio = -1;
    if (!formats || !json_object_is_type(formats, json_type_array)) return false;
    int n = (int)json_object_array_length(formats);
    double best_v = -1, best_a = -1;
    for (int i = 0; i < n; i++) {
        struct json_object *f = json_object_array_get_idx(formats, i);
        if (!downloadable(f)) continue;
        const char *v = jstr(f, "vcodec");
        const char *a = jstr(f, "acodec");
        const char *ext = jstr(f, "ext");

        if (v && !missing_codec(v) && missing_codec(a)) {
            int h = (int)jdouble(f, "height");
            if (max_height > 0 && h > max_height) continue;
            double score = (double)h * 1000.0 + jdouble(f, "tbr");
            if (ext && !strcmp(ext, "mp4")) score += 500;
            if (v && !strncmp(v, "avc1", 4)) score += 200;
            if (score > best_v) {
                best_v = score;
                *video = i;
            }
        } else if (a && !missing_codec(a) && missing_codec(v)) {
            double score = jdouble(f, "abr") * 1000.0 + jdouble(f, "tbr");
            /* m4a keeps the merged file in mp4 instead of mkv. */
            if (ext && (!strcmp(ext, "m4a") || !strcmp(ext, "mp4"))) score += 100000;
            if (a && !strncmp(a, "mp4a", 4)) score += 50000;
            if (score > best_a) {
                best_a = score;
                *audio = i;
            }
        }
    }
    if (*video >= 0 && *audio >= 0) return true;
    *video = *audio = -1;
    return false;
}

bool ytdlp_ffmpeg_available(void) { return market_tool_path("ffmpeg", "ffmpeg") != NULL; }

static int quality_height(YtdlpQuality q) {
    switch (q) {
    case YTDLP_QUALITY_1080: return 1080;
    case YTDLP_QUALITY_720: return 720;
    case YTDLP_QUALITY_480: return 480;
    case YTDLP_QUALITY_360: return 360;
    default: return 0;
    }
}

static bool resolve(const char *url, YtdlpQuality quality, const RequestOptions *req,
                    Resolved *out, char *err, size_t cap) {
    const char *bin = market_tool_path("yt-dlp", "yt-dlp");
    if (!bin) {
        snprintf(err, cap, "yt-dlp is not installed.");
        return false;
    }

    char *args[128];
    int n = 0;
    args[n++] = (char *)bin;
    args[n++] = "-J";
    args[n++] = "--no-playlist";
    args[n++] = "--no-warnings";
    args[n++] = "--socket-timeout";
    args[n++] = "15";
    args[n++] = "--extractor-args";
    args[n++] = "youtube:player_client=default,android";
    int config_fd = -1;
    char config_path[64] = {0};
    if (!request_options_empty(req)) {
        config_fd = request_options_ytdlp_config(req, config_path, sizeof config_path);
        if (config_fd < 0) {
            snprintf(err, cap, "Could not prepare request headers.");
            return false;
        }
        args[n++] = "--config-locations";
        args[n++] = config_path;
    }
    args[n++] = "--";
    args[n++] = (char *)url;
    args[n] = NULL;

    int fds[2];
    if (pipe(fds) != 0) {
        if (config_fd >= 0) close(config_fd);
        snprintf(err, cap, "Could not start yt-dlp.");
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        if (config_fd >= 0) close(config_fd);
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
        /* Default plus android: some networks only expose the combined format
         * through android, while the default clients expose adaptive streams. */
        execv(bin, args);
        _exit(127);
    }
    close(fds[1]);
    if (config_fd >= 0) close(config_fd);

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
    json_object_object_get_ex(root, "formats", &formats);
    int combined = ytdlp_best_format(formats);
    int target = quality_height(quality);
    int vi = -1, ai = -1;
    bool pair = ytdlp_pick_pair(formats, target, &vi, &ai);
    bool can_merge = pair && ytdlp_ffmpeg_available();
    int combined_height = combined >= 0 ? (int)jdouble(json_object_array_get_idx(formats, combined), "height") : -1;
    /* A single file at or above the requested height is simpler and faster. */
    bool use_merge = can_merge && !(target > 0 && combined_height >= target);

    struct json_object *v = NULL, *a = NULL, *c = NULL;
    if (use_merge) {
        v = json_object_array_get_idx(formats, vi);
        a = json_object_array_get_idx(formats, ai);
    } else if (combined >= 0) {
        c = json_object_array_get_idx(formats, combined);
    } else {
        json_object_put(root);
        if (pair)
            snprintf(err, cap, "This video needs ffmpeg to merge its video and audio. "
                               "Install ffmpeg from the Marketplace.");
        else
            snprintf(err, cap, "This link has no single-file stream. Try another video.");
        return false;
    }

    const char *url_out = v ? jstr(v, "url") : c ? jstr(c, "url") : root_url;
    const char *ext = v ? jstr(v, "ext") : c ? jstr(c, "ext") : root_ext;
    if (!url_out || !url_out[0]) {
        json_object_put(root);
        snprintf(err, cap, "This link has no downloadable stream. Try another video.");
        return false;
    }

    memset(out, 0, sizeof *out);
    snprintf(out->title, sizeof out->title, "%s", title ? title : "video");
    safe_base(out->title, out->base, sizeof out->base);
    snprintf(out->ext, sizeof out->ext, "%s", ext && *ext ? ext : "mp4");
    snprintf(out->url, sizeof out->url, "%s", url_out);
    copy_headers(v ? v : c ? c : root, out->headers, &out->header_count);
    out->merge = v != NULL;

    if (v) {
        snprintf(out->video_ext, sizeof out->video_ext, "%s", ext && *ext ? ext : "mp4");
        const char *aurl = jstr(a, "url");
        const char *aext = jstr(a, "ext");
        snprintf(out->audio_url, sizeof out->audio_url, "%s", aurl ? aurl : "");
        snprintf(out->audio_ext, sizeof out->audio_ext, "%s", aext && *aext ? aext : "m4a");
        const char *aiid = jstr(a, "format_id");
        snprintf(out->audio_id, sizeof out->audio_id, "%s", aiid ? aiid : "a");
        copy_headers(a, out->audio_headers, &out->audio_header_count);
        bool mp4 = !strcmp(out->video_ext, "mp4") &&
                   (!strcmp(out->audio_ext, "m4a") || !strcmp(out->audio_ext, "mp4"));
        snprintf(out->ext, sizeof out->ext, "%s", mp4 ? "mp4" : "mkv");
    } else if (!ytdlp_ffmpeg_available() && pair) {
        snprintf(out->note, sizeof out->note,
                 "ffmpeg is not installed; downloaded a single-file stream instead.");
    }

    json_object_put(root);
    return true;
}

static bool add_one(int port, const char *url, const char *dir, const char *name,
                    char headers[][512], int header_count, const RequestOptions *req,
                    char *gid, size_t gidcap, char *err, size_t cap) {
    Rpc rpc;
    rpc_init(&rpc, port);
    struct json_object *opts = json_object_new_object();
    json_object_object_add(opts, "dir", json_object_new_string(dir));
    json_object_object_add(opts, "out", json_object_new_string(name));
    struct json_object *h = json_object_new_array();
    for (int i = 0; i < header_count; i++) {
        char header_name[128];
        size_t n = strcspn(headers[i], ":");
        if (n >= sizeof header_name) n = sizeof header_name - 1;
        memcpy(header_name, headers[i], n);
        while (n && (header_name[n - 1] == ' ' || header_name[n - 1] == '\t')) n--;
        header_name[n] = 0;
        if (request_options_has_header(req, header_name)) continue;
        json_object_array_add(h, json_object_new_string(headers[i]));
    }
    request_options_add_headers(h, req);
    if (json_object_array_length(h)) json_object_object_add(opts, "header", h);
    else json_object_put(h);
    struct json_object *params = json_object_new_array();
    struct json_object *urls = json_object_new_array();
    json_object_array_add(urls, json_object_new_string(url));
    json_object_array_add(params, urls);
    json_object_array_add(params, opts);
    struct json_object *res = rpc_call(&rpc, "aria2.addUri", params);
    if (!res) {
        snprintf(err, cap, "%s", rpc.error[0] ? rpc.error : "aria2 refused the download.");
        return false;
    }
    const char *s = json_object_get_string(res);
    snprintf(gid, gidcap, "%s", s ? s : "");
    json_object_put(res);
    return gid[0] != 0;
}

static void remove_rpc(int port, const char *gid) {
    if (!gid || !gid[0]) return;
    Rpc rpc;
    rpc_init(&rpc, port);
    struct json_object *params = json_object_new_array();
    json_object_array_add(params, json_object_new_string(gid));
    struct json_object *v = rpc_call(&rpc, "aria2.forceRemove", params);
    if (v) json_object_put(v);
    params = json_object_new_array();
    json_object_array_add(params, json_object_new_string(gid));
    v = rpc_call(&rpc, "aria2.removeDownloadResult", params);
    if (v) json_object_put(v);
}

static Job *job_alloc(void) {
    for (int i = 0; i < JOBS_MAX; i++)
        if (!jobs[i].used) return &jobs[i];
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].stage == STAGE_DONE || jobs[i].stage == STAGE_ERROR ||
            jobs[i].stage == STAGE_CANCELLED) {
            memset(&jobs[i], 0, sizeof jobs[i]);
            return &jobs[i];
        }
    return NULL;
}

/* aria2 keeps the two downloads in its session, but the pairing and the
 * merge stage only live in this process. A small journal in the config dir
 * keeps the paths and stage; GIDs are resolved again from the download list. */
#define JOURNAL_SEP '\x1f'

static void journal_path(char *out, size_t cap) {
    snprintf(out, cap, "%s/merge.queue", config_dir());
}

static void journal_save_locked(void) {
    char path[PATH_MAX], tmp[PATH_MAX];
    journal_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    bool any = false;
    for (int i = 0; i < JOBS_MAX; i++) {
        Job *j = &jobs[i];
        if (!j->used || !j->merge) continue;
        if (j->stage != STAGE_DOWNLOAD && j->stage != STAGE_MERGE) continue;
        fprintf(f, "%d%c%d%c%s%c%s%c%s%c%s%c%s\n", (int)j->stage, JOURNAL_SEP,
                j->youtube ? 1 : 0, JOURNAL_SEP, j->ext, JOURNAL_SEP, j->video_path,
                JOURNAL_SEP, j->audio_path, JOURNAL_SEP, j->final_path, JOURNAL_SEP,
                j->tmp_path);
        any = true;
    }
    if (fflush(f) == 0) fsync(fileno(f));
    fclose(f);
    if (!any) {
        unlink(tmp);
        unlink(path);
        return;
    }
    if (rename(tmp, path) != 0) unlink(tmp);
}

static void journal_load(void) {
    char path[PATH_MAX];
    journal_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) > 0) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        char *field[7];
        int n = 0;
        field[n++] = line;
        for (char *p = line; *p && n < 7; p++) {
            if (*p == JOURNAL_SEP) {
                *p = 0;
                field[n++] = p + 1;
            }
        }
        if (n != 7) continue;
        int stage = atoi(field[0]);
        if (stage != STAGE_DOWNLOAD && stage != STAGE_MERGE) continue;
        if (!field[3][0] || !field[4][0] || !field[5][0] || !field[6][0]) continue;
        bool dup = false;
        for (int k = 0; k < JOBS_MAX; k++)
            if (jobs[k].used && !strcmp(jobs[k].audio_path, field[4])) {
                dup = true;
                break;
            }
        if (dup) continue;
        Job *j = job_alloc();
        if (!j) continue;
        memset(j, 0, sizeof *j);
        j->used = true;
        j->id = ++job_seq;
        j->merge = true;
        j->youtube = field[1][0] == '1';
        j->stage = (Stage)stage;
        j->restored = true;
        j->last_seen = time(NULL);
        snprintf(j->ext, sizeof j->ext, "%s", field[2]);
        snprintf(j->video_path, sizeof j->video_path, "%s", field[3]);
        snprintf(j->audio_path, sizeof j->audio_path, "%s", field[4]);
        snprintf(j->final_path, sizeof j->final_path, "%s", field[5]);
        snprintf(j->tmp_path, sizeof j->tmp_path, "%s", field[6]);
    }
    free(line);
    fclose(f);
}

static void job_register(bool merge, const char *video_gid, const char *audio_gid,
                         const char *video_path, const char *audio_path,
                         const char *final_path, const char *tmp_path, const char *ext,
                         bool youtube) {
    pthread_mutex_lock(&jobs_lock);
    Job *j = job_alloc();
    if (j) {
        memset(j, 0, sizeof *j);
        j->used = true;
        j->id = ++job_seq;
        j->merge = merge;
        j->youtube = youtube;
        j->stage = merge ? STAGE_DOWNLOAD : STAGE_DONE;
        snprintf(j->video_gid, sizeof j->video_gid, "%s", video_gid);
        snprintf(j->audio_gid, sizeof j->audio_gid, "%s", audio_gid ? audio_gid : "");
        snprintf(j->video_path, sizeof j->video_path, "%s", video_path ? video_path : "");
        snprintf(j->audio_path, sizeof j->audio_path, "%s", audio_path ? audio_path : "");
        snprintf(j->final_path, sizeof j->final_path, "%s", final_path ? final_path : "");
        snprintf(j->tmp_path, sizeof j->tmp_path, "%s", tmp_path ? tmp_path : "");
        snprintf(j->ext, sizeof j->ext, "%s", ext ? ext : "mp4");
        if (merge) journal_save_locked();
    }
    pthread_mutex_unlock(&jobs_lock);
}

typedef struct {
    int slot;
    int id;
} MergeArg;

static void *merge_worker(void *p) {
    MergeArg *arg = p;
    int slot = arg->slot, id = arg->id;
    free(arg);

    char video[PATH_MAX], audio[PATH_MAX], final[PATH_MAX], tmp[PATH_MAX], ext[8];
    pthread_mutex_lock(&jobs_lock);
    if (!jobs[slot].used || jobs[slot].id != id || jobs[slot].cancel) {
        pthread_mutex_unlock(&jobs_lock);
        return NULL;
    }
    snprintf(video, sizeof video, "%s", jobs[slot].video_path);
    snprintf(audio, sizeof audio, "%s", jobs[slot].audio_path);
    snprintf(final, sizeof final, "%s", jobs[slot].final_path);
    snprintf(tmp, sizeof tmp, "%s", jobs[slot].tmp_path);
    snprintf(ext, sizeof ext, "%s", jobs[slot].ext);
    pthread_mutex_unlock(&jobs_lock);

    const char *ff = market_tool_path("ffmpeg", "ffmpeg");
    if (!ff) {
        pthread_mutex_lock(&jobs_lock);
        if (jobs[slot].used && jobs[slot].id == id) {
            jobs[slot].stage = STAGE_ERROR;
            snprintf(jobs[slot].error, sizeof jobs[slot].error,
                     "ffmpeg is not installed. Install it from the Marketplace.");
            journal_save_locked();
        }
        pthread_mutex_unlock(&jobs_lock);
        return NULL;
    }

    unlink(tmp);
    pid_t pid = fork();
    if (pid == 0) {
        if (!strcmp(ext, "mkv"))
            execl(ff, ff, "-nostdin", "-v", "error", "-y", "-i", video, "-i", audio,
                  "-map", "0:v:0", "-map", "1:a:0", "-c", "copy", "-f", "matroska", tmp,
                  (char *)NULL);
        else
            execl(ff, ff, "-nostdin", "-v", "error", "-y", "-i", video, "-i", audio,
                  "-map", "0:v:0", "-map", "1:a:0", "-c", "copy", "-movflags", "+faststart",
                  tmp, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) {
        pthread_mutex_lock(&jobs_lock);
        if (jobs[slot].used && jobs[slot].id == id) {
            jobs[slot].stage = STAGE_ERROR;
            snprintf(jobs[slot].error, sizeof jobs[slot].error, "Could not start ffmpeg.");
            journal_save_locked();
        }
        pthread_mutex_unlock(&jobs_lock);
        return NULL;
    }

    pthread_mutex_lock(&jobs_lock);
    if (jobs[slot].used && jobs[slot].id == id) jobs[slot].ffmpeg_pid = pid;
    bool cancel = jobs[slot].used && jobs[slot].id == id && jobs[slot].cancel;
    pthread_mutex_unlock(&jobs_lock);
    if (cancel) kill(pid, SIGTERM);

    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;

    pthread_mutex_lock(&jobs_lock);
    if (!jobs[slot].used || jobs[slot].id != id) {
        pthread_mutex_unlock(&jobs_lock);
        unlink(tmp);
        return NULL;
    }
    jobs[slot].ffmpeg_pid = 0;
    cancel = jobs[slot].cancel;
    pthread_mutex_unlock(&jobs_lock);

    ok = ok && !cancel;
    if (ok) {
        if (rename(tmp, final) != 0) ok = false;
        else unlink(audio);
    }
    if (!ok) unlink(tmp);

    pthread_mutex_lock(&jobs_lock);
    if (jobs[slot].used && jobs[slot].id == id) {
        if (ok) jobs[slot].stage = STAGE_DONE;
        else if (cancel) jobs[slot].stage = STAGE_CANCELLED;
        else {
            jobs[slot].stage = STAGE_ERROR;
            snprintf(jobs[slot].error, sizeof jobs[slot].error,
                     "Merging failed. Check that ffmpeg can read both streams.");
        }
        journal_save_locked();
    }
    pthread_mutex_unlock(&jobs_lock);
    return NULL;
}

static bool start_merge(Job *j) {
    if (j->merge_started) return true;
    MergeArg *arg = calloc(1, sizeof *arg);
    if (!arg) return false;
    arg->slot = (int)(j - jobs);
    arg->id = j->id;
    pthread_t t;
    if (pthread_create(&t, NULL, merge_worker, arg) != 0) {
        free(arg);
        return false;
    }
    pthread_detach(t);
    j->merge_started = true;
    return true;
}

static Download *find_gid(Download *items, int n, const char *gid) {
    if (!gid || !gid[0]) return NULL;
    for (int i = 0; i < n; i++)
        if (!strcmp(items[i].gid, gid)) return &items[i];
    return NULL;
}

static bool is_failed(const Download *d) {
    return !strcmp(d->status, "error") || !strcmp(d->status, "removed");
}

static bool file_complete(const char *path) {
    if (!path || !path[0]) return false;
    struct stat sb;
    if (stat(path, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_size == 0) return false;
    char ctl[PATH_MAX];
    snprintf(ctl, sizeof ctl, "%s.aria2", path);
    return access(ctl, F_OK) != 0;
}

/* <title>.greed-<format>-audio.<ext> -> <dir> and <title>. */
static bool audio_base(const char *audio_path, char *dir, size_t dircap, char *base,
                       size_t basecap) {
    const char *slash = strrchr(audio_path, '/');
    const char *name = slash ? slash + 1 : audio_path;
    const char *mark = NULL;
    for (const char *p = name; (p = strstr(p, ".greed-")) != NULL; p += 7)
        mark = p;
    if (!mark) return false;
    if (slash) {
        size_t n = (size_t)(slash - audio_path);
        if (n >= dircap) return false;
        memcpy(dir, audio_path, n);
        dir[n] = 0;
    } else {
        snprintf(dir, dircap, ".");
    }
    size_t n = (size_t)(mark - name);
    if (n >= basecap) return false;
    memcpy(base, name, n);
    base[n] = 0;
    return true;
}

static bool video_name(const char *name, const char *base, const char *ext) {
    size_t n = strlen(base);
    if (strncmp(name, base, n) != 0 || name[n] != '.') return false;
    const char *rest = name + n + 1;
    if (strstr(rest, "greed-")) return false;
    size_t elen = strlen(ext), rlen = strlen(rest);
    if (rlen == elen) return !strcmp(rest, ext);
    if (rlen < elen + 1) return false;
    return rest[rlen - elen - 1] == '.' && !strcmp(rest + rlen - elen, ext);
}

static Download *match_video(Download *items, int n, const Job *j) {
    for (int i = 0; i < n; i++) {
        char full[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", items[i].dir, items[i].name);
        if (!strcmp(full, j->video_path)) return &items[i];
    }
    char dir[PATH_MAX], base[PATH_MAX];
    if (!audio_base(j->audio_path, dir, sizeof dir, base, sizeof base)) return NULL;
    for (int i = 0; i < n; i++) {
        if (strcmp(items[i].dir, dir) != 0) continue;
        if (video_name(items[i].name, base, j->ext)) return &items[i];
    }
    return NULL;
}

static Download *match_audio(Download *items, int n, const Job *j) {
    for (int i = 0; i < n; i++) {
        char full[PATH_MAX];
        snprintf(full, sizeof full, "%s/%s", items[i].dir, items[i].name);
        if (!strcmp(full, j->audio_path)) return &items[i];
    }
    char dir[PATH_MAX], base[PATH_MAX];
    if (!audio_base(j->audio_path, dir, sizeof dir, base, sizeof base)) return NULL;
    size_t nbase = strlen(base);
    for (int i = 0; i < n; i++) {
        if (strcmp(items[i].dir, dir) != 0) continue;
        const char *name = items[i].name;
        if (strncmp(name, base, nbase) != 0 || name[nbase] != '.') continue;
        if (strstr(name + nbase, ".greed-") && strstr(name + nbase, "-audio."))
            return &items[i];
    }
    return NULL;
}

/* When both downloads finished while Greed was closed, aria2 no longer lists
 * them. The newest matching video file is the one downloaded after the audio
 * part was registered; an older file with the same name is left alone. */
static bool newest_video(const char *audio_path, const char *ext, char *out, size_t cap) {
    char dir[PATH_MAX], base[PATH_MAX];
    if (!audio_base(audio_path, dir, sizeof dir, base, sizeof base)) return false;
    DIR *d = opendir(dir);
    if (!d) return false;
    char best[PATH_MAX] = {0};
    time_t best_time = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!video_name(e->d_name, base, ext)) continue;
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat sb;
        if (!file_complete(path) || stat(path, &sb) != 0) continue;
        if (!best[0] || sb.st_mtime >= best_time) {
            best_time = sb.st_mtime;
            snprintf(best, sizeof best, "%s", path);
        }
    }
    closedir(d);
    if (!best[0]) return false;
    snprintf(out, cap, "%s", best);
    return true;
}

void ytdlp_track(Download *items, int n, int port) {
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < JOBS_MAX; i++) {
        Job *j = &jobs[i];
        if (!j->used || !j->merge) continue;

        /* A job restored in the merge stage may have no aria2 rows left. */
        if (j->stage == STAGE_MERGE && !j->merge_started) {
            if (access(j->audio_path, F_OK) != 0 && file_complete(j->final_path)) {
                /* ffmpeg finished before the crash and the audio was removed. */
                j->stage = STAGE_DONE;
                journal_save_locked();
                continue;
            }
            if (!start_merge(j)) {
                j->stage = STAGE_ERROR;
                snprintf(j->error, sizeof j->error, "Could not start ffmpeg.");
                journal_save_locked();
                continue;
            }
        }

        Download *v = find_gid(items, n, j->video_gid);
        Download *a = find_gid(items, n, j->audio_gid);
        if (!v) v = match_video(items, n, j);
        if (!a) a = match_audio(items, n, j);
        bool changed = false;
        if (v) {
            j->last_seen = time(NULL);
            if (strcmp(v->gid, j->video_gid) != 0) {
                snprintf(j->video_gid, sizeof j->video_gid, "%s", v->gid);
                changed = true;
            }
        }
        if (a) {
            j->last_seen = time(NULL);
            if (strcmp(a->gid, j->audio_gid) != 0) {
                snprintf(j->audio_gid, sizeof j->audio_gid, "%s", a->gid);
                changed = true;
            }
        }

        if (!v && j->stage == STAGE_DOWNLOAD && file_complete(j->audio_path)) {
            /* Both downloads finished while Greed was closed; aria2 no longer
             * lists them. Find the video half on disk and merge it. */
            char found[PATH_MAX];
            if (newest_video(j->audio_path, j->ext, found, sizeof found)) {
                snprintf(j->video_path, sizeof j->video_path, "%s", found);
                snprintf(j->final_path, sizeof j->final_path, "%s", found);
                j->stage = STAGE_MERGE;
                journal_save_locked();
                if (!start_merge(j)) {
                    j->stage = STAGE_ERROR;
                    snprintf(j->error, sizeof j->error, "Could not start ffmpeg.");
                    journal_save_locked();
                }
                continue;
            }
        }

        if (!v) {
            /* A restored job with no rows and no usable files would poll
             * forever; give up after a grace period. */
            if (j->restored && j->stage == STAGE_DOWNLOAD && !j->merge_started &&
                time(NULL) - j->last_seen > 30) {
                j->used = false;
                journal_save_locked();
            } else if (changed) {
                journal_save_locked();
            }
            continue;
        }

        /* aria2 renames the video half when the target name exists; follow
         * its name so the merge uses the file that was actually downloaded. */
        if (j->stage == STAGE_DOWNLOAD && v->file_count > 0 && v->name[0]) {
            char actual[PATH_MAX];
            snprintf(actual, sizeof actual, "%s/%s", v->dir, v->name);
            if (strcmp(actual, j->video_path) != 0) {
                snprintf(j->video_path, sizeof j->video_path, "%s", actual);
                snprintf(j->final_path, sizeof j->final_path, "%s", actual);
                changed = true;
            }
        }
        if (a && a->name[0]) {
            char actual[PATH_MAX];
            snprintf(actual, sizeof actual, "%s/%s", a->dir, a->name);
            if (strcmp(actual, j->audio_path) != 0) {
                snprintf(j->audio_path, sizeof j->audio_path, "%s", actual);
                changed = true;
            }
        }
        if (changed) journal_save_locked();

        const char *base = strrchr(j->final_path, '/');
        if (j->final_path[0])
            snprintf(v->name, sizeof v->name, "%s", base ? base + 1 : j->final_path);

        if (j->stage == STAGE_ERROR) {
            snprintf(v->status, sizeof v->status, "error");
            snprintf(v->error, sizeof v->error, "%s", j->error);
            continue;
        }
        if (j->stage == STAGE_DONE) {
            snprintf(v->status, sizeof v->status, "complete");
            struct stat sb;
            if (stat(j->final_path, &sb) == 0) {
                v->total = v->completed = (unsigned long long)sb.st_size;
            }
            v->down_speed = 0;
            v->connections = 0;
            continue;
        }
        if (j->stage == STAGE_MERGE) {
            snprintf(v->status, sizeof v->status, "merging");
            v->down_speed = 0;
            v->connections = 0;
            continue;
        }

        if (is_failed(v) || (a && is_failed(a))) {
            j->stage = STAGE_ERROR;
            snprintf(j->error, sizeof j->error, "%.*s", (int)sizeof j->error - 1,
                     is_failed(v) ? v->error : (a ? a->error : "Download failed."));
            if (is_failed(v) && a) remove_rpc(port, a->gid);
            if (a && is_failed(a) && !is_failed(v)) remove_rpc(port, v->gid);
            snprintf(v->status, sizeof v->status, "error");
            snprintf(v->error, sizeof v->error, "%s", j->error);
            journal_save_locked();
            continue;
        }

        if (a) {
            v->total += a->total;
            v->completed += a->completed;
            v->down_speed += a->down_speed;
            v->connections += a->connections;
        }
        bool vdone = !strcmp(v->status, "complete");
        if (j->restored && vdone && !a && !file_complete(j->audio_path)) {
            j->stage = STAGE_ERROR;
            snprintf(j->error, sizeof j->error,
                     "The audio half is missing. Remove this row and download the video again.");
            snprintf(v->status, sizeof v->status, "error");
            snprintf(v->error, sizeof v->error, "%s", j->error);
            journal_save_locked();
            continue;
        }
        bool adone = a ? !strcmp(a->status, "complete") : file_complete(j->audio_path);
        if (vdone && adone) {
            j->stage = STAGE_MERGE;
            snprintf(v->status, sizeof v->status, "merging");
            v->down_speed = 0;
            v->connections = 0;
            /* The audio half is done; drop its stopped result so it cannot
             * surface as a stray row later. */
            if (j->audio_gid[0]) remove_rpc(port, j->audio_gid);
            journal_save_locked();
            if (!start_merge(j)) {
                j->stage = STAGE_ERROR;
                snprintf(j->error, sizeof j->error, "Could not start ffmpeg.");
                snprintf(v->status, sizeof v->status, "error");
                snprintf(v->error, sizeof v->error, "%s", j->error);
                journal_save_locked();
            }
            continue;
        }
        if (!strcmp(v->status, "active") || (a && !strcmp(a->status, "active")))
            snprintf(v->status, sizeof v->status, "active");
        else if (vdone && a)
            snprintf(v->status, sizeof v->status, "%s", a->status);
    }
    pthread_mutex_unlock(&jobs_lock);
}

bool ytdlp_is_audio_part(const char *gid) {
    if (!gid || !gid[0]) return false;
    bool found = false;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].used && jobs[i].audio_gid[0] && !strcmp(jobs[i].audio_gid, gid)) {
            found = true;
            break;
        }
    pthread_mutex_unlock(&jobs_lock);
    return found;
}

bool ytdlp_is_video(const char *gid) {
    if (!gid || !gid[0]) return false;
    bool found = false;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].used && !strcmp(jobs[i].video_gid, gid)) {
            found = true;
            break;
        }
    pthread_mutex_unlock(&jobs_lock);
    return found;
}

bool ytdlp_is_youtube(const char *gid) {
    if (!gid || !gid[0]) return false;
    bool found = false;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].used && jobs[i].youtube &&
            (!strcmp(jobs[i].video_gid, gid) || (jobs[i].audio_gid[0] && !strcmp(jobs[i].audio_gid, gid)))) {
            found = true;
            break;
        }
    pthread_mutex_unlock(&jobs_lock);
    return found;
}

bool ytdlp_busy(void) {
    bool busy = false;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].used && jobs[i].merge &&
            (jobs[i].stage == STAGE_DOWNLOAD || jobs[i].stage == STAGE_MERGE)) {
            busy = true;
            break;
        }
    pthread_mutex_unlock(&jobs_lock);
    return busy;
}

bool ytdlp_audio_gid(const char *gid, char *out, size_t cap) {
    if (!gid || !gid[0] || !out || !cap) return false;
    bool found = false;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < JOBS_MAX; i++)
        if (jobs[i].used && jobs[i].audio_gid[0] && !strcmp(jobs[i].video_gid, gid)) {
            snprintf(out, cap, "%s", jobs[i].audio_gid);
            found = true;
            break;
        }
    pthread_mutex_unlock(&jobs_lock);
    return found;
}

bool ytdlp_forget(const char *gid, char *audio_gid, size_t cap) {
    if (!gid || !gid[0]) return false;
    bool has_audio = false;
    pthread_mutex_lock(&jobs_lock);
    for (int i = 0; i < JOBS_MAX; i++) {
        Job *j = &jobs[i];
        if (!j->used || (strcmp(j->video_gid, gid) && strcmp(j->audio_gid, gid))) continue;
        j->cancel = true;
        if (j->ffmpeg_pid > 0) kill(j->ffmpeg_pid, SIGTERM);
        if (j->audio_gid[0] && strcmp(j->audio_gid, gid)) {
            has_audio = true;
            if (audio_gid && cap) snprintf(audio_gid, cap, "%s", j->audio_gid);
        }
        j->used = false;
        journal_save_locked();
        break;
    }
    pthread_mutex_unlock(&jobs_lock);
    return has_audio;
}

typedef struct {
    YtdlpJob *job;
    int port;
    char *text;
    char dir[1024];
    YtdlpQuality quality;
    RequestOptions req;
} Arg;

static void set_note(YtdlpJob *job, const char *note) {
    if (!note || !note[0]) return;
    pthread_mutex_lock(&job->lock);
    if (!job->note[0]) snprintf(job->note, sizeof job->note, "%s", note);
    pthread_mutex_unlock(&job->lock);
}

static void *worker(void *p) {
    Arg *arg = p;
    char *copy = arg->text ? strdup(arg->text) : NULL;
    int done = 0, total = 0;
    char last_error[512] = {0};

    char *save = NULL;
    for (char *line = copy ? strtok_r(copy, "\n", &save) : NULL; line;
         line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t') line++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\r' || line[len - 1] == '\t'))
            line[--len] = 0;
        if (!*line) continue;
        total++;

        Resolved r;
        char err[512] = {0};
        if (!resolve(line, arg->quality, &arg->req, &r, err, sizeof err)) {
            snprintf(last_error, sizeof last_error, "%s", err);
            continue;
        }
        set_note(arg->job, r.note);

        char final_name[512], part_video[512], part_audio[512];
        char final_path[PATH_MAX], video_path[PATH_MAX], audio_path[PATH_MAX], tmp_path[PATH_MAX];
        snprintf(final_name, sizeof final_name, "%s.%s", r.base, r.ext);
        snprintf(final_path, sizeof final_path, "%s/%s", arg->dir, final_name);
        snprintf(tmp_path, sizeof tmp_path, "%s/%s.greed-tmp.%s", arg->dir, r.base, r.ext);

        bool ok = false;
        if (r.merge) {
            /* The video half lands on the final name so a finished merge
             * survives a restart without any bookkeeping. */
            snprintf(part_video, sizeof part_video, "%s", final_name);
            snprintf(part_audio, sizeof part_audio, "%s.greed-%s-audio.%s", r.base, r.audio_id,
                     r.audio_ext);
            snprintf(video_path, sizeof video_path, "%s/%s", arg->dir, part_video);
            snprintf(audio_path, sizeof audio_path, "%s/%s", arg->dir, part_audio);
            char vgid[32] = {0}, agid[32] = {0};
            if (add_one(arg->port, r.url, arg->dir, part_video, r.headers, r.header_count,
                        &arg->req, vgid, sizeof vgid, err, sizeof err) &&
                add_one(arg->port, r.audio_url, arg->dir, part_audio, r.audio_headers,
                        r.audio_header_count, &arg->req, agid, sizeof agid, err, sizeof err)) {
                job_register(true, vgid, agid, video_path, audio_path, final_path, tmp_path,
                             r.ext, is_youtube_url(line));
                ok = true;
            } else if (vgid[0]) {
                remove_rpc(arg->port, vgid);
            }
        } else {
            char gid[32] = {0};
            if (add_one(arg->port, r.url, arg->dir, final_name, r.headers, r.header_count,
                        &arg->req, gid, sizeof gid, err, sizeof err)) {
                job_register(false, gid, NULL, NULL, NULL, final_path, NULL, r.ext,
                             is_youtube_url(line));
                ok = true;
            }
        }
        if (!ok) {
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
    pthread_mutex_lock(&jobs_lock);
    memset(jobs, 0, sizeof jobs);
    job_seq = 0;
    journal_load();
    pthread_mutex_unlock(&jobs_lock);
}

bool ytdlp_available(void) { return market_tool_path("yt-dlp", "yt-dlp") != NULL; }

bool ytdlp_start(YtdlpJob *j, int rpc_port, const char *text, const char *dir,
                 YtdlpQuality quality, const RequestOptions *req) {
    pthread_mutex_lock(&j->lock);
    bool busy = j->running;
    if (!busy) {
        j->running = true;
        j->done = 0;
        j->total = 0;
        j->error[0] = 0;
        j->note[0] = 0;
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
    arg->quality = quality;
    if (req) arg->req = *req;
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

void ytdlp_snapshot(YtdlpJob *j, bool *running, int *done, int *total, char *error, size_t cap,
                    char *note, size_t note_cap) {
    pthread_mutex_lock(&j->lock);
    if (running) *running = j->running;
    if (done) *done = j->done;
    if (total) *total = j->total;
    if (error && cap) snprintf(error, cap, "%s", j->error);
    if (note && note_cap) snprintf(note, note_cap, "%s", j->note);
    pthread_mutex_unlock(&j->lock);
}
