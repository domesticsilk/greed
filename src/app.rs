//! GUI application.

use crate::aria2::{self, Client};
use crate::config::Config;
use crate::model::{Download, GlobalStat};
use crate::theme;
use eframe::egui;
use std::sync::mpsc;
use std::time::Duration;

pub struct GreedApp {
    pub client: Client,
    pub config: Config,
    pub downloads: Vec<Download>,
    pub global: GlobalStat,
    pub filter: String,
    pub show_settings: bool,
    pub show_add: bool,
    pub add_url: String,
    pub status_msg: String,
    pub rx: mpsc::Receiver<(Vec<Download>, GlobalStat)>,
    pub clipboard_rx: mpsc::Receiver<String>,
    pub pending_clipboard: Option<String>,
}

impl GreedApp {
    pub fn new(_cc: &eframe::CreationContext<'_>) -> Self {
        theme::apply(& _cc.egui_ctx);
        let config = Config::load();
        let client = Client::new("127.0.0.1", config.rpc_port, None);

        // Spawn aria2 if needed
        if !client.probe() && config.auto_start_aria2 {
            if aria2::aria2_available() {
                let _ = aria2::spawn(config.rpc_port, None, false);
                // wait briefly
                for _ in 0..20 {
                    std::thread::sleep(Duration::from_millis(100));
                    if client.probe() {
                        break;
                    }
                }
            }
        }

        let (tx, rx) = mpsc::channel();
        let c = client.clone();
        std::thread::spawn(move || loop {
            std::thread::sleep(Duration::from_secs(1));
            let mut list = Vec::new();
            if let Ok(v) = c.tell_active() {
                if let Some(arr) = v.as_array() {
                    for item in arr {
                        list.push(Download::from_value(item));
                    }
                }
            }
            if let Ok(v) = c.tell_waiting(0, 100) {
                if let Some(arr) = v.as_array() {
                    for item in arr {
                        list.push(Download::from_value(item));
                    }
                }
            }
            if let Ok(v) = c.tell_stopped(0, 100) {
                if let Some(arr) = v.as_array() {
                    for item in arr {
                        list.push(Download::from_value(item));
                    }
                }
            }
            let global = c
                .get_global_stat()
                .map(|v| GlobalStat::from_value(&v))
                .unwrap_or_default();
            let _ = tx.send((list, global));
        });

        // Clipboard monitor
        let (cb_tx, cb_rx) = mpsc::channel();
        if config.clipboard_monitor {
            std::thread::spawn(move || {
                let mut clip = arboard::Clipboard::new().ok();
                let mut last = String::new();
                loop {
                    std::thread::sleep(Duration::from_millis(500));
                    if let Some(c) = clip.as_mut() {
                        if let Ok(text) = c.get_text() {
                            if text != last && is_url(&text) {
                                last = text.clone();
                                let _ = cb_tx.send(text);
                            }
                        }
                    }
                }
            });
        }

        Self {
            client,
            config,
            downloads: Vec::new(),
            global: GlobalStat::default(),
            filter: String::new(),
            show_settings: false,
            show_add: false,
            add_url: String::new(),
            status_msg: String::new(),
            rx,
            clipboard_rx: cb_rx,
            pending_clipboard: None,
        }
    }
}

fn is_url(s: &str) -> bool {
    let s = s.trim();
    s.starts_with("http://")
        || s.starts_with("https://")
        || s.starts_with("ftp://")
        || s.starts_with("magnet:")
        || s.ends_with(".torrent")
}

impl eframe::App for GreedApp {
    fn ui(&mut self, _root: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = _root.ctx().clone();
        // drain poll
        while let Ok((list, g)) = self.rx.try_recv() {
            self.downloads = list;
            self.global = g;
        }
        // drain clipboard
        while let Ok(u) = self.clipboard_rx.try_recv() {
            self.pending_clipboard = Some(u);
        }

        // Top bar
        egui::Panel::top("top").show(_root, |ui| {
            ui.horizontal(|ui| {
                ui.heading("Greed");
                ui.label(format!(
                    "↓ {}  ↑ {}",
                    fmt_speed(self.global.download_speed),
                    fmt_speed(self.global.upload_speed)
                ));
                ui.label(format!(
                    "active {}  waiting {}",
                    self.global.num_active, self.global.num_waiting
                ));
                if ui.button("Add").clicked() {
                    self.show_add = true;
                }
                if ui.button("Pause All").clicked() {
                    for d in &self.downloads {
                        let _ = self.client.pause(&d.gid);
                    }
                }
                if ui.button("Resume All").clicked() {
                    for d in &self.downloads {
                        let _ = self.client.unpause(&d.gid);
                    }
                }
                if ui.button("Settings").clicked() {
                    self.show_settings = true;
                }
            });
        });

        // Filter bar
        egui::Panel::top("filter").show(_root, |ui| {
            ui.horizontal(|ui| {
                ui.label("Filter:");
                ui.text_edit_singleline(&mut self.filter);
            });
        });

        // Bottom status
        egui::Panel::bottom("status").show(_root, |ui| {
            ui.horizontal(|ui| {
                ui.label(&self.status_msg);
                ui.label(format!("{} downloads", self.downloads.len()));
            });
        });

        // Central list
        egui::CentralPanel::default().show(_root, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                for d in &self.downloads {
                    if !self.filter.is_empty()
                        && !d
                            .files
                            .first()
                            .cloned()
                            .unwrap_or_default()
                            .to_lowercase()
                            .contains(&self.filter.to_lowercase())
                    {
                        continue;
                    }
                    ui.group(|ui| {
                        ui.horizontal(|ui| {
                            let name = d.files.first().cloned().unwrap_or_else(|| d.gid.clone());
                            ui.label(&name);
                            ui.label(format!("{:.1}%", d.progress() * 100.0));
                            ui.label(format!(
                                "{}/{}",
                                fmt_bytes(d.completed_length),
                                fmt_bytes(d.total_length)
                            ));
                            ui.label(fmt_speed(d.download_speed));
                            if d.download_speed > 0 && d.total_length > d.completed_length {
                                let eta = (d.total_length - d.completed_length) / d.download_speed.max(1);
                                ui.label(format!("ETA {}s", eta));
                            }
                            if ui.small_button("Pause").clicked() {
                                let _ = self.client.pause(&d.gid);
                            }
                            if ui.small_button("Resume").clicked() {
                                let _ = self.client.unpause(&d.gid);
                            }
                            if ui.small_button("Cancel").clicked() {
                                let _ = self.client.force_remove(&d.gid);
                            }
                            if ui.small_button("Remove").clicked() {
                                let _ = self.client.remove(&d.gid);
                            }
                        });
                        let (a, b) = theme::progress_gradient();
                        let _ = (a, b);
                        ui.add(egui::ProgressBar::new(d.progress()).show_percentage());
                    });
                }
            });
        });

        // Add dialog
        if self.show_add {
            egui::Window::new("Add download").collapsible(false).show(&ctx, |ui| {
                    ui.label("URL:");
                    ui.text_edit_singleline(&mut self.add_url);
                    ui.horizontal(|ui| {
                        if ui.button("Add").clicked() {
                            let opts = serde_json::json!({"dir": self.config.download_dir});
                            let res = if self.add_url.ends_with(".torrent") {
                                self.client.add_torrent(&self.add_url, &opts)
                            } else {
                                self.client.add_uri(&[self.add_url.clone()], &opts)
                            };
                            match res {
                                Ok(gid) => self.status_msg = format!("Added {}", gid),
                                Err(e) => self.status_msg = format!("Error: {}", e),
                            }
                            self.show_add = false;
                            self.add_url.clear();
                        }
                        if ui.button("Cancel").clicked() {
                            self.show_add = false;
                        }
                        if ui.button("Open .torrent").clicked() {
                            if let Some(path) = rfd::FileDialog::new()
                                .add_filter("Torrent", &["torrent"])
                                .pick_file()
                            {
                                let opts = serde_json::json!({"dir": self.config.download_dir});
                                match self.client.add_torrent(&path.to_string_lossy(), &opts) {
                                    Ok(gid) => self.status_msg = format!("Added {}", gid),
                                    Err(e) => self.status_msg = format!("Error: {}", e),
                                }
                                self.show_add = false;
                            }
                        }
                    });
                });
        }

        // Settings dialog
        if self.show_settings {
            egui::Window::new("Settings").show(&ctx, |ui| {
                ui.label("Download dir:");
                ui.text_edit_singleline(&mut self.config.download_dir);
                ui.label("Max concurrent:");
                ui.add(egui::Slider::new(&mut self.config.max_concurrent, 1..=16));
                ui.label("RPC port:");
                ui.add(egui::DragValue::new(&mut self.config.rpc_port));
                ui.checkbox(&mut self.config.clipboard_monitor, "Clipboard monitor");
                ui.checkbox(&mut self.config.notify_on_complete, "Notify on complete");
                if ui.button("Save").clicked() {
                    let _ = self.config.save();
                    self.show_settings = false;
                }
                if ui.button("Close").clicked() {
                    self.show_settings = false;
                }
            });
        }

        // Clipboard popup
        if let Some(u) = self.pending_clipboard.clone() {
            egui::Window::new("Download this?").collapsible(false).show(&ctx, |ui| {
                    ui.label(&u);
                    if ui.button("Yes").clicked() {
                        let opts = serde_json::json!({"dir": self.config.download_dir});
                        let _ = self.client.add_uri(&[u.clone()], &opts);
                        self.pending_clipboard = None;
                    }
                    if ui.button("No").clicked() {
                        self.pending_clipboard = None;
                    }
                });
        }

        ctx.request_repaint_after(Duration::from_secs(1));
    }
}

fn fmt_speed(b: u64) -> String {
    if b >= 1_073_741_824 {
        format!("{:.2} GB/s", b as f64 / 1_073_741_824.0)
    } else if b >= 1_048_576 {
        format!("{:.2} MB/s", b as f64 / 1_048_576.0)
    } else if b >= 1024 {
        format!("{:.2} KB/s", b as f64 / 1024.0)
    } else {
        format!("{} B/s", b)
    }
}

fn fmt_bytes(b: u64) -> String {
    if b >= 1_073_741_824 {
        format!("{:.2} GB", b as f64 / 1_073_741_824.0)
    } else if b >= 1_048_576 {
        format!("{:.2} MB", b as f64 / 1_048_576.0)
    } else if b >= 1024 {
        format!("{:.2} KB", b as f64 / 1024.0)
    } else {
        format!("{} B", b)
    }
}
