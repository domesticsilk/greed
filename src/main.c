#include "cli.h"
#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void) {
    printf("greed: download manager\n"
           "\n"
           "Usage:\n"
           "  greed                     open the GUI\n"
           "  greed gui                 open the GUI\n"
           "  greed add <link|file.torrent>... [-d DIR] [-o NAME] [-x CONN]\n"
           "  greed list [--json]\n"
           "  greed status <gid> [--json]\n"
           "  greed pause <gid>...\n"
           "  greed resume <gid>...\n"
           "  greed remove <gid>... [--delete]\n"
           "  greed stats\n"
           "  greed shutdown            stop the aria2 engine\n"
           "  greed version\n"
           "  greed help\n");
}

static bool has_display(void) {
    const char *d = getenv("DISPLAY");
    const char *w = getenv("WAYLAND_DISPLAY");
    return (d && d[0]) || (w && w[0]);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        if (!has_display()) {
            print_help();
            return 1;
        }
        return ui_run();
    }

    const char *cmd = argv[1];
    if (!strcmp(cmd, "gui")) return ui_run();
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) {
        print_help();
        return 0;
    }
    if (!strcmp(cmd, "version") || !strcmp(cmd, "--version") || !strcmp(cmd, "-V")) {
        printf("greed 0.1.0\n");
        return 0;
    }
    return cli_run(cmd, argc - 2, argv + 2);
}
