# Greed

A dark download manager for Linux. GUI and CLI in one C binary, powered by
[aria2](https://aria2.github.io/) and drawn with [CUI](https://github.com/yappologistic/CUI).

![Greed](assets/screenshot.png)

![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-blue.svg)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![C99](https://img.shields.io/badge/C-C99-blue.svg)

## Features

- **aria2 core** — HTTP/HTTPS/FTP, BitTorrent, Metalink; resume, multi-connection, checksums
- **Bundled aria2** — `vendor/aria2/` ships with the repo; falls back to a system `aria2c`
- **Frameless window** — the title bar is drawn inside the app, with drag, minimize, maximize and edge resize
- **GUI** — Witch's Ink theme, sidebar tabs (All/Active/Waiting/Stopped/Complete), a searchable
  download table with progress, speed, ETA and status, a per-download details pane and a live
  transfer sparkline in the sidebar
- **Bulk actions** — check several downloads and pause, resume or remove them in one go
- **CLI** — `greed add/list/status/pause/resume/remove/stats/shutdown`, with `--json` for
  `list` and `status`
- **Torrents** — magnet links and `.torrent` files (file contents are base64-encoded for aria2)
- **Bandwidth control** — global download/upload caps in KB/s, applied to the running engine,
  with a live throughput chart that draws the cap on the line
- **Seeding** — keep sharing completed torrents up to a chosen ratio
- **Statistics** — live transfer chart, downloads-by-state donut and a disk-space meter
- **Scheduled downloads** — pick a date and time; Greed hands the link to aria2 when it is due
- **Settings** — General, Bandwidth and Torrents tabs with sliders, switches and limits
- **Clipboard monitoring** — offers a toast when a link is copied (toggleable)
- **Notifications** — desktop notification when a download completes (toggleable)
- **Session persistence** — the queue survives restarts through aria2's session file
- **Live stats** — global and per-download speed, size, ETA, connections, seeders

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

Keyboard shortcuts: `Ctrl+N` add, `Ctrl+F` search, `Space` pause/resume, `Delete` remove,
`Esc` close details.

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
  "notify_on_complete": true
}
```

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
src/x11.c      frameless window, drag/resize, maximize, minimize
vendor/cui/    CUI 1.4.0 amalgamation (MIT)
vendor/aria2/  aria2c + libaria2 (GPL-2.0)
```

## License

GPL-2.0. CUI is MIT-licensed and vendored under `vendor/cui/LICENSE`; aria2 is GPL-2.0 and
vendored under `vendor/aria2/`.

The titlebar wordmark (`src/logo.h`) is a rendered image of the Stitch Warrior demo font,
which is free for personal use only. The font file itself is not included, and the wordmark
is not covered by the GPL; replace it if you need a different license.
