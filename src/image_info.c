#include "image_info.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static unsigned be16(const unsigned char *p) {
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned be32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static bool dims_ok(int w, int h, char *err, size_t errcap) {
    if (w <= 0 || h <= 0) {
        snprintf(err, errcap, "The image has no usable dimensions.");
        return false;
    }
    if (w > GREED_IMAGE_MAX_SIDE || h > GREED_IMAGE_MAX_SIDE) {
        snprintf(err, errcap, "The longest side must be %d px or less.",
                 GREED_IMAGE_MAX_SIDE);
        return false;
    }
    if ((long long)w * h > GREED_IMAGE_MAX_PIXELS) {
        snprintf(err, errcap, "The image is larger than 24 megapixels.");
        return false;
    }
    return true;
}

static bool png_info(FILE *f, int *w, int *h, char *err, size_t errcap) {
    unsigned char head[24];
    if (fread(head, 1, sizeof head, f) != sizeof head ||
        memcmp(head, "\x89PNG\r\n\x1a\n", 8) != 0 || memcmp(head + 12, "IHDR", 4) != 0) {
        snprintf(err, errcap, "The PNG file is damaged.");
        return false;
    }
    *w = (int)be32(head + 16);
    *h = (int)be32(head + 20);
    return dims_ok(*w, *h, err, errcap);
}

static bool jpeg_info(FILE *f, int *w, int *h, char *err, size_t errcap) {
    if (fseek(f, 2, SEEK_SET) != 0) {
        snprintf(err, errcap, "The JPEG file is damaged.");
        return false;
    }
    for (;;) {
        int c = fgetc(f);
        if (c == EOF) break;
        if (c != 0xFF) continue;
        int m = fgetc(f);
        while (m == 0xFF) m = fgetc(f);
        if (m == EOF) break;
        if (m == 0xD8 || m == 0xD9 || m == 0x01) continue;
        if (m == 0xDA) break;
        int hi = fgetc(f), lo = fgetc(f);
        if (hi == EOF || lo == EOF) break;
        int len = (hi << 8) | lo;
        if (len < 2) break;
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            unsigned char sof[5];
            if (fread(sof, 1, sizeof sof, f) != sizeof sof) break;
            *h = (int)be16(sof + 1);
            *w = (int)be16(sof + 3);
            return dims_ok(*w, *h, err, errcap);
        }
        if (fseek(f, len - 2, SEEK_CUR) != 0) break;
    }
    snprintf(err, errcap, "The JPEG file is damaged.");
    return false;
}

bool greed_image_info(const char *path, int *width, int *height, int *format, char *err,
                      size_t errcap) {
    if (err && errcap) err[0] = 0;
    if (width) *width = 0;
    if (height) *height = 0;
    if (format) *format = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errcap, "The image could not be opened.");
        return false;
    }
    struct stat st;
    if (fstat(fileno(f), &st) != 0 || st.st_size <= 0) {
        fclose(f);
        snprintf(err, errcap, "The image is empty.");
        return false;
    }
    if (st.st_size > GREED_IMAGE_MAX_BYTES) {
        fclose(f);
        snprintf(err, errcap, "The image is larger than 12 MB.");
        return false;
    }
    unsigned char sig[8];
    if (fread(sig, 1, sizeof sig, f) != sizeof sig) {
        fclose(f);
        snprintf(err, errcap, "The image could not be read.");
        return false;
    }
    bool ok;
    if (!memcmp(sig, "\x89PNG\r\n\x1a\n", 8)) {
        if (format) *format = 1;
        rewind(f); /* png_info reads the signature again */
        ok = png_info(f, width, height, err, errcap);
    } else if (sig[0] == 0xFF && sig[1] == 0xD8) {
        if (format) *format = 2;
        ok = jpeg_info(f, width, height, err, errcap);
    } else {
        snprintf(err, errcap, "Use a PNG or JPEG image.");
        ok = false;
    }
    fclose(f);
    return ok;
}
