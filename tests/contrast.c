/* Self-check: every theme, including glass composites, keeps text at 4.5:1,
 * disabled text at 3:1 and focus rings at 3:1. Built by CMake, no window. */
#include "../src/theme.c"

const char *data_dir(void) { return GREED_MARKETPLACE_DIR; }

static int failures;

static void check(const char *theme, const char *what, float got, float want) {
    if (got + 0.001f >= want) return;
    printf("FAIL %-20s %-28s %.2f < %.2f\n", theme, what, got, want);
    failures++;
}

static cui_color disabled(cui_color fg, cui_color bg) {
    return mix_srgb(bg, fg, 0.80f);
}

int main(void) {
    cui_ctx *ui = cui_create(&(cui_ctx_desc){0});
    if (!ui) {
        printf("FAIL could not create a CUI context\n");
        return 1;
    }
    int glass = 0;
    for (int i = 0; i < greed_theme_total(); i++) {
        const GreedTheme *t = greed_theme_at(i);
        greed_theme_apply(ui, t, 1.0f, 1.0f, 1.0f);
        cui_color *c = cui_theme_get(ui)->colors;
        cui_color base[6], surfaces[16];
        theme_base(t, base);
        int n = theme_surfaces(t, base, c[CUI_TOKEN_ACCENT], t->dark ? 0.18f : 0.14f, surfaces);
        if (n == 16) glass++;

        check(t->name, "foreground", min_on(c[CUI_TOKEN_FOREGROUND], surfaces, n), 4.5f);
        check(t->name, "text secondary", min_on(c[CUI_TOKEN_TEXT_SECONDARY], surfaces, n), 4.5f);
        check(t->name, "text muted", min_on(c[CUI_TOKEN_TEXT_MUTED], surfaces, n), 4.5f);
        check(t->name, "accent strong", min_on(c[CUI_TOKEN_ACCENT_STRONG], surfaces, n), 4.5f);
        check(t->name, "danger", min_on(c[CUI_TOKEN_DANGER], surfaces, n), 4.5f);
        check(t->name, "success", min_on(c[CUI_TOKEN_SUCCESS], surfaces, n), 4.5f);
        check(t->name, "warning", min_on(c[CUI_TOKEN_WARNING], surfaces, n), 4.5f);
        check(t->name, "focus ring", min_on(c[CUI_TOKEN_FOCUS_RING], surfaces, n), 3.0f);
        check(t->name, "accent foreground", color_contrast(c[CUI_TOKEN_ACCENT_FOREGROUND],
                                                           c[CUI_TOKEN_ACCENT]),
              4.5f);
        /* CUI paints disabled nodes at 80% opacity over whatever is behind them. */
        cui_color off_accent = mix_srgb(base[0], c[CUI_TOKEN_ACCENT], 0.80f);
        cui_color off_label = mix_srgb(base[0], c[CUI_TOKEN_ACCENT_FOREGROUND], 0.80f);
        check(t->name, "disabled accent label", color_contrast(off_label, off_accent), 3.0f);
        for (int s = 0; s < n; s++) {
            check(t->name, "disabled foreground",
                  color_contrast(disabled(c[CUI_TOKEN_FOREGROUND], surfaces[s]), surfaces[s]),
                  3.0f);
            check(t->name, "disabled secondary",
                  color_contrast(disabled(c[CUI_TOKEN_TEXT_SECONDARY], surfaces[s]), surfaces[s]),
                  3.0f);
            check(t->name, "disabled muted",
                  color_contrast(disabled(c[CUI_TOKEN_TEXT_MUTED], surfaces[s]), surfaces[s]),
                  3.0f);
        }
    }
    if (!greed_theme_exists("Liquid Glass") || !greed_theme_exists("Liquid Glass Dark")) {
        printf("FAIL Liquid Glass themes not found under %s/themes\n", GREED_MARKETPLACE_DIR);
        failures++;
    }
    if (glass != 2) {
        printf("FAIL expected 2 glass themes, found %d\n", glass);
        failures++;
    }
    cui_destroy(ui);
    if (failures) {
        printf("%d contrast checks failed\n", failures);
        return 1;
    }
    printf("%d themes, %d glass: contrast checks passed\n", greed_theme_total(), glass);
    return 0;
}
