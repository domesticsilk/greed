# Changelog

## Unreleased

### Interface

- Theme packs can ship a wallpaper. Greed cover-crops it behind the interface, paints the theme
  background over it at `wallpaper_scrim`, and keeps panels translucent with `surface_alpha`.
  Text colours are checked against the scrimmed worst case.
- Ado and Echidna join the Marketplace. Ado is a dark stage with electric blue; Echidna is a teal
  night with mint, violet wings and muted gold.
- Installing a theme with a wallpaper downloads and verifies both files; Remove deletes both.

## 0.1.0 - 2026-11-21

First public release.

### Downloads

- HTTP, HTTPS, FTP, BitTorrent and Metalink through a bundled aria2 engine, with a fallback to
  the system `aria2c`
- Resume, multiple connections per download, global speed limits and checksum verification
- Video pages through yt-dlp with Best, 1080p, 720p, 480p and 360p choices; separate video and
  audio streams merge into one MP4 or MKV with ffmpeg
- Merge state survives a restart: downloads that finished while Greed was closed are merged on
  the next start, and an interrupted merge resumes
- HTTP proxy with optional credentials and a bypass list, applied to the running engine
- Per-download Referer, User-Agent, cookies and extra `Name: value` headers in the Add dialog and
  `greed add`; yt-dlp reads them from an unlinked mode-0600 file so cookies stay out of the
  process list, and none of it is saved
- Background tray indicator with the active count while downloads continue after the window
  closes (X11/XEmbed), click to reopen
- Torrent inspector with per-file progress and peer state, seeding ratio control and session
  persistence
- Scheduled downloads, statistics, clipboard monitoring and completion notifications

### Interface

- One C binary with a GUI and a CLI, running on X11 and XWayland
- Frameless window with an internal title bar; minimum size 640x420
- 76 built-in themes with Dark Mono as the default, plus an optional Liquid Glass theme
- Sidebar tabs and automatic file-type categories that appear once they have downloads
- Sortable table with hidden-column controls, drag resizing and an Added column
- Details pane on the right for a single selection; selecting two or more downloads with a drag
  band, `Ctrl+click` or `Shift+Up/Down` opens a bottom panel of side-by-side cards with pause,
  resume, remove and per-card deselect
- Animations can be turned off in Settings, which also disables the animation speed control
- Command palette, Vim mode and a workflow that works without a mouse
- WCAG AA text contrast on every theme surface and visible keyboard focus
- Settings for the engine, network, torrents, appearance, keys and updates

### Marketplace and updates

- Marketplace for engines, tools and themes, with SHA-256 verified installs
- FFmpeg and yt-dlp entries, with the FFmpeg logo and All, Engines and Themes tabs
- In-app updates from GitHub releases; the updater checks the published SHA-256 before replacing
  the running AppImage

### Packaging

- Portable single-file AppImage built against glibc 2.35, with aria2 and its non-core libraries
  bundled, plus a lite AppImage that uses the system `aria2c`
- `.torrent`, `.metalink` and `magnet:` system handlers, single-instance link handoff and drag
  and drop
- Chromium extension for right-click downloads, with a native messaging helper registered by
  `greed browser install`
- CI builds and tests every push; a tag builds the AppImage and publishes it with its checksum
