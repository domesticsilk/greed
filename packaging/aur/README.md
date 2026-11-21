# AUR packages

The AUR already has an unrelated game named `greed`, so these packages are called
`greed-dm` and `greed-dm-git`. Both install the same `greed`
binary, desktop entry, icon and the browser extension under `/usr/share/greed/extension`.

- `greed-dm` builds the `v0.2.0` tag.
- `greed-dm-git` builds the current `main`.

## Test locally

```sh
cd packaging/aur/greed-dm
makepkg -si
```

## Publish

The source must be public before the AUR package can build. Push the repository and the
`v0.2.0` tag first, then create both AUR repositories and push `PKGBUILD` and `.SRCINFO`:

```sh
git clone ssh://aur@aur.archlinux.org/greed-dm.git
cp packaging/aur/greed-dm/{PKGBUILD,.SRCINFO} greed-dm/
cd greed-dm
git add PKGBUILD .SRCINFO
git commit -m "Add greed-dm"
git push
```

Repeat for `greed-dm-git`. Regenerate `.SRCINFO` after every PKGBUILD change:

```sh
makepkg --printsrcinfo > .SRCINFO
```

The package does not register the browser helper. After installing, run:

```sh
greed browser install
```

Then open `chrome://extensions`, enable Developer mode, choose Load unpacked and select
`/usr/share/greed/extension`.
