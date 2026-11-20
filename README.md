# Greed

A dark download manager for Linux. GUI and CLI in one C binary, powered by
[aria2](https://aria2.github.io/) and drawn with [CUI](https://github.com/yappologistic/CUI).

![Greed](assets/screenshot.png)

![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-blue.svg)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![C99](https://img.shields.io/badge/C-C99-blue.svg)

## Features

- **aria2 core**: HTTP/HTTPS/FTP, BitTorrent, Metalink; resume, multi-connection, checksums
- **Bundled aria2**: `vendor/aria2/` ships with the repo; falls back to a system `aria2c`
- **Frameless window**: the title bar is drawn inside the app, with drag, minimize, maximize and edge resize
- **GUI**: 76 built-in themes (default Dark Mono) plus marketplace theme packs, sidebar tabs
  (All/Active/Waiting/Stopped/Complete), a searchable download table with progress, speed, ETA
  and status, a per-download details pane and a live transfer sparkline in the sidebar
- **Liquid Glass**: an optional marketplace theme, in light and dark, that switches to
  macOS-style chrome: a unified toolbar, traffic lights, a 220px sidebar, sentence-case headers
  and optical glass on the toolbar, sidebar and dialog chrome. Other themes keep Greed's chrome
- **Keyboard first**: a command palette, full list navigation and an optional Vim mode; every
  destructive action asks first
- **Row actions**: open a finished file, open its folder, copy the source link, or retry a
  failed download from the row menu
- **Bulk actions**: drag a selection band or check several downloads, then pause, resume or
  remove them in one go
- **CLI**: `greed add/list/status/pause/resume/remove/stats/shutdown`, with `--json` for
  `list` and `status`
- **Torrents**: magnet links and `.torrent` files (file contents are base64-encoded for aria2);
  the info dialog adds Files and Peers tabs with per-file progress, skipped files, seed/peer
  state and live peer speeds from aria2
- **Bandwidth control**: global download/upload caps in KB/s, applied to the running engine,
  with a live throughput chart that draws the cap on the line
- **Seeding**: keep sharing completed torrents up to a chosen ratio
- **Statistics**: 60-second throughput chart with peak marker, live download/upload speeds and a
  download-folder card with free space and an Open folder button
- **Scheduled downloads**: presets or an exact date and time; Greed hands the link to aria2 when it is due
- **Settings**: General, Bandwidth, Torrents, Appearance, Keys and Updates tabs; theme, font
  family, text size, spacing, motion, engine limits and a keymap diagram
- **In-app updates**: Settings → Updates checks the latest GitHub release, downloads the
  AppImage and replaces the running copy (or installs it to `~/.local/bin/greed`), then offers
  a restart
- **Marketplace**: optional engines, tools and theme packs, each downloaded and checked against
  its SHA-256 before it is installed; yt-dlp adds a video resolver that picks a single-file
  stream and hands the direct URL to aria2
- **Accessibility**: text meets WCAG AA on every theme surface, focus rings keep 3:1
  non-text contrast, and the whole app works from the keyboard
- **Clipboard monitoring**: offers a toast when a link is copied (toggleable)
- **Notifications**: desktop notification when a download completes (toggleable)
- **Session persistence**: the queue survives restarts through aria2's session file
- **Live stats**: global and per-download speed, size, ETA, connections, seeders

## Requirements

- Linux (X11; on Wayland it runs through XWayland)
- OpenGL (falls back to software rendering)
- C compiler, CMake, libcurl, json-c, X11 headers

Arch:

```sh
sudo pacman -S base-devel cmake curl json-c libx11
```

Debian/Ubuntu:

```sh
sudo apt install build-essential cmake libcurl4-openssl-dev libjson-c-dev libx11-dev
```

aria2 is bundled, so nothing else is needed. A system `aria2c` is only used when the
bundled one is missing.

## Build

```sh
git clone https://github.com/domesticsilk/greed.git
cd greed
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/greed
```

`cui.c` is the large translation unit (~30 s on a slow machine); CMake builds it once as a
static library, so later rebuilds only touch the app sources.

## AppImage

```sh
./packaging/build-appimage.sh
```

It writes `build/Greed-x86_64.AppImage` (about 3.4 MB) with the private aria2 library
inside, so the release runs on a machine with no aria2 installed. The first run downloads
`appimagetool` into `build/`; when FUSE is unavailable the script falls back to extracting it.

Publish that file as the `Greed-x86_64.AppImage` asset of a GitHub release. Settings → Updates
looks for that exact name and compares the release tag with `GREED_VERSION` in
`src/version.h`.

## Usage

```sh
greed                              # open the GUI
greed add https://example.com/file.zip
greed add magnet:?xt=urn:btih:...  # magnet
greed add ./debian.torrent         # torrent file
greed add URL -d ~/Downloads -o name.iso -x 8
greed list [--json]
greed status <gid> [--json]
greed pause <gid>...
greed resume <gid>...
greed remove <gid>... [--delete]   # --delete also drops the stopped result
greed stats
greed shutdown                     # stop the aria2 engine
```

`greed` with no arguments opens the GUI when `DISPLAY` or `WAYLAND_DISPLAY` is set, and
prints help otherwise.

Keyboard shortcuts:

| Where | Keys | Action |
| --- | --- | --- |
| Anywhere | `Ctrl+K` | Command palette |
| Anywhere | `Ctrl+N` / `Ctrl+,` | Add download / Settings |
| Anywhere | `Ctrl+B` | Toggle sidebar |
| Anywhere | `Ctrl+1` `Ctrl+2` `Ctrl+3` | Downloads / Statistics / Scheduled |
| Anywhere | `?` / `Ctrl+Q` | Shortcuts / quit |
| List | `Up` `Down` `Home` `End` `PgUp` `PgDn` | Move the cursor |
| List | `Enter` / `Space` / `Delete` | Details / pause or resume / remove |
| List | `Ctrl+A` | Select all |
| List | `Ctrl+I` | Download info (Overview, Files, Peers) |
| Vim mode | `j` `k` `gg` `G` | Move the cursor |
| Vim mode | `Ctrl+D` `Ctrl+U` | Page down / up |
| Vim mode | `h` `l` | Previous / next tab |
| Vim mode | `x` / `dd` / `o` / `/` / `i` | Toggle / remove / add / search / info |
| Vim mode | `:` | Command palette |
| No Vim | `Ctrl+F` | Search |

`Esc` clears the selection or closes the open dialog. Settings scrolls with `PgUp`/`PgDn`.

## Marketplace

The cart button in the title bar (or the command palette) opens the Marketplace. It lists
optional engines, tools and theme packs from
`https://raw.githubusercontent.com/domesticsilk/greed/main/marketplace/catalog.json`, with a
built-in copy when the network or the catalog is unavailable.

- Every asset is downloaded to a temporary file, checked against the `sha256` in the catalog,
  and only then moved into place.
- Installs go to `~/.local/share/greed/` (or `$XDG_DATA_HOME/greed/`): themes under `themes/`,
  tools under `tools/<id>/`. Nothing outside that folder is written.
- The catalog is cached as `~/.local/share/greed/marketplace/catalog.json` and installed items
  are tracked in `installed.json` next to it. Remove deletes the installed file.
- yt-dlp is the first engine. With it installed, the Add dialog offers "Resolve video pages
  with yt-dlp": Greed asks yt-dlp for the best single-file progressive stream and adds the
  direct URL to aria2 with the headers that stream needs. Streams that need merging are not
  offered yet.
- Theme packs use the same JSON format as `themes/*.json`; after install they appear in
  Settings → Appearance next to the 76 built-in themes.
- Installing a theme does not switch to it. Installed themes show Apply in the Marketplace and
  in Settings → Appearance; Apply takes effect immediately. Removing the active theme falls back
  to Dark Mono.
- Theme JSON may set `chrome` (`greed` or `macos`) and `material` (`solid` or `glass`), plus a
  `glass` object with `tint`, `tint_alpha`, `highlight`, `highlight_alpha` and `shadow_alpha`.
  Liquid Glass uses both. macOS chrome also prefers SF Pro Text/Display, then Inter, when
  `font` is `default`.

`marketplace/catalog.json` documents the catalog format: each item has an `id`, a `type`
(`engine`, `tool` or `theme`), a version, an asset URL, a SHA-256 and an optional preview
palette.

## Configuration

`~/.config/greed/config.json` (or `$XDG_CONFIG_HOME/greed/config.json`):

```json
{
  "download_dir": "~/Downloads",
  "max_concurrent": 5,
  "rpc_port": 6800,
  "max_download_limit": 0,
  "max_upload_limit": 0,
  "seed_after_complete": true,
  "seed_ratio": 1.0,
  "auto_start_aria2": true,
  "clipboard_monitor": true,
  "notify_on_complete": true,
  "verify_after_complete": true,
  "theme": "Dark Mono",
  "font": "default",
  "text_scale": 1.0,
  "spacing": 1.0,
  "motion_speed": 1.0,
  "vim_mode": false
}
```

`font` is one of `default`, `sans`, `serif` or `mono`; `text_scale` ranges from 0.85 to 1.3 and
`spacing` from 0.9 to 1.12. The same values can be changed in Settings > Appearance.

The limits are in KB/s; 0 means unlimited. The aria2 session is saved next to it as
`aria2.session`. The same directory and format are used by the earlier Rust version.

## Layout

```
src/main.c     CLI/GUI dispatch
src/ui.c       CUI interface: title bar, sidebar, table, details, dialogs, toasts
src/logo.h     titlebar wordmark alpha mask (generated asset)
src/model.c    aria2 JSON parsing and formatters
src/rpc.c      JSON-RPC over libcurl, aria2 discovery/spawn, base64
src/config.c   config file
src/cli.c      subcommands
src/market.c   marketplace catalog, checksum-verified installs
src/ytdlp.c    yt-dlp resolver and aria2 handoff
src/x11.c      frameless window, drag/resize, maximize, minimize
marketplace/   catalog and theme pack assets
vendor/cui/    CUI 1.4.0 amalgamation (MIT)
vendor/aria2/  aria2c + libaria2 (GPL-2.0)
```

## License

GPL-2.0. CUI is MIT-licensed and vendored under `vendor/cui/LICENSE`; aria2 is GPL-2.0 and
vendored under `vendor/aria2/`.

The titlebar wordmark (`src/logo.h`) is a rendered image of the Stitch Warrior demo font,
which is free for personal use only. The font file itself is not included, and the wordmark
is not covered by the GPL; replace it if you need a different license.
