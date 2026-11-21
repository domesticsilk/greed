#!/bin/sh
# Build the release AppImage inside an Ubuntu 22.04 rootfs, so the result
# needs no newer than glibc 2.35. Uses user namespaces, no Docker required.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cache=${PORTABLE_CACHE:-"$root/build/portable"}
rootfs="$cache/rootfs"
tarball="$cache/ubuntu-base-22.04.5-base-amd64.tar.gz"
url=${UBUNTU_BASE_URL:-https://cdimage.ubuntu.com/ubuntu-base/releases/22.04/release/ubuntu-base-22.04.5-base-amd64.tar.gz}
resolv=${RESOLV_CONF:-/etc/resolv.conf}   # override when the host resolver is broken

if ! unshare -rm true 2>/dev/null; then
    echo "error: user namespaces are unavailable; run this on a host that allows unshare -r" >&2
    exit 1
fi

mkdir -p "$cache" "$cache/out/build"
if [ ! -x "$rootfs/bin/bash" ]; then
    [ -f "$tarball" ] || curl -fSL -o "$tarball" "$url"
    mkdir -p "$rootfs"
    tar -xzf "$tarball" -C "$rootfs"
fi
cp "$resolv" "$rootfs/etc/resolv.conf"
# Ubuntu Base ships no CA bundle; copy the host one and use HTTPS mirrors so
# networks that filter plain HTTP still work.
if [ -f /etc/ssl/certs/ca-certificates.crt ]; then
    install -Dm644 /etc/ssl/certs/ca-certificates.crt "$rootfs/etc/ssl/certs/ca-certificates.crt"
    sed -i 's|http://|https://|g' "$rootfs/etc/apt/sources.list"
fi
mkdir -p "$rootfs/src" "$rootfs/proc" "$rootfs/dev" "$rootfs/sys" "$rootfs/out"

# Reuse the appimagetool already downloaded for the local build.
if [ -x "$root/build/appimagetool-x86_64.AppImage" ]; then
    cp "$root/build/appimagetool-x86_64.AppImage" "$cache/out/build/appimagetool-x86_64.AppImage"
    chmod +x "$cache/out/build/appimagetool-x86_64.AppImage"
fi

# appimagetool downloads its runtime by default; reuse the one inside an
# existing AppImage so the build also works offline.
runtime=${APPIMAGE_RUNTIME:-}
if [ -z "$runtime" ] && [ -x "$root/build/Greed-x86_64.AppImage" ] && command -v python3 >/dev/null 2>&1; then
    runtime="$cache/runtime-x86_64"
    python3 - "$root/build/Greed-x86_64.AppImage" "$runtime" <<'PY'
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
pos = 0
while True:
    i = data.find(b'hsqs', pos)
    if i < 0:
        sys.exit('no squashfs superblock in ' + src)
    if i + 96 <= len(data):
        block, = struct.unpack_from('<I', data, i + 12)
        major, minor = struct.unpack_from('<HH', data, i + 28)
        used, = struct.unpack_from('<Q', data, i + 40)
        if major == 4 and minor == 0 and 4096 <= block <= 1048576 and \
           block & (block - 1) == 0 and used <= len(data) - i:
            break
    pos = i + 1
open(dst, 'wb').write(data[:i])
PY
fi
if [ -n "$runtime" ]; then
    cp "$runtime" "$cache/out/runtime-x86_64"
fi

cat > "$rootfs/tmp/portable-build.sh" <<'EOF'
#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
if [ ! -f /.deps ]; then
    rm -rf /var/lib/apt/lists/*
    apt-get -o APT::Sandbox::User=root update -qq
    apt-get -o APT::Sandbox::User=root install -y -qq --no-install-recommends \
        build-essential cmake pkg-config libcurl4-openssl-dev libjson-c-dev \
        libx11-dev libxext-dev aria2 libaria2-0 ca-certificates curl file
    touch /.deps
fi
cmake -S /src -B /out/build -DCMAKE_BUILD_TYPE=Release
cmake --build /out/build -j"$(nproc)"
ctest --test-dir /out/build --output-on-failure

[ -f /out/runtime-x86_64 ] && export APPIMAGE_RUNTIME=/out/runtime-x86_64
ARIA2_BIN=/usr/bin/aria2c ARIA2_LIB=/usr/lib/x86_64-linux-gnu/libaria2.so.0 \
    BUNDLE_DEPS=1 BUILD_DIR=/out/build /src/packaging/build-appimage.sh

echo -n "highest glibc symbol: "
objdump -T /out/build/AppDir/usr/bin/greed /out/build/AppDir/usr/bin/aria2c |
    grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1

if LD_LIBRARY_PATH=/out/build/AppDir/usr/bin/lib ldd /out/build/AppDir/usr/bin/aria2c |
    grep -q 'not found'; then
    echo "error: the bundled aria2 still has unresolved libraries" >&2
    exit 1
fi

APPIMAGE_EXTRACT_AND_RUN=1 /out/build/Greed-x86_64.AppImage version
EOF
chmod +x "$rootfs/tmp/portable-build.sh"

unshare -rmp --fork sh -c "
    mount -t proc proc '$rootfs/proc'
    mount --bind '$root' '$rootfs/src'
    mount --bind '$cache/out' '$rootfs/out'
    mount --rbind /dev '$rootfs/dev'
    mount --rbind /sys '$rootfs/sys'
    chroot '$rootfs' /bin/bash /tmp/portable-build.sh
"

cp "$cache/out/build/Greed-x86_64.AppImage" "$root/build/Greed-x86_64.AppImage.new"
mv "$root/build/Greed-x86_64.AppImage.new" "$root/build/Greed-x86_64.AppImage"
ls -l "$root/build/Greed-x86_64.AppImage"
