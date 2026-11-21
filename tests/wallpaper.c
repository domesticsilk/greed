#include "wallpaper.h"

#include <assert.h>
#include <string.h>

int main(void) {
    enum { W = 128, H = 128 };
    static unsigned char px[W * H * 4];
    memset(px, 0, sizeof px);
    for (int y = 48; y < 80; y++)
        for (int x = 48; x < 80; x++) {
            unsigned char *p = px + ((size_t)y * W + x) * 4;
            p[0] = p[1] = p[2] = p[3] = 255;
        }

    greed_wallpaper_blur_rgba(px, W, H, 1.0f);
    const unsigned char *center = px + ((size_t)64 * W + 64) * 4;
    const unsigned char *edge = px + ((size_t)64 * W + 44) * 4;
    assert(center[0] > 240);
    assert(edge[0] > 0 && edge[0] < 80);
    assert(px[0] == 0);

    unsigned char before[4];
    memcpy(before, center, 4);
    greed_wallpaper_blur_rgba(px, W, H, 0);
    assert(memcmp(before, center, 4) == 0);
    return 0;
}
