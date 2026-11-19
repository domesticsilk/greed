#include "rpc.h"
#include "config.h"

#include <curl/curl.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char *data;
    size_t len;
} Buf;

static size_t on_write(char *ptr, size_t size, size_t nmemb, void *user) {
    Buf *b = user;
    size_t n = size * nmemb;
    char *p = realloc(b->data, b->len + n + 1);
    if (!p) return 0;
    b->data = p;
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = 0;
    return n;
}

void rpc_init(Rpc *r, int port) {
    r->port = port;
    r->error[0] = 0;
    static bool curl_ready;
    if (!curl_ready) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ready = true;
    }
}

void rpc_set_port(Rpc *r, int port) { r->port = port; }

struct json_object *rpc_call(Rpc *r, const char *method, struct json_object *params) {
    r->error[0] = 0;
    struct json_object *req = json_object_new_object();
    json_object_object_add(req, "jsonrpc", json_object_new_string("2.0"));
    json_object_object_add(req, "id", json_object_new_string("greed"));
    json_object_object_add(req, "method", json_object_new_string(method));
    json_object_object_add(req, "params", params ? params : json_object_new_array());
    const char *body = json_object_to_json_string_ext(req, JSON_C_TO_STRING_PLAIN);

    CURL *c = curl_easy_init();
    Buf buf = {0};
    struct json_object *result = NULL;
    if (!c) {
        snprintf(r->error, sizeof r->error, "cannot create HTTP handle");
        json_object_put(req);
        return NULL;
    }
    char url[128];
    snprintf(url, sizeof url, "http://127.0.0.1:%d/jsonrpc", r->port);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 2000L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);

    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        snprintf(r->error, sizeof r->error, "aria2 unreachable: %s", curl_easy_strerror(rc));
    } else {
        struct json_object *resp = json_tokener_parse(buf.data ? buf.data : "");
        if (!resp) {
            snprintf(r->error, sizeof r->error, "invalid response from aria2");
        } else {
            struct json_object *err = NULL;
            if (json_object_object_get_ex(resp, "error", &err)) {
                const char *msg = "";
                struct json_object *m = NULL;
                if (json_object_object_get_ex(err, "message", &m))
                    msg = json_object_get_string(m);
                snprintf(r->error, sizeof r->error, "%s", msg ? msg : "aria2 error");
            } else {
                struct json_object *res = NULL;
                if (json_object_object_get_ex(resp, "result", &res))
                    result = json_object_get(res);
            }
            json_object_put(resp);
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    free(buf.data);
    json_object_put(req);
    return result;
}

bool rpc_probe(Rpc *r) {
    struct json_object *v = rpc_call(r, "aria2.getVersion", NULL);
    if (!v) return false;
    json_object_put(v);
    return true;
}

const char *aria2_binary(void) {
    static char buf[PATH_MAX];
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            snprintf(buf, sizeof buf, "%s/aria2c", exe);
            if (access(buf, X_OK) == 0) return buf;
            for (int i = 0; i < 4; i++) {
                snprintf(buf, sizeof buf, "%s/vendor/aria2/aria2c", exe);
                if (access(buf, X_OK) == 0) return buf;
                slash = strrchr(exe, '/');
                if (!slash) break;
                *slash = 0;
            }
        }
    }
    if (access("vendor/aria2/aria2c", X_OK) == 0) return "vendor/aria2/aria2c";
    return "aria2c";
}

const char *aria2_lib_dir(void) {
    static char lib[PATH_MAX];
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", aria2_binary());
    char *slash = strrchr(tmp, '/');
    if (!slash) return NULL;
    *slash = 0;
    snprintf(lib, sizeof lib, "%s/lib", tmp);
    return access(lib, R_OK) == 0 ? lib : NULL;
}

bool aria2_available(void) {
    const char *bin = aria2_binary();
    const char *lib = aria2_lib_dir();
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        if (lib) setenv("LD_LIBRARY_PATH", lib, 1);
        execl(bin, bin, "--version", (char *)NULL);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

bool aria2_spawn(const Config *cfg) {
    const char *bin = aria2_binary();
    const char *lib = aria2_lib_dir();
    char session[PATH_MAX];
    snprintf(session, sizeof session, "%s/aria2.session", config_dir());

    char sport[32], smax[32], sdir[PATH_MAX + 8], ssess[PATH_MAX + 16], sinput[PATH_MAX + 16];
    char sdl[64], sul[64], sseed[64];
    snprintf(sport, sizeof sport, "--rpc-listen-port=%d", cfg->rpc_port);
    snprintf(smax, sizeof smax, "--max-concurrent-downloads=%d", cfg->max_concurrent);
    snprintf(sdir, sizeof sdir, "--dir=%s", cfg->download_dir);
    snprintf(ssess, sizeof ssess, "--save-session=%s", session);
    snprintf(sdl, sizeof sdl, "--max-overall-download-limit=%d", cfg->max_download_limit * 1024);
    snprintf(sul, sizeof sul, "--max-overall-upload-limit=%d", cfg->max_upload_limit * 1024);
    snprintf(sseed, sizeof sseed, "--seed-ratio=%.2f",
             cfg->seed_after_complete ? (double)cfg->seed_ratio : 0.0);

    const char *argv[20];
    int n = 0;
    argv[n++] = bin;
    argv[n++] = "--enable-rpc";
    argv[n++] = sport;
    argv[n++] = "--rpc-listen-all=false";
    argv[n++] = smax;
    argv[n++] = sdir;
    argv[n++] = ssess;
    argv[n++] = sdl;
    argv[n++] = sul;
    argv[n++] = sseed;
    argv[n++] = "--save-session-interval=30";
    argv[n++] = "--continue=true";
    argv[n++] = "--daemon=true";
    argv[n++] = "--quiet";
    if (access(session, R_OK) == 0) {
        snprintf(sinput, sizeof sinput, "--input-file=%s", session);
        argv[n++] = sinput;
    }
    argv[n] = NULL;

    mkdir(config_dir(), 0755);
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        if (lib) setenv("LD_LIBRARY_PATH", lib, 1);
        execv(bin, (char *const *)argv);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

bool aria2_apply_options(Rpc *r, const Config *cfg) {
    struct json_object *opts = json_object_new_object();
    char v[32];
    snprintf(v, sizeof v, "%d", cfg->max_concurrent);
    json_object_object_add(opts, "max-concurrent-downloads", json_object_new_string(v));
    snprintf(v, sizeof v, "%lld", (long long)cfg->max_download_limit * 1024);
    json_object_object_add(opts, "max-overall-download-limit", json_object_new_string(v));
    snprintf(v, sizeof v, "%lld", (long long)cfg->max_upload_limit * 1024);
    json_object_object_add(opts, "max-overall-upload-limit", json_object_new_string(v));
    snprintf(v, sizeof v, "%.2f", cfg->seed_after_complete ? (double)cfg->seed_ratio : 0.0);
    json_object_object_add(opts, "seed-ratio", json_object_new_string(v));
    struct json_object *params = json_object_new_array();
    json_object_array_add(params, opts);
    struct json_object *res = rpc_call(r, "aria2.changeGlobalOption", params);
    if (!res) return false;
    json_object_put(res);
    return true;
}

bool aria2_ensure_running(const Config *cfg, bool auto_start) {
    Rpc r;
    rpc_init(&r, cfg->rpc_port);
    if (rpc_probe(&r)) {
        aria2_apply_options(&r, cfg);
        return true;
    }
    if (!auto_start || !aria2_available()) return false;
    aria2_spawn(cfg);
    for (int i = 0; i < 25; i++) {
        usleep(100000);
        if (rpc_probe(&r)) {
            aria2_apply_options(&r, cfg);
            return true;
        }
    }
    return false;
}

static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64-encodes a whole file, as aria2.addTorrent expects. */
char *base64_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    unsigned char *data = malloc((size_t)size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);

    size_t cap = ((got + 2) / 3) * 4 + 1;
    char *out = malloc(cap);
    if (!out) {
        free(data);
        return NULL;
    }
    size_t o = 0;
    for (size_t i = 0; i < got; i += 3) {
        unsigned v = (unsigned)data[i] << 16;
        if (i + 1 < got) v |= (unsigned)data[i + 1] << 8;
        if (i + 2 < got) v |= data[i + 2];
        out[o++] = b64chars[(v >> 18) & 63];
        out[o++] = b64chars[(v >> 12) & 63];
        out[o++] = i + 1 < got ? b64chars[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < got ? b64chars[v & 63] : '=';
    }
    out[o] = 0;
    free(data);
    if (out_len) *out_len = o;
    return out;
}
