#include "ui.h"

#include "config.h"
#include "model.h"
#include "rpc.h"
#include "x11.h"

#include "cui.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

enum { TAB_ALL, TAB_ACTIVE, TAB_WAITING, TAB_STOPPED, TAB_COMPLETE, TAB_COUNT };

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
    int tab;
    char filter[128];
    bool focus_search;
    char selected[32];
    bool show_add;
    bool show_settings;
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

static void apply_theme(cui_ctx *ui) {
    cui_theme *t = cui_theme_get(ui);
    t->colors[CUI_TOKEN_BACKGROUND] = CUI_RGB(0x121419);
    t->colors[CUI_TOKEN_SURFACE] = CUI_RGB(0x16181d);
    t->colors[CUI_TOKEN_SURFACE_RAISED] = CUI_RGB(0x1d2026);
    t->colors[CUI_TOKEN_SURFACE_MUTED] = CUI_RGB(0x16181d);
    t->colors[CUI_TOKEN_FOREGROUND] = CUI_RGB(0xecefee);
    t->colors[CUI_TOKEN_TEXT_SECONDARY] = CUI_RGB(0x9a9fa4);
    t->colors[CUI_TOKEN_TEXT_MUTED] = CUI_RGB(0x7d7977);
    t->colors[CUI_TOKEN_BORDER] = CUI_RGB(0x2a2d34);
    t->colors[CUI_TOKEN_BORDER_SUBTLE] = CUI_RGB(0x1e2126);
    t->colors[CUI_TOKEN_BORDER_STRONG] = CUI_RGB(0x47484e);
    t->colors[CUI_TOKEN_ACCENT] = CUI_RGB(0x95849b);
    t->colors[CUI_TOKEN_ACCENT_STRONG] = CUI_RGB(0xcbaeb9);
    t->colors[CUI_TOKEN_ACCENT_SUBTLE] = CUI_RGB(0x2a2d34);
    t->colors[CUI_TOKEN_ACCENT_FOREGROUND] = CUI_RGB(0x121419);
    t->colors[CUI_TOKEN_SUCCESS] = CUI_RGB(0xb49a70);
    t->colors[CUI_TOKEN_WARNING] = CUI_RGB(0xb49a70);
    t->colors[CUI_TOKEN_DANGER] = CUI_RGB(0xf87171);
    t->colors[CUI_TOKEN_INFO] = CUI_RGB(0x95849b);
    t->colors[CUI_TOKEN_HOVER] = CUI_RGB(0x1d2026);
    t->colors[CUI_TOKEN_ACTIVE] = CUI_RGB(0x2a2d34);
    t->colors[CUI_TOKEN_FOCUS_RING] = CUI_RGB(0xcbaeb9);
}

static cui_color status_color(const Download *d) {
    if (!strcmp(d->status, "active")) return CUI_COLOR_ACCENT_STRONG;
    if (!strcmp(d->status, "paused") || !strcmp(d->status, "complete")) return CUI_COLOR_WARNING;
    if (!strcmp(d->status, "error")) return CUI_COLOR_DANGER;
    return CUI_COLOR_TEXT_MUTED;
}

static cui_tone status_tone(const Download *d) {
    if (!strcmp(d->status, "active")) return CUI_TONE_ACCENT;
    if (!strcmp(d->status, "paused") || !strcmp(d->status, "complete")) return CUI_TONE_WARNING;
    if (!strcmp(d->status, "error")) return CUI_TONE_DANGER;
    return CUI_TONE_NEUTRAL;
}

static Download *find_item(App *a, const char *gid) {
    for (int i = 0; i < a->count; i++)
        if (!strcmp(a->items[i].gid, gid)) return &a->items[i];
    return NULL;
}

static void notify_send(const char *name) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("notify-send", "notify-send", "Greed — download complete", name, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) waitpid(pid, NULL, 0);
}

static void remember_statuses(App *a, const Download *items, int n, bool notify) {
    Known *next = n ? calloc((size_t)n, sizeof *next) : NULL;
    for (int i = 0; i < n; i++) {
        snprintf(next[i].gid, sizeof next[i].gid, "%s", items[i].gid);
        snprintf(next[i].status, sizeof next[i].status, "%s", items[i].status);
        if (!notify || !a->cfg.notify_on_complete) continue;
        for (int k = 0; k < a->known_count; k++) {
            if (strcmp(a->known[k].gid, items[i].gid)) continue;
            if (!strcmp(items[i].status, "complete") && strcmp(a->known[k].status, "complete"))
                notify_send(items[i].name);
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
        return;
    }
    a->connected = true;
    remember_statuses(a, fresh, n, a->known_count > 0);
    free(a->items);
    a->items = fresh;
    a->count = n;
    a->cap = cap;
    if (a->selected[0] && !find_item(a, a->selected)) a->selected[0] = 0;

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

static void add_links(App *a, const char *text, const char *dir, const char *out, int conn) {
    char *copy = strdup(text);
    if (!copy) return;
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
        a->add_urls[0] = 0;
        a->add_name[0] = 0;
        a->show_add = false;
    }
    a->last_poll = 0;
}

static void open_add(App *a) {
    a->show_add = true;
    snprintf(a->add_dir, sizeof a->add_dir, "%s", a->cfg.download_dir);
    a->add_conn = a->cfg.max_concurrent;
}

static void open_settings(App *a) {
    a->edit = a->cfg;
    a->show_settings = true;
}

static void toggle_selected(App *a) {
    Download *d = find_item(a, a->selected);
    if (!d) return;
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

static void remove_selected(App *a) {
    Download *d = find_item(a, a->selected);
    if (!d) return;
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
    a->selected[0] = 0;
    set_status(a, "Removed");
    a->last_poll = 0;
}

static void start_engine(App *a) {
    if (!aria2_available()) {
        set_status(a, "aria2 binary not found");
        return;
    }
    if (aria2_ensure_running(a->cfg.rpc_port, a->cfg.download_dir, a->cfg.max_concurrent, true)) {
        a->connected = true;
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

static void titlebar(App *a, cui_ctx *ui) {
    char down[32], up[32];
    fmt_speed(down, sizeof down, a->gs.down_speed);
    fmt_speed(up, sizeof up, a->gs.up_speed);

    cui_row(ui, .width = CUI_FILL, .height = CUI_PX(46), .bg = CUI_COLOR_SURFACE, .pad_x = 10,
            .gap = 8, .align = CUI_ALIGN_CENTER) {
        cui_row(ui, .id = "dragzone", .width = CUI_FILL, .height = CUI_FILL, .gap = 8,
                .align = CUI_ALIGN_CENTER) {
            cui_icon(ui, CUI_ICON_DOWNLOAD, 18, CUI_COLOR_ACCENT_STRONG);
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

static void sidebar(App *a, cui_ctx *ui) {
    int counts[TAB_COUNT] = {0};
    for (int i = 0; i < a->count; i++)
        for (int t = 0; t < TAB_COUNT; t++)
            if (tab_matches(t, &a->items[i])) counts[t]++;

    cui_column(ui, .width = CUI_PX(198), .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .pad = 10,
               .gap = 2) {
        cui_nav_section(ui, "LIBRARY");
        for (int t = 0; t < TAB_COUNT; t++) {
            char badge[16];
            snprintf(badge, sizeof badge, "%d", counts[t]);
            if (cui_nav_item(ui, tab_names[t], .icon = tab_icons[t], .badge = badge,
                             .selected = a->tab == t, .group = "tabs"))
                a->tab = t;
        }
        cui_box(ui, .height = CUI_PX(14)) {}
        cui_separator(ui);
        cui_nav_section(ui, "TRANSFER");
        char down[32], up[32];
        fmt_speed(down, sizeof down, a->gs.down_speed);
        fmt_speed(up, sizeof up, a->gs.up_speed);
        cui_row(ui, .width = CUI_FILL) {
            cui_text(ui, "Download", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_spacer(ui);
            cui_text(ui, down, .size = 12, .color = CUI_COLOR_ACCENT_STRONG, .tabular = true);
        }
        cui_row(ui, .width = CUI_FILL) {
            cui_text(ui, "Upload", .size = 12, .color = CUI_COLOR_TEXT_SECONDARY);
            cui_spacer(ui);
            cui_text(ui, up, .size = 12, .color = CUI_COLOR_TEXT_MUTED, .tabular = true);
        }
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
        cui_row(ui, .width = CUI_FILL, .pad_top = 8, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_box(ui, .width = CUI_PX(8), .height = CUI_PX(8), .radius = 4,
                    .bg = a->connected ? CUI_COLOR_SUCCESS : CUI_COLOR_DANGER);
            cui_text(ui, a->connected ? "aria2 connected" : "aria2 offline", .size = 12,
                     .color = CUI_COLOR_TEXT_MUTED);
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
    if (!d) return;
    cui_separator(ui);
    cui_column(ui, .width = CUI_PX(290), .height = CUI_FILL, .bg = CUI_COLOR_SURFACE, .pad = 14,
               .gap = 8) {
        cui_row(ui, .width = CUI_FILL, .align = CUI_ALIGN_CENTER) {
            cui_text(ui, "DETAILS", .size = 11, .font = CUI_FONT_MEDIUM,
                     .color = CUI_COLOR_TEXT_MUTED);
            cui_spacer(ui);
            if (cui_icon_button(ui, CUI_ICON_X, .size = CUI_SIZE_SM, .tooltip = "Close"))
                a->selected[0] = 0;
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
            else snprintf(eta, sizeof eta, "—");
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
            if (d->error[0])
                cui_alert(ui, "Download failed", .description = d->error, .tone = CUI_TONE_DANGER,
                          .icon = CUI_ICON_CIRCLE_ALERT);
            bool paused = !strcmp(d->status, "paused");
            bool running = !strcmp(d->status, "active") || !strcmp(d->status, "waiting");
            if ((paused || running) &&
                cui_button(ui, paused ? "Resume" : "Pause",
                           .icon = paused ? CUI_ICON_PLAY : CUI_ICON_PAUSE, .size = CUI_SIZE_SM,
                           .full_width = true))
                toggle_selected(a);
            cui_row(ui, .gap = 8) {
                if (cui_button(ui, "Folder", .icon = CUI_ICON_FOLDER_OPEN, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST, .width = CUI_FILL))
                    cui_app_open_url(d->dir);
                if (cui_button(ui, "Remove", .icon = CUI_ICON_TRASH_2, .size = CUI_SIZE_SM,
                               .variant = CUI_BUTTON_GHOST, .width = CUI_FILL))
                    remove_selected(a);
            }
        }
    }
}

static void download_table(App *a, cui_ctx *ui) {
    int rows[512];
    int n = collect_rows(a, rows, 512);
    if (n > 512) n = 512;
    if (n == 0) {
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
    cui_table(ui, "downloads", cols, 6, .height = CUI_FILL, .striped = true, .compact = true,
              .selectable = true) {
        for (int r = 0; r < n; r++) {
            Download *d = &a->items[rows[r]];
            bool selected = !strcmp(a->selected, d->gid);
            bool clicked = cui_table_row(ui, selected);
            if (clicked) {
                double now = cui_time(ui);
                if (now - a->last_click < 0.35 && !strcmp(a->last_click_gid, d->gid)) {
                    cui_app_open_url(d->dir);
                } else {
                    snprintf(a->selected, sizeof a->selected, "%s", d->gid);
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
                else snprintf(size, sizeof size, "—");
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
                else snprintf(speed, sizeof speed, "—");
                cui_text(ui, speed, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
            }
            cui_table_cell(ui); {
                char eta[32];
                unsigned long long secs;
                if (download_eta(d, &secs)) fmt_eta(eta, sizeof eta, secs);
                else snprintf(eta, sizeof eta, "—");
                cui_text(ui, eta, .size = 12, .color = CUI_COLOR_TEXT_SECONDARY,
                         .align = CUI_TEXT_RIGHT, .width = CUI_FILL, .tabular = true);
            }
            cui_table_cell(ui); {
                cui_text(ui, download_status_label(d), .size = 12, .color = status_color(d),
                         .nowrap = true);
            }
        }
    }
}

static void content(App *a, cui_ctx *ui) {
    Download *sel = find_item(a, a->selected);
    bool can_pause = sel && (!strcmp(sel->status, "active") || !strcmp(sel->status, "waiting"));
    bool can_resume = sel && !strcmp(sel->status, "paused");

    cui_column(ui, .width = CUI_FILL, .height = CUI_FILL, .pad = 14, .gap = 10) {
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
            char shown[32];
            snprintf(shown, sizeof shown, "%d shown", collect_rows(a, NULL, 0));
            cui_text(ui, shown, .size = 12, .color = CUI_COLOR_TEXT_MUTED);
            if (cui_icon_button(ui, CUI_ICON_PAUSE, .size = CUI_SIZE_SM, .tooltip = "Pause",
                                .disabled = !can_pause))
                toggle_selected(a);
            if (cui_icon_button(ui, CUI_ICON_PLAY, .size = CUI_SIZE_SM, .tooltip = "Resume",
                                .disabled = !can_resume))
                toggle_selected(a);
            if (cui_icon_button(ui, CUI_ICON_TRASH_2, .size = CUI_SIZE_SM, .tooltip = "Remove",
                                .disabled = !sel))
                remove_selected(a);
        }
        download_table(a, ui);
    }
}

static void statusbar(App *a, cui_ctx *ui) {
    char down[32], up[32], count[48];
    fmt_speed(down, sizeof down, a->gs.down_speed);
    fmt_speed(up, sizeof up, a->gs.up_speed);
    snprintf(count, sizeof count, "%d downloads", a->count);
    cui_status_bar(ui, "statusbar") {
        cui_status_bar_text(ui, a->status);
        cui_status_bar_spacer(ui);
        cui_status_bar_text(ui, count);
        cui_status_bar_text(ui, down);
        cui_status_bar_text(ui, up);
    }
}

static void add_dialog(App *a, cui_ctx *ui) {
    if (!a->show_add) return;
    cui_dialog(ui, "Add download", &a->show_add, .width = 560) {
        cui_label(ui, "Links");
        cui_textarea(ui, "links", a->add_urls, sizeof a->add_urls,
                     .placeholder = "One link per line — http, magnet, or .torrent",
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

static void settings_dialog(App *a, cui_ctx *ui) {
    if (!a->show_settings) return;
    cui_dialog(ui, "Settings", &a->show_settings, .width = 520) {
        cui_label(ui, "Save to");
        cui_row(ui, .width = CUI_FILL, .gap = 8, .align = CUI_ALIGN_CENTER) {
            cui_input(ui, "set-dir", a->edit.download_dir, sizeof a->edit.download_dir,
                      .width = CUI_FILL);
            if (cui_button(ui, "Browse…", .size = CUI_SIZE_SM)) {
                char path[PATH_MAX];
                if (cui_app_file_dialog(NULL,
                                        &(cui_file_dialog_opts){.kind = CUI_FILE_FOLDER,
                                                                .title = "Save downloads to"},
                                        path, sizeof path))
                    snprintf(a->edit.download_dir, sizeof a->edit.download_dir, "%s", path);
            }
        }
        double concurrent = a->edit.max_concurrent;
        if (cui_number(ui, "set-concurrent", &concurrent, 1, 16,
                       .label = "Max concurrent downloads", .step = 1, .width = CUI_PX(240)))
            a->edit.max_concurrent = (int)concurrent;
        double port = a->edit.rpc_port;
        if (cui_number(ui, "set-port", &port, 1024, 65535, .label = "RPC port", .step = 1))
            a->edit.rpc_port = (int)port;
        cui_muted(ui, "The RPC port applies after restarting Greed.");
        cui_switch(ui, "Start aria2 automatically", &a->edit.auto_start_aria2);
        cui_switch(ui, "Watch clipboard for links", &a->edit.clipboard_monitor);
        cui_switch(ui, "Notify when a download completes", &a->edit.notify_on_complete);
        cui_dialog_footer(ui) {
            if (cui_button(ui, "Cancel", .variant = CUI_BUTTON_GHOST)) a->show_settings = false;
            if (cui_button(ui, "Save", .variant = CUI_BUTTON_PRIMARY)) {
                a->cfg = a->edit;
                if (!config_save(&a->cfg)) set_status(a, "Could not save settings");
                else set_status(a, "Settings saved");
                char value[16];
                snprintf(value, sizeof value, "%d", a->cfg.max_concurrent);
                struct json_object *opts = json_object_new_object();
                json_object_object_add(opts, "max-concurrent-downloads",
                                       json_object_new_string(value));
                struct json_object *params = json_object_new_array();
                json_object_array_add(params, opts);
                struct json_object *v =
                    rpc_call(&a->rpc, "aria2.changeGlobalOption", params);
                if (v) json_object_put(v);
                a->show_settings = false;
            }
        }
    }
}

static void handle_keys(App *a, cui_ctx *ui) {
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_N)) open_add(a);
    if (cui_shortcut(ui, CUI_MOD_PRIMARY, CUI_KEY_F)) a->focus_search = true;
    if (cui_wants_keyboard(ui)) return;
    if (cui_key_pressed(ui, CUI_KEY_DELETE)) remove_selected(a);
    if (cui_key_pressed(ui, CUI_KEY_SPACE)) toggle_selected(a);
    if (cui_key_pressed(ui, CUI_KEY_ESCAPE)) a->selected[0] = 0;
}

static void frame(cui_ctx *ui, void *user) {
    App *a = user;
    apply_theme(ui);

    if (!a->frame_ready) {
        void *native = cui_app_native_window(NULL);
        if (frame_init(&a->frame, native)) frame_make_frameless(&a->frame);
        a->frame_ready = true;
    }

    handle_keys(a, ui);

    double now = cui_time(ui);
    if (now - a->last_poll >= 0.8) {
        a->last_poll = now;
        poll_engine(a);
    }
    /* Screenshot/test hook: GREED_DEMO=details selects the first download. */
    if (a->demo_details && !a->selected[0] && a->count)
        snprintf(a->selected, sizeof a->selected, "%s", a->items[0].gid);
    poll_clipboard(a, ui);
    cui_request_wakeup(ui, 0.8f);

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
            sidebar(a, ui);
            content(a, ui);
            details(a, ui);
        }
        cui_separator(ui);
        statusbar(a, ui);
    }

    add_dialog(a, ui);
    settings_dialog(a, ui);
}

int ui_run(void) {
    Config cfg;
    config_load(&cfg);
    if (cfg.auto_start_aria2)
        aria2_ensure_running(cfg.rpc_port, cfg.download_dir, cfg.max_concurrent, true);

    App *a = calloc(1, sizeof *a);
    if (!a) return 1;
    a->cfg = cfg;
    a->edit = cfg;
    a->clip_toast = -1;
    a->add_conn = cfg.max_concurrent;
    snprintf(a->add_dir, sizeof a->add_dir, "%s", cfg.download_dir);
    snprintf(a->status, sizeof a->status, "Ready");
    rpc_init(&a->rpc, cfg.rpc_port);

    /* Test hook for screenshots: GREED_DEMO=add|settings|details|empty */
    const char *demo = getenv("GREED_DEMO");
    if (demo && !strcmp(demo, "add")) open_add(a);
    else if (demo && !strcmp(demo, "settings")) open_settings(a);
    else if (demo && !strcmp(demo, "details")) a->demo_details = true;
    else if (demo && !strcmp(demo, "empty")) snprintf(a->filter, sizeof a->filter, "zzzz");

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
