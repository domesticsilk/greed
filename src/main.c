#include "cli.h"

#include "browser.h"
#include "links.h"
#include "native.h"
#include "single.h"
#include "tray.h"
#include "ui.h"
#include "version.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void) {
    printf("greed: download manager\n"
           "\n"
           "Usage:\n"
           "  greed                     open the GUI\n"
           "  greed <link|file>         open the GUI with a download ready to add\n"
           "  greed gui                 open the GUI\n"
           "  greed add <link|file.torrent>... [-d DIR] [-o NAME] [-x CONN]\n"
           "            [--referer URL] [--user-agent UA] [--cookie TEXT]\n"
           "            [-H 'Name: value']...\n"
           "  greed list [--json]\n"
           "  greed status <gid> [--json]\n"
           "  greed pause <gid>...\n"
           "  greed resume <gid>...\n"
           "  greed remove <gid>... [--delete]\n"
           "  greed stats\n"
           "  greed shutdown            stop the aria2 engine\n"
           "  greed browser install     register the browser extension helper\n"
           "  greed browser uninstall   remove the browser extension helper\n"
           "  greed tray                show the background download indicator\n"
           "  greed version\n"
           "  greed help\n");
}

static bool has_display(void) {
    const char *d = getenv("DISPLAY");
    const char *w = getenv("WAYLAND_DISPLAY");
    return (d && d[0]) || (w && w[0]);
}

/* One GUI per user: later processes hand their links to the running one. */
static int run_gui(char *links) {
    if (!single_acquire()) {
        bool ok = single_handoff(links);
        free(links);
        return ok ? 0 : 1;
    }
    int rc = ui_run(links);
    free(links);
    single_release();
    tray_spawn();
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        if (!has_display()) {
            print_help();
            return 1;
        }
        return run_gui(NULL);
    }

    const char *cmd = argv[1];
    if (!strcmp(cmd, "native-host")) return native_host_run();
    if (!strcmp(cmd, "tray")) return tray_run();
    if (!strcmp(cmd, "browser")) {
        if (argc < 3 || (strcmp(argv[2], "install") && strcmp(argv[2], "uninstall"))) {
            fprintf(stderr, "usage: greed browser install|uninstall\n");
            return 1;
        }
        return !strcmp(argv[2], "install") ? browser_install() : browser_uninstall();
    }
    if (!strcmp(cmd, "gui")) {
        char *links = argc > 2 ? greed_join_links(argc - 2, argv + 2) : NULL;
        return run_gui(links);
    }
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) {
        print_help();
        return 0;
    }
    if (!strcmp(cmd, "version") || !strcmp(cmd, "--version") || !strcmp(cmd, "-V")) {
        printf("greed " GREED_VERSION "\n");
        return 0;
    }
    if (greed_link_arg(cmd)) {
        if (!has_display()) return cli_run("add", argc - 1, argv + 1);
        return run_gui(greed_join_links(argc - 1, argv + 1));
    }
    return cli_run(cmd, argc - 2, argv + 2);
}
