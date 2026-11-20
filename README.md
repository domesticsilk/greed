# Greed

A dark, keyboard-first download manager for Linux. Greed ships a native GUI and a full
command-line interface in one C binary, using [aria2](https://aria2.github.io/) as the download
engine and [CUI](https://github.com/yappologistic/CUI) for its interface.

![Greed](assets/screenshot.png)

[![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-1a1a1a?style=for-the-badge&labelColor=000000&logo=gnu&logoColor=ffffff)](LICENSE)
![Platform: Linux](https://img.shields.io/badge/Platform-Linux-242424?style=for-the-badge&labelColor=0a0a0a&logo=linux&logoColor=ffffff)
![Language: C99](https://img.shields.io/badge/Language-C99-2e2e2e?style=for-the-badge&labelColor=141414&logo=c&logoColor=ffffff)
![Engine: aria2](https://img.shields.io/badge/Engine-aria2-383838?style=for-the-badge&labelColor=1e1e1e)
![Interface: CUI 1.4.0](https://img.shields.io/badge/Interface-CUI_1.4.0-424242?style=for-the-badge&labelColor=282828)
![Mode: GUI + CLI](https://img.shields.io/badge/Mode-GUI_%2B_CLI-4c4c4c?style=for-the-badge&labelColor=323232)
![Themes: 76 built-in](https://img.shields.io/badge/Themes-76_built--in-565656?style=for-the-badge&labelColor=3c3c3c)
![Downloads: BitTorrent + Metalink](https://img.shields.io/badge/Downloads-BitTorrent_%2B_Metalink-606060?style=for-the-badge&labelColor=464646)
![Display: X11 + XWayland](https://img.shields.io/badge/Display-X11_%2B_XWayland-6a6a6a?style=for-the-badge&labelColor=505050)

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Build](#build)
- [AppImage](#appimage)
- [Usage](#usage)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [Marketplace](#marketplace)
- [Configuration](#configuration)
- [Project layout](#project-layout)
- [License](#license)

## Features

**Download engine**

- HTTP, HTTPS, FTP, BitTorrent and Metalink downloads through aria2
- Resume, multiple connections per download and checksum verification
- Bundled aria2 runtime, with a fallback to the system `aria2c`
- Global download and upload limits in KB/s, applied live, with a throughput chart that draws
  the cap
- Seeding control: keep sharing completed torrents up to a chosen ratio
- Scheduled downloads by preset or exact date and time
- Torrent inspector with per-file progress, skipped files, peer state and live peer speeds
- Session persistence through aria2's session file

**Interface**

- Frameless window with an internal title bar for dragging, minimizing, maximizing and resizing;
  the minimum window size is 640x420
- 76 built-in themes, with Dark Mono as the default, plus marketplace theme packs
- Optional Liquid Glass theme with light and dark variants, a unified toolbar, traffic lights and
  optical glass on the toolbar, sidebar and dialogs
- Sidebar tabs for All, Active, Waiting, Stopped and Complete
- Resizable sidebar from 140 to 420 px, with live transfer speeds in the status bar
- Searchable download table with Name, Size, Progress, Speed, ETA, Added, Location, Connections
  and Status; every column except Name can be hidden
- Click a header to sort, drag a divider to resize the column on its right, and use the Columns
  menu to show, hide or reset columns; sort order and widths persist across restarts
- Details pane with progress, size, speed, ETA, Added time, save location, connections, seeders
  and files
- Statistics view with a 60-second chart, peak marker, live speeds and a download-folder card
  with free space and an Open folder button
- Clipboard monitoring and desktop notifications on completion
- Settings for General, Bandwidth, Torrents, Appearance, Keys and Updates, including theme, any
  installed font, logo style, text size, sidebar width, spacing, motion, engine limits and a
  keymap diagram

**Workflow**

- Row actions: open a finished file, open its folder, copy the source link or retry a failed
  download
- Bulk selection by drag band or `Ctrl+click`, then pause, resume or remove in one go
- Add dialog with a per-download directory, file name and connection count

**Keyboard and accessibility**

- Command palette, full list navigation and an optional Vim mode
- Confirmation before destructive actions
- WCAG AA text contrast on every theme surface, focus rings with 3:1 non-text contrast and
  visible keyboard focus
- The complete workflow is available without a mouse

**System integration**

- Single-instance window: later launches hand their links to the running instance and raise it
- `.torrent`, `.metalink` and `magnet:` handlers, plus drag and drop for torrent and metalink files
- CLI commands for scripting and headless use
- In-app updates from Settings > Updates, with AppImage replacement or installation to
  `~/.local/bin/greed`
- Marketplace for optional engines, tools and themes, with SHA-256 verification

## Requirements

- Linux with X11; on Wayland, Greed runs through XWayland
- OpenGL, with a software-rendering fallback
- A C compiler, CMake, libcurl, json-c and X11 development headers

Arch:

```sh
sudo pacman -S base-devel cmake curl json-c libx11
```

Debian/Ubuntu:

```sh
sudo apt install build-essential cmake libcurl4-openssl-dev libjson-c-dev libx11-dev
```

aria2 is bundled, so no separate installation is required. A system `aria2c` is used only when
the bundled binary is missing.

## Build

```sh
git clone https://github.com/domesticsilk/greed.git
cd greed
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/greed
```

`vendor/cui/cui.c` is the large translation unit, taking about 30 seconds on a slow machine.
CMake builds it once as a static library, so later rebuilds only compile the application sources.

Run the test suite from the build directory:

```sh
cd build
ctest --output-on-failure
```

## AppImage

```sh
./packaging/build-appimage.sh
```

The script writes `build/Greed-x86_64.AppImage` (about 3.4 MB) with the private aria2 library
inside, so the release runs on a machine with no aria2 installed. The first run downloads
`appimagetool` into `build/`; when FUSE is unavailable, the script falls back to extracting it.

Publish that file as the `Greed-x86_64.AppImage` asset of a GitHub release. Settings > Updates
looks for that exact name and compares the release tag with `GREED_VERSION` in `src/version.h`.

## Usage

### GUI

```sh
greed                              # open the GUI
greed https://example.com/file.zip # open the Add dialog with the link
greed magnet:?xt=urn:btih:...      # magnet in the Add dialog
greed ./debian.torrent             # torrent or metalink file in the Add dialog
```

With no arguments, `greed` opens the GUI when `DISPLAY` or `WAYLAND_DISPLAY` is set and prints
help otherwise. A bare link or torrent/metalink path opens the GUI with the Add dialog filled; on
a machine without a display, it falls back to `greed add`.

### CLI

```sh
greed add https://example.com/file.zip
greed add magnet:?xt=urn:btih:...
greed add ./debian.torrent
greed add URL -d ~/Downloads -o name.iso -x 8
greed list [--json]
greed status <gid> [--json]
greed pause <gid>...
greed resume <gid>...
greed remove <gid>... [--delete]   # --delete also drops the stopped result
greed stats
greed shutdown                     # stop the aria2 engine
```

### Single instance

Only one GUI runs per user. A second `greed` or `greed <link>` writes the links to the running
instance, raises its window and exits. The running window opens the Add dialog, or appends to the
open one. The lock and handoff files live in `$XDG_RUNTIME_DIR`, or `/tmp` when it is unset.

### Desktop integration

`packaging/greed.desktop` registers `application/x-bittorrent`, `application/metalink4+xml`,
`application/metalink+xml` and `x-scheme-handler/magnet`, and uses `Exec=greed %U`. Install it
under `~/.local/share/applications/` and run:

```sh
xdg-mime default greed.desktop application/x-bittorrent
xdg-mime default greed.desktop application/metalink4+xml
xdg-mime default greed.desktop application/metalink+xml
xdg-mime default greed.desktop x-scheme-handler/magnet
```

Drag and drop accepts `.torrent` and `.metalink` files. X11 forwards file URIs only, so a link
dragged from a browser is not delivered; copying it works through clipboard monitoring.

### Table controls

- Click a column header to sort by that column. Click the same header again to reverse the order.
- Use the Columns button in the toolbar to show or hide columns. Name is always visible.
- Drag the divider at a column's left edge to resize it. The column to the right of the divider
  changes width; Name absorbs the difference.
- Choose Reset columns to restore the default columns and widths.

## Keyboard shortcuts

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

The cart button in the title bar, or the command palette, opens the Marketplace. It lists
optional engines, tools and theme packs from
`https://raw.githubusercontent.com/domesticsilk/greed/main/marketplace/catalog.json`, with a
built-in copy when the network or the catalog is unavailable.

- Every asset is downloaded to a temporary file, checked against the `sha256` in the catalog,
  and only then moved into place.
- Installs go to `~/.local/share/greed/` (or `$XDG_DATA_HOME/greed/`): themes under `themes/`,
  tools under `tools/<id>/`. Nothing outside that folder is written.
- The catalog is cached as `~/.local/share/greed/marketplace/catalog.json`, and installed items
  are tracked in `installed.json` next to it. Remove deletes the installed file.
- yt-dlp is the first engine. With it installed, the Add dialog offers "Resolve video pages with
  yt-dlp": Greed asks yt-dlp for the best single-file progressive stream and adds the direct URL
  to aria2 with the headers that stream needs. Streams that need merging are not offered yet.
- Theme packs use Greed's theme JSON format. After installation they appear in
  Settings > Appearance next to the 76 built-in themes.
- Installing a theme does not switch to it. Installed themes show Apply in the Marketplace and
  in Settings > Appearance; Apply takes effect immediately. Removing the active theme falls back
  to Dark Mono.
- Theme JSON may set `chrome` (`greed` or `macos`) and `material` (`solid` or `glass`), plus a
  `glass` object with `tint`, `tint_alpha`, `highlight`, `highlight_alpha` and `shadow_alpha`.
  Liquid Glass uses both. macOS chrome also prefers SF Pro Text/Display, then Inter, when `font`
  is `default`.

`marketplace/catalog.json` documents the catalog format: each item has an `id`, a `type`
(`engine`, `tool` or `theme`), a version, an asset URL, a SHA-256 and an optional preview
palette.

## Configuration

Greed reads `~/.config/greed/config.json`, or `$XDG_CONFIG_HOME/greed/config.json` when
`XDG_CONFIG_HOME` is set:

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
  "logo_app_font": false,
  "sidebar_width": 0,
  "columns": 319,
  "sort_column": 5,
  "sort_desc": true,
  "col_widths": [0, 0, 0, 0, 0, 0, 0, 0, 0],
  "text_scale": 1.0,
  "spacing": 1.0,
  "motion_speed": 1.0,
  "vim_mode": false
}
```

`font` is a preset (`default`, `sans`, `serif`, `mono`) or any font family installed on the
device. `logo_app_font` draws the title bar wordmark in the application font instead of the Greed
logo. `sidebar_width` is in pixels from 140 to 420, where 0 uses the theme default (198, or 220
with macOS chrome). `text_scale` ranges from 0.85 to 1.3 and `spacing` from 0.9 to 1.12. The same
values can be changed in Settings > Appearance, where the font list is searchable and the sidebar
width also has a slider.

`columns` is a bitmask of the visible table columns: Name 1, Size 2, Progress 4, Speed 8, ETA 16,
Added 32, Location 64, Connections 128 and Status 256. Name is always visible. `sort_column`
uses the same order (5 is Added) and `sort_desc` flips the direction. `col_widths` holds one
width per column in points at text scale 1; 0 keeps the column's default and 48 to 900 is the
accepted range. The Columns menu, header clicks and divider drags write the same values.

The download and upload limits are in KB/s; 0 means unlimited. The aria2 session is saved next to
the configuration file as `aria2.session`. The same directory and format are used by the earlier
Rust version.

## Project layout

```
src/main.c        CLI/GUI dispatch
src/ui.c          CUI interface: title bar, sidebar, table, details, dialogs, toasts
src/model.c       aria2 JSON parsing and formatters
src/rpc.c         JSON-RPC over libcurl, aria2 discovery and spawning, base64
src/config.c      configuration file
src/history.c     first-seen timestamps for the Added column
src/cli.c         subcommands
src/market.c      marketplace catalog and checksum-verified installs
src/ytdlp.c       yt-dlp resolver and aria2 handoff
src/links.c       link detection and file:// normalization
src/single.c      single-instance lock, link handoff and window activation
src/x11.c         frameless window, drag/resize, maximize, minimize
src/checksum.c    checksum helpers
src/update.c      in-app updates
src/theme.c       built-in themes and theme JSON loading
src/logo.h        title bar wordmark alpha mask (generated asset)
tests/            link, history and contrast checks
marketplace/      catalog and theme pack assets
vendor/cui/       CUI 1.4.0 amalgamation (MIT)
vendor/aria2/     aria2c and libaria2 (GPL-2.0)
```

## License

GPL-2.0. CUI is MIT-licensed and vendored under `vendor/cui/LICENSE`; aria2 is GPL-2.0 and
vendored under `vendor/aria2/`.

The title bar wordmark (`src/logo.h`) is a rendered image of the Stitch Warrior demo font, which
is free for personal use only. The font file itself is not included, and the wordmark is not
covered by the GPL; replace it if you need a different license.
