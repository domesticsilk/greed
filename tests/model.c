#include "model.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int category(const char *name) {
    Download d;
    memset(&d, 0, sizeof d);
    snprintf(d.name, sizeof d.name, "%s", name);
    return download_category(&d);
}

int main(void) {
    assert(category("movie.MKV") == CAT_VIDEO);
    assert(category("song.flac") == CAT_AUDIO);
    assert(category("cover.JPEG") == CAT_IMAGES);
    assert(category("manual.pdf") == CAT_DOCUMENTS);
    assert(category("backup.tar.gz") == CAT_ARCHIVES);
    assert(category("Greed-x86_64.AppImage") == CAT_PROGRAMS);
    assert(category("magnet-abcdef") == CAT_OTHER);
    assert(category("README") == CAT_OTHER);
    assert(category("movie.") == CAT_OTHER);
    assert(category("archive.tar.gz.tmp") == CAT_OTHER);
    puts("model: ok");
    return 0;
}
