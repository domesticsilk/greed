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
aria2_bin=${ARIA2_BIN:-"$root/vendor/aria2/aria2c"}
aria2_lib=${ARIA2_LIB:-"$root/vendor/aria2/lib/libaria2.so.0"}
install -Dm755 "$aria2_bin" "$appdir/usr/bin/aria2c"
install -Dm755 "$aria2_lib" "$appdir/usr/bin/lib/libaria2.so.0"
# A portable build bundles aria2's non-core libraries next to the binary.
# rpc.c points only the aria2c child at this folder, so the system libcurl
# and GL stack never see an older bundled library.
if [ "${BUNDLE_DEPS:-0}" = 1 ]; then
    ldd "$aria2_bin" "$aria2_lib" | awk '{print $3}' | grep '^/' | sort -u |
    while read -r lib; do
        case "$(basename "$lib")" in
            libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libresolv.so.*|ld-linux*) continue ;;
        esac
        install -Dm755 "$lib" "$appdir/usr/bin/lib/$(basename "$lib")"
    done
fi
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
make_appimage() {
    if [ -n "${APPIMAGE_RUNTIME:-}" ]; then
        ARCH=x86_64 "$tool" --runtime-file "$APPIMAGE_RUNTIME" "$appdir" "$build/Greed-x86_64.AppImage"
    else
        ARCH=x86_64 "$tool" "$appdir" "$build/Greed-x86_64.AppImage"
    fi
}
if ! make_appimage 2>/dev/null; then
    APPIMAGE_EXTRACT_AND_RUN=1 make_appimage
fi
echo "$build/Greed-x86_64.AppImage"
