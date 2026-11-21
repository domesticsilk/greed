#ifndef GREED_IMAGE_INFO_H
#define GREED_IMAGE_INFO_H

#include <stdbool.h>
#include <stddef.h>

#define GREED_IMAGE_MAX_BYTES (12 * 1024 * 1024)
#define GREED_IMAGE_MAX_PIXELS (24 * 1000 * 1000)
#define GREED_IMAGE_MAX_SIDE 8192

/* Reads PNG or JPEG dimensions without decoding pixels. `format` is 1 for
 * PNG and 2 for JPEG. False with a user-readable message on bad input. */
bool greed_image_info(const char *path, int *width, int *height, int *format, char *err,
                      size_t errcap);

#endif
