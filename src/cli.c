#include "cli.h"

#include "config.h"
#include "model.h"
#include "rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Rpc rpc;

static bool ensure_client(const Config *cfg) {
    rpc_init(&rpc, cfg->rpc_port);
    if (rpc_probe(&rpc)) return true;
    if (!aria2_ensure_running(cfg->rpc_port, cfg->download_dir, cfg->max_concurrent,
                              cfg->auto_start_aria2)) {
        fprintf(stderr, "error: aria2 RPC not available. Start aria2c --enable-rpc or install aria2.\n");
        return false;
    }
    return true;
}

static struct json_object *tell_all(void) {
    struct json_object *all = json_object_new_array();
    const char *methods[] = {"aria2.tellActive", "aria2.tellWaiting", "aria2.tellStopped"};
    struct json_object *params[] = {NULL, NULL, NULL};
    params[1] = json_object_new_array();
    json_object_array_add(params[1], json_object_new_int(0));
    json_object_array_add(params[1], json_object_new_int(1000));
    params[2] = json_object_new_array();
    json_object_array_add(params[2], json_object_new_int(0));
    json_object_array_add(params[2], json_object_new_int(1000));
    for (int i = 0; i < 3; i++) {
        struct json_object *v = rpc_call(&rpc, methods[i], params[i]);
        if (!v) continue;
        size_t n = json_object_array_length(v);
        for (size_t k = 0; k < n; k++) {
            Download d;
            if (download_parse(json_object_array_get_idx(v, k), &d)) {
                struct json_object *o = json_object_new_object();
                json_object_object_add(o, "gid", json_object_new_string(d.gid));
                json_object_object_add(o, "status", json_object_new_string(d.status));
                json_object_object_add(o, "name", json_object_new_string(d.name));
                json_object_object_add(o, "totalLength", json_object_new_int64((int64_t)d.total));
                json_object_object_add(o, "completedLength", json_object_new_int64((int64_t)d.completed));
                json_object_object_add(o, "downloadSpeed", json_object_new_int64((int64_t)d.down_speed));
                json_object_object_add(o, "progress", json_object_new_double(download_progress(&d)));
                json_object_object_add(o, "dir", json_object_new_string(d.dir));
                json_object_array_add(all, o);
            }
        }
        json_object_put(v);
    }
    return all;
}

static bool is_torrent_path(const char *s) {
    size_t n = strlen(s);
    return n > 8 && !strcmp(s + n - 8, ".torrent");
}

static struct json_object *torrent_params(const char *path, struct json_object *opts) {
    size_t len = 0;
    char *b64 = base64_file(path, &len);
    if (!b64) return NULL;
    struct json_object *params = json_object_new_array();
    json_object_array_add(params, json_object_new_string_len(b64, (int)len));
    json_object_array_add(params, json_object_new_array());
    json_object_array_add(params, opts);
    free(b64);
    return params;
}

static int cmd_add(int argc, char **argv) {
    const char *dir = NULL;
    const char *name = NULL;
    int conn = 0;
    const char *urls[128];
    int url_count = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && i + 1 < argc)
            dir = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc)
            name = argv[++i];
        else if (!strcmp(argv[i], "-x") && i + 1 < argc)
            conn = atoi(argv[++i]);
        else if (argv[i][0] != '-' && url_count < 128)
            urls[url_count++] = argv[i];
    }
    if (!url_count) {
        fprintf(stderr, "error: no link given\n");
        return 1;
    }
    Config cfg;
    config_load(&cfg);
    if (!ensure_client(&cfg)) return 1;

    int added = 0;
    for (int i = 0; i < url_count; i++) {
        struct json_object *opts = json_object_new_object();
        json_object_object_add(opts, "dir",
                               json_object_new_string(dir ? dir : cfg.download_dir));
        if (name && url_count == 1) json_object_object_add(opts, "out", json_object_new_string(name));
        if (conn > 1) {
            json_object_object_add(opts, "split", json_object_new_int(conn));
            json_object_object_add(opts, "max-connection-per-server", json_object_new_int(conn));
        }
        struct json_object *result = NULL;
        if (is_torrent_path(urls[i])) {
            struct json_object *params = torrent_params(urls[i], opts);
            if (params) result = rpc_call(&rpc, "aria2.addTorrent", params);
            else {
                fprintf(stderr, "error: cannot read %s\n", urls[i]);
                return 1;
            }
        } else {
            struct json_object *params = json_object_new_array();
            struct json_object *list = json_object_new_array();
            json_object_array_add(list, json_object_new_string(urls[i]));
            json_object_array_add(params, list);
            json_object_array_add(params, opts);
            result = rpc_call(&rpc, "aria2.addUri", params);
        }
        if (!result) {
            fprintf(stderr, "error: %s\n", rpc.error);
            return 1;
        }
        printf("Added: %s (%s)\n", json_object_get_string(result), urls[i]);
        json_object_put(result);
        added++;
    }
    printf("%d download%s added\n", added, added == 1 ? "" : "s");
    return 0;
}

static int cmd_list(bool json_out) {
    struct json_object *all = tell_all();
    if (json_out) {
        printf("%s\n", json_object_to_json_string_ext(all, JSON_C_TO_STRING_PRETTY));
    } else {
        printf("%-14s %-10s %7s %12s %s\n", "GID", "STATUS", "PROG", "SPEED", "NAME");
        size_t n = json_object_array_length(all);
        for (size_t i = 0; i < n; i++) {
            struct json_object *o = json_object_array_get_idx(all, i);
            const char *gid = json_object_get_string(json_object_object_get(o, "gid"));
            const char *status = json_object_get_string(json_object_object_get(o, "status"));
            const char *name = json_object_get_string(json_object_object_get(o, "name"));
            double progress = json_object_get_double(json_object_object_get(o, "progress"));
            unsigned long long speed =
                (unsigned long long)json_object_get_int64(json_object_object_get(o, "downloadSpeed"));
            char sp[32];
            fmt_speed(sp, sizeof sp, speed);
            printf("%-14.14s %-10.10s %6.1f%% %12s %s\n", gid, status, progress * 100.0, sp, name);
        }
    }
    json_object_put(all);
    return 0;
}

static int cmd_status(const char *gid, bool json_out) {
    struct json_object *params = json_object_new_array();
    json_object_array_add(params, json_object_new_string(gid));
    struct json_object *v = rpc_call(&rpc, "aria2.tellStatus", params);
    if (!v) {
        fprintf(stderr, "error: %s\n", rpc.error);
        return 1;
    }
    if (json_out) {
        printf("%s\n", json_object_to_json_string_ext(v, JSON_C_TO_STRING_PRETTY));
    } else {
        Download d;
        if (!download_parse(v, &d)) {
            fprintf(stderr, "error: bad status\n");
            json_object_put(v);
            return 1;
        }
        char a[32], b[32], c[32];
        fmt_bytes(a, sizeof a, d.completed);
        fmt_bytes(b, sizeof b, d.total);
        fmt_speed(c, sizeof c, d.down_speed);
        printf("GID: %s\nStatus: %s\nProgress: %.1f%%\nSize: %s / %s\nSpeed: %s\n", d.gid,
               d.status, download_progress(&d) * 100.0, a, b, c);
        if (d.seeders) printf("Seeders: %u\n", d.seeders);
        if (d.error[0]) printf("Error: %s\n", d.error);
    }
    json_object_put(v);
    return 0;
}

static int cmd_action(const char *method, const char *verb, int argc, char **argv) {
    int done = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--delete")) continue;
        struct json_object *params = json_object_new_array();
        json_object_array_add(params, json_object_new_string(argv[i]));
        struct json_object *v = rpc_call(&rpc, method, params);
        if (!v) {
            fprintf(stderr, "error: %s\n", rpc.error);
            return 1;
        }
        json_object_put(v);
        printf("%s %s\n", verb, argv[i]);
        done++;
    }
    if (!done) {
        fprintf(stderr, "error: no GID given\n");
        return 1;
    }
    return 0;
}

static struct json_object *gid_params(const char *gid) {
    struct json_object *p = json_object_new_array();
    json_object_array_add(p, json_object_new_string(gid));
    return p;
}

static int cmd_remove(int argc, char **argv, bool purge) {
    int done = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--delete")) continue;
        struct json_object *v = rpc_call(&rpc, "aria2.forceRemove", gid_params(argv[i]));
        if (v) json_object_put(v);
        else if (purge) {
            /* Already stopped: just drop the result below. */
            v = rpc_call(&rpc, "aria2.removeDownloadResult", gid_params(argv[i]));
            if (!v) {
                fprintf(stderr, "error: %s\n", rpc.error);
                return 1;
            }
            json_object_put(v);
            printf("Removed %s\n", argv[i]);
            done++;
            continue;
        } else {
            fprintf(stderr, "error: %s\n", rpc.error);
            return 1;
        }
        if (purge) {
            struct json_object *r =
                rpc_call(&rpc, "aria2.removeDownloadResult", gid_params(argv[i]));
            if (r) json_object_put(r);
        }
        printf("Removed %s\n", argv[i]);
        done++;
    }
    if (!done) {
        fprintf(stderr, "error: no GID given\n");
        return 1;
    }
    return 0;
}

static int cmd_stats(void) {
    struct json_object *v = rpc_call(&rpc, "aria2.getGlobalStat", NULL);
    if (!v) {
        fprintf(stderr, "error: %s\n", rpc.error);
        return 1;
    }
    GlobalStat g;
    memset(&g, 0, sizeof g);
    const char *keys[] = {"downloadSpeed", "uploadSpeed", "numActive", "numWaiting", "numStopped"};
    unsigned long long *out[] = {&g.down_speed, &g.up_speed, &g.active, &g.waiting, &g.stopped};
    for (int i = 0; i < 5; i++) {
        struct json_object *o = json_object_object_get(v, keys[i]);
        if (o) *out[i] = strtoull(json_object_get_string(o), NULL, 10);
    }
    char a[32], b[32];
    fmt_speed(a, sizeof a, g.down_speed);
    fmt_speed(b, sizeof b, g.up_speed);
    printf("Download: %s\nUpload: %s\nActive: %llu  Waiting: %llu  Stopped: %llu\n", a, b,
           g.active, g.waiting, g.stopped);
    json_object_put(v);
    return 0;
}

int cli_run(const char *cmd, int argc, char **argv) {
    Config cfg;
    config_load(&cfg);
    if (!strcmp(cmd, "version")) {
        printf("greed 0.1.0\n");
        return 0;
    }
    if (!ensure_client(&cfg)) return 1;

    if (!strcmp(cmd, "add")) return cmd_add(argc, argv);
    if (!strcmp(cmd, "list")) {
        bool json_out = false;
        for (int i = 0; i < argc; i++)
            if (!strcmp(argv[i], "--json")) json_out = true;
        return cmd_list(json_out);
    }
    if (!strcmp(cmd, "status")) {
        bool json_out = false;
        const char *gid = NULL;
        for (int i = 0; i < argc; i++) {
            if (!strcmp(argv[i], "--json")) json_out = true;
            else if (argv[i][0] != '-') gid = argv[i];
        }
        if (!gid) {
            fprintf(stderr, "error: missing GID\n");
            return 1;
        }
        return cmd_status(gid, json_out);
    }
    if (!strcmp(cmd, "pause")) return cmd_action("aria2.pause", "Paused", argc, argv);
    if (!strcmp(cmd, "resume")) return cmd_action("aria2.unpause", "Resumed", argc, argv);
    if (!strcmp(cmd, "remove")) {
        bool purge = false;
        for (int i = 0; i < argc; i++)
            if (!strcmp(argv[i], "--delete")) purge = true;
        return cmd_remove(argc, argv, purge);
    }
    if (!strcmp(cmd, "stats")) return cmd_stats();
    if (!strcmp(cmd, "shutdown")) {
        struct json_object *v = rpc_call(&rpc, "aria2.forceShutdown", NULL);
        if (!v) {
            fprintf(stderr, "error: %s\n", rpc.error);
            return 1;
        }
        json_object_put(v);
        printf("aria2 shut down\n");
        return 0;
    }
    fprintf(stderr, "error: unknown command: %s\n", cmd);
    return 1;
}
