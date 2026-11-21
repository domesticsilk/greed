const $ = (id) => document.getElementById(id);

function httpUrl(url) {
  return typeof url === "string" && /^https?:\/\//i.test(url);
}

async function currentTab() {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  return tab;
}

async function refresh() {
  const status = await chrome.runtime.sendMessage({ type: "status" });
  const ok = !!(status && status.ok);
  document.body.dataset.online = ok ? "true" : "false";
  $("status-text").textContent = ok
    ? `Greed helper ${status.version || ""} connected`.trim()
    : "Greed helper not connected";
  $("fix").hidden = ok;

  const tab = await currentTab();
  const url = tab && tab.url;
  $("page").dataset.url = httpUrl(url) ? url : "";
  $("page-url").textContent = httpUrl(url) ? url : "This page cannot be sent.";
  $("page").disabled = !ok || !httpUrl(url);
}

$("page").addEventListener("click", async () => {
  const url = $("page").dataset.url;
  if (!url) return;
  $("page").disabled = true;
  const res = await chrome.runtime.sendMessage({ type: "add", url });
  if (res && res.ok) {
    $("note").textContent = "Sent to Greed.";
    setTimeout(() => window.close(), 800);
  } else {
    $("note").textContent = (res && res.error) || "Could not send the link.";
    $("page").disabled = false;
  }
});

refresh();
