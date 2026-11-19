#ifndef GREED_THEME_H
#define GREED_THEME_H

#include <stdbool.h>
#include <stdint.h>

#include "cui.h"

typedef struct {
    const char *name;
    bool dark;
    uint32_t bg, surface, raised, muted, text, dim, border;
    uint32_t accent, accent_strong, accent_fg;
    uint32_t danger, success, warning, info;
    uint32_t series[6];
} GreedTheme;

extern const GreedTheme greed_themes[];
extern const int greed_theme_count;

const GreedTheme *greed_theme_find(const char *name);
/* motion_speed: 1 = normal, higher is faster (CUI's motion token is slower-is-higher).
 * text_scale multiplies every text size; spacing scales control heights. */
void greed_theme_apply(cui_ctx *ui, const GreedTheme *theme, float motion_speed,
                       float text_scale, float spacing);

#endif
