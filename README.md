# Greed

A minimal, advanced download manager for Linux. GUI + CLI in a single Rust binary, powered by [aria2](https://aria2.github.io/).

![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-blue.svg)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![Rust](https://img.shields.io/badge/rust-1.88%2B-orange.svg)

## Features

- **aria2 core** — HTTP/HTTPS/FTP/BitTorrent/Metalink, resume, multi-connection, checksums
- **GUI (egui)** — dark Echidna theme (Re:Zero), tabs (Active/Waiting/Stopped/Complete), compact list with progress/speed/ETA, command palette
- **CLI** — `greed add/list/status/pause/resume/remove/stats/shutdown/version` with `--json`
- **Accurate statistics** — global and per-download speed, size, ETA, peers/seeders
- **Clipboard monitoring** — popup on copied URL (toggleable)
- **Torrent support** — magnet links and `.torrent` files
- **Keyboard-first** — shortcuts + command palette (Ctrl+Shift+P)
- **Maia-inspired** — config JSON, crash log, `--install`/`--uninstall`, first-run tour

## Requirements

- Linux (X11 or Wayland via XWayland)
- OpenGL
- `aria2` installed (`sudo pacman -S aria2` / `sudo apt install aria2`)
- Rust 1.88+ (to build)

## Install

### From release
Download the latest release binary and run:

```sh
./greed --install
```

This places `greed` in `~/.local/bin` and adds a launcher entry.

### Build from source
```sh
git clone https://github.com/thefool/greed.git
cd greed
cargo build --release
./target/release/greed --install
```

Release profile uses `opt-level="z"`, fat LTO, strip, `panic=abort` for a minimal binary.

## Usage

### GUI
```sh
greed            # opens GUI if DISPLAY is set
greed gui        # force GUI
```

### CLI
```sh
greed add https://example.com/file.zip -d ~/Downloads -x 8
greed list
greed status <GID>
greed pause <GID>
greed resume <GID>
greed remove <GID> --delete
greed stats
greed shutdown
```

Add `--json` to `list`/`status` for machine-readable output.

## Configuration

Settings live in `~/.config/greed/config.json`. Crash log: `~/.local/state/greed/crash.log`.

## License

GPL-2.0-only. See [LICENSE](LICENSE).
