# Greed browser extension

Sends links from Chrome, Chromium, Helium, Brave, Edge, Vivaldi or Opera to Greed.

## Install

1. Register the native helper from the same user account as the browser:

   ```sh
   greed browser install
   ```

   It writes a small wrapper in `~/.local/share/greed/native-host` and a host manifest into
   every browser profile it finds.

2. Open `chrome://extensions`, enable **Developer mode**, choose **Load unpacked**, and select
   this `extension/` folder.

3. Right-click a link, image, video or audio and choose **Download with Greed**.

The extension ID is pinned in `manifest.json`, so the helper registration survives reloads. If you
move the Greed AppImage, run `greed browser install` again so the helper points at the new path.

## Use

- Right-click a link, image, video or audio and choose **Download with Greed**.
- Or open the toolbar popup and send the current page.
- Greed opens its Add dialog with the links. When Greed is not running, the helper starts it.

## Limits

- Only `http` and `https` URLs are sent.
- The helper passes the URL to Greed; cookies and logged-in sessions are not copied. Links that
  need a browser session may fail.
- Video pages such as YouTube should be added through Greed itself, so yt-dlp can resolve the
  formats.
- Flatpak and Snap browser profiles are not detected. Register the helper for a native browser,
  or copy the manifest into the sandboxed profile by hand.

## Remove

```sh
greed browser uninstall
```

Then remove the extension from `chrome://extensions`.
