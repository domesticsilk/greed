#include "theme.h"

#include <assert.h>
#include <json-c/json.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct json_object *load_json(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f);
    assert(n >= 0 && fseek(f, 0, SEEK_SET) == 0);
    char *buf = malloc((size_t)n + 1);
    assert(buf);
    assert(fread(buf, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    buf[n] = 0;
    struct json_object *o = json_tokener_parse(buf);
    free(buf);
    assert(o);
    return o;
}

static double number(struct json_object *o, const char *key) {
    struct json_object *v = NULL;
    assert(json_object_object_get_ex(o, key, &v));
    return json_object_get_double(v);
}

int main(void) {
    char tmp[] = "/tmp/greed-theme-test-XXXXXX";
    assert(mkdtemp(tmp));
    assert(setenv("XDG_DATA_HOME", tmp, 1) == 0);

    GreedThemeDraft d;
    memset(&d, 0, sizeof d);
    snprintf(d.name, sizeof d.name, "Blur Test");
    snprintf(d.author, sizeof d.author, "Test");
    snprintf(d.license, sizeof d.license, "CC0");
    d.theme.dark = true;
    d.theme.bg = 0x101010;
    d.theme.surface = 0x181818;
    d.theme.text = 0xffffff;
    d.theme.accent = 0x5fe0c0;
    d.theme.wallpaper_scrim = 0.72f;
    d.theme.surface_alpha = 0.82f;
    d.theme.wallpaper_blur = 0.8f;
    d.theme.material = GREED_MATERIAL_GLASS;
    d.theme.glass_blur = 0.45f;
    snprintf(d.wallpaper_src, sizeof d.wallpaper_src, "%s/themes/echidna.jpg",
             GREED_MARKETPLACE_DIR);
    snprintf(d.wallpaper_ext, sizeof d.wallpaper_ext, "jpg");
    d.theme.wallpaper = d.wallpaper_src;

    char *json = greed_theme_json(&d, "blur-test.jpg");
    assert(json);
    struct json_object *o = json_tokener_parse(json);
    assert(o);
    assert(fabs(number(o, "wallpaper_blur") - 0.8) < 0.001);
    struct json_object *glass = NULL;
    assert(json_object_object_get_ex(o, "glass", &glass));
    assert(fabs(number(glass, "blur") - 0.45) < 0.001);
    struct json_object *wp = NULL;
    assert(json_object_object_get_ex(o, "wallpaper", &wp));
    assert(!strcmp(json_object_get_string(wp), "blur-test.jpg"));
    json_object_put(o);
    free(json);

    /* The field belongs to the wallpaper block. */
    json = greed_theme_json(&d, NULL);
    assert(json);
    o = json_tokener_parse(json);
    assert(o);
    assert(!json_object_object_get_ex(o, "wallpaper_blur", &wp));
    json_object_put(o);
    free(json);

    char path[PATH_MAX], err[256];
    assert(greed_theme_save(&d, false, path, sizeof path, err, sizeof err));
    assert(strstr(path, "/greed/themes/blur-test.json"));
    o = load_json(path);
    assert(fabs(number(o, "wallpaper_blur") - 0.8) < 0.001);
    json_object_put(o);
    const GreedTheme *loaded = greed_theme_find("Blur Test");
    assert(loaded);
    assert(!strcmp(loaded->name, "Blur Test"));
    assert(loaded->material == GREED_MATERIAL_GLASS);
    assert(fabs(loaded->glass_blur - 0.45f) < 0.001f);
    char copied[PATH_MAX];
    snprintf(copied, sizeof copied, "%s/greed/themes/blur-test.jpg", tmp);
    assert(access(copied, R_OK) == 0);

    assert(!greed_theme_save(&d, false, path, sizeof path, err, sizeof err));
    assert(strstr(err, "Replace"));
    assert(greed_theme_save(&d, true, path, sizeof path, err, sizeof err));

    snprintf(d.name, sizeof d.name, "Dark Mono");
    assert(!greed_theme_save(&d, true, path, sizeof path, err, sizeof err));
    assert(strstr(err, "built-in"));

    char slug[64];
    greed_theme_slug("  My  Theme!! 2 ", slug, sizeof slug);
    assert(!strcmp(slug, "my-theme-2"));

    unlink(path);
    unlink(copied);
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/greed/themes", tmp);
    rmdir(dir);
    snprintf(dir, sizeof dir, "%s/greed", tmp);
    rmdir(dir);
    rmdir(tmp);
    return 0;
}
