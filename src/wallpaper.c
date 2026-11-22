#include "wallpaper.h"

#include "image_info.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CUI embeds stb_image in cui.c without STB_IMAGE_STATIC. */
extern unsigned char *stbi_load(const char *filename, int *x, int *y, int *channels_in_file,
                                int desired_channels);
extern int stbi_info(const char *filename, int *x, int *y, int *channels_in_file);
extern void stbi_image_free(void *retval_from_stbi_load);

#define WALLPAPER_MAX_SIDE 1600
#define WALLPAPER_BLUR_PX 48.0f

static void drop_texture(cui_ctx *ui, GreedWallpaper *wp) {
    if (wp->tex) cui_image_destroy(ui, wp->tex);
    wp->tex = 0;
    wp->w = wp->h = 0;
}

static void drop_glass_texture(cui_ctx *ui, GreedWallpaper *wp) {
    if (wp->glass_tex) cui_image_destroy(ui, wp->glass_tex);
    wp->glass_tex = 0;
    wp->glass_w = wp->glass_h = 0;
}

void greed_wallpaper_release(cui_ctx *ui, GreedWallpaper *wp) {
    drop_texture(ui, wp);
    drop_glass_texture(ui, wp);
    free(wp->base);
    memset(wp, 0, sizeof *wp);
}

static unsigned char *shrink_rgba(const unsigned char *src, int sw, int sh, int dw, int dh) {
    unsigned char *dst = malloc((size_t)dw * (size_t)dh * 4);
    if (!dst) return NULL;
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((long long)y * sh / dh);
        int y1 = (int)((long long)(y + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((long long)x * sw / dw);
            int x1 = (int)((long long)(x + 1) * sw / dw);
            if (x1 <= x0) x1 = x0 + 1;
            unsigned sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; sy++) {
                const unsigned char *p = src + ((size_t)sy * (size_t)sw + (size_t)x0) * 4;
                for (int sx = x0; sx < x1; sx++, p += 4) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    sum[3] += p[3];
                }
            }
            unsigned n = (unsigned)((x1 - x0) * (y1 - y0));
            unsigned char *q = dst + ((size_t)y * (size_t)dw + (size_t)x) * 4;
            q[0] = (unsigned char)(sum[0] / n);
            q[1] = (unsigned char)(sum[1] / n);
            q[2] = (unsigned char)(sum[2] / n);
            q[3] = (unsigned char)(sum[3] / n);
        }
    }
    return dst;
}

static unsigned char *decode_small(const char *path, int *out_w, int *out_h) {
    int iw = 0, ih = 0, comp = 0;
    if (!stbi_info(path, &iw, &ih, &comp)) return NULL;
    if (iw <= 0 || ih <= 0 || iw > GREED_IMAGE_MAX_SIDE || ih > GREED_IMAGE_MAX_SIDE ||
        (long long)iw * ih > GREED_IMAGE_MAX_PIXELS)
        return NULL;
    unsigned char *px = stbi_load(path, &iw, &ih, &comp, 4);
    if (!px) return NULL;
    int dw = iw, dh = ih;
    if (iw > WALLPAPER_MAX_SIDE || ih > WALLPAPER_MAX_SIDE) {
        if (iw >= ih) {
            dw = WALLPAPER_MAX_SIDE;
            dh = (int)((long long)ih * WALLPAPER_MAX_SIDE / iw);
        } else {
            dh = WALLPAPER_MAX_SIDE;
            dw = (int)((long long)iw * WALLPAPER_MAX_SIDE / ih);
        }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    if (dw != iw || dh != ih) {
        unsigned char *small = shrink_rgba(px, iw, ih, dw, dh);
        stbi_image_free(px);
        px = small;
    }
    if (!px) return NULL;
    *out_w = dw;
    *out_h = dh;
    return px;
}

static int blur_radius(int w, float blur) {
    int r = (int)lroundf(blur * WALLPAPER_BLUR_PX * (float)w / (float)WALLPAPER_MAX_SIDE);
    return r < 1 ? 1 : r;
}

void greed_wallpaper_blur_rgba(unsigned char *rgba, int w, int h, float blur) {
    if (!rgba || w <= 0 || h <= 0 || blur <= 0) return;
    int r = blur_radius(w, blur);
    size_t stride = (size_t)w * 4;
    size_t line_cap = stride > (size_t)h * 4 ? stride : (size_t)h * 4;
    unsigned char *line = malloc(line_cap);
    if (!line) return;
    unsigned win = (unsigned)(r * 2 + 1);

    for (int y = 0; y < h; y++) {
        unsigned char *row = rgba + (size_t)y * stride;
        unsigned sum[4] = {0, 0, 0, 0};
        for (int k = -r; k <= r; k++) {
            int x = k < 0 ? 0 : (k >= w ? w - 1 : k);
            for (int c = 0; c < 4; c++) sum[c] += row[(size_t)x * 4 + (size_t)c];
        }
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 4; c++)
                line[(size_t)x * 4 + (size_t)c] = (unsigned char)(sum[c] / win);
            int xo = x - r < 0 ? 0 : x - r;
            int xi = x + r + 1 >= w ? w - 1 : x + r + 1;
            for (int c = 0; c < 4; c++)
                sum[c] += row[(size_t)xi * 4 + (size_t)c] - row[(size_t)xo * 4 + (size_t)c];
        }
        memcpy(row, line, stride);
    }

    for (int x = 0; x < w; x++) {
        unsigned sum[4] = {0, 0, 0, 0};
        for (int k = -r; k <= r; k++) {
            int y = k < 0 ? 0 : (k >= h ? h - 1 : k);
            for (int c = 0; c < 4; c++)
                sum[c] += rgba[(size_t)y * stride + (size_t)x * 4 + (size_t)c];
        }
        for (int y = 0; y < h; y++) {
            for (int c = 0; c < 4; c++)
                line[(size_t)y * 4 + (size_t)c] = (unsigned char)(sum[c] / win);
            int yo = y - r < 0 ? 0 : y - r;
            int yi = y + r + 1 >= h ? h - 1 : y + r + 1;
            for (int c = 0; c < 4; c++)
                sum[c] += rgba[(size_t)yi * stride + (size_t)x * 4 + (size_t)c] -
                          rgba[(size_t)yo * stride + (size_t)x * 4 + (size_t)c];
        }
        for (int y = 0; y < h; y++)
            memcpy(rgba + (size_t)y * stride + (size_t)x * 4, line + (size_t)y * 4, 4);
    }
    free(line);
}

static bool ensure_base(GreedWallpaper *wp) {
    if (!wp->base && !wp->base_failed) {
        wp->base = decode_small(wp->path, &wp->base_w, &wp->base_h);
        if (!wp->base) wp->base_failed = true;
    }
    return wp->base != NULL;
}

cui_texture greed_wallpaper_get(cui_ctx *ui, GreedWallpaper *wp, const char *path, float blur) {
    if (!path) path = "";
    if (blur < 0) blur = 0;
    if (blur > 1) blur = 1;
    if (wp->loaded && !strcmp(wp->path, path) && wp->blur == blur) return wp->tex;

    if (strcmp(wp->path, path)) {
        drop_texture(ui, wp);
        drop_glass_texture(ui, wp);
        wp->glass_loaded = false;
        free(wp->base);
        wp->base = NULL;
        wp->base_w = wp->base_h = 0;
        wp->base_failed = false;
        snprintf(wp->path, sizeof wp->path, "%s", path);
    }
    wp->loaded = true;
    wp->blur = blur;

    if (!path[0]) {
        drop_texture(ui, wp);
        return 0;
    }
    if (blur <= 0.001f) {
        drop_texture(ui, wp);
        wp->tex = cui_image_load(ui, path, &wp->w, &wp->h);
        return wp->tex;
    }
    if (!ensure_base(wp)) {
        drop_texture(ui, wp);
        wp->tex = cui_image_load(ui, path, &wp->w, &wp->h);
        return wp->tex;
    }

    size_t size = (size_t)wp->base_w * (size_t)wp->base_h * 4;
    unsigned char *px = malloc(size);
    if (!px) return wp->tex;
    memcpy(px, wp->base, size);
    greed_wallpaper_blur_rgba(px, wp->base_w, wp->base_h, blur);
    cui_texture tex = cui_image_create(ui, wp->base_w, wp->base_h, px);
    free(px);
    if (!tex) return wp->tex;
    drop_texture(ui, wp);
    wp->tex = tex;
    wp->w = wp->base_w;
    wp->h = wp->base_h;
    return wp->tex;
}

cui_texture greed_wallpaper_glass(cui_ctx *ui, GreedWallpaper *wp, const char *path, float blur) {
    if (!path) path = "";
    if (blur < 0) blur = 0;
    if (blur > 1) blur = 1;
    /* The main texture owns the path and the shared base; load it first. */
    if (strcmp(wp->path, path)) return 0;
    if (wp->glass_loaded && wp->glass_blur == blur) return wp->glass_tex;

    wp->glass_loaded = true;
    wp->glass_blur = blur;

    if (!path[0] || blur <= 0.001f) {
        drop_glass_texture(ui, wp);
        return 0;
    }
    if (!ensure_base(wp)) {
        drop_glass_texture(ui, wp);
        return 0;
    }

    size_t size = (size_t)wp->base_w * (size_t)wp->base_h * 4;
    unsigned char *px = malloc(size);
    if (!px) return wp->glass_tex;
    memcpy(px, wp->base, size);
    greed_wallpaper_blur_rgba(px, wp->base_w, wp->base_h, blur);
    cui_texture tex = cui_image_create(ui, wp->base_w, wp->base_h, px);
    free(px);
    if (!tex) return wp->glass_tex;
    drop_glass_texture(ui, wp);
    wp->glass_tex = tex;
    wp->glass_w = wp->base_w;
    wp->glass_h = wp->base_h;
    return wp->glass_tex;
}
