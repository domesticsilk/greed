#ifndef GREED_THEME_H
#define GREED_THEME_H

#include <stdbool.h>
#include <stdint.h>

#include "cui.h"

typedef enum {
    GREED_CHROME_GREED = 0,
    GREED_CHROME_MACOS,
} GreedChrome;

typedef enum {
    GREED_MATERIAL_SOLID = 0,
    GREED_MATERIAL_GLASS,
} GreedMaterial;

typedef struct {
    const char *name;
    bool dark;
    uint32_t bg, surface, raised, muted, text, dim, border;
    uint32_t accent, accent_strong, accent_fg;
    uint32_t danger, success, warning, info;
    uint32_t series[6];
    GreedChrome chrome;
    GreedMaterial material;
    uint32_t glass_tint, glass_highlight;
    float glass_tint_alpha, glass_highlight_alpha, glass_shadow_alpha;
    /* Optional background image. The path is absolute once loaded. */
    const char *wallpaper;
    float wallpaper_scrim; /* background painted over the image, 0..1 */
    float surface_alpha;   /* opacity of surface fills, 0..1 */
} GreedTheme;

extern const GreedTheme greed_themes[];
extern const int greed_theme_count;

/* Built-in themes plus the JSON files in <data_dir>/themes. */
int greed_theme_total(void);
const GreedTheme *greed_theme_at(int index);
bool greed_theme_exists(const char *name);
void greed_theme_reload(void);

const GreedTheme *greed_theme_find(const char *name);
/* True when the theme asks for the macOS window chrome. */
bool greed_theme_macos(const GreedTheme *theme);
/* motion_speed: 1 = normal, higher is faster (CUI's motion token is slower-is-higher).
 * text_scale multiplies every text size; spacing scales control heights. */
void greed_theme_apply(cui_ctx *ui, const GreedTheme *theme, float motion_speed,
                       float text_scale, float spacing);

#endif
