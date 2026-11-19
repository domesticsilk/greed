# Greed

A dark download manager for Linux. GUI + CLI in a single Rust binary, powered by [aria2](https://aria2.github.io/).

![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-blue.svg)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Rust](https://img.shields.io/badge/rust-1.88%2B-orange.svg)

## Features

- **aria2 core** — HTTP/HTTPS/FTP, BitTorrent, Metalink; resume, multi-connection, checksums
- **Bundled aria2** — `vendor/aria2/` ships with the repo; falls back to a system `aria2c`
- **GUI (egui)** — dark Witch's Ink theme, sidebar tabs (All/Active/Waiting/Stopped/Complete), searchable table with progress, speed, ETA and status, details panel per download
- **CLI** — `greed add/list/status/pause/resume/remove/stats/shutdown` with `--json` for `list` and `status`
- **Torrents** — magnet links and `.torrent` files
- **Clipboard monitoring** — offers to download a copied link (toggleable)
- **Notifications** — desktop notification when a download completes (toggleable)
- **Live stats** — global and per-download speed, size, ETA, connections, seeders

## Requirements

- Linux (X11 or Wayland via XWayland)
- OpenGL
- Rust 1.88+ (to build)

aria2 is bundled, so nothing else is needed. A system `aria2c` is only used when the bundled one is missing.

## Build

```sh
git clone https://github.com/domesticsilk/greed.git
cd greed
cargo build --release
./target/release/greed
```

Release profile uses `opt-level="z"`, fat LTO, strip and `panic=abort` for a small binary.

## Usage

### GUI

```sh
greed            # opens the GUI if DISPLAY or WAYLAND_DISPLAY is set
greed gui        # force the GUI
```

Shortcuts: `Ctrl+N` add, `Ctrl+F` search, `Space` pause/resume, `Delete` remove, `Esc` clear selection. Double-click a row to open its folder.

### CLI

```sh
greed add https://example.com/file.zip -d ~/Downloads -x 8
greed add magnet:?xt=urn:btih:…
greed list
greed list --json
greed status <GID>
greed pause <GID>
greed resume <GID>
greed remove <GID>          # keep the download result entry
greed remove <GID> --delete # remove it from aria2 entirely
greed stats
greed shutdown
```

The CLI starts aria2 automatically when it is not already running.

## Configuration

Settings live in `~/.config/greed/config.json` and are also editable in the GUI (Settings):

| Key | Default | Meaning |
| --- | --- | --- |
| `download_dir` | `~/Downloads` | Default save location |
| `max_concurrent` | `5` | Max simultaneous downloads |
| `rpc_port` | `6800` | aria2 JSON-RPC port (takes effect after restart) |
| `auto_start_aria2` | `true` | Start the engine on launch |
| `clipboard_monitor` | `true` | Offer copied links |
| `notify_on_complete` | `true` | Desktop notification on completion |

## License

GPL-2.0-only. See [LICENSE](LICENSE).
