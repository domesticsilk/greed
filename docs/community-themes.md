# Community themes

Greed has no theme server and no accounts. A community theme is a theme JSON file plus an
optional wallpaper, reviewed through a GitHub issue and then added to the same
`marketplace/catalog.json` as the official items.

## Create a theme in the app

1. Open Settings > Appearance and choose **Create a theme**, use the command palette, or open
   Marketplace > Community and choose **Create a theme**.
2. Edit the palette on **Colors**. The preview applies the same readability pass as the live app.
3. On **Wallpaper**, drop a PNG or JPEG. Adjust **Blur**, **Scrim** and **Panel opacity**.
4. Fill in name, author, summary, description and license on **Details**.
5. Save locally with **Save**, or use **Publish** to export a submission bundle.

Saved themes live in `~/.local/share/greed/themes/<slug>.json` (or
`$XDG_DATA_HOME/greed/themes/`), with the wallpaper next to it. They appear in Settings >
Appearance immediately.

## Export and submit

**Publish > Export bundle** writes `<download_dir>/<slug>-submission/`:

- `<slug>.json`: the theme
- `<slug>.jpg` or `<slug>.png`: the wallpaper, when present
- `catalog-entry.json`: the marketplace entry with SHA-256 checksums and sizes
- `README.txt`: the steps for this submission

Then:

1. Open the GitHub issue from the Publish tab, or use the issue template directly.
2. Attach the theme JSON and wallpaper.
3. Paste `catalog-entry.json` into the catalog entry field.
4. Confirm that you made the theme and the wallpaper, or have permission to share them.

Review checks WCAG AA contrast, the image limits and the rights statement. Accepted themes are
committed to `marketplace/catalog.json` with `"community": true` and appear in Marketplace >
Community.

## Theme JSON

```json
{
  "name": "My Theme",
  "author": "Your name",
  "summary": "One line for the marketplace card.",
  "description": "A longer description.",
  "license": "CC BY 4.0",
  "dark": true,
  "bg": "#101010",
  "surface": "#181818",
  "raised": "#222222",
  "muted": "#0c0c0c",
  "text": "#f5f5f5",
  "dim": "#a0a0a0",
  "border": "#303030",
  "accent": "#5fe0c0",
  "accent_strong": "#9df5e2",
  "accent_fg": "#04211c",
  "danger": "#ff8fb3",
  "success": "#6fd6a0",
  "warning": "#e6c76a",
  "info": "#6fb6ff",
  "wallpaper": "my-theme.jpg",
  "wallpaper_scrim": 0.72,
  "wallpaper_blur": 0.35,
  "surface_alpha": 0.82,
  "series": ["#5fe0c0", "#6fd6a0", "#e6c76a", "#b39cff", "#6fb6ff", "#ff8fb3"]
}
```

- `wallpaper` is a filename next to the JSON. Absolute paths work locally but are not portable.
- `wallpaper_scrim` is 0 to 1; higher values paint more of the background colour over the image.
  The default is 0.65.
- `wallpaper_blur` is 0 to 1; 0 keeps the image sharp and 1 is the softest setting. The default
  is 0. Greed decodes the image once, keeps a reduced copy for live changes, and blurs it before
  uploading the texture.
- `surface_alpha` is 0 to 1; lower values let more of the wallpaper through panels. The default
  is 0.84.
- Text colours are checked against the scrimmed worst case. Keep body text at WCAG AA (4.5:1)
  and large text at 3:1.
- `chrome` can be `greed` or `macos`; `material` can be `solid` or `glass`. Glass adds a `glass`
  object with `tint`, `tint_alpha`, `highlight`, `highlight_alpha`, `shadow_alpha` and `blur`.
  `blur` is 0 to 1 and defaults to 0.6; it softens the wallpaper behind the toolbar, sidebar and
  list, so it needs a wallpaper to show.

## Image limits

- PNG or JPEG
- 12 MB or less
- 24 megapixels or less
- longest side 8192 px or less

Greed reads the header before decoding and rejects anything outside these limits.

## Install a theme without the Marketplace

Copy the JSON and its wallpaper into `~/.local/share/greed/themes/` (or
`$XDG_DATA_HOME/greed/themes/`) and restart Greed. A relative `wallpaper` path resolves next to
the JSON.

## Rights

Only submit art you created or have permission to share. Fan art without permission is removed.
The license you pick covers the theme file; the wallpaper needs its own permission.
