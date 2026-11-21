#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/history.h"

int main(void) {
    char tmpl[] = "/tmp/greed-history-XXXXXX";
    char *dir = mkdtemp(tmpl);
    assert(dir);
    assert(setenv("XDG_CONFIG_HOME", dir, 1) == 0);

    history_load();
    assert(history_added("t:abc", 1000) == 1000);
    assert(history_added("t:abc", 2000) == 1000);
    assert(history_added("p:/tmp/a", 1500) == 1500);
    history_flush();

    /* Reload from disk: both timestamps survive, first-seen does not change. */
    history_load();
    assert(history_added("t:abc", 3000) == 1000);
    assert(history_added("p:/tmp/a", 3000) == 1500);

    history_forget("t:abc");
    history_flush();
    history_load();
    assert(history_added("t:abc", 4000) == 4000);

    char path[512];
    snprintf(path, sizeof path, "%s/greed/history.json", dir);
    struct stat sb;
    assert(stat(path, &sb) == 0);
    assert((sb.st_mode & 0777) == 0600);
    snprintf(path, sizeof path, "%s/greed/history.json.tmp", dir);
    assert(access(path, F_OK) != 0);
    snprintf(path, sizeof path, "%s/greed/history.json", dir);
    unlink(path);
    snprintf(path, sizeof path, "%s/greed", dir);
    rmdir(path);
    rmdir(dir);
    puts("history ok");
    return 0;
}
