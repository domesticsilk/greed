const HOST = "com.domesticsilk.greed";
const MENU_ID = "greed-download";

async function add(url) {
  if (!/^https?:\/\//i.test(url)) throw new Error("Only http and https links can be sent.");
  const reply = await chrome.runtime.sendNativeMessage(HOST, { urls: [url] });
  if (!reply || !reply.ok) throw new Error((reply && reply.error) || "Greed refused the link.");
}

async function flash(text, color) {
  await chrome.action.setBadgeBackgroundColor({ color });
  await chrome.action.setBadgeText({ text });
  setTimeout(() => chrome.action.setBadgeText({ text: "" }), 3000);
}

function hint(error) {
  if (/not found|not installed|forbidden/i.test(error))
    return "Run greed browser install, then reload this extension.";
  return error;
}

async function submit(url) {
  try {
    await add(url);
    flash("\u2713", "#1f7a3d");
    return { ok: true };
  } catch (e) {
    const message = hint(e.message || String(e));
    flash("!", "#a33");
    chrome.notifications.create({
      type: "basic",
      iconUrl: "icons/icon128.png",
      title: "Greed could not take the link",
      message,
    });
    return { ok: false, error: message };
  }
}

chrome.runtime.onInstalled.addListener(() => {
  chrome.contextMenus.removeAll(() => {
    chrome.contextMenus.create({
      id: MENU_ID,
      title: "Download with Greed",
      contexts: ["link", "image", "video", "audio"],
    });
  });
});

chrome.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId !== MENU_ID) return;
  const url = info.linkUrl || info.srcUrl;
  if (url) submit(url);
});

chrome.runtime.onMessage.addListener((msg, _sender, reply) => {
  if (msg && msg.type === "add" && msg.url) {
    submit(msg.url).then(reply);
    return true;
  }
  if (msg && msg.type === "status") {
    chrome.runtime
      .sendNativeMessage(HOST, { type: "ping" })
      .then((res) => reply({ ok: !!(res && res.ok), version: res && res.version }))
      .catch((e) => reply({ ok: false, error: hint(e.message || String(e)) }));
    return true;
  }
});
