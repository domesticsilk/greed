#ifndef GREED_WALLPAPER_H
#define GREED_WALLPAPER_H

#include <limits.h>
#include <stdbool.h>

#include "cui.h"

/* Wallpaper texture plus the downscaled pixels kept for live blur changes.
 * The path keys the cache; changing it frees and decodes again. */
typedef struct {
    char path[PATH_MAX];
    unsigned char *base; /* downscaled RGBA */
    int base_w, base_h;
    bool base_failed;
    float blur;
    cui_texture tex;
    int w, h;
    bool loaded;
} GreedWallpaper;

/* Returns the texture for `path` with `blur` (0..1) applied, or 0. */
cui_texture greed_wallpaper_get(cui_ctx *ui, GreedWallpaper *wp, const char *path, float blur);
void greed_wallpaper_release(cui_ctx *ui, GreedWallpaper *wp);

/* Box-blurs RGBA pixels in place. Exposed for tests. */
void greed_wallpaper_blur_rgba(unsigned char *rgba, int w, int h, float blur);

#endif
