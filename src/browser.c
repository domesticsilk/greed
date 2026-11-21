#include "browser.h"

#include "config.h"

#include <json-c/json.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

bool browser_parse_message(const char *json, char *links, size_t cap, bool *ping,
                           char *error, size_t errcap) {
    if (links && cap) links[0] = 0;
    if (ping) *ping = false;
    if (error && errcap) error[0] = 0;

    struct json_object *root = json_tokener_parse(json ? json : "");
    if (!root || !json_object_is_type(root, json_type_object)) {
        if (root) json_object_put(root);
        snprintf(error, errcap, "The message is not valid JSON.");
        return false;
    }

    struct json_object *type = NULL;
    if (json_object_object_get_ex(root, "type", &type) &&
        !strcmp(json_object_get_string(type), "ping")) {
        if (ping) *ping = true;
        json_object_put(root);
        return true;
    }

    struct json_object *urls = NULL;
    if (!json_object_object_get_ex(root, "urls", &urls) ||
        !json_object_is_type(urls, json_type_array)) {
        snprintf(error, errcap, "The message has no links.");
        json_object_put(root);
        return false;
    }

    bool ok = true;
    size_t used = 0;
    size_t n = json_object_array_length(urls);
    for (size_t i = 0; i < n; i++) {
        struct json_object *v = json_object_array_get_idx(urls, i);
        const char *url = json_object_get_string(v);
        if (!url) continue;
        if (strncasecmp(url, "http://", 7) && strncasecmp(url, "https://", 8)) continue;
        size_t len = strlen(url);
        if (used + (used ? 1 : 0) + len + 1 > cap) {
            snprintf(error, errcap, "The links are too long.");
            ok = false;
            break;
        }
        if (used) links[used++] = '\n';
        memcpy(links + used, url, len);
        used += len;
        links[used] = 0;
    }
    if (ok && !used) {
        snprintf(error, errcap, "The message has no http or https links.");
        ok = false;
    }
    json_object_put(root);
    return ok;
}

static bool config_home(char *out, size_t cap) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return snprintf(out, cap, "%s", xdg) < (int)cap;
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    return snprintf(out, cap, "%s/.config", home) < (int)cap;
}

static const char *const browser_dirs[] = {
    "google-chrome",
    "google-chrome-beta",
    "google-chrome-unstable",
    "chromium",
    "BraveSoftware/Brave-Browser",
    "microsoft-edge",
    "vivaldi",
    "opera",
};

static void wrapper_path(char *out, size_t cap) {
    snprintf(out, cap, "%s/native-host", data_dir());
}

/* Single-quote a path for the small wrapper script. */
static void sh_quote(const char *in, char *out, size_t cap) {
    size_t o = 0;
    if (o + 1 < cap) out[o++] = '\'';
    for (const char *p = in; *p && o + 5 < cap; p++) {
        if (*p == '\'') {
            memcpy(out + o, "'\\''", 4);
            o += 4;
        } else {
            out[o++] = *p;
        }
    }
    if (o + 1 < cap) out[o++] = '\'';
    out[o] = 0;
}

static bool self_path(char *out, size_t cap) {
    const char *app = getenv("APPIMAGE");
    if (app && *app) return snprintf(out, cap, "%s", app) < (int)cap;
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return false;
    out[n] = 0;
    return true;
}

static bool write_wrapper(const char *path) {
    char exe[PATH_MAX], quoted[PATH_MAX * 4 + 8], body[PATH_MAX * 4 + 128];
    if (!self_path(exe, sizeof exe)) return false;
    sh_quote(exe, quoted, sizeof quoted);
    const char *extract = getenv("APPIMAGE_EXTRACT_AND_RUN");
    if (extract && *extract && strcmp(extract, "0"))
        snprintf(body, sizeof body,
                 "#!/bin/sh\nAPPIMAGE_EXTRACT_AND_RUN=1 exec %s native-host \"$@\"\n", quoted);
    else
        snprintf(body, sizeof body, "#!/bin/sh\nexec %s native-host \"$@\"\n", quoted);
    if (!file_write_atomic(path, body)) return false;
    return chmod(path, 0755) == 0;
}

static bool write_manifest(const char *dir) {
    char hosts[PATH_MAX], path[PATH_MAX];
    if (snprintf(hosts, sizeof hosts, "%s/NativeMessagingHosts", dir) >= (int)sizeof hosts)
        return false;
    mkdir(hosts, 0755);
    if (snprintf(path, sizeof path, "%s/%s.json", hosts, GREED_NATIVE_HOST) >=
        (int)sizeof path)
        return false;

    char wrapper[PATH_MAX];
    wrapper_path(wrapper, sizeof wrapper);
    struct json_object *m = json_object_new_object();
    json_object_object_add(m, "name", json_object_new_string(GREED_NATIVE_HOST));
    json_object_object_add(m, "description",
                           json_object_new_string("Send links to the Greed download manager"));
    json_object_object_add(m, "path", json_object_new_string(wrapper));
    json_object_object_add(m, "type", json_object_new_string("stdio"));
    struct json_object *origins = json_object_new_array();
    json_object_array_add(origins,
                          json_object_new_string("chrome-extension://" GREED_EXTENSION_ID "/"));
    json_object_object_add(m, "allowed_origins", origins);

    const char *text = json_object_to_json_string_ext(
        m, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
    bool ok = file_write_atomic(path, text);
    json_object_put(m);
    return ok;
}

int browser_install(void) {
    char home[PATH_MAX];
    if (!config_home(home, sizeof home)) {
        fprintf(stderr, "error: cannot find the config directory\n");
        return 1;
    }
    mkdir(data_dir(), 0755);
    char wrapper[PATH_MAX];
    wrapper_path(wrapper, sizeof wrapper);
    if (!write_wrapper(wrapper)) {
        fprintf(stderr, "error: could not write %s\n", wrapper);
        return 1;
    }

    int installed = 0;
    for (size_t i = 0; i < sizeof browser_dirs / sizeof browser_dirs[0]; i++) {
        char dir[PATH_MAX];
        if (snprintf(dir, sizeof dir, "%s/%s", home, browser_dirs[i]) >= (int)sizeof dir)
            continue;
        struct stat sb;
        if (stat(dir, &sb) != 0 || !S_ISDIR(sb.st_mode)) continue;
        if (write_manifest(dir)) {
            printf("Registered for %s\n", browser_dirs[i]);
            installed++;
        } else {
            fprintf(stderr, "error: could not register for %s\n", browser_dirs[i]);
        }
    }
    if (!installed) {
        fprintf(stderr, "No Chromium browser profile found. Start the browser once, then run "
                        "this again.\n");
        return 1;
    }
    printf("Load the extension folder, then reload the extension if it was already open.\n");
    return 0;
}

int browser_uninstall(void) {
    char home[PATH_MAX];
    if (!config_home(home, sizeof home)) return 1;
    for (size_t i = 0; i < sizeof browser_dirs / sizeof browser_dirs[0]; i++) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof path, "%s/%s/NativeMessagingHosts/%s.json", home,
                     browser_dirs[i], GREED_NATIVE_HOST) >= (int)sizeof path)
            continue;
        if (unlink(path) == 0) printf("Removed for %s\n", browser_dirs[i]);
    }
    char wrapper[PATH_MAX];
    wrapper_path(wrapper, sizeof wrapper);
    unlink(wrapper);
    return 0;
}
