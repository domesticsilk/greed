/* Proxy URL normalization and validation. Built by CMake, no window. */
#include "../src/config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check(bool ok, const char *what) {
    if (ok) return;
    printf("FAIL %s\n", what);
    failures++;
}

static void set_proxy(Config *c, const char *url) {
    snprintf(c->proxy, sizeof c->proxy, "%s", url);
}

int main(void) {
    Config c = {0};

    set_proxy(&c, "  proxy.local:3128  ");
    snprintf(c.no_proxy, sizeof c.no_proxy, "  localhost, 127.0.0.1  ");
    config_normalize_proxy(&c);
    check(!strcmp(c.proxy, "http://proxy.local:3128"), "adds the default scheme");
    check(!strcmp(c.no_proxy, "localhost, 127.0.0.1"), "trims the bypass list");
    check(!config_proxy_error(c.proxy), "normalized proxy is valid");

    set_proxy(&c, "http://user:pass@proxy.local:3128");
    config_normalize_proxy(&c);
    check(!strcmp(c.proxy, "http://user:pass@proxy.local:3128"), "keeps an explicit scheme");
    check(!config_proxy_error(c.proxy), "accepts credentials");

    set_proxy(&c, "HTTP://proxy.local:8080");
    check(!config_proxy_error(c.proxy), "scheme is case insensitive");

    set_proxy(&c, "socks5://proxy.local:1080");
    check(config_proxy_error(c.proxy), "rejects a non-HTTP scheme");

    set_proxy(&c, "http://");
    check(config_proxy_error(c.proxy), "requires a host");

    set_proxy(&c, "http://user:pass@");
    check(config_proxy_error(c.proxy), "requires a host after credentials");

    set_proxy(&c, "http://:3128");
    check(config_proxy_error(c.proxy), "requires a host before the port");

    set_proxy(&c, "http://proxy local:3128");
    check(config_proxy_error(c.proxy), "rejects spaces");

    set_proxy(&c, "http://proxy.local:3128/path");
    check(config_proxy_error(c.proxy), "rejects a path");

    c.proxy[0] = 0;
    check(!config_proxy_error(c.proxy), "empty proxy is valid");

    char tmpl[] = "/tmp/greed-proxy-XXXXXX";
    char *dir = mkdtemp(tmpl);
    check(dir != NULL, "temp config dir");
    if (dir) {
        check(setenv("XDG_CONFIG_HOME", dir, 1) == 0, "sets XDG_CONFIG_HOME");
        Config saved = {0};
        snprintf(saved.proxy, sizeof saved.proxy, "http://user:pass@proxy.local:3128");
        snprintf(saved.no_proxy, sizeof saved.no_proxy, "localhost, 127.0.0.1");
        check(config_save(&saved), "saves the config");

        char path[512];
        snprintf(path, sizeof path, "%s/greed/config.json", dir);
        struct stat st;
        check(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600,
              "config file is owner-only");

        chmod(path, 0644);
        Config loaded = {0};
        config_load(&loaded);
        check(!strcmp(loaded.proxy, saved.proxy), "proxy survives a reload");
        check(!strcmp(loaded.no_proxy, saved.no_proxy), "bypass list survives a reload");
        check(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600,
              "load tightens the config permissions");

        unlink(path);
        snprintf(path, sizeof path, "%s/greed", dir);
        rmdir(path);
        rmdir(dir);
    }

    if (failures) {
        printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    puts("proxy ok");
    return 0;
}
