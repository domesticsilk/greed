#include "image_info.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    int w = 0, h = 0, format = 0;
    char err[256];

    assert(greed_image_info(GREED_MARKETPLACE_DIR "/themes/echidna.jpg", &w, &h, &format,
                            err, sizeof err));
    assert(format == 2 && w > 0 && h > 0);
    assert(greed_image_info(GREED_MARKETPLACE_DIR "/themes/ado.jpg", &w, &h, &format, err,
                            sizeof err));
    assert(format == 2 && w > 0 && h > 0);

    /* Signature and IHDR are all the header reader needs. */
    static const unsigned char png[] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 13,
        'I',  'H', 'D', 'R', 0,    0,    0,    1,    0, 0, 0, 1,
    };
    const char *path = "/tmp/greed-image-info-test.png";
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(png, 1, sizeof png, f) == sizeof png);
    fclose(f);
    assert(greed_image_info(path, &w, &h, &format, err, sizeof err));
    assert(format == 1 && w == 1 && h == 1);

    unsigned char wide[sizeof png];
    memcpy(wide, png, sizeof png);
    wide[18] = 0x23;
    wide[19] = 0x29; /* 9001 px */
    f = fopen(path, "wb");
    assert(f);
    assert(fwrite(wide, 1, sizeof wide, f) == sizeof wide);
    fclose(f);
    assert(!greed_image_info(path, &w, &h, &format, err, sizeof err));
    assert(strstr(err, "8192"));

    f = fopen(path, "wb");
    assert(f);
    fputs("not an image", f);
    fclose(f);
    assert(!greed_image_info(path, &w, &h, &format, err, sizeof err));
    assert(strstr(err, "PNG or JPEG"));
    remove(path);
    return 0;
}
