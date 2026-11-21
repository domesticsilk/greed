#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../src/browser.h"

int main(void) {
    char links[512], error[128];
    bool ping = false;

    assert(browser_parse_message(
        "{\"urls\":[\"https://a/x\",\"ftp://b\",\"http://c\"]}", links, sizeof links,
        &ping, error, sizeof error));
    assert(!strcmp(links, "https://a/x\nhttp://c"));
    assert(!ping);

    assert(browser_parse_message("{\"type\":\"ping\"}", links, sizeof links, &ping, error,
                                 sizeof error));
    assert(ping);

    assert(!browser_parse_message("{}", links, sizeof links, &ping, error, sizeof error));
    assert(error[0]);
    assert(!browser_parse_message("not json", links, sizeof links, &ping, error,
                                  sizeof error));
    assert(!browser_parse_message("{\"urls\":[\"ftp://x\"]}", links, sizeof links, &ping,
                                  error, sizeof error));
    assert(!browser_parse_message("{\"urls\":[\"https://a\"]}", links, 8, &ping, error,
                                  sizeof error));

    puts("browser ok");
    return 0;
}
