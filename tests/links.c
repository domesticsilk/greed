/* Self-check for link detection and file:// normalization. Built by CMake, no window. */
#include "../src/links.c"

#include <stdio.h>

static int failures;

static void check(bool ok, const char *what) {
    if (ok) return;
    printf("FAIL %s\n", what);
    failures++;
}

int main(void) {
    check(greed_link_arg("magnet:?xt=urn:btih:abc"), "magnet link");
    check(greed_link_arg("https://example.com/a.zip"), "https link");
    check(greed_link_arg("http://example.com/a.zip"), "http link");
    check(greed_link_arg("ftp://example.com/a.zip"), "ftp link");
    check(greed_link_arg("/tmp/a.torrent"), "torrent path");
    check(greed_link_arg("a.metalink"), "metalink path");
    check(greed_link_arg("a.meta4"), "meta4 path");
    check(!greed_link_arg("add"), "command is not a link");
    check(!greed_link_arg("list"), "command is not a link");
    check(!greed_link_arg(""), "empty is not a link");

    char *one = greed_join_links(1, (char *[]){"file:///tmp/a%20b.torrent"});
    check(one && !strcmp(one, "/tmp/a b.torrent"), "file URI becomes a path");
    free(one);

    char *host = greed_join_links(1, (char *[]){"file://host/tmp/x.meta4"});
    check(host && !strcmp(host, "/tmp/x.meta4"), "file URI drops the host");
    free(host);

    char *two = greed_join_links(2, (char *[]){"magnet:?xt=1", "https://e/x"});
    check(two && !strcmp(two, "magnet:?xt=1\nhttps://e/x"), "links join with newlines");
    free(two);

    char *none = greed_join_links(0, NULL);
    check(!none, "no links returns NULL");
    free(none);

    if (failures) {
        printf("%d link checks failed\n", failures);
        return 1;
    }
    printf("link checks passed\n");
    return 0;
}
