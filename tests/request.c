#include <assert.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/request.h"

static void check_headers(struct json_object *opts, const char **want, int n) {
    struct json_object *h = NULL;
    assert(json_object_object_get_ex(opts, "header", &h));
    assert((int)json_object_array_length(h) == n);
    for (int i = 0; i < n; i++)
        assert(!strcmp(json_object_get_string(json_object_array_get_idx(h, i)), want[i]));
}

int main(void) {
    /* The config file goes to XDG_RUNTIME_DIR when it is set; /tmp works everywhere,
     * including the minimal release-build rootfs, which has no /run. */
    unsetenv("XDG_RUNTIME_DIR");
    RequestOptions o = {0};
    char err[256];
    assert(request_options_empty(&o));
    assert(request_options_validate(&o, err, sizeof err));

    snprintf(o.referer, sizeof o.referer, "https://example.com/page");
    snprintf(o.user_agent, sizeof o.user_agent, "GreedTest/1.0");
    snprintf(o.cookie, sizeof o.cookie, "sid=abc; theme=dark");
    snprintf(o.headers, sizeof o.headers,
             "Accept-Language: en\nReferer: https://ignored.example\nX-Test:  one  ");
    assert(!request_options_empty(&o));
    assert(request_options_validate(&o, err, sizeof err));

    struct json_object *opts = json_object_new_object();
    request_options_apply_aria2(opts, &o);
    const char *want[] = {
        "Referer: https://example.com/page",
        "User-Agent: GreedTest/1.0",
        "Cookie: sid=abc; theme=dark",
        "Accept-Language: en",
        "X-Test: one",
    };
    check_headers(opts, want, 5);
    json_object_put(opts);

    assert(request_options_has_header(&o, "referer"));
    assert(request_options_has_header(&o, "User-Agent"));
    assert(request_options_has_header(&o, "x-test"));
    assert(!request_options_has_header(&o, "Accept"));

    char path[64];
    int fd = request_options_ytdlp_config(&o, path, sizeof path);
    assert(fd >= 0);
    assert(!strncmp(path, "/dev/fd/", 8));
    char cfg[4096];
    ssize_t n = read(fd, cfg, sizeof cfg - 1);
    assert(n > 0);
    cfg[n] = 0;
    assert(strstr(cfg, "--referer\n'https://example.com/page'\n"));
    assert(strstr(cfg, "--user-agent\n'GreedTest/1.0'\n"));
    assert(strstr(cfg, "--add-header\n'Cookie: sid=abc; theme=dark'\n"));
    assert(strstr(cfg, "--add-header\n'Accept-Language: en'\n"));
    assert(strstr(cfg, "--add-header\n'X-Test: one'\n"));
    assert(!strstr(cfg, "ignored.example"));
    close(fd);

    RequestOptions quoted = {0};
    snprintf(quoted.headers, sizeof quoted.headers, "X-Quote: it's fine");
    fd = request_options_ytdlp_config(&quoted, path, sizeof path);
    assert(fd >= 0);
    n = read(fd, cfg, sizeof cfg - 1);
    assert(n > 0);
    cfg[n] = 0;
    assert(strstr(cfg, "'X-Quote: it'\"'\"'s fine'\n"));
    close(fd);

    RequestOptions bad = {0};
    snprintf(bad.headers, sizeof bad.headers, "NoColon");
    assert(!request_options_validate(&bad, err, sizeof err));
    snprintf(bad.headers, sizeof bad.headers, "Bad Name: x");
    assert(!request_options_validate(&bad, err, sizeof err));
    snprintf(bad.headers, sizeof bad.headers, "X-Test: a\x01b");
    assert(!request_options_validate(&bad, err, sizeof err));
    snprintf(bad.headers, sizeof bad.headers, "");
    snprintf(bad.referer, sizeof bad.referer, "https://x/\nInjected: 1");
    assert(!request_options_validate(&bad, err, sizeof err));

    RequestOptions empty = {0};
    opts = json_object_new_object();
    request_options_apply_aria2(opts, &empty);
    struct json_object *h = NULL;
    assert(!json_object_object_get_ex(opts, "header", &h));
    json_object_put(opts);
    assert(request_options_ytdlp_config(&empty, path, sizeof path) < 0);

    request_options_clear(&o);
    assert(request_options_empty(&o));
    puts("request tests passed");
    return 0;
}
