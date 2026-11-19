#include "ui.h"

#include "checksum.h"
#include "config.h"
#include "logo.h"
#include "model.h"
#include "rpc.h"
#include "theme.h"
#include "x11.h"

#include "cui.h"

#include <limits.h>
#include <math.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { TAB_ALL, TAB_ACTIVE, TAB_WAITING, TAB_STOPPED, TAB_COMPLETE, TAB_COUNT };
enum { VIEW_DOWNLOADS, VIEW_STATS, VIEW_SCHEDULED };
enum { HIST = 72 };

typedef struct {
    char gid[32];
    char status[24];
} Known;

typedef struct {
    Config cfg;
    Config edit;
    Rpc rpc;
    Download *items;
    int count;
    int cap;
    GlobalStat gs;
    bool connected;
    bool engine_applied;
    int tab;
    int view;
    GreedScheduled sched[GREED_MAX_SCHEDULED];
    int sched_count;
    long long sched_check;
    bool show_schedule;
    char sched_url[2048];
    cui_date sched_date;
    int sched_hour;
    int sched_min;
    int sched_preset;
    char filter[128];
    bool focus_search;
    char selected[32];
    /* Multi-selection: every action button works on the whole set. */
    char sel[256][32];
    int sel_count;
    bool drag_active, drag_moved, drag_add;
    double drag_x, drag_y;
    char drag_base[256][32];
    int drag_base_count;
    bool suppress_click;
    cui_rect row_rect[512];
    char row_gid[512][32];
    int row_n;
    /* Filter fade and page transitions. */
    unsigned filter_hash, row_gen;
    struct {
        char gid[32];
        float v;
        unsigned gen;
    } fade[512];
    int fade_count;
    int page_key;
    float page_fade;
    float conn_fade;
    cui_id menu_anchor;
    bool show_add;
    bool show_settings;
    bool show_info;
    bool details_open;
    char info_gid[32];
    bool sidebar_open;
    int settings_tab;
    int settings_seen_tab;
    char theme_query[64];
    int theme_filter;
    char add_urls[16384];
    char add_dir[1024];
    char add_name[256];
    int add_conn;
    char status[256];
    double last_poll;
    double next_clip;
    Known *known;
    int known_count;
    char clip_link[2048];
    char last_clip[2048];
    int clip_toast;
    double last_click;
    char last_click_gid[32];
    GreedFrame frame;
    bool frame_ready;
    bool demo_details;
    float hist_down[HIST];
    float hist_up[HIST];
    char hist_text[HIST][16];
    const char *hist_labels[HIST];
    int hist_count;
} App;

static const char *tab_names[TAB_COUNT] = {"All", "Active", "Waiting", "Stopped", "Complete"};
static const char *tab_icons[TAB_COUNT] = {CUI_ICON_LAYERS, CUI_ICON_ACTIVITY, CUI_ICON_CLOCK,
                                           CUI_ICON_CIRCLE_ALERT, CUI_ICON_CIRCLE_CHECK};

static void set_status(App *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->status, sizeof a->status, fmt, ap);
    va_end(ap);
}

static void apply_theme(cui_ctx *ui, const char *name, float motion_speed) {
    greed_theme_apply(ui, greed_theme_find(name), motion_speed);
}

static cui_color status_color(const Download *d) {
    if (!strcmp(d->status, "active")) return CUI_COLOR_ACCENT_STRONG;
    if (!strcmp(d->status, "complete")) return CUI_COLOR_SUCCESS;
    if (!strcmp(d->status, "paused")) return CUI_COLOR_WARNING;
    if (!strcmp(d->status, "error")) return CUI_COLOR_DANGER;
    return CUI_COLOR_TEXT_MUTED;
}

static cui_tone status_tone(const Download *d) {
    if (!strcmp(d->status, "active")) return CUI_TONE_ACCENT;
    if (!strcmp(d->status, "complete")) return CUI_TONE_SUCCESS;
    if (!strcmp(d->status, "paused")) return CUI_TONE_WARNING;
    if (!strcmp(d->status, "error")) return CUI_TONE_DANGER;
    return CUI_TONE_NEUTRAL;
}

static Download *find_item(App *a, const char *gid) {
    for (int i = 0; i < a->count; i++)
        if (!strcmp(a->items[i].gid, gid)) return &a->items[i];
    return NULL;
}

static bool selection_has(const App *a, const char *gid) {
    for (int i = 0; i < a->sel_count; i++)
        if (!strcmp(a->sel[i], gid)) return true;
    return false;
}

static void selection_add(App *a, const char *gid) {
    if (selection_has(a, gid) || a->sel_count >= 256) return;
    snprintf(a->sel[a->sel_count++], sizeof a->sel[0], "%s", gid);
}

static void selection_remove(App *a, const char *gid) {
    for (int i = 0; i < a->sel_count; i++) {
        if (strcmp(a->sel[i], gid)) continue;
        memmove(a->sel[i], a->sel[i + 1], (size_t)(a->sel_count - i - 1) * sizeof a->sel[0]);
        a->sel_count--;
        if (!strcmp(a->selected, gid)) {
            if (a->sel_count)
                snprintf(a->selected, sizeof a->selected, "%s", a->sel[a->sel_count - 1]);
            else a->selected[0] = 0;
        }
        return;
    }
}

static void selection_set(App *a, const char *gid) {
    a->sel_count = 0;
    selection_add(a, gid);
    snprintf(a->selected, sizeof a->selected, "%s", gid);
}

static void selection_clear(App *a) {
    a->sel_count = 0;
    a->selected[0] = 0;
}

static void selection_toggle(App *a, const char *gid) {
    if (selection_has(a, gid)) {
        selection_remove(a, gid);
        return;
    }
    selection_add(a, gid);
    snprintf(a->selected, sizeof a->selected, "%s", gid);
}

static void selection_prune(App *a) {
    int w = 0;
    for (int i = 0; i < a->sel_count; i++) {
        if (!find_item(a, a->sel[i])) continue;
        if (w != i) memmove(a->sel[w], a->sel[i], sizeof a->sel[0]);
        w++;
    }
    a->sel_count = w;
    if (!selection_has(a, a->selected)) {
        if (w) snprintf(a->selected, sizeof a->selected, "%s", a->sel[w - 1]);
        else a->selected[0] = 0;
    }
}

static bool selection_can_pause(App *a) {
    for (int i = 0; i < a->sel_count; i++) {
        Download *d = find_item(a, a->sel[i]);
        if (d && (!strcmp(d->status, "active") || !strcmp(d->status, "waiting"))) return true;
    }
    return false;
}

static bool selection_can_resume(App *a) {
    for (int i = 0; i < a->sel_count; i++) {
        Download *d = find_item(a, a->sel[i]);
        if (d && (!strcmp(d->status, "paused") || !strcmp(d->status, "error"))) return true;
    }
    return false;
}

/* aria2 reports a torrent's name as its folder, so multi-file checksums are out of scope. */
static bool download_path(const Download *d, char *out, size_t cap) {
    if (d->file_count > 1 || !d->name[0]) return false;
    if (d->dir[0]) snprintf(out, cap, "%s/%s", d->dir, d->name);
    else snprintf(out, cap, "%s", d->name);
    return true;
}

static bool spawn_wait(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Ask the desktop's file-manager service, not the inode/directory handler:
 * desktop files for other apps sometimes claim that MIME type. */
static void open_folder(const char *dir) {
    if (!dir || !dir[0]) return;
    static const char hex[] = "0123456789ABCDEF";
    char encoded[PATH_MAX * 3 + 1];
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)dir; *p && o + 4 < sizeof encoded; p++) {
        if (isalnum(*p) || strchr("/-._~", *p)) encoded[o++] = (char)*p;
        else {
            encoded[o++] = '%';
            encoded[o++] = hex[*p >> 4];
            encoded[o++] = hex[*p & 15];
        }
    }
    encoded[o] = 0;
    char spec[PATH_MAX * 3 + 32];
    snprintf(spec, sizeof spec, "array:string:file://%s", encoded);
    char *const argv[] = {"dbus-send",
                          "--session",
                          "--dest=org.freedesktop.FileManager1",
                          "--type=method_call",
                          "/org/freedesktop/FileManager1",
                          "org.freedesktop.FileManager1.ShowFolders",
                          spec,
                          "string:",
                          NULL};
    if (spawn_wait(argv)) return;
    cui_app_open_url(dir);
}

static void verify_download(App *a, Download *d) {
    if (d->file_count > 1) {
        set_status(a, "Checksum verification does not support multi-file torrents yet");
        return;
    }
    if (!download_is_finished(d)) {
        set_status(a, "Checksum verification starts when the download is complete");
        return;
    }
    char path[PATH_MAX];
    if (!download_path(d, path, sizeof path)) {
        set_status(a, "No file to verify");
        return;
    }
    if (!checksum_queue(path)) {
        set_status(a, "Checksum check already queued");
        return;
    }
    set_status(a, "Checking checksum for %s…", d->name);
}

static void checksum_tick(App *a) {
    char path[PATH_MAX], hex[65];
    if (!checksum_poll(path, sizeof path, hex, sizeof hex)) return;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!hex[0]) {
        set_status(a, "Checksum failed for %s", base);
        return;
    }
    char stored[65];
    int state = checksum_stored(path, stored, sizeof stored);
    if (state == 0) {
        checksum_record(path, hex, true);
        set_status(a, "Checksum recorded for %s", base);
    } else if (!strcmp(stored, hex)) {
        checksum_record(path, hex, true);
        set_status(a, "Checksum verified: %s", base);
    } else {
        checksum_record(path, stored, false);
        set_status(a, "Checksum mismatch: %s", base);
    }
}

static void notify_send(const char *name) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("notify-send", "notify-send", "Greed: download complete", name, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

static void remember_statuses(App *a, const Download *items, int n, bool notify) {
    Known *next = n ? calloc((size_t)n, sizeof *next) : NULL;
    for (int i = 0; i < n; i++) {
        snprintf(next[i].gid, sizeof next[i].gid, "%s", items[i].gid);
        snprintf(next[i].status, sizeof next[i].status, "%s", items[i].status);
        for (int k = 0; k < a->known_count; k++) {
            if (strcmp(a->known[k].gid, items[i].gid)) continue;
            if (!strcmp(items[i].status, "complete") &&
                strcmp(a->known[k].status, "complete")) {
                if (notify && a->cfg.notify_on_complete) notify_send(items[i].name);
                if (a->cfg.verify_after_complete) {
                    char path[PATH_MAX];
                    if (download_path(&items[i], path, sizeof path)) checksum_queue(path);
                }
            }
            break;
        }
    }
    free(a->known);
    a->known = next;
    a->known_count = n;
}

static void poll_engine(App *a) {
    Download *fresh = NULL;
    int n = 0, cap = 0;
    bool ok = false;

    struct json_object *waiting = json_object_new_array();
    json_object_array_add(waiting, json_object_new_int(0));
    json_object_array_add(waiting, json_object_new_int(1000));
    struct json_object *stopped = json_object_new_array();
    json_object_array_add(stopped, json_object_new_int(0));
    json_object_array_add(stopped, json_object_new_int(1000));

    const char *methods[3] = {"aria2.tellActive", "aria2.tellWaiting", "aria2.tellStopped"};
    struct json_object *params[3] = {NULL, waiting, stopped};
    for (int m = 0; m < 3; m++) {
        struct json_object *v = rpc_call(&a->rpc, methods[m], params[m]);
        if (!v) continue;
        ok = true;
        size_t len = json_object_array_length(v);
        for (size_t i = 0; i < len; i++) {
            Download d;
            if (!download_parse(json_object_array_get_idx(v, i), &d)) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 32;
                Download *p = realloc(fresh, (size_t)cap * sizeof *fresh);
                if (!p) break;
                fresh = p;
            }
            fresh[n++] = d;
        }
        json_object_put(v);
    }

    if (!ok) {
        free(fresh);
        a->connected = false;
        memset(&a->gs, 0, sizeof a->gs);
        return;
    }
    a->connected = true;
    remember_statuses(a, fresh, n, a->known_count > 0);
    free(a->items);
    a->items = fresh;
    a->count = n;
    a->cap = cap;
    selection_prune(a);

    struct json_object *st = rpc_call(&a->rpc, "aria2.getGlobalStat", NULL);
    if (st) {
        const char *keys[5] = {"downloadSpeed", "uploadSpeed", "numActive", "numWaiting",
                               "numStopped"};
        unsigned long long *out[5] = {&a->gs.down_speed, &a->gs.up_speed, &a->gs.active,
                                      &a->gs.waiting, &a->gs.stopped};
        for (int i = 0; i < 5; i++) {
            struct json_object *o = json_object_object_get(st, keys[i]);
            if (o) *out[i] = strtoull(json_object_get_string(o), NULL, 10);
        }
        json_object_put(st);
    }
}

static void sample_speed(App *a) {
    if (a->hist_count < HIST) a->hist_count++;
    else {
        memmove(a->hist_down, a->hist_down + 1, sizeof(float) * (HIST - 1));
        memmove(a->hist_up, a->hist_up + 1, sizeof(float) * (HIST - 1));
    }
    int last = a->hist_count - 1;
    a->hist_down[last] = (float)(a->gs.down_speed / 1024.0);
    a->hist_up[last] = (float)(a->gs.up_speed / 1024.0);
    for (int i = 0; i < a->hist_count; i++) {
        if (i == last) snprintf(a->hist_text[i], sizeof a->hist_text[i], "now");
        else if ((last - i) % 12 == 0)
            snprintf(a->hist_text[i], sizeof a->hist_text[i], "-%ds", (int)((last - i) * 0.8));
        else a->hist_text[i][0] = 0;
        a->hist_labels[i] = a->hist_text[i];
    }
}

static void apply_engine_options(App *a) {
    if (!a->connected) return;
    if (aria2_apply_options(&a->rpc, &a->cfg)) a->engine_applied = true;
    for (int i = 0; i < a->count; i++)
        if (a->items[i].is_torrent) aria2_apply_seed_options(&a->rpc, a->items[i].gid, &a->cfg);
}

static bool looks_like_link(const char *s) {
    return !strncmp(s, "http://", 7) || !strncmp(s, "https://", 8) || !strncmp(s, "ftp://", 6) ||
           !strncmp(s, "magnet:", 7);
}

static void poll_clipboard(App *a, cui_ctx *ui) {
    if (!a->cfg.clipboard_monitor) return;
    double now = cui_time(ui);
    if (now < a->next_clip) return;
    a->next_clip = now + 1.2;

    char text[2048] = {0};
    FILE *p = popen("timeout 1 wl-paste -n 2>/dev/null", "r");
    if (!p) return;
    if (!fgets(text, sizeof text, p)) text[0] = 0;
    pclose(p);
    size_t len = strlen(text);
    while (len && (text[len - 1] == '\n' || text[len - 1] == '\r')) text[--len] = 0;
    if (!looks_like_link(text) || !strcmp(text, a->last_clip)) return;
    snprintf(a->last_clip, sizeof a->last_clip, "%s", text);
    snprintf(a->clip_link, sizeof a->clip_link, "%s", text);
    a->clip_toast = cui_toast(ui, "Link copied", .description = a->clip_link,
                              .action = "Download", .tone = CUI_TONE_INFO, .duration = 8);
}

static bool is_torrent_path(const char *s) {
    size_t n = strlen(s);
    return n > 8 && !strcmp(s + n - 8, ".torrent");
}

static int add_links_impl(App *a, const char *text, const char *dir, const char *out, int conn,
                          bool ui_feedback) {
    char *copy = strdup(text);
    if (!copy) return 0;
    bool single = !strchr(text, '\n');
    int added = 0;
    char err[256] = {0};

    for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        while (*line == ' ' || *line == '\t') line++;
        size_t len = strlen(line);
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\r' || line[len - 1] == '\t'))
            line[--len] = 0;
        if (!*line) continue;

        struct json_object *opts = json_object_new_object();
        json_object_object_add(opts, "dir", json_object_new_string(dir));
        if (out && out[0] && single) json_object_object_add(opts, "out", json_object_new_string(out));
        if (conn > 1) {
            json_object_object_add(opts, "split", json_object_new_int(conn));
            json_object_object_add(opts, "max-connection-per-server", json_object_new_int(conn));
        }

        struct json_object *result = NULL;
        if (is_torrent_path(line) && access(line, R_OK) == 0) {
            size_t b64len = 0;
            char *b64 = base64_file(line, &b64len);
            if (b64) {
                struct json_object *params = json_object_new_array();
                json_object_array_add(params, json_object_new_string_len(b64, (int)b64len));
                json_object_array_add(params, json_object_new_array());
                json_object_array_add(params, opts);
                result = rpc_call(&a->rpc, "aria2.addTorrent", params);
                free(b64);
            } else {
                json_object_put(opts);
                snprintf(err, sizeof err, "cannot read %s", line);
                break;
            }
        } else {
            struct json_object *params = json_object_new_array();
            struct json_object *urls = json_object_new_array();
            json_object_array_add(urls, json_object_new_string(line));
            json_object_array_add(params, urls);
            json_object_array_add(params, opts);
            result = rpc_call(&a->rpc, "aria2.addUri", params);
        }
        if (!result) {
            snprintf(err, sizeof err, "%s", a->rpc.error);
            break;
        }
        json_object_put(result);
        added++;
    }
    free(copy);

    if (err[0]) {
        set_status(a, "Added %d, error: %s", added, err);
    } else {
        set_status(a, "Added %d download%s", added, added == 1 ? "" : "s");
        if (ui_feedback) {
            a->add_urls[0] = 0;
            a->add_name[0] = 0;
            a->show_add = false;
        }
    }
    a->last_poll = 0;
    return added;
}

static int add_links(App *a, const char *text, const char *dir, const char *out, int conn) {
    return add_links_impl(a, text, dir, out, conn, true);
}

/* Fire everything that is due; entries stay queued while aria2 is offline. */
static void schedule_tick(App *a) {
    long long now = (long long)time(NULL);
    if (now == a->sched_check) return;
    a->sched_check = now;
    if (!a->sched_count || !a->connected) return;

    int w = 0, fired = 0;
    for (int i = 0; i < a->sched_count; i++) {
        if (a->sched[i].due > now ||
            add_links_impl(a, a->sched[i].url, a->sched[i].dir, NULL,
                           a->cfg.max_concurrent, false) <= 0) {
            if (w != i) a->sched[w] = a->sched[i];
            w++;
            continue;
        }
        fired++;
    }
    if (fired) {
        a->sched_count = w;
        config_schedules_save(a->sched, a->sched_count);
        set_status(a, "Started %d scheduled download%s", fired, fired == 1 ? "" : "s");
    }
}

static void open_add(App *a) {
    a->show_add = true;
    snprintf(a->add_dir, sizeof a->add_dir, "%s", a->cfg.download_dir);
    a->add_conn = a->cfg.max_concurrent;
}

static void set_sched_due(App *a, time_t due) {
    struct tm t;
    localtime_r(&due, &t);
    a->sched_date = (cui_date){t.tm_year + 1900, t.tm_mon + 1, t.tm_mday};
    a->sched_hour = t.tm_hour;
    a->sched_min = t.tm_min;
}

static time_t next_clock(int hour) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    t.tm_hour = hour;
    t.tm_min = 0;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t due = mktime(&t);
    if (due <= now) {
        t.tm_mday += 1;
        t.tm_isdst = -1;
        due = mktime(&t);
    }
    return due;
}

/* 0 in 15 min, 1 in 1 hour, 2 at 22:00, 3 at 02:00 (the next occurrence) */
static time_t sched_preset_due(int preset) {
    time_t now = time(NULL);
    if (preset == 0) return now + 15 * 60;
    if (preset == 1) return now + 3600;
    return next_clock(preset == 2 ? 22 : 2);
}

static void open_schedule(App *a) {
    a->show_schedule = true;
    a->sched_url[0] = 0;
    a->sched_preset = 1;
    time_t due = sched_preset_due(1);
    set_sched_due(a, due);
}

static void open_settings(App *a) {
    a->edit = a->cfg;
    a->settings_tab = 0;
    a->settings_seen_tab = -1;
    a->theme_query[0] = 0;
    a->theme_filter = 0;
    a->show_settings = true;
}

static void toggle_download(App *a, Download *d) {
    bool paused = !strcmp(d->status, "paused");
    struct json_object *params = json_object_new_array();
    json_object_array_add(params, json_object_new_string(d->gid));
    struct json_object *v = rpc_call(&a->rpc, paused ? "aria2.unpause" : "aria2.pause", params);
    if (!v) {
        set_status(a, "Error: %s", a->rpc.error);
        return;
    }
    json_object_put(v);
    set_status(a, paused ? "Resumed" : "Paused");
    a->last_poll = 0;
}

static void toggle_selected(App *a) {
    Download *d = find_item(a, a->selected);
    if (d) toggle_download(a, d);
}

/* aria2 has no retry RPC: drop the failed result and add the source again. */
static void retry_download(App *a, Download *d) {
    if (!d->url[0]) {
        set_status(a, "This download cannot be retried; add it again");
        return;
    }
    char name[512], url[2048], dir[1024];
    snprintf(name, sizeof name, "%s", d->name);
    snprintf(url, sizeof url, "%s", d->url);
    snprintf(dir, sizeof dir, "%s", d->dir);
    bool torrent = d->is_torrent;

    struct json_object *params = json_object_new_array();
    json_object_array_add(params, json_object_new_string(d->gid));
    struct json_object *v = rpc_call(&a->rpc, "aria2.removeDownloadResult", params);
    if (!v) {
        set_status(a, "Error: %s", a->rpc.error);
        return;
    }
    json_object_put(v);
    add_links(a, url, dir, torrent ? NULL : name, a->cfg.max_concurrent);
    set_status(a, "Retrying %s", name);
}

static void remove_download(App *a, Download *d) {
    struct json_object *params = json_object_new_array();
    json_object_array_add(params, json_object_new_string(d->gid));
    struct json_object *v = rpc_call(&a->rpc, download_is_finished(d) ? "aria2.removeDownloadResult"
                                                                     : "aria2.forceRemove",
                                     params);
    if (!v) {
        set_status(a, "Error: %s", a->rpc.error);
        return;
    }
    json_object_put(v);
    if (!download_is_finished(d)) {
        struct json_object *purge = json_object_new_array();
        json_object_array_add(purge, json_object_new_string(d->gid));
        struct json_object *r = rpc_call(&a->rpc, "aria2.removeDownloadResult", purge);
        if (r) json_object_put(r);
    }
    set_status(a, "Removed");
    selection_remove(a, d->gid);
    a->last_poll = 0;
}

static void remove_selected(App *a) {
    char gids[256][32];
    int n = a->sel_count;
    memcpy(gids, a->sel, (size_t)n * sizeof gids[0]);
    for (int i = 0; i < n; i++) {
        Download *d = find_item(a, gids[i]);
        if (d) remove_download(a, d);
    }
}

static void pause_selection(App *a) {
    char gids[256][32];
    int n = a->sel_count;
    memcpy(gids, a->sel, (size_t)n * sizeof gids[0]);
    for (int i = 0; i < n; i++) {
        Download *d = find_item(a, gids[i]);
        if (d && (!strcmp(d->status, "active") || !strcmp(d->status, "waiting")))
            toggle_download(a, d);
    }
}

static void resume_selection(App *a) {
    char gids[256][32];
    int n = a->sel_count;
    memcpy(gids, a->sel, (size_t)n * sizeof gids[0]);
    for (int i = 0; i < n; i++) {
        Download *d = find_item(a, gids[i]);
        if (!d) continue;
        if (!strcmp(d->status, "paused")) toggle_download(a, d);
        else if (!strcmp(d->status, "error")) retry_download(a, d);
    }
}

static void start_engine(App *a) {
    if (!aria2_available()) {
        set_status(a, "aria2 binary not found");
        return;
    }
    if (aria2_ensure_running(&a->cfg, true)) {
        a->connected = true;
        a->engine_applied = false;
        apply_engine_options(a);
        set_status(a, "aria2 started");
        a->last_poll = 0;
    } else {
        set_status(a, "aria2 failed to start");
    }
}

static bool tab_matches(int tab, const Download *d) {
    switch (tab) {
    case TAB_ACTIVE: return !strcmp(d->status, "active");
    case TAB_WAITING: return !strcmp(d->status, "waiting") || !strcmp(d->status, "paused");
    case TAB_STOPPED: return !strcmp(d->status, "error") || !strcmp(d->status, "removed");
    case TAB_COMPLETE: return !strcmp(d->status, "complete");
    default: return true;
    }
}

static bool contains_ci(const char *haystack, const char *needle) {
    if (!needle[0]) return true;
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++)
        if (!strncasecmp(p, needle, n)) return true;
    return false;
}

static int collect_rows(App *a, int *rows, int cap) {
    int n = 0;
    for (int i = 0; i < a->count; i++) {
        const Download *d = &a->items[i];
        if (!tab_matches(a->tab, d)) continue;
        if (!contains_ci(d->name, a->filter) && !contains_ci(d->url, a->filter)) continue;
        if (rows && n < cap) rows[n] = i;
        n++;
    }
    return n;
}

static cui_texture logo_texture(cui_ctx *ui) {
    static cui_texture tex;
    if (tex) return tex;
    int n = GREED_LOGO_W * GREED_LOGO_H;
    unsigned char *rgba = malloc((size_t)n * 4);
    if (!rgba) return 0;
    for (int i = 0; i < n; i++) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = greed_logo_alpha[i];
    }
    tex = cui_image_create(ui, GREED_LOGO_W, GREED_LOGO_H, rgba);
    free(rgba);
    return tex;
}

static void titlebar(App *a, cui_ctx *ui) {
    char down[32], up[32];
    fmt_speed(down, sizeof down, a->gs.down_speed);
    fmt_speed(up, sizeof up, a->gs.up_speed);

    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(46), .bg = CUI_COLOR_SURFACE, .pad_x = 10,
            .gap = 8, .align = CUI_ALIGN_CENTER) {
        if (cui_icon_button(ui, CUI_ICON_PANEL_LEFT, .size = CUI_SIZE_SM,
                            .tooltip = a->sidebar_open ? "Hide sidebar" : "Show sidebar"))
            a->sidebar_open = !a->sidebar_open;
        cui_row(ui, .id = "dragzone", .width = CUI_FILL, .height = CUI_FILL, .gap = 8,
                .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_DOWNLOAD, 18, CUI_COLOR_ACCENT_STRONG);
            cui_texture logo = logo_texture(ui);
            if (logo)
                cui_image(ui, logo, .width = CUI_PX(GREED_LOGO_W / 2),
                          .height = CUI_PX(GREED_LOGO_H / 2), .tint = CUI_COLOR_FOREGROUND);
            else
                cui_text(ui, "Greed", .font = CUI_FONT_MEDIUM, .size = 15);
            cui_gap(ui, 6);
            cui_icon(ui, CUI_ICON_ARROW_DOWN, 13, CUI_COLOR_ACCENT_STRONG);
            cui_text(ui, down, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
            cui_icon(ui, CUI_ICON_ARROW_UP, 13, CUI_COLOR_TEXT_MUTED);
            cui_text(ui, up, .size = 12, .color = CUI_COLOR_TEXT_MUTED, .tabular = true);
        }
        if (cui_button(ui, "Add", .icon = CUI_ICON_PLUS, .variant = CUI_BUTTON_PRIMARY,
                       .size = CUI_SIZE_SM))
            open_add(a);
        if (cui_icon_button(ui, CUI_ICON_SETTINGS, .size = CUI_SIZE_SM, .tooltip = "Settings"))
            open_settings(a);
        cui_box(ui, .width = CUI_PX(1), .height = CUI_PX(18), .bg = CUI_COLOR_BORDER) {}
        if (cui_icon_button(ui, CUI_ICON_MINUS, .size = CUI_SIZE_SM, .tooltip = "Minimize"))
            frame_minimize(&a->frame);
        bool maximized = frame_is_maximized(&a->frame);
        if (cui_icon_button(ui, maximized ? CUI_ICON_MINIMIZE_2 : CUI_ICON_MAXIMIZE_2,
                            .size = CUI_SIZE_SM, .tooltip = maximized ? "Restore" : "Maximize"))
            frame_toggle_maximize(&a->frame);
        if (cui_icon_button(ui, CUI_ICON_X, .size = CUI_SIZE_SM, .tooltip = "Close"))
            cui_app_quit(NULL);
    }
}

static void frame_interactions(App *a, cui_ctx *ui) {
    if (!cui_mouse_clicked(ui, CUI_MOUSE_LEFT)) return;
    cui_vec2 m = cui_mouse_pos(ui);
    cui_vec2 v = cui_viewport_size(ui);

    if (!frame_is_maximized(&a->frame)) {
        const float edge = 5.0f;
        bool left = m.x <= edge, right = m.x >= v.x - edge;
        bool top = m.y <= edge, bottom = m.y >= v.y - edge;
        int dir = -1;
        if (top && left) dir = 0;
        else if (top && right) dir = 2;
        else if (bottom && left) dir = 6;
        else if (bottom && right) dir = 4;
        else if (top) dir = 1;
        else if (bottom) dir = 5;
        else if (left) dir = 7;
        else if (right) dir = 3;
        if (dir >= 0) {
            frame_begin_resize(&a->frame, dir);
            return;
        }
    }

    cui_rect dz = cui_get_rect(ui, cui_make_id(ui, "dragzone"));
    if (m.x >= dz.x && m.x <= dz.x + dz.w && m.y >= dz.y && m.y <= dz.y + dz.h)
        frame_begin_move(&a->frame);
}

static void sidebar(App *a, cui_ctx *ui, float width) {
    int counts[TAB_COUNT] = {0};
    for (int i = 0; i < a->count; i++)
        for (int t = 0; t < TAB_COUNT; t++)
            if (tab_matches(t, &a->items[i])) counts[t]++;
    /* Fade with the width so the clipped labels do not flash while collapsing. */
    float opacity = width / 198.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (opacity < 0.05f) opacity = 0.05f;

    cui_column(ui, .width = CUI_PX(width), .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .pad = 10,
               .gap = 2, .clip = true, .opacity = opacity) {
        cui_nav_section(ui, "LIBRARY");
        for (int t = 0; t < TAB_COUNT; t++) {
            char badge[16] = {0};
            if (counts[t] > 0) snprintf(badge, sizeof badge, "%d", counts[t]);
            if (cui_nav_item(ui, tab_names[t], .icon = tab_icons[t],
                             .badge = badge[0] ? badge : NULL,
                             .selected = a->view == VIEW_DOWNLOADS && a->tab == t,
                             .group = "tabs")) {
                a->tab = t;
                a->view = VIEW_DOWNLOADS;
            }
        }
        char sched_badge[16] = {0};
        if (a->sched_count) snprintf(sched_badge, sizeof sched_badge, "%d", a->sched_count);
        if (cui_nav_item(ui, "Scheduled", .icon = CUI_ICON_CALENDAR,
                         .badge = sched_badge[0] ? sched_badge : NULL,
                         .selected = a->view == VIEW_SCHEDULED, .group = "tabs"))
            a->view = VIEW_SCHEDULED;
        cui_box(ui, .height = CUI_PX(14)) {}
        cui_separator(ui);
        cui_nav_section(ui, "INSIGHTS");
        if (cui_nav_item(ui, "Statistics", .icon = CUI_ICON_CHART_LINE,
                         .selected = a->view == VIEW_STATS, .group = "tabs"))
            a->view = VIEW_STATS;
        cui_box(ui, .height = CUI_PX(14)) {}
        cui_separator(ui);
        cui_nav_section(ui, "TRANSFER");
        char num[32];
        cui_row(ui, .width = CUI_FILL) {
            cui_text(ui, "Active", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_spacer(ui);
            snprintf(num, sizeof num, "%llu", a->gs.active);
            cui_text(ui, num, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
        }
        cui_row(ui, .width = CUI_FILL) {
            cui_text(ui, "Queued", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_spacer(ui);
            snprintf(num, sizeof num, "%llu", a->gs.waiting);
            cui_text(ui, num, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .tabular = true);
        }
        cui_spacer(ui);
        cui_separator(ui);
        /* Solid status pill; the fill itself fades when the engine comes or goes. */
        float dt = cui_delta_time(ui);
        float speed = a->show_settings ? a->edit.motion_speed : a->cfg.motion_speed;
        float step = 1.0f - expf(-dt * 10.0f * speed);
        a->conn_fade += ((a->connected ? 1.0f : 0.0f) - a->conn_fade) * step;
        cui_color engine_bg = cui_mix(cui_color_of(ui, CUI_COLOR_DANGER),
                                      cui_color_of(ui, CUI_COLOR_SUCCESS), a->conn_fade);
        engine_bg = cui_mix(engine_bg, cui_color_of(ui, CUI_COLOR_SURFACE), 0.18f);
        if (cui_button(ui, a->connected ? "aria2 connected" : "aria2 offline",
                       .variant = CUI_BUTTON_GHOST, .size = CUI_SIZE_SM, .full_width = true,
                       .background = engine_bg, .color = CUI_COLOR_BACKGROUND)) {
            if (!a->connected && a->cfg.auto_start_aria2)
                aria2_ensure_running(&a->cfg, true);
            a->last_poll = 0;
            set_status(a, a->connected ? "Connection refreshed" : "Reconnecting to aria2…");
        }
    }
}

static void kv(cui_ctx *ui, const char *key, const char *value) {
    cui_row(ui, .width = CUI_FILL) {
        cui_text(ui, key, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        cui_spacer(ui);
        cui_text(ui, value, .size = 12, .width = CUI_PX(170), .align = CUI_TEXT_RIGHT, .nowrap = true);
    }
}

static void details(App *a, cui_ctx *ui) {
    Download *d = find_item(a, a->selected);
    if (!d) a->details_open = false;
    bool show = a->view == VIEW_DOWNLOADS && a->details_open && d;
    float width = cui_animate_spring(ui, cui_make_id(ui, "details-width"),
                                     show ? 290.0f : 0.0f, 0.24f, 0.0f);
    if (width <= 0.5f || !show) return;
    /* Fade with the width so the clipped labels do not flash while closing. */
    float opacity = width / 290.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (opacity < 0.05f) opacity = 0.05f;
    cui_separator(ui);
    cui_column(ui, .width = CUI_PX(width), .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .pad = 14,
               .gap = 8, .clip = true, .opacity = opacity) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_text(ui, "DETAILS", .size = 11, .font = CUI_FONT_MEDIUM,
                     .color = CUI_COLOR_TEXT_MUTED);
            if (a->sel_count > 1) {
                char badge[24];
                snprintf(badge, sizeof badge, "%d selected", a->sel_count);
                cui_badge(ui, badge, .tone = CUI_TONE_ACCENT);
            }
            cui_spacer(ui);
            if (cui_icon_button(ui, CUI_ICON_X, .size = CUI_SIZE_SM, .tooltip = "Close details"))
                a->details_open = false;
        }
        cui_scroll(ui, .id = "details", .height = CUI_FILL, .gap = 10) {
            cui_text(ui, d->name, .font = CUI_FONT_MEDIUM, .size = 15, .line_height = 1.3f);
            cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
                cui_badge(ui, download_status_label(d), .tone = status_tone(d), .dot = true);
            }
            double p = download_progress(d);
            cui_progress(ui, (float)p, .height = 6, .tone = status_tone(d));
            char a1[32], a2[32];
            fmt_bytes(a1, sizeof a1, d->completed);
            fmt_bytes(a2, sizeof a2, d->total);
            char pct[32], amount[80];
            snprintf(pct, sizeof pct, "%.1f%%", p * 100.0);
            snprintf(amount, sizeof amount, "%s / %s", a1, a2);
            cui_row(ui, .width = CUI_FILL) {
                cui_text(ui, pct, .size = 12, .tabular = true);
                cui_spacer(ui);
                cui_text(ui, amount, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .tabular = true);
            }
            char speed[32], up[32], eta[32];
            fmt_speed(speed, sizeof speed, d->down_speed);
            fmt_speed(up, sizeof up, d->up_speed);
            unsigned long long secs;
            if (download_eta(d, &secs)) fmt_eta(eta, sizeof eta, secs);
            else snprintf(eta, sizeof eta, "n/a");
            kv(ui, "Speed", speed);
            kv(ui, "Upload", up);
            kv(ui, "ETA", eta);
            char num[32];
            snprintf(num, sizeof num, "%u", d->connections);
            kv(ui, "Connections", num);
            if (d->is_torrent) {
                snprintf(num, sizeof num, "%u", d->seeders);
                kv(ui, "Seeders", num);
            }
            if (d->file_count > 1) {
                snprintf(num, sizeof num, "%d", d->file_count);
                kv(ui, "Files", num);
            }
            cui_text(ui, "Directory", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            cui_text(ui, d->dir, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY, .nowrap = true);
            cui_text(ui, "GID", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            cui_text(ui, d->gid, .size = 12, .font = CUI_FONT_MONO,
                     .color = CUI_COLOR_TEXT_SECONDARY);
            char file[PATH_MAX];
            if (download_path(d, file, sizeof file)) {
                char hex[65];
                int state = checksum_stored(file, hex, sizeof hex);
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_text(ui, "SHA-256", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                    cui_spacer(ui);
                    if (state)
                        cui_badge(ui, state == 1 ? "Verified" : "Mismatch",
                                  .tone = state == 1 ? CUI_TONE_SUCCESS : CUI_TONE_DANGER,
                                  .dot = true);
                }
                if (state) {
                    cui_text(ui, hex, .size = 10, .font = CUI_FONT_MONO,
                             .color = CUI_COLOR_TEXT_SECONDARY);
                } else {
                    cui_text(ui, "Not recorded yet", .size = 12,
                             .color = CUI_COLOR_TEXT_SECONDARY);
                }
                if (download_is_finished(d) &&
                    cui_button(ui, "Verify checksum", .icon = CUI_ICON_CHECK_CHECK,
                               .size = CUI_SIZE_SM, .variant = CUI_BUTTON_GHOST,
                               .full_width = true))
                    verify_download(a, d);
            }
            if (d->error[0])
                cui_alert(ui, "Download failed", .description = d->error, .tone = CUI_TONE_DANGER,
                          .icon = CUI_ICON_CIRCLE_ALERT);
            bool failed = !strcmp(d->status, "error");
            bool paused = !strcmp(d->status, "paused");
            bool running = !strcmp(d->status, "active") || !strcmp(d->status, "waiting");
            if ((failed || paused || running) &&
                cui_button(ui, failed ? "Retry" : paused ? "Resume" : "Pause",
                           .icon = failed || paused ? CUI_ICON_PLAY : CUI_ICON_PAUSE,
                           .size = CUI_SIZE_SM, .full_width = true)) {
                if (failed) retry_download(a, d);
                else toggle_selected(a);
            }
            cui_row(ui, .gap = 8) {
                if (cui_button(ui, "Folder", .icon = CUI_ICON_FOLDER_OPEN, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST, .width = CUI_FILL))
                    open_folder(d->dir);
                if (cui_button(ui, "Remove", .icon = CUI_ICON_TRASH_2, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST, .width = CUI_FILL))
                    remove_selected(a);
            }
        }
    }
}

static unsigned hash_text(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}

/* Filter changes bump row_gen, so every surviving row fades in again. */
static float row_fade(App *a, const char *gid, float step) {
    int slot = -1;
    for (int i = 0; i < a->fade_count; i++) {
        if (strcmp(a->fade[i].gid, gid)) continue;
        slot = i;
        break;
    }
    if (slot < 0) {
        if (a->fade_count >= 512) return 1.0f;
        slot = a->fade_count++;
        snprintf(a->fade[slot].gid, sizeof a->fade[0].gid, "%s", gid);
        a->fade[slot].v = 0.0f;
    }
    if (a->fade[slot].gen != a->row_gen) {
        a->fade[slot].gen = a->row_gen;
        a->fade[slot].v = 0.0f;
    }
    a->fade[slot].v += (1.0f - a->fade[slot].v) * step;
    if (a->fade[slot].v > 0.995f) a->fade[slot].v = 1.0f;
    return a->fade[slot].v;
}

static void drag_selection(App *a, cui_vec2 mp) {
    float x0 = a->drag_x < mp.x ? a->drag_x : mp.x;
    float x1 = a->drag_x > mp.x ? a->drag_x : mp.x;
    float y0 = a->drag_y < mp.y ? a->drag_y : mp.y;
    float y1 = a->drag_y > mp.y ? a->drag_y : mp.y;
    a->sel_count = 0;
    if (a->drag_add) {
        for (int i = 0; i < a->drag_base_count; i++)
            snprintf(a->sel[a->sel_count++], sizeof a->sel[0], "%s", a->drag_base[i]);
    }
    for (int i = 0; i < a->row_n; i++) {
        cui_rect r = a->row_rect[i];
        if (r.x >= x1 || r.x + r.w <= x0 || r.y >= y1 || r.y + r.h <= y0) continue;
        selection_add(a, a->row_gid[i]);
    }
    if (a->sel_count) snprintf(a->selected, sizeof a->selected, "%s", a->sel[a->sel_count - 1]);
    else a->selected[0] = 0;
}

static void download_table(App *a, cui_ctx *ui) {
    int rows[512];
    int n = collect_rows(a, rows, 512);
    if (n > 512) n = 512;
    if (n == 0) {
        a->row_n = 0;
        a->suppress_click = false;
        a->drag_active = false;
        cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_CENTER,
                   .justify = CUI_JUSTIFY_CENTER, .gap = 10) {
            cui_icon(ui, CUI_ICON_DOWNLOAD, 36, CUI_COLOR_TEXT_MUTED);
            cui_text(ui, a->count ? "Nothing in this tab" : "No downloads yet",
                     .font = CUI_FONT_DISPLAY, .size = 18);
            cui_text(ui, "Paste a link, or add a magnet / .torrent file.", .size = 13,
                     .color = CUI_COLOR_TEXT_SECONDARY);
            if (cui_button(ui, "Add download", .icon = CUI_ICON_PLUS,
                           .variant = CUI_BUTTON_PRIMARY))
                open_add(a);
        }
        return;
    }

    static const cui_table_column cols[] = {
        {"NAME", CUI_FILL, CUI_TEXT_START, false},
        {"SIZE", CUI_PX(80), CUI_TEXT_RIGHT, false},
        {"PROGRESS", CUI_PX(150), CUI_TEXT_START, false},
        {"SPEED", CUI_PX(92), CUI_TEXT_RIGHT, false},
        {"ETA", CUI_PX(64), CUI_TEXT_RIGHT, false},
        {"STATUS", CUI_PX(104), CUI_TEXT_START, false},
    };
    double speed = a->show_settings ? a->edit.motion_speed : a->cfg.motion_speed;
    float step = 1.0f - expf(-cui_delta_time(ui) * 12.0f * speed);
    unsigned filter_hash = hash_text(a->filter);
    if (filter_hash != a->filter_hash) {
        a->filter_hash = filter_hash;
        a->row_gen++;
    }
    bool down = cui_mouse_down(ui, CUI_MOUSE_LEFT);
    bool press = cui_mouse_clicked(ui, CUI_MOUSE_LEFT);
    cui_vec2 mp = cui_mouse_pos(ui);
    if (a->drag_active && !down) {
        a->suppress_click = a->drag_moved;
        a->drag_active = false;
    }
    if (a->drag_active && down) {
        if (fabs(mp.x - a->drag_x) + fabs(mp.y - a->drag_y) > 4) a->drag_moved = true;
        if (a->drag_moved) drag_selection(a, mp);
    }
    cui_table(ui, "downloads", cols, 6, .height = CUI_FILL, .striped = true, .compact = true,
              .selectable = true) {
        a->row_n = 0;
        for (int r = 0; r < n; r++) {
            Download *d = &a->items[rows[r]];
            bool selected = selection_has(a, d->gid);
            bool clicked = cui_table_row_ex(ui, selected, row_fade(a, d->gid, step));
            bool hovered = cui_last_hovered(ui);
            if (hovered && press && !a->drag_active) {
                a->drag_active = true;
                a->drag_moved = false;
                a->drag_x = mp.x;
                a->drag_y = mp.y;
                a->drag_add = (cui_key_mods(ui) & CUI_MOD_CTRL) != 0;
                a->drag_base_count = 0;
                if (a->drag_add) {
                    for (int i = 0; i < a->sel_count; i++)
                        snprintf(a->drag_base[i], sizeof a->drag_base[0], "%s", a->sel[i]);
                    a->drag_base_count = a->sel_count;
                }
            }
            if (a->row_n < 512) {
                a->row_rect[a->row_n] = cui_last_rect(ui);
                snprintf(a->row_gid[a->row_n], sizeof a->row_gid[0], "%s", d->gid);
                a->row_n++;
            }
            cui_id anchor = cui_last_id(ui);
            bool over = cui_mouse_clicked(ui, CUI_MOUSE_RIGHT) && hovered;
            if (over) {
                if (!selection_has(a, d->gid)) selection_set(a, d->gid);
                a->menu_anchor = anchor;
            }
            bool menu_row = a->menu_anchor && anchor == a->menu_anchor;
            if (menu_row) {
                if (cui_context_menu_begin(ui, "download-menu")) {
                    if (cui_menu_item(ui, "Select", .icon = CUI_ICON_SQUARE_CHECK,
                                      .checked = selection_has(a, d->gid))) {
                        selection_toggle(a, d->gid);
                        a->details_open = true;
                    }
                    if (cui_menu_item(ui, "Info", .icon = CUI_ICON_INFO)) {
                        snprintf(a->info_gid, sizeof a->info_gid, "%s", d->gid);
                        a->show_info = true;
                    }
                    bool can_verify = download_is_finished(d) && d->file_count <= 1;
                    if (cui_menu_item(ui, "Verify checksum", .icon = CUI_ICON_CHECK_CHECK,
                                      .disabled = !can_verify))
                        verify_download(a, d);
                    cui_menu_separator(ui);
                    bool paused = !strcmp(d->status, "paused");
                    bool running = !strcmp(d->status, "active") || !strcmp(d->status, "waiting");
                    bool failed = !strcmp(d->status, "error");
                    if (failed) {
                        if (cui_menu_item(ui, "Retry", .icon = CUI_ICON_PLAY))
                            retry_download(a, d);
                    } else if (paused) {
                        if (cui_menu_item(ui, "Resume", .icon = CUI_ICON_PLAY))
                            resume_selection(a);
                    } else if (cui_menu_item(ui, "Pause", .icon = CUI_ICON_PAUSE,
                                             .disabled = !running)) {
                        pause_selection(a);
                    }
                    if (cui_menu_item(ui, "Open folder", .icon = CUI_ICON_FOLDER_OPEN))
                        open_folder(d->dir);
                    cui_menu_separator(ui);
                    if (cui_menu_item(ui, "Remove", .icon = CUI_ICON_TRASH_2, .danger = true))
                        remove_selected(a);
                    cui_context_menu_end(ui);
                } else {
                    a->menu_anchor = 0;
                }
            }
            if (clicked && !a->suppress_click) {
                double now = cui_time(ui);
                if (now - a->last_click < 0.35 && !strcmp(a->last_click_gid, d->gid)) {
                    open_folder(d->dir);
                } else if (cui_key_mods(ui) & CUI_MOD_CTRL) {
                    selection_toggle(a, d->gid);
                    a->details_open = true;
                } else {
                    selection_set(a, d->gid);
                    a->details_open = true;
                }
                a->last_click = now;
                snprintf(a->last_click_gid, sizeof a->last_click_gid, "%s", d->gid);
            }
            cui_table_cell(ui); {
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_box(ui, .width = CUI_PX(8), .height = CUI_PX(8), .radius = 4,
                            .bg = status_color(d));
                    cui_text(ui, d->name, .width = CUI_FILL, .nowrap = true);
                }
            }
            cui_table_cell(ui); {
                char size[32];
                if (d->total) fmt_bytes(size, sizeof size, d->total);
                else snprintf(size, sizeof size, "n/a");
                cui_text(ui, size, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
            }
            cui_table_cell(ui); {
                double p = download_progress(d);
                char pct[16];
                snprintf(pct, sizeof pct, "%.0f%%", p * 100.0);
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_progress(ui, (float)p, .height = 6, .width = CUI_FILL, .tone = status_tone(d));
                    cui_text(ui, pct, .size = 11, .color = CUI_COLOR_TEXT_MUTED,
                             .width = CUI_PX(36), .align = CUI_TEXT_RIGHT, .tabular = true);
                }
            }
            cui_table_cell(ui); {
                char speed[32];
                if (!strcmp(d->status, "active")) fmt_speed(speed, sizeof speed, d->down_speed);
                else snprintf(speed, sizeof speed, "n/a");
                cui_text(ui, speed, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
            }
            cui_table_cell(ui); {
                char eta[32];
                unsigned long long secs;
                if (download_eta(d, &secs)) fmt_eta(eta, sizeof eta, secs);
                else snprintf(eta, sizeof eta, "n/a");
                cui_text(ui, eta, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
            }
            cui_table_cell(ui); {
                cui_text(ui, download_status_label(d), .size = 12, .color = status_color(d),
                         .nowrap = true);
            }
        }
    }
    if (a->drag_active && a->drag_moved) {
        cui_rect band = {
            a->drag_x < mp.x ? a->drag_x : mp.x,
            a->drag_y < mp.y ? a->drag_y : mp.y,
            fabsf(mp.x - a->drag_x),
            fabsf(mp.y - a->drag_y),
        };
        cui_color accent = cui_color_of(ui, CUI_COLOR_ACCENT);
        cui_overlay_rect(ui, band, 3, cui_alpha(accent, 0.12f), cui_alpha(accent, 0.55f), 1);
    }
    a->suppress_click = false;
}

static bool disk_usage(const char *path, unsigned long long *free_bytes,
                       unsigned long long *total_bytes) {
    struct statvfs s;
    if (statvfs(path, &s) != 0 || s.f_blocks == 0 || s.f_frsize == 0) return false;
    *total_bytes = (unsigned long long)s.f_blocks * s.f_frsize;
    *free_bytes = (unsigned long long)s.f_bavail * s.f_frsize;
    return true;
}

static void fmt_disk(char *out, size_t cap, unsigned long long bytes) {
    double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1000.0) snprintf(out, cap, "%.2f TB", gb / 1024.0);
    else if (gb >= 100.0) snprintf(out, cap, "%.0f GB", gb);
    else snprintf(out, cap, "%.1f GB", gb);
}

static void stats_page(App *a, cui_ctx *ui, float opacity) {
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 12,
               .opacity = opacity) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_heading(ui, "Statistics", 2);
            cui_spacer(ui);
            if (a->connected) {
                cui_badge(ui, "Live", .tone = CUI_TONE_SUCCESS, .dot = true);
            } else {
                cui_text(ui, "aria2 is offline", .size = 12, .color = CUI_COLOR_DANGER);
                if (cui_button(ui, "Start aria2", .size = CUI_SIZE_SM)) start_engine(a);
            }
        }
        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 12) {
            cui_row(ui, .width = CUI_FILL, .gap = 14, .align = CUI_ALIGN_CENTER) {
                cui_column(ui, .gap = 2) {
                    cui_text(ui, "Throughput", .font = CUI_FONT_MEDIUM, .size = 14);
                    cui_text(ui, "Last 60 seconds · KB/s", .size = 11,
                             .color = CUI_COLOR_TEXT_SECONDARY);
                }
                cui_spacer(ui);
                char down[32] = "0 B/s", up[32] = "0 B/s";
                if (a->connected) {
                    fmt_speed(down, sizeof down, a->gs.down_speed);
                    fmt_speed(up, sizeof up, a->gs.up_speed);
                }
                cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ARROW_DOWN, 14, CUI_COLOR_ACCENT_STRONG);
                    cui_text(ui, down, .size = 12, .tabular = true, .nowrap = true);
                }
                cui_row(ui, .gap = 4, .align = CUI_ALIGN_CENTER) {
                    cui_icon(ui, CUI_ICON_ARROW_UP, 14, CUI_COLOR_SERIES(1));
                    cui_text(ui, up, .size = 12, .tabular = true, .nowrap = true);
                }
            }
            if (a->hist_count > 1) {
                char peak_label[48], peak_detail[24];
                int peak = 0;
                for (int i = 1; i < a->hist_count; i++)
                    if (a->hist_down[i] > a->hist_down[peak]) peak = i;
                cui_chart_annotation note = {0};
                if (a->hist_down[peak] > 0.0f) {
                    char speed[32];
                    fmt_speed(speed, sizeof speed,
                              (unsigned long long)(a->hist_down[peak] * 1024.0f));
                    snprintf(peak_label, sizeof peak_label, "Peak %s", speed);
                    snprintf(peak_detail, sizeof peak_detail, "%ds ago",
                             (int)((a->hist_count - 1 - peak) * 0.8));
                    note = (cui_chart_annotation){.kind = CUI_ANNOTATION_POINT,
                                                  .x = peak,
                                                  .y = a->hist_down[peak],
                                                  .label = peak_label,
                                                  .detail = peak_detail};
                }
                cui_series series[2] = {
                    {"Download", a->hist_down, a->hist_count, CUI_COLOR_ACCENT_STRONG},
                    {"Upload", a->hist_up, a->hist_count, CUI_COLOR_SERIES(1)},
                };
                cui_line_chart(ui, "speed-history", series, 2, .height = 260,
                               .labels = a->hist_labels, .label_count = a->hist_count,
                               .smooth = true, .area = true,
                               .annotations = note.label ? &note : NULL,
                               .annotation_count = note.label ? 1 : 0);
            } else {
                cui_row(ui, .width = CUI_FILL, .height = CUI_PX(260), .gap = 10,
                        .align = CUI_ALIGN_CENTER, .justify = CUI_JUSTIFY_CENTER) {
                    cui_icon(ui, CUI_ICON_CHART_LINE, 28, CUI_COLOR_TEXT_MUTED);
                    cui_column(ui, .gap = 2) {
                        cui_text(ui, "Waiting for the first samples",
                                 .font = CUI_FONT_MEDIUM, .size = 13);
                        cui_text(ui, "aria2 reports speed about once a second.", .size = 12,
                                 .color = CUI_COLOR_TEXT_SECONDARY);
                    }
                }
            }
        }
        cui_card(ui, .width = CUI_FILL, .pad = 16, .gap = 10) {
            cui_row(ui, .width = CUI_FILL, .gap = 10, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_FOLDER, 16, CUI_COLOR_TEXT_SECONDARY);
                cui_text(ui, "Download folder", .font = CUI_FONT_MEDIUM, .size = 13,
                         .nowrap = true);
                cui_text(ui, a->cfg.download_dir, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .nowrap = true);
                if (cui_button(ui, "Open folder", .size = CUI_SIZE_SM,
                               .icon = CUI_ICON_FOLDER_OPEN))
                    open_folder(a->cfg.download_dir);
            }
            unsigned long long free_bytes = 0, total_bytes = 0;
            if (disk_usage(a->cfg.download_dir, &free_bytes, &total_bytes)) {
                char free_text[32], total_text[32], line[96];
                fmt_disk(free_text, sizeof free_text, free_bytes);
                fmt_disk(total_text, sizeof total_text, total_bytes);
                snprintf(line, sizeof line, "%s free of %s", free_text, total_text);
                double used = total_bytes ? 1.0 - (double)free_bytes / (double)total_bytes : 0;
                cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
                    cui_progress(ui, (float)used, .height = 8, .width = CUI_FILL,
                                 .tone = used > 0.9 ? CUI_TONE_DANGER :
                                         used > 0.75 ? CUI_TONE_WARNING : CUI_TONE_NEUTRAL);
                    cui_text(ui, line, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY,
                             .nowrap = true, .tabular = true);
                }
            } else {
                cui_text(ui, "Cannot read the download folder.", .size = 12,
                         .color = CUI_COLOR_DANGER);
            }
        }
    }
}

static void sched_name(const char *url, char *out, size_t cap) {
    if (!strncmp(url, "magnet:", 7)) {
        const char *dn = strstr(url, "dn=");
        if (dn) {
            dn += 3;
            size_t o = 0;
            while (*dn && *dn != '&' && o + 1 < cap) {
                if (*dn == '%' && isxdigit((unsigned char)dn[1]) &&
                    isxdigit((unsigned char)dn[2])) {
                    char hex[3] = {dn[1], dn[2], 0};
                    out[o++] = (char)strtol(hex, NULL, 16);
                    dn += 3;
                } else if (*dn == '+') {
                    out[o++] = ' ';
                    dn++;
                } else {
                    out[o++] = *dn++;
                }
            }
            out[o] = 0;
            if (o) return;
        }
        snprintf(out, cap, "Magnet link");
        return;
    }
    const char *slash = strrchr(url, '/');
    const char *name = slash && slash[1] ? slash + 1 : url;
    size_t o = 0;
    while (name[o] && name[o] != '?' && name[o] != '#' && o + 1 < cap) {
        out[o] = name[o];
        o++;
    }
    out[o] = 0;
    if (!o) snprintf(out, cap, "Link");
}

static void fmt_sched_time(long long due, char *out, size_t cap) {
    time_t nowt = time(NULL);
    time_t t = (time_t)due;
    struct tm tm, now, tomorrow;
    localtime_r(&t, &tm);
    localtime_r(&nowt, &now);
    if (tm.tm_year == now.tm_year && tm.tm_yday == now.tm_yday) {
        strftime(out, cap, "Today at %H:%M", &tm);
        return;
    }
    time_t tom = nowt + 86400;
    localtime_r(&tom, &tomorrow);
    if (tm.tm_year == tomorrow.tm_year && tm.tm_yday == tomorrow.tm_yday)
        strftime(out, cap, "Tomorrow at %H:%M", &tm);
    else
        strftime(out, cap, "%a %d %b at %H:%M", &tm);
}

static void fmt_countdown(long long secs, char *out, size_t cap) {
    if (secs <= 0) {
        snprintf(out, cap, "now");
        return;
    }
    if (secs < 60) {
        snprintf(out, cap, "in <1m");
        return;
    }
    long long mins = (secs + 59) / 60;   /* round up: 59m 40s reads better as 1h 00m */
    if (mins < 60) snprintf(out, cap, "in %lldm", mins);
    else if (mins < 1440) snprintf(out, cap, "in %lldh %02lldm", mins / 60, mins % 60);
    else snprintf(out, cap, "in %lldd %lldh", mins / 1440, (mins % 1440) / 60);
}

static void schedule_page(App *a, cui_ctx *ui, float opacity) {
    long long now = (long long)time(NULL);
    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 10,
               .opacity = opacity) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_column(ui, .gap = 2) {
                cui_heading(ui, "Scheduled downloads", 2);
                cui_text(ui, "Queue links to start at a set time. Times are local.",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            }
            cui_spacer(ui);
            if (cui_button(ui, "Schedule download", .icon = CUI_ICON_CALENDAR,
                           .variant = CUI_BUTTON_PRIMARY))
                open_schedule(a);
        }
        if (!a->connected && a->sched_count) {
            cui_row(ui, .width = CUI_FILL, .bg = CUI_COLOR_SURFACE_RAISED, .border = 1,
                    .border_color = CUI_COLOR_WARNING, .radius = 8, .pad_x = 12, .pad_y = 8,
                    .gap = 10, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_CIRCLE_ALERT, 16, CUI_COLOR_WARNING);
                cui_text(ui, "aria2 is offline", .font = CUI_FONT_MEDIUM);
                cui_text(ui, "Due downloads wait for the engine.", .size = 12,
                         .color = CUI_COLOR_TEXT_SECONDARY);
                cui_spacer(ui);
                if (cui_button(ui, "Start aria2", .size = CUI_SIZE_SM)) start_engine(a);
            }
        }
        if (a->sched_count == 0) {
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_CENTER,
                       .justify = CUI_JUSTIFY_CENTER, .gap = 10) {
                cui_icon(ui, CUI_ICON_CALENDAR, 36, CUI_COLOR_TEXT_MUTED);
                cui_text(ui, "No scheduled downloads", .font = CUI_FONT_MEDIUM, .size = 15);
                cui_text(ui, "Pick a link and a start time; Greed adds it to aria2 for you.",
                         .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
                if (cui_button(ui, "Schedule a download", .icon = CUI_ICON_CALENDAR))
                    open_schedule(a);
            }
        } else {
            static const cui_table_column cols[] = {
                {"NAME", CUI_FILL, CUI_TEXT_START, false},
                {"STARTS", CUI_PX(170), CUI_TEXT_START, false},
                {"IN", CUI_PX(96), CUI_TEXT_END, false},
                {"STATUS", CUI_PX(140), CUI_TEXT_START, false},
                {"", CUI_PX(36), CUI_TEXT_END, false},
            };
            int n = a->sched_count;
            int remove_idx = -1;
            /* soonest first: the list is small, so insertion sort is enough */
            int order[GREED_MAX_SCHEDULED];
            for (int i = 0; i < n; i++) order[i] = i;
            for (int i = 1; i < n; i++) {
                int k = order[i], j = i - 1;
                while (j >= 0 && a->sched[order[j]].due > a->sched[k].due) {
                    order[j + 1] = order[j];
                    j--;
                }
                order[j + 1] = k;
            }
            cui_table(ui, "scheduled", cols, 5, .height = CUI_FILL, .striped = true,
                      .compact = true) {
                for (int r = 0; r < n; r++) {
                    int i = order[r];
                    GreedScheduled *s = &a->sched[i];
                    char name[256], when[64], in[48];
                    sched_name(s->url, name, sizeof name);
                    fmt_sched_time(s->due, when, sizeof when);
                    fmt_countdown(s->due - now, in, sizeof in);
                    bool overdue = s->due <= now;
                    cui_table_row(ui, false);
                    cui_table_cell_text(ui, name);
                    cui_table_cell_text(ui, when);
                    cui_table_cell(ui);
                    {
                        cui_text(ui, in, .size = 12,
                                 .color = overdue ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_SECONDARY,
                                 .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
                    }
                    cui_table_cell(ui);
                    {
                        const char *label = "Scheduled";
                        cui_color tone = CUI_COLOR_TEXT_SECONDARY;
                        if (overdue && a->connected) {
                            label = "Starting…";
                            tone = CUI_COLOR_ACCENT;
                        } else if (overdue) {
                            label = "Waiting for aria2";
                            tone = CUI_COLOR_DANGER;
                        }
                        cui_text(ui, label, .size = 12, .color = tone);
                    }
                    cui_table_cell(ui);
                    {
                        if (cui_icon_button(ui, CUI_ICON_TRASH_2, .size = CUI_SIZE_SM,
                                            .tooltip = "Remove schedule"))
                            remove_idx = i;
                    }
                }
            }
            if (remove_idx >= 0) {
                memmove(&a->sched[remove_idx], &a->sched[remove_idx + 1],
                        (size_t)(a->sched_count - remove_idx - 1) * sizeof a->sched[0]);
                a->sched_count--;
                config_schedules_save(a->sched, a->sched_count);
                set_status(a, "Schedule removed");
            }
        }
        cui_text(ui, "Greed must be running at the scheduled time.", .size = 11,
                 .color = CUI_COLOR_TEXT_SECONDARY);
    }
}

static void content(App *a, cui_ctx *ui) {
    int page = a->view;
    if (page != a->page_key) {
        a->page_key = page;
        a->page_fade = 0.0f;
    }
    float speed = a->show_settings ? a->edit.motion_speed : a->cfg.motion_speed;
    a->page_fade += (1.0f - a->page_fade) * (1.0f - expf(-cui_delta_time(ui) * 14.0f * speed));
    float page_opacity = a->page_fade > 0.999f ? 0.0f : (a->page_fade > 0.001f ? a->page_fade : 0.001f);
    if (a->view == VIEW_STATS) {
        stats_page(a, ui, page_opacity);
        return;
    }
    if (a->view == VIEW_SCHEDULED) {
        schedule_page(a, ui, page_opacity);
        return;
    }

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 10,
               .opacity = page_opacity) {
        if (!a->connected) {
            cui_row(ui, .width = CUI_FILL, .bg = CUI_COLOR_SURFACE_RAISED, .border = 1,
                    .border_color = CUI_COLOR_DANGER, .radius = 8, .pad_x = 12, .pad_y = 8,
                    .gap = 10, .align = CUI_ALIGN_CENTER) {
                cui_icon(ui, CUI_ICON_CIRCLE_ALERT, 16, CUI_COLOR_DANGER);
                cui_text(ui, "aria2 is offline", .font = CUI_FONT_MEDIUM);
                cui_text(ui, "Start the engine to download.", .size = 12,
                         .color = CUI_COLOR_TEXT_SECONDARY);
                cui_spacer(ui);
                if (cui_button(ui, "Start aria2", .size = CUI_SIZE_SM)) start_engine(a);
            }
        }
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_search(ui, "filter", a->filter, sizeof a->filter, .placeholder = "Search downloads",
                       .width = CUI_FILL, .autofocus = a->focus_search);
            a->focus_search = false;
            char shown[48];
            if (a->sel_count > 1) snprintf(shown, sizeof shown, "%d selected", a->sel_count);
            else snprintf(shown, sizeof shown, "%d shown", collect_rows(a, NULL, 0));
            cui_text(ui, shown, .size = 12,
                     .color = a->sel_count > 1 ? CUI_COLOR_ACCENT : CUI_COLOR_TEXT_MUTED);
            if (cui_icon_button(ui, CUI_ICON_PAUSE, .size = CUI_SIZE_SM, .tooltip = "Pause",
                                .disabled = !selection_can_pause(a)))
                pause_selection(a);
            if (cui_icon_button(ui, CUI_ICON_PLAY, .size = CUI_SIZE_SM, .tooltip = "Resume",
                                .disabled = !selection_can_resume(a)))
                resume_selection(a);
            if (cui_icon_button(ui, CUI_ICON_TRASH_2, .size = CUI_SIZE_SM, .tooltip = "Remove",
                                .disabled = a->sel_count == 0))
                remove_selected(a);
        }
        download_table(a, ui);
    }
}

static void statusbar(App *a, cui_ctx *ui) {
    char count[64];
    if (a->sel_count > 1)
        snprintf(count, sizeof count, "%d selected · %d downloads", a->sel_count, a->count);
    else
        snprintf(count, sizeof count, "%d downloads", a->count);
    cui_status_bar(ui, "statusbar") {
        cui_status_bar_text(ui, a->status);
        cui_status_bar_spacer(ui);
        cui_status_bar_text(ui, count);
    }
}

static void add_dialog(App *a, cui_ctx *ui) {
    if (!a->show_add) return;
    cui_dialog(ui, "Add download", &a->show_add, .width = 560) {
        cui_label(ui, "Links");
        cui_textarea(ui, "links", a->add_urls, sizeof a->add_urls,
                     .placeholder = "One link per line: http, magnet, or .torrent",
                     .height = 104);
        cui_label(ui, "Save to");
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_input(ui, "add-dir", a->add_dir, sizeof a->add_dir, .width = CUI_FILL);
            if (cui_button(ui, "Browse…", .size = CUI_SIZE_SM)) {
                char path[PATH_MAX];
                if (cui_app_file_dialog(NULL,
                                        &(cui_file_dialog_opts){.kind = CUI_FILE_FOLDER,
                                                                .title = "Save downloads to"},
                                        path, sizeof path))
                    snprintf(a->add_dir, sizeof a->add_dir, "%s", path);
            }
        }
        cui_row(ui, .width = CUI_FILL, .gap = 12, .align = CUI_ALIGN_CENTER) {
            cui_input(ui, "add-name", a->add_name, sizeof a->add_name,
                      .placeholder = "Filename (optional, single link)", .width = CUI_FILL);
            double conn = a->add_conn;
            if (cui_number(ui, "add-conn", &conn, 1, 16, .label = "Connections", .step = 1))
                a->add_conn = (int)conn;
        }
        if (cui_button(ui, "Open .torrent…", .icon = CUI_ICON_FILE, .size = CUI_SIZE_SM,
                       .variant = CUI_BUTTON_GHOST)) {
            char path[PATH_MAX];
            if (cui_app_file_dialog(NULL,
                                    &(cui_file_dialog_opts){.kind = CUI_FILE_OPEN,
                                                            .title = "Open torrent",
                                                            .filter_name = "Torrent",
                                                            .filter = "torrent"},
                                    path, sizeof path)) {
                char *text = malloc(sizeof a->add_urls);
                if (text) {
                    snprintf(text, sizeof a->add_urls, "%s", a->add_urls);
                    size_t used = strlen(text);
                    snprintf(text + used, sizeof a->add_urls - used, "%s%s", used ? "\n" : "", path);
                    add_links(a, text, a->add_dir, NULL, a->add_conn);
                    free(text);
                }
            }
        }
        cui_dialog_footer(ui) {
            if (cui_button(ui, "Cancel", .variant = CUI_BUTTON_GHOST)) a->show_add = false;
            if (cui_button(ui, "Add", .variant = CUI_BUTTON_PRIMARY)) {
                const char *name = a->add_name[0] ? a->add_name : NULL;
                add_links(a, a->add_urls, a->add_dir, name, a->add_conn);
            }
        }
    }
}

static time_t schedule_due(const App *a) {
    struct tm t = {0};
    t.tm_year = a->sched_date.year - 1900;
    t.tm_mon = a->sched_date.month - 1;
    t.tm_mday = a->sched_date.day;
    t.tm_hour = a->sched_hour;
    t.tm_min = a->sched_min;
    t.tm_isdst = -1;
    return mktime(&t);
}

static void schedule_dialog(App *a, cui_ctx *ui) {
    if (!a->show_schedule) return;
    time_t due = schedule_due(a);
    time_t now = time(NULL);
    bool valid = a->sched_url[0] && due > now;
    char when[80] = {0};
    if (due > 0) fmt_sched_time((long long)due, when, sizeof when);

    cui_dialog(ui, "Schedule download", &a->show_schedule, .width = 580) {
        cui_label(ui, "Link");
        cui_input(ui, "sched-url", a->sched_url, sizeof a->sched_url,
                  .placeholder = "https://example.com/file.iso or magnet link");
        cui_label(ui, "Start");
        static const char *presets[] = {"In 15 min", "In 1 hour", "At 22:00", "At 02:00"};
        cui_row(ui, .width = CUI_FILL, .gap = 6) {
            for (int i = 0; i < 4; i++) {
                bool on = a->sched_preset == i;
                if (cui_chip(ui, presets[i], &on) && on) {
                    a->sched_preset = i;
                    set_sched_due(a, sched_preset_due(i));
                }
            }
        }
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_END) {
            if (cui_date_picker(ui, "sched-date", &a->sched_date, .label = "Date",
                                .width = CUI_PX(200)))
                a->sched_preset = -1;
            if (cui_time_picker(ui, "sched-time", &a->sched_hour, &a->sched_min,
                                .label = "Time", .minute_step = 1, .width = CUI_PX(130)))
                a->sched_preset = -1;
        }
        if (due <= now) {
            cui_text(ui, "Choose a time in the future.", .size = 12,
                     .color = CUI_COLOR_DANGER);
        } else if (when[0]) {
            char in[48], line[160];
            fmt_countdown((long long)(due - now), in, sizeof in);
            snprintf(line, sizeof line, "Starts %s (%s)", when, in);
            cui_text(ui, line, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
        }
        cui_text(ui, "Times are local. Greed must be running when a download starts.",
                 .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
        cui_dialog_footer(ui) {
            if (cui_button(ui, "Cancel", .variant = CUI_BUTTON_GHOST))
                a->show_schedule = false;
            if (cui_button(ui, "Schedule", .variant = CUI_BUTTON_PRIMARY,
                           .disabled = !valid)) {
                if (a->sched_count >= GREED_MAX_SCHEDULED) {
                    set_status(a, "The schedule is full (%d entries)", GREED_MAX_SCHEDULED);
                } else {
                    GreedScheduled *s = &a->sched[a->sched_count++];
                    snprintf(s->url, sizeof s->url, "%s", a->sched_url);
                    snprintf(s->dir, sizeof s->dir, "%s", a->cfg.download_dir);
                    s->due = (long long)due;
                    config_schedules_save(a->sched, a->sched_count);
                    a->show_schedule = false;
                    a->view = VIEW_SCHEDULED;
                    set_status(a, "Scheduled for %s", when);
                }
            }
        }
    }
}

static bool theme_matches(const GreedTheme *t, const char *query, int filter) {
    if (filter == 1 && !t->dark) return false;
    if (filter == 2 && t->dark) return false;
    if (!query[0]) return true;
    char name[96], needle[96];
    size_t i = 0;
    for (; t->name[i] && i + 1 < sizeof name; i++)
        name[i] = (char)tolower((unsigned char)t->name[i]);
    name[i] = 0;
    for (i = 0; query[i] && i + 1 < sizeof needle; i++)
        needle[i] = (char)tolower((unsigned char)query[i]);
    needle[i] = 0;
    return strstr(name, needle) != NULL;
}

static bool theme_card(cui_ctx *ui, const GreedTheme *t, bool current) {
    bool clicked = cui_card_begin(ui, &(cui_card_opts){
        .id = t->name, .width = CUI_PX(184), .pad = 7, .gap = 6, .hoverable = true,
        .selected = current});
    cui_box(ui, .width = CUI_FILL, .height = CUI_PX(46), .radius = 5, .pad = 4,
            .bg = CUI_RGB(t->bg), .border = 1, .border_color = CUI_RGB(t->border)) {
        cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 4) {
            cui_box(ui, .width = CUI_PX(20), .height = CUI_FILL, .radius = 3,
                    .bg = CUI_RGB(t->surface));
            cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .gap = 4,
                       .justify = CUI_JUSTIFY_CENTER) {
                cui_row(ui, .width = CUI_FILL, .gap = 3, .align = CUI_ALIGN_CENTER) {
                    cui_box(ui, .width = CUI_PX(22), .height = CUI_PX(7), .radius = 2,
                            .bg = CUI_RGB(t->accent));
                    cui_box(ui, .width = CUI_FILL, .height = CUI_PX(5), .radius = 2,
                            .bg = CUI_RGB(t->text));
                }
                cui_box(ui, .width = CUI_FILL, .height = CUI_PX(5), .radius = 2,
                        .bg = CUI_RGB(t->dim));
                cui_box(ui, .width = CUI_FILL, .height = CUI_PX(5), .radius = 2,
                        .bg = CUI_RGB(t->raised));
            }
        }
    }
    cui_row(ui, .width = CUI_FILL, .gap = 4, .align = CUI_ALIGN_CENTER) {
        cui_text(ui, t->name, .size = 11, .font = CUI_FONT_MEDIUM, .width = CUI_FILL,
                 .nowrap = true);
        if (current) cui_icon(ui, CUI_ICON_CHECK, 12, CUI_COLOR_ACCENT_STRONG);
    }
    cui_card_end(ui);
    return clicked;
}

static void settings_dialog(App *a, cui_ctx *ui) {
    if (!a->show_settings) return;
    static const char *tabs[] = {"General", "Bandwidth", "Torrents", "Appearance"};
    static const char *icons[] = {CUI_ICON_SETTINGS, CUI_ICON_SLIDERS_HORIZONTAL,
                                  CUI_ICON_GLOBE, CUI_ICON_PALETTE};
    float body_h = cui_viewport_size(ui).y > 760 ? 430 : 300;
    cui_dialog(ui, "Settings", &a->show_settings, .width = 640) {
        cui_tabs(ui, "settings-tabs", tabs, 4, &a->settings_tab, .icons = icons);
        if (a->settings_seen_tab != a->settings_tab) {
            cui_scroll_to(ui, "settings-body", 0);
            a->settings_seen_tab = a->settings_tab;
        }
        cui_scroll(ui, .id = "settings-body", .width = CUI_FILL, .height = CUI_PX(body_h),
                   .gap = 16, .fade_edges = true,
                   .fade_color = CUI_COLOR_SURFACE_RAISED) {
            if (a->settings_tab == 0) {
                cui_heading(ui, "Storage", 4);
                cui_label(ui, "Save to");
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_input(ui, "set-dir", a->edit.download_dir,
                              sizeof a->edit.download_dir, .width = CUI_FILL);
                    if (cui_button(ui, "Browse…", .size = CUI_SIZE_SM)) {
                        char path[PATH_MAX];
                        if (cui_app_file_dialog(NULL,
                                                &(cui_file_dialog_opts){
                                                    .kind = CUI_FILE_FOLDER,
                                                    .title = "Save downloads to"},
                                                path, sizeof path))
                            snprintf(a->edit.download_dir, sizeof a->edit.download_dir, "%s",
                                     path);
                    }
                }
                cui_muted(ui, "New downloads start here. Existing downloads keep their folder.");

                cui_heading(ui, "Engine", 4);
                float concurrent = (float)a->edit.max_concurrent;
                if (cui_slider(ui, "Max concurrent downloads", &concurrent, 1, 16,
                               .step = 1, .show_value = true, .width = CUI_FILL))
                    a->edit.max_concurrent = (int)concurrent;
                double port = a->edit.rpc_port;
                if (cui_number(ui, "set-port", &port, 1024, 65535, .label = "RPC port",
                               .step = 1, .width = CUI_PX(240)))
                    a->edit.rpc_port = (int)port;
                cui_muted(ui, "A new RPC port applies after Greed restarts.");

                cui_heading(ui, "Behavior", 4);
                cui_switch(ui, "Start aria2 automatically", &a->edit.auto_start_aria2,
                           .description = "Launch the engine when Greed opens.");
                cui_switch(ui, "Watch clipboard for links", &a->edit.clipboard_monitor,
                           .description = "Offer a download when a link is copied.");
                cui_switch(ui, "Notify when a download completes", &a->edit.notify_on_complete,
                           .description = "Send a desktop notification on completion.");
                cui_switch(ui, "Verify checksums after download", &a->edit.verify_after_complete,
                           .description =
                               "Record a SHA-256 baseline and check it after every download.");

                cui_heading(ui, "Interface", 4);
                float speed = a->edit.motion_speed;
                if (cui_slider(ui, "Animation speed", &speed, 0.5f, 2.0f, .step = 0.1f,
                               .show_value = true, .format = "%.1fx", .width = CUI_FILL))
                    a->edit.motion_speed = speed;
                cui_muted(ui, "How fast panels and menus settle. Applies immediately.");
            } else if (a->settings_tab == 1) {
                cui_heading(ui, "Live throughput", 4);
                if (a->hist_count > 1) {
                    float peak = 0;
                    for (int i = 0; i < a->hist_count; i++)
                        if (a->hist_down[i] > peak) peak = a->hist_down[i];
                    char limit_label[48];
                    cui_chart_annotation limit_line;
                    int lines = 0;
                    float limit = (float)a->edit.max_download_limit;
                    if (limit > 0) {
                        snprintf(limit_label, sizeof limit_label, "Limit %d KB/s",
                                 a->edit.max_download_limit);
                        limit_line = (cui_chart_annotation){
                            .kind = CUI_ANNOTATION_Y, .y = limit, .label = limit_label};
                        lines = 1;
                    }
                    cui_series series = {"Download", a->hist_down, a->hist_count,
                                         CUI_COLOR_ACCENT_STRONG};
                    cui_line_chart(ui, "bw-history", &series, 1, .height = 150,
                                   .labels = a->hist_labels, .label_count = a->hist_count,
                                   .smooth = true, .area = true, .format = "%.0f",
                                   .annotations = lines ? &limit_line : NULL,
                                   .annotation_count = lines, .min = 0,
                                   .max = limit > peak ? limit * 1.15f : 0);
                } else {
                    cui_muted(ui, "Collecting samples…");
                }
                cui_muted(ui, "Download speed in KB/s, sampled over the last minute.");

                cui_heading(ui, "Global limits", 4);
                cui_muted(ui, "0 means unlimited. Limits apply to every download, and reach "
                              "the running engine when you save.");
                float download = (float)a->edit.max_download_limit;
                if (cui_slider(ui, "Download limit", &download, 0, 102400, .step = 128,
                               .show_value = true, .format = "%.0f KB/s", .width = CUI_FILL))
                    a->edit.max_download_limit = (int)download;
                float upload = (float)a->edit.max_upload_limit;
                if (cui_slider(ui, "Upload limit", &upload, 0, 51200, .step = 64,
                               .show_value = true, .format = "%.0f KB/s", .width = CUI_FILL))
                    a->edit.max_upload_limit = (int)upload;
                cui_muted(ui, "The title bar shows the current transfer rates.");
            } else if (a->settings_tab == 2) {
                cui_heading(ui, "Seeding", 4);
                cui_switch(ui, "Keep seeding after download",
                           &a->edit.seed_after_complete,
                           .description = "Share finished torrents with other peers.");
                float ratio = a->edit.seed_ratio;
                if (cui_slider(ui, "Stop at ratio", &ratio, 0.5f, 5.0f, .step = 0.5f,
                               .show_value = true, .format = "%.1f×",
                               .disabled = !a->edit.seed_after_complete,
                               .width = CUI_FILL))
                    a->edit.seed_ratio = ratio;
                cui_muted(ui, "aria2 stops a torrent when its uploaded bytes reach this "
                              "multiple of the downloaded bytes.");
            } else {
                static const char *filters[] = {"All", "Dark", "Light"};
                cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
                    cui_search(ui, "theme-search", a->theme_query, sizeof a->theme_query,
                               .placeholder = "Search themes", .icon = CUI_ICON_SEARCH,
                               .width = CUI_FILL);
                    cui_segmented(ui, "theme-filter", filters, 3, &a->theme_filter,
                                  .size = CUI_SIZE_SM);
                }
                int shown = 0;
                for (int i = 0; i < greed_theme_count; i++)
                    if (theme_matches(&greed_themes[i], a->theme_query, a->theme_filter))
                        shown++;
                char count[48];
                snprintf(count, sizeof count, "%d theme%s", shown, shown == 1 ? "" : "s");
                cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                    cui_text(ui, count, .size = 11, .color = CUI_COLOR_TEXT_SECONDARY);
                    cui_spacer(ui);
                    cui_muted(ui, "Changes preview instantly; Save keeps it.");
                }
                if (!shown) {
                    cui_muted(ui, "No theme matches that search.");
                } else {
                    cui_box(ui, .dir = CUI_GRID, .columns = 3, .gap = 10, .width = CUI_FILL) {
                        for (int i = 0; i < greed_theme_count; i++) {
                            const GreedTheme *t = &greed_themes[i];
                            if (!theme_matches(t, a->theme_query, a->theme_filter)) continue;
                            if (theme_card(ui, t, !strcmp(a->edit.theme, t->name)))
                                snprintf(a->edit.theme, sizeof a->edit.theme, "%s", t->name);
                        }
                    }
                }
            }
        }
        cui_dialog_footer(ui) {
            if (cui_button(ui, "Cancel", .variant = CUI_BUTTON_GHOST)) a->show_settings = false;
            if (cui_button(ui, "Save", .variant = CUI_BUTTON_PRIMARY)) {
                a->cfg = a->edit;
                if (!config_save(&a->cfg)) set_status(a, "Could not save settings");
                else set_status(a, "Settings saved");
                a->engine_applied = false;
                apply_engine_options(a);
                a->show_settings = false;
                a->last_poll = 0;
            }
        }
    }
}

static void info_dialog(App *a, cui_ctx *ui) {
    if (!a->show_info) return;
    Download *d = find_item(a, a->info_gid);
    if (!d) {
        a->show_info = false;
        return;
    }
    cui_dialog(ui, "Download info", &a->show_info, .width = 560) {
        cui_text(ui, d->name, .font = CUI_FONT_MEDIUM, .size = 15, .line_height = 1.3f);
        cui_row(ui, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_badge(ui, download_status_label(d), .tone = status_tone(d), .dot = true);
        }
        double p = download_progress(d);
        char pct[32];
        snprintf(pct, sizeof pct, "%.1f%%", p * 100.0);
        cui_progress(ui, (float)p, .height = 6, .tone = status_tone(d));
        kv(ui, "Progress", pct);
        char size[32], done[32], speed[32], up[32], eta[32], num[32];
        if (d->total) fmt_bytes(size, sizeof size, d->total);
        else snprintf(size, sizeof size, "n/a");
        fmt_bytes(done, sizeof done, d->completed);
        fmt_speed(speed, sizeof speed, d->down_speed);
        fmt_speed(up, sizeof up, d->up_speed);
        unsigned long long secs;
        if (download_eta(d, &secs)) fmt_eta(eta, sizeof eta, secs);
        else snprintf(eta, sizeof eta, "n/a");
        kv(ui, "Size", size);
        kv(ui, "Downloaded", done);
        kv(ui, "Speed", speed);
        kv(ui, "Upload", up);
        kv(ui, "ETA", eta);
        snprintf(num, sizeof num, "%u", d->connections);
        kv(ui, "Connections", num);
        if (d->is_torrent) {
            snprintf(num, sizeof num, "%u", d->seeders);
            kv(ui, "Seeders", num);
        }
        if (d->file_count > 1) {
            snprintf(num, sizeof num, "%d", d->file_count);
            kv(ui, "Files", num);
        }
        kv(ui, "GID", d->gid);
        cui_text(ui, "Directory", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        cui_text(ui, d->dir, .size = 12, .font = CUI_FONT_MONO,
                 .color = CUI_COLOR_TEXT_SECONDARY);
        cui_text(ui, "URL", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
        cui_text(ui, d->url, .size = 10, .font = CUI_FONT_MONO,
                 .color = CUI_COLOR_TEXT_SECONDARY);

        char file[PATH_MAX];
        if (download_path(d, file, sizeof file)) {
            char hex[65];
            int state = checksum_stored(file, hex, sizeof hex);
            cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
                cui_text(ui, "SHA-256", .size = 12, .color = CUI_COLOR_TEXT_MUTED);
                cui_spacer(ui);
                if (state)
                    cui_badge(ui, state == 1 ? "Verified" : "Mismatch",
                              .tone = state == 1 ? CUI_TONE_SUCCESS : CUI_TONE_DANGER, .dot = true);
            }
            cui_text(ui, state ? hex : "No baseline recorded yet", .size = 10,
                     .font = CUI_FONT_MONO, .color = CUI_COLOR_TEXT_SECONDARY);
        } else {
            cui_muted(ui, "Checksum verification is not available for multi-file torrents.");
        }

        cui_dialog_footer(ui) {
            if (download_is_finished(d) && d->file_count <= 1 &&
                cui_button(ui, "Verify checksum", .icon = CUI_ICON_CHECK_CHECK,
                           .variant = CUI_BUTTON_GHOST))
                verify_download(a, d);
            if (cui_button(ui, "Close", .variant = CUI_BUTTON_PRIMARY)) a->show_info = false;
        }
    }
}

/* Select every row the current tab and filter show. */
static void select_all(App *a) {
    int rows[512];
    int n = collect_rows(a, rows, 512);
    for (int i = 0; i < n; i++) selection_add(a, a->items[rows[i]].gid);
    if (a->sel_count) snprintf(a->selected, sizeof a->selected, "%s", a->sel[0]);
    a->details_open = a->sel_count > 0;
}

static void handle_keys(App *a, cui_ctx *ui) {
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_N)) open_add(a);
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_B)) a->sidebar_open = !a->sidebar_open;
    if (!cui_wants_keyboard(ui) && cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_A)) select_all(a);
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_F)) {
        a->view = VIEW_DOWNLOADS;
        a->focus_search = true;
    }
    if (cui_wants_keyboard(ui)) return;
    if (cui_key_pressed(ui, CUI_KEY_DELETE)) remove_selected(a);
    if (cui_key_pressed(ui, CUI_KEY_SPACE)) toggle_selected(a);
    if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) selection_clear(a);
}

static void frame(cui_ctx *ui, void *user) {
    App *a = user;
    apply_theme(ui, a->show_settings ? a->edit.theme : a->cfg.theme,
                a->show_settings ? a->edit.motion_speed : a->cfg.motion_speed);

    if (!a->frame_ready) {
        void *native = cui_app_native_window(NULL);
        if (frame_init(&a->frame, native)) frame_make_frameless(&a->frame);
        a->frame_ready = true;
    }

    handle_keys(a, ui);

    double now = cui_time(ui);
    if (now - a->last_poll >= 0.8) {
        a->last_poll = now;
        bool was_connected = a->connected;
        poll_engine(a);
        if (a->connected && (!was_connected || !a->engine_applied))
            apply_engine_options(a);
        sample_speed(a);
    }
    checksum_tick(a);
    schedule_tick(a);
    /* Screenshot/test hook: GREED_DEMO=details selects the first download. */
    if (a->demo_details && !a->selected[0] && a->count)
        snprintf(a->selected, sizeof a->selected, "%s", a->items[0].gid);
    poll_clipboard(a, ui);
    cui_request_wakeup(ui, 0.8f);
    /* Our fades and the drag band are not CUI tweens, so keep frames coming. */
    bool animating = a->page_fade < 0.999f || a->drag_active ||
                     (a->sidebar_open &&
                      fabsf(a->conn_fade - (a->connected ? 1.0f : 0.0f)) > 0.01f);
    for (int i = 0; i < a->fade_count && !animating; i++)
        if (a->fade[i].v < 0.99f) animating = true;
    if (animating) cui_request_wakeup(ui, 1.0f / 60.0f);

    if (a->clip_toast >= 0 && cui_toast_action_clicked(ui, a->clip_toast)) {
        add_links(a, a->clip_link, a->cfg.download_dir, NULL, a->cfg.max_concurrent);
        cui_toast_dismiss(ui, a->clip_toast);
        a->clip_toast = -1;
    }

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .bg = CUI_COLOR_BACKGROUND) {
        titlebar(a, ui);
        frame_interactions(a, ui);
        cui_separator(ui);
        cui_row(ui, .width = CUI_FILL, .height = CUI_FILL, .align = CUI_ALIGN_STRETCH) {
            float sw = cui_animate_spring(ui, cui_make_id(ui, "sidebar-width"),
                                          a->sidebar_open ? 198.0f : 0.0f, 0.28f, 0.0f);
            if (sw > 0.5f) sidebar(a, ui, sw);
            content(a, ui);
            details(a, ui);
        }
        cui_separator(ui);
        statusbar(a, ui);
    }

    add_dialog(a, ui);
    schedule_dialog(a, ui);
    settings_dialog(a, ui);
    info_dialog(a, ui);
}

int ui_run(void) {
    Config cfg;
    config_load(&cfg);
    if (cfg.auto_start_aria2) aria2_ensure_running(&cfg, true);

    App *a = calloc(1, sizeof *a);
    if (!a) return 1;
    a->cfg = cfg;
    a->edit = cfg;
    a->clip_toast = -1;
    a->sidebar_open = true;
    a->add_conn = cfg.max_concurrent;
    snprintf(a->add_dir, sizeof a->add_dir, "%s", cfg.download_dir);
    snprintf(a->status, sizeof a->status, "Ready");
    rpc_init(&a->rpc, cfg.rpc_port);
    a->sched_count = config_schedules_load(a->sched, GREED_MAX_SCHEDULED);

    /* Test hook for screenshots: GREED_DEMO=add|settings|settings-bandwidth|settings-torrents|details|stats|empty */
    const char *demo = getenv("GREED_DEMO");
    if (demo && !strcmp(demo, "add")) open_add(a);
    else if (demo && !strcmp(demo, "settings")) open_settings(a);
    else if (demo && !strcmp(demo, "settings-bandwidth")) {
        open_settings(a);
        a->settings_tab = 1;
    } else if (demo && !strcmp(demo, "settings-torrents")) {
        open_settings(a);
        a->settings_tab = 2;
    } else if (demo && !strcmp(demo, "settings-appearance")) {
        open_settings(a);
        a->settings_tab = 3;
    } else if (demo && !strcmp(demo, "details")) a->demo_details = true;
    else if (demo && !strcmp(demo, "stats")) a->view = VIEW_STATS;
    else if (demo && !strcmp(demo, "scheduled")) a->view = VIEW_SCHEDULED;
    else if (demo && !strcmp(demo, "scheduled-add")) {
        a->view = VIEW_SCHEDULED;
        open_schedule(a);
    } else if (demo && !strcmp(demo, "empty")) snprintf(a->filter, sizeof a->filter, "zzzz");

    int rc = cui_app_run(&(cui_app_desc){.title = "Greed",
                                         .width = 1240,
                                         .height = 780,
                                         .min_width = 1000,
                                         .min_height = 620,
                                         .theme = CUI_THEME_DARK,
                                         .accent = CUI_ACCENT_VIOLET,
                                         .app_id = "greed"},
                         frame, a);

    frame_shutdown(&a->frame);
    free(a->items);
    free(a->known);
    free(a);
    return rc;
}
