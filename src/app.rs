//! GUI application: layout, state and actions.

use crate::aria2::{self, Client};
use crate::config::Config;
use crate::model::{fmt_bytes, fmt_eta, fmt_speed, Download, GlobalStat};
use crate::theme::{self, *};
use crate::ui;
use eframe::egui::{
    self, Align, Align2, Color32, FontId, Frame, Layout, Margin, RichText, Stroke,
};
use std::collections::HashMap;
use std::sync::mpsc;
use std::time::Duration;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Tab {
    All,
    Active,
    Waiting,
    Stopped,
    Complete,
}

impl Tab {
    pub const ALL: [Tab; 5] = [
        Tab::All,
        Tab::Active,
        Tab::Waiting,
        Tab::Stopped,
        Tab::Complete,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Tab::All => "All",
            Tab::Active => "Active",
            Tab::Waiting => "Waiting",
            Tab::Stopped => "Stopped",
            Tab::Complete => "Complete",
        }
    }

    pub fn matches(self, d: &Download) -> bool {
        match self {
            Tab::All => true,
            Tab::Active => d.status == "active",
            Tab::Waiting => matches!(d.status.as_str(), "waiting" | "paused"),
            Tab::Stopped => matches!(d.status.as_str(), "error" | "removed"),
            Tab::Complete => d.status == "complete",
        }
    }
}

pub enum Action {
    Select(String),
    Deselect,
    Toggle(String),
    Remove(String),
    OpenFolder(String),
}

struct Snapshot {
    downloads: Vec<Download>,
    global: GlobalStat,
    connected: bool,
}

pub struct GreedApp {
    pub client: Client,
    pub config: Config,
    pub downloads: Vec<Download>,
    pub global: GlobalStat,
    pub connected: bool,
    pub filter: String,
    pub tab: Tab,
    pub selected: Option<String>,
    pub show_add: bool,
    pub show_settings: bool,
    pub add_urls: String,
    pub add_dir: String,
    pub add_name: String,
    pub add_conn: u32,
    pub status_msg: String,
    pub pending_clipboard: Option<String>,
    pub search_focus: bool,
    rx: mpsc::Receiver<Snapshot>,
    clipboard_rx: mpsc::Receiver<String>,
    known_status: HashMap<String, String>,
}

impl GreedApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        theme::apply(&cc.egui_ctx);
        let config = Config::load();
        let client = Client::new("127.0.0.1", config.rpc_port, None);

        let (tx, rx) = mpsc::channel();
        let c = client.clone();
        let ctx = cc.egui_ctx.clone();
        std::thread::spawn(move || loop {
            std::thread::sleep(Duration::from_millis(800));
            let mut downloads = Vec::new();
            for result in [
                c.tell_active(),
                c.tell_waiting(0, 1000),
                c.tell_stopped(0, 1000),
            ] {
                if let Ok(value) = result {
                    if let Some(items) = value.as_array() {
                        downloads.extend(items.iter().map(Download::from_value));
                    }
                }
            }
            let stat = c.get_global_stat();
            let connected = stat.is_ok();
            let global = stat
                .map(|v| GlobalStat::from_value(&v))
                .unwrap_or_default();
            if tx.send(Snapshot {
                downloads,
                global,
                connected,
            })
            .is_err()
            {
                break;
            }
            ctx.request_repaint();
        });

        let (clip_tx, clip_rx) = mpsc::channel();
        let clip_ctx = cc.egui_ctx.clone();
        std::thread::spawn(move || {
            let mut clipboard = arboard::Clipboard::new().ok();
            let mut last = clipboard
                .as_mut()
                .and_then(|c| c.get_text().ok())
                .unwrap_or_default();
            loop {
                std::thread::sleep(Duration::from_millis(600));
                let Some(c) = clipboard.as_mut() else { break };
                let Ok(text) = c.get_text() else { continue };
                if text != last && looks_like_link(&text) {
                    last = text.clone();
                    if clip_tx.send(text).is_err() {
                        break;
                    }
                    clip_ctx.request_repaint();
                }
            }
        });

        let add_dir = config.download_dir.clone();
        let add_conn = config.max_concurrent.clamp(1, 16);
        let mut app = Self {
            client,
            config,
            downloads: Vec::new(),
            global: GlobalStat::default(),
            connected: false,
            filter: String::new(),
            tab: Tab::All,
            selected: None,
            show_add: false,
            show_settings: false,
            add_urls: String::new(),
            add_dir,
            add_name: String::new(),
            add_conn,
            status_msg: "Ready".to_string(),
            pending_clipboard: None,
            search_focus: false,
            rx,
            clipboard_rx: clip_rx,
            known_status: HashMap::new(),
        };
        if !app.client.probe() && app.config.auto_start_aria2 {
            app.start_engine();
        }
        app
    }

    fn drain(&mut self) {
        while let Ok(snapshot) = self.rx.try_recv() {
            self.downloads = snapshot.downloads;
            self.global = snapshot.global;
            self.connected = snapshot.connected;
            if let Some(gid) = &self.selected {
                if !self.downloads.iter().any(|d| &d.gid == gid) {
                    self.selected = None;
                }
            }
            self.notify_completed();
        }
        while let Ok(link) = self.clipboard_rx.try_recv() {
            if self.config.clipboard_monitor {
                self.pending_clipboard = Some(link);
            }
        }
    }

    fn notify_completed(&mut self) {
        if self.known_status.is_empty() {
            self.known_status = self
                .downloads
                .iter()
                .map(|d| (d.gid.clone(), d.status.clone()))
                .collect();
            return;
        }
        if self.config.notify_on_complete {
            for d in &self.downloads {
                let was = self.known_status.get(&d.gid);
                if d.status == "complete" && was.is_some_and(|s| s != "complete") {
                    let _ = std::process::Command::new("notify-send")
                        .arg("Greed — download complete")
                        .arg(&d.name)
                        .spawn();
                }
            }
        }
        self.known_status = self
            .downloads
            .iter()
            .map(|d| (d.gid.clone(), d.status.clone()))
            .collect();
    }

    fn handle_keys(&mut self, ctx: &egui::Context) {
        let typing = ctx.egui_wants_keyboard_input();
        let (del, space, ctrl_n, ctrl_f) = ctx.input(|i| {
            (
                !typing && i.key_pressed(egui::Key::Delete),
                !typing && i.key_pressed(egui::Key::Space),
                i.modifiers.command && i.key_pressed(egui::Key::N),
                i.modifiers.command && i.key_pressed(egui::Key::F),
            )
        });
        if ctrl_n {
            self.show_add = true;
        }
        if ctrl_f {
            self.search_focus = true;
        }
        if let Some(gid) = self.selected.clone() {
            if del {
                self.remove_gid(&gid);
            }
            if space {
                self.toggle_pause(&gid);
            }
        }
        if !self.show_add
            && !self.show_settings
            && ctx.input(|i| i.key_pressed(egui::Key::Escape))
        {
            self.selected = None;
            self.pending_clipboard = None;
        }
    }

    fn header(&mut self, root: &mut egui::Ui) {
        egui::Panel::top("header")
            .exact_size(54.0)
            .frame(
                Frame::new()
                    .fill(PANEL)
                    .inner_margin(Margin::symmetric(16, 0)),
            )
            .show(root, |ui| {
                ui.horizontal_centered(|ui| {
                    ui.label(RichText::new("Greed").size(19.0).strong().color(TEXT));
                    ui.add_space(14.0);
                    ui.label(
                        RichText::new(format!("↓ {}", fmt_speed(self.global.download_speed)))
                            .small()
                            .color(HIGHLIGHT),
                    );
                    ui.add_space(2.0);
                    ui.label(
                        RichText::new(format!("↑ {}", fmt_speed(self.global.upload_speed)))
                            .small()
                            .color(MUTED),
                    );
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        if ui.button("Settings").clicked() {
                            self.show_settings = true;
                        }
                        if ui.add(theme::primary_button("+  Add")).clicked() {
                            self.show_add = true;
                        }
                        ui.add_space(6.0);
                        ui.label(
                            RichText::new(format!(
                                "{} active · {} queued",
                                self.global.num_active, self.global.num_waiting
                            ))
                            .small()
                            .color(MUTED),
                        );
                    });
                });
            });
    }

    fn status_bar(&mut self, root: &mut egui::Ui) {
        egui::Panel::bottom("statusbar")
            .exact_size(28.0)
            .frame(
                Frame::new()
                    .fill(PANEL)
                    .inner_margin(Margin::symmetric(16, 0)),
            )
            .show(root, |ui| {
                ui.horizontal_centered(|ui| {
                    ui.label(RichText::new(&self.status_msg).small().color(MUTED));
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.label(
                            RichText::new(format!(
                                "↓ {}   ↑ {}",
                                fmt_speed(self.global.download_speed),
                                fmt_speed(self.global.upload_speed)
                            ))
                            .small()
                            .color(MUTED),
                        );
                        ui.add_space(10.0);
                        ui.label(
                            RichText::new(format!("{} downloads", self.downloads.len()))
                                .small()
                                .color(MUTED),
                        );
                        dot(ui, if self.connected { GOLD } else { ERROR });
                    });
                });
            });
    }

    fn sidebar(&mut self, root: &mut egui::Ui) {
        egui::Panel::left("sidebar")
            .exact_size(196.0)
            .resizable(false)
            .frame(
                Frame::new()
                    .fill(PANEL)
                    .inner_margin(Margin::symmetric(10, 12)),
            )
            .show(root, |ui| {
                ui.label(
                    RichText::new("LIBRARY")
                        .size(10.5)
                        .strong()
                        .color(MUTED),
                );
                ui.add_space(4.0);
                for tab in Tab::ALL {
                    let count = self.downloads.iter().filter(|d| tab.matches(d)).count();
                    if nav_row(ui, tab.label(), count, self.tab == tab).clicked() {
                        self.tab = tab;
                    }
                }
                ui.add_space(16.0);
                ui.separator();
                ui.add_space(10.0);
                ui.label(
                    RichText::new("TRANSFER")
                        .size(10.5)
                        .strong()
                        .color(MUTED),
                );
                ui.add_space(4.0);
                stat_row(ui, "Download", &fmt_speed(self.global.download_speed), HIGHLIGHT);
                stat_row(ui, "Upload", &fmt_speed(self.global.upload_speed), MUTED);
                stat_row(ui, "Active", &self.global.num_active.to_string(), TEXT);
                stat_row(ui, "Queued", &self.global.num_waiting.to_string(), TEXT);
                ui.with_layout(Layout::bottom_up(Align::LEFT), |ui| {
                    ui.horizontal(|ui| {
                        dot(ui, if self.connected { GOLD } else { ERROR });
                        ui.label(
                            RichText::new(if self.connected {
                                "aria2 connected"
                            } else {
                                "aria2 offline"
                            })
                            .small()
                            .color(MUTED),
                        );
                    });
                });
            });
    }

    fn details(&mut self, root: &mut egui::Ui) {
        let Some(d) = self.selected_download() else {
            return;
        };
        let mut action: Option<Action> = None;
        egui::Panel::right("details")
            .exact_size(300.0)
            .resizable(false)
            .frame(
                Frame::new()
                    .fill(PANEL)
                    .inner_margin(Margin::symmetric(14, 12)),
            )
            .show(root, |ui| {
                egui::ScrollArea::vertical()
                    .auto_shrink([false, false])
                    .show(ui, |ui| {
                        ui.horizontal(|ui| {
                            ui.label(
                                RichText::new("DETAILS")
                                    .size(10.5)
                                    .strong()
                                    .color(MUTED),
                            );
                            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                                if ui.small_button("×").on_hover_text("Close").clicked() {
                                    action = Some(Action::Deselect);
                                }
                            });
                        });
                        ui.add_space(6.0);
                        ui.add(
                            egui::Label::new(RichText::new(&d.name).size(15.0).strong().color(TEXT))
                                .wrap(),
                        );
                        ui.add_space(6.0);
                        ui.horizontal(|ui| {
                            Frame::new()
                                .fill(SECONDARY)
                                .corner_radius(10)
                                .inner_margin(Margin::symmetric(8, 2))
                                .show(ui, |ui| {
                                    ui.label(
                                        RichText::new(d.status_label())
                                            .small()
                                            .color(theme::status_color(&d.status)),
                                    );
                                });
                        });
                        ui.add_space(10.0);
                        theme::progress_bar(
                            ui,
                            d.progress(),
                            ui.available_width(),
                            6.0,
                            theme::status_color(&d.status),
                        );
                        ui.add_space(4.0);
                        ui.horizontal(|ui| {
                            ui.label(
                                RichText::new(format!("{:.1}%", d.progress() * 100.0))
                                    .small()
                                    .color(TEXT),
                            );
                            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                                ui.label(
                                    RichText::new(format!(
                                        "{} / {}",
                                        fmt_bytes(d.completed_length),
                                        fmt_bytes(d.total_length)
                                    ))
                                    .small()
                                    .color(MUTED),
                                );
                            });
                        });
                        ui.add_space(10.0);
                        kv(ui, "Speed", &fmt_speed(d.download_speed));
                        kv(ui, "Upload", &fmt_speed(d.upload_speed));
                        kv(
                            ui,
                            "ETA",
                            &d.eta_secs().map(fmt_eta).unwrap_or_else(|| "—".to_string()),
                        );
                        kv(ui, "Connections", &d.connections.to_string());
                        if d.is_torrent {
                            kv(ui, "Seeders", &d.num_seeders.to_string());
                        }
                        if d.file_count > 1 {
                            kv(ui, "Files", &d.file_count.to_string());
                        }
                        ui.add_space(8.0);
                        ui.label(RichText::new("Directory").small().color(MUTED));
                        ui.add(
                            egui::Label::new(
                                RichText::new(&d.dir).small().monospace().color(TEXT),
                            )
                            .truncate(),
                        )
                        .on_hover_text(&d.dir);
                        ui.add_space(4.0);
                        ui.label(RichText::new("GID").small().color(MUTED));
                        ui.label(
                            RichText::new(&d.gid)
                                .small()
                                .monospace()
                                .color(MUTED),
                        );
                        if !d.error_message.is_empty() {
                            ui.add_space(10.0);
                            Frame::new()
                                .fill(Color32::from_rgb(0x2a, 0x16, 0x19))
                                .stroke(Stroke::new(1.0, ERROR))
                                .corner_radius(6)
                                .inner_margin(Margin::same(8))
                                .show(ui, |ui| {
                                    ui.add(
                                        egui::Label::new(
                                            RichText::new(&d.error_message).small().color(ERROR),
                                        )
                                        .wrap(),
                                    );
                                });
                        }
                        ui.add_space(12.0);
                        ui.horizontal_wrapped(|ui| {
                            let toggle = if d.status == "paused" { "Resume" } else { "Pause" };
                            if ui.button(toggle).clicked() {
                                action = Some(Action::Toggle(d.gid.clone()));
                            }
                            if ui.button("Open folder").clicked() {
                                action = Some(Action::OpenFolder(d.dir.clone()));
                            }
                            if ui.button("Remove").clicked() {
                                action = Some(Action::Remove(d.gid.clone()));
                            }
                        });
                    });
            });
        if let Some(action) = action {
            self.apply(action);
        }
    }

    fn central(&mut self, ui: &mut egui::Ui) {
        if !self.connected {
            let mut start = false;
            Frame::new()
                .fill(RAISED)
                .stroke(Stroke::new(1.0, LINE))
                .corner_radius(8)
                .inner_margin(Margin::symmetric(12, 8))
                .show(ui, |ui| {
                    ui.horizontal(|ui| {
                        dot(ui, ERROR);
                        ui.label(RichText::new("aria2 is offline").strong().color(TEXT));
                        ui.label(
                            RichText::new("Start the engine to download.")
                                .small()
                                .color(MUTED),
                        );
                        if ui.button("Start aria2").clicked() {
                            start = true;
                        }
                    });
                });
            ui.add_space(10.0);
            if start {
                self.start_engine();
            }
        }

        let selected = self.selected_download();
        let can_pause = selected
            .as_ref()
            .is_some_and(|d| matches!(d.status.as_str(), "active" | "waiting"));
        let can_resume = selected.as_ref().is_some_and(|d| d.status == "paused");
        let mut action: Option<Action> = None;

        ui.horizontal(|ui| {
            let search = ui.add(
                egui::TextEdit::singleline(&mut self.filter)
                    .hint_text("Search downloads…")
                    .desired_width(280.0),
            );
            if self.search_focus {
                search.request_focus();
                self.search_focus = false;
            }
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                if ui
                    .add_enabled(selected.is_some(), egui::Button::new("Remove"))
                    .clicked()
                {
                    if let Some(d) = &selected {
                        action = Some(Action::Remove(d.gid.clone()));
                    }
                }
                if ui
                    .add_enabled(can_resume, egui::Button::new("Resume"))
                    .clicked()
                {
                    if let Some(d) = &selected {
                        action = Some(Action::Toggle(d.gid.clone()));
                    }
                }
                if ui
                    .add_enabled(can_pause, egui::Button::new("Pause"))
                    .clicked()
                {
                    if let Some(d) = &selected {
                        action = Some(Action::Toggle(d.gid.clone()));
                    }
                }
                ui.add_space(4.0);
                ui.label(
                    RichText::new(format!("{} shown", self.filtered().len()))
                        .small()
                        .color(MUTED),
                );
            });
        });

        if let Some(action) = action {
            self.apply(action);
        }

        ui.add_space(8.0);
        if self.filtered().is_empty() {
            self.empty_state(ui);
        } else {
            ui::list::table(self, ui);
        }
    }

    fn empty_state(&mut self, ui: &mut egui::Ui) {
        let message = if self.downloads.is_empty() {
            "No downloads yet"
        } else {
            "Nothing in this tab"
        };
        ui.add_space(64.0);
        ui.vertical_centered(|ui| {
            ui.label(RichText::new(message).size(18.0).strong().color(TEXT));
            ui.add_space(6.0);
            ui.label(
                RichText::new("Paste a link, or add a magnet / .torrent file.")
                    .color(MUTED),
            );
            ui.add_space(16.0);
            if ui.add(theme::primary_button("+  Add download")).clicked() {
                self.show_add = true;
            }
        });
    }

    #[allow(deprecated)]
    fn clipboard_toast(&mut self, ctx: &egui::Context) {
        let Some(url) = self.pending_clipboard.clone() else {
            return;
        };
        let mut add = false;
        let mut dismiss = false;
        egui::Area::new(egui::Id::new("clipboard_toast"))
            .anchor(egui::Align2::RIGHT_BOTTOM, egui::vec2(-18.0, -44.0))
            .order(egui::Order::Foreground)
            .show(ctx, |ui| {
                Frame::new()
                    .fill(RAISED)
                    .stroke(Stroke::new(1.0, LINE))
                    .corner_radius(10)
                    .inner_margin(Margin::same(12))
                    .show(ui, |ui| {
                        ui.set_max_width(380.0);
                        ui.label(RichText::new("Link copied").strong().color(HIGHLIGHT));
                        ui.add_space(2.0);
                        ui.add(
                            egui::Label::new(RichText::new(&url).small().color(MUTED)).truncate(),
                        );
                        ui.add_space(8.0);
                        ui.horizontal(|ui| {
                            if ui.add(theme::primary_button("Download")).clicked() {
                                add = true;
                            }
                            if ui.button("Dismiss").clicked() {
                                dismiss = true;
                            }
                        });
                    });
            });
        if add {
            let opts = serde_json::json!({ "dir": self.config.download_dir });
            match self.client.add_uri(&[url], &opts) {
                Ok(_) => self.status_msg = "Added from clipboard".to_string(),
                Err(e) => self.status_msg = format!("Error: {e}"),
            }
            self.pending_clipboard = None;
        } else if dismiss {
            self.pending_clipboard = None;
        }
    }

    pub fn selected_download(&self) -> Option<Download> {
        let gid = self.selected.as_ref()?;
        self.downloads.iter().find(|d| &d.gid == gid).cloned()
    }

    pub fn filtered(&self) -> Vec<Download> {
        let query = self.filter.trim().to_lowercase();
        self.downloads
            .iter()
            .filter(|d| self.tab.matches(d))
            .filter(|d| {
                query.is_empty()
                    || d.name.to_lowercase().contains(&query)
                    || d.url.to_lowercase().contains(&query)
                    || d.gid.starts_with(&query)
            })
            .cloned()
            .collect()
    }

    pub fn apply(&mut self, action: Action) {
        match action {
            Action::Select(gid) => self.selected = Some(gid),
            Action::Deselect => self.selected = None,
            Action::Toggle(gid) => self.toggle_pause(&gid),
            Action::Remove(gid) => self.remove_gid(&gid),
            Action::OpenFolder(dir) => match std::process::Command::new("xdg-open")
                .arg(&dir)
                .spawn()
            {
                Ok(_) => self.status_msg = format!("Opened {dir}"),
                Err(e) => self.status_msg = format!("Could not open folder: {e}"),
            },
        }
    }

    pub fn toggle_pause(&mut self, gid: &str) {
        let Some(d) = self.downloads.iter().find(|d| d.gid == gid).cloned() else {
            return;
        };
        let result = if d.status == "paused" {
            self.client.unpause(gid)
        } else {
            self.client.pause(gid)
        };
        match result {
            Ok(()) => {
                self.status_msg = if d.status == "paused" {
                    "Resumed".to_string()
                } else {
                    "Paused".to_string()
                };
            }
            Err(e) => self.status_msg = e,
        }
    }

    pub fn remove_gid(&mut self, gid: &str) {
        let finished = self
            .downloads
            .iter()
            .find(|d| d.gid == gid)
            .is_none_or(Download::is_finished);
        let result = if finished {
            self.client.remove_result(gid)
        } else {
            match self.client.force_remove(gid) {
                Ok(()) => {
                    // forceRemove leaves a "removed" result that aria2 saves into the session.
                    let _ = self.client.remove_result(gid);
                    Ok(())
                }
                Err(e) => Err(e),
            }
        };
        match result {
            Ok(()) => {
                self.status_msg = "Removed".to_string();
                if self.selected.as_deref() == Some(gid) {
                    self.selected = None;
                }
            }
            Err(e) => self.status_msg = e,
        }
    }

    pub fn add_links(&mut self, text: &str, dir: &str, out: Option<&str>, conn: u32) {
        let links: Vec<&str> = text
            .lines()
            .map(str::trim)
            .filter(|l| !l.is_empty())
            .collect();
        if links.is_empty() {
            self.status_msg = "Paste at least one link".to_string();
            return;
        }
        let mut added = 0;
        let mut error = None;
        for link in &links {
            let mut opts = serde_json::json!({ "dir": dir });
            if let Some(name) = out {
                if links.len() == 1 {
                    opts["out"] = serde_json::json!(name);
                }
            }
            if conn > 1 {
                opts["split"] = serde_json::json!(conn);
                opts["max-connection-per-server"] = serde_json::json!(conn);
            }
            let result = if link.ends_with(".torrent") && std::path::Path::new(link).is_file() {
                self.client.add_torrent(link, &opts)
            } else {
                self.client.add_uri(&[link.to_string()], &opts)
            };
            match result {
                Ok(_) => added += 1,
                Err(e) => {
                    error = Some(e);
                    break;
                }
            }
        }
        match error {
            Some(e) => self.status_msg = format!("Added {added}, error: {e}"),
            None => {
                self.status_msg = format!(
                    "Added {added} download{}",
                    if added == 1 { "" } else { "s" }
                );
                self.add_urls.clear();
                self.add_name.clear();
                self.show_add = false;
            }
        }
    }

    pub fn add_torrent_file(&mut self, path: &str) {
        let opts = serde_json::json!({ "dir": self.config.download_dir });
        match self.client.add_torrent(path, &opts) {
            Ok(_) => {
                self.status_msg = "Torrent added".to_string();
                self.show_add = false;
            }
            Err(e) => self.status_msg = format!("Torrent error: {e}"),
        }
    }

    pub fn save_settings(&mut self) {
        if let Err(e) = self.config.save() {
            self.status_msg = e;
            return;
        }
        let _ = self.client.change_global_option(&serde_json::json!({
            "max-concurrent-downloads": self.config.max_concurrent.to_string()
        }));
        self.status_msg = "Settings saved".to_string();
    }

    pub fn start_engine(&mut self) -> bool {
        if self.client.probe() {
            self.connected = true;
            return true;
        }
        if !aria2::aria2_available() {
            self.status_msg = "aria2 binary not found".to_string();
            return false;
        }
        match aria2::spawn(
            self.config.rpc_port,
            None,
            &self.config.download_dir,
            self.config.max_concurrent,
        ) {
            Ok(_) => {
                for _ in 0..25 {
                    std::thread::sleep(Duration::from_millis(100));
                    if self.client.probe() {
                        self.connected = true;
                        self.status_msg = "aria2 started".to_string();
                        return true;
                    }
                }
                self.status_msg = "aria2 failed to start".to_string();
                false
            }
            Err(e) => {
                self.status_msg = format!("Failed to start aria2: {e}");
                false
            }
        }
    }
}

impl eframe::App for GreedApp {
    fn ui(&mut self, root: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = root.ctx().clone();
        self.drain();
        self.handle_keys(&ctx);

        self.header(root);
        self.status_bar(root);
        self.sidebar(root);
        self.details(root);
        egui::CentralPanel::default()
            .frame(
                Frame::new()
                    .fill(BG)
                    .inner_margin(Margin::symmetric(14, 12)),
            )
            .show(root, |ui| self.central(ui));

        ui::add_dialog::show(self, &ctx);
        ui::settings::show(self, &ctx);
        self.clipboard_toast(&ctx);
    }
}

fn looks_like_link(s: &str) -> bool {
    let s = s.trim();
    s.starts_with("http://")
        || s.starts_with("https://")
        || s.starts_with("ftp://")
        || s.starts_with("magnet:")
}

fn dot(ui: &mut egui::Ui, color: Color32) {
    let (rect, _) = ui.allocate_exact_size(egui::vec2(8.0, 8.0), egui::Sense::hover());
    ui.painter().circle_filled(rect.center(), 3.5, color);
}

fn nav_row(ui: &mut egui::Ui, label: &str, count: usize, selected: bool) -> egui::Response {
    let (rect, response) = ui.allocate_exact_size(
        egui::vec2(ui.available_width(), 30.0),
        egui::Sense::click(),
    );
    let background = if selected {
        SECONDARY
    } else if response.hovered() {
        RAISED
    } else {
        Color32::TRANSPARENT
    };
    if background != Color32::TRANSPARENT {
        ui.painter()
            .rect_filled(rect, egui::CornerRadius::same(6), background);
    }
    let fg = if selected { TEXT } else { MUTED };
    ui.painter().text(
        egui::pos2(rect.left() + 10.0, rect.center().y),
        Align2::LEFT_CENTER,
        label,
        FontId::proportional(13.5),
        fg,
    );
    ui.painter().text(
        egui::pos2(rect.right() - 10.0, rect.center().y),
        Align2::RIGHT_CENTER,
        count.to_string(),
        FontId::proportional(12.0),
        if selected { HIGHLIGHT } else { MUTED },
    );
    if response.hovered() {
        ui.ctx().set_cursor_icon(egui::CursorIcon::PointingHand);
    }
    response
}

fn stat_row(ui: &mut egui::Ui, key: &str, value: &str, color: Color32) {
    ui.horizontal(|ui| {
        ui.label(RichText::new(key).small().color(MUTED));
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            ui.label(RichText::new(value).small().color(color));
        });
    });
}

fn kv(ui: &mut egui::Ui, key: &str, value: &str) {
    ui.horizontal(|ui| {
        ui.label(RichText::new(key).small().color(MUTED));
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            ui.add(egui::Label::new(RichText::new(value).small().color(TEXT)).truncate());
        });
    });
}
