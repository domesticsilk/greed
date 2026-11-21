#include "model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *jstr(struct json_object *v, const char *key) {
    struct json_object *o;
    if (v && json_object_object_get_ex(v, key, &o)) {
        const char *s = json_object_get_string(o);
        if (s) return s;
    }
    return "";
}

static unsigned long long jnum(struct json_object *v, const char *key) {
    const char *s = jstr(v, key);
    return s[0] ? strtoull(s, NULL, 10) : 0;
}

static void copy_str(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool download_parse(struct json_object *v, Download *d) {
    memset(d, 0, sizeof *d);
    copy_str(d->gid, sizeof d->gid, jstr(v, "gid"));
    if (!d->gid[0]) return false;
    copy_str(d->status, sizeof d->status, jstr(v, "status"));
    copy_str(d->dir, sizeof d->dir, jstr(v, "dir"));
    copy_str(d->info_hash, sizeof d->info_hash, jstr(v, "infoHash"));
    copy_str(d->error, sizeof d->error, jstr(v, "errorMessage"));
    d->total = jnum(v, "totalLength");
    d->completed = jnum(v, "completedLength");
    d->down_speed = jnum(v, "downloadSpeed");
    d->up_speed = jnum(v, "uploadSpeed");
    d->connections = (unsigned)jnum(v, "connections");
    d->seeders = (unsigned)jnum(v, "numSeeders");

    const char *bt_name = "";
    struct json_object *bt;
    if (json_object_object_get_ex(v, "bittorrent", &bt)) {
        d->is_torrent = true;
        struct json_object *info;
        if (json_object_object_get_ex(bt, "info", &info))
            bt_name = jstr(info, "name");
    }
    if (d->info_hash[0]) d->is_torrent = true;

    const char *path = "";
    const char *url = "";
    struct json_object *files;
    if (json_object_object_get_ex(v, "files", &files) && json_object_array_length(files) > 0) {
        struct json_object *f = json_object_array_get_idx(files, 0);
        path = jstr(f, "path");
        struct json_object *uris;
        if (json_object_object_get_ex(f, "uris", &uris) && json_object_array_length(uris) > 0)
            url = jstr(json_object_array_get_idx(uris, 0), "uri");
    }
    d->file_count = files ? (int)json_object_array_length(files) : 0;
    copy_str(d->url, sizeof d->url, url);

    if (bt_name[0]) {
        copy_str(d->name, sizeof d->name, bt_name);
    } else if (path[0]) {
        copy_str(d->name, sizeof d->name, base_name(path));
    } else if (d->is_torrent && d->info_hash[0]) {
        snprintf(d->name, sizeof d->name, "Magnet %.8s", d->info_hash);
    } else if (url[0]) {
        copy_str(d->name, sizeof d->name, url);
    } else {
        copy_str(d->name, sizeof d->name, d->gid);
    }
    return true;
}

double download_progress(const Download *d) {
    if (!d->total) return 0.0;
    double p = (double)d->completed / (double)d->total;
    return p < 0 ? 0 : p > 1 ? 1 : p;
}

bool download_eta(const Download *d, unsigned long long *secs) {
    if (d->down_speed && d->total > d->completed) {
        *secs = (d->total - d->completed) / d->down_speed;
        return true;
    }
    return false;
}

const char *download_status_label(const Download *d) {
    if (!strcmp(d->status, "active"))
        return d->is_torrent && d->total == 0 ? "Metadata" : "Downloading";
    if (!strcmp(d->status, "waiting")) return "Queued";
    if (!strcmp(d->status, "paused")) return "Paused";
    if (!strcmp(d->status, "complete")) return "Completed";
    if (!strcmp(d->status, "merging")) return "Merging";
    if (!strcmp(d->status, "error")) return "Failed";
    if (!strcmp(d->status, "removed")) return "Removed";
    return "Unknown";
}

bool download_is_finished(const Download *d) {
    return !strcmp(d->status, "complete") || !strcmp(d->status, "error") ||
           !strcmp(d->status, "removed");
}

void fmt_speed(char *out, size_t cap, unsigned long long bps) {
    if (bps >= 1073741824ULL)
        snprintf(out, cap, "%.2f GB/s", (double)bps / 1073741824.0);
    else if (bps >= 1048576ULL)
        snprintf(out, cap, "%.2f MB/s", (double)bps / 1048576.0);
    else if (bps >= 1024ULL)
        snprintf(out, cap, "%.1f KB/s", (double)bps / 1024.0);
    else
        snprintf(out, cap, "%llu B/s", bps);
}

void fmt_bytes(char *out, size_t cap, unsigned long long bytes) {
    if (bytes >= 1073741824ULL)
        snprintf(out, cap, "%.2f GB", (double)bytes / 1073741824.0);
    else if (bytes >= 1048576ULL)
        snprintf(out, cap, "%.1f MB", (double)bytes / 1048576.0);
    else if (bytes >= 1024ULL)
        snprintf(out, cap, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(out, cap, "%llu B", bytes);
}

void fmt_eta(char *out, size_t cap, unsigned long long secs) {
    if (secs < 60)
        snprintf(out, cap, "%llus", secs);
    else if (secs < 3600)
        snprintf(out, cap, "%llum %02llus", secs / 60, secs % 60);
    else if (secs < 86400)
        snprintf(out, cap, "%lluh %02llum", secs / 3600, (secs % 3600) / 60);
    else
        snprintf(out, cap, "%llud %lluh", secs / 86400, (secs % 86400) / 3600);
}
