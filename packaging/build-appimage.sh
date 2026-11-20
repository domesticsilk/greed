#!/bin/sh
# Build build/Greed-x86_64.AppImage. Needs curl once, to fetch appimagetool.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${BUILD_DIR:-"$root/build"}
appdir="$build/AppDir"
tool=${APPIMAGETOOL:-"$build/appimagetool-x86_64.AppImage"}

if [ ! -x "$build/greed" ]; then
    cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$build" -j
fi

rm -rf "$appdir"
install -Dm755 "$build/greed" "$appdir/usr/bin/greed"
install -Dm755 "$root/vendor/aria2/aria2c" "$appdir/usr/bin/aria2c"
install -Dm755 "$root/vendor/aria2/lib/libaria2.so.0" "$appdir/usr/lib/libaria2.so.0"
install -Dm644 "$root/packaging/greed.desktop" "$appdir/greed.desktop"
install -Dm644 "$root/packaging/greed.png" "$appdir/greed.png"
install -Dm644 "$root/packaging/greed.png" "$appdir/usr/share/icons/hicolor/128x128/apps/greed.png"
install -Dm755 "$root/packaging/AppRun" "$appdir/AppRun"

if [ ! -x "$tool" ]; then
    echo "fetching appimagetool" >&2
    curl -fsSL -o "$tool" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$tool"
fi

# Some containers lack FUSE; the tool can unpack itself instead.
if ! ARCH=x86_64 "$tool" "$appdir" "$build/Greed-x86_64.AppImage" 2>/dev/null; then
    ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$tool" "$appdir" "$build/Greed-x86_64.AppImage"
fi
echo "$build/Greed-x86_64.AppImage"
