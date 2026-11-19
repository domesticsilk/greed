//! Add-download dialog.

use crate::app::GreedApp;
use crate::theme::{self, HIGHLIGHT, MUTED};
use eframe::egui::{self, Align, Layout, RichText};

pub fn show(app: &mut GreedApp, ctx: &egui::Context) {
    if !app.show_add {
        return;
    }
    let mut open = true;
    let mut cancel = false;
    let mut add = false;
    let mut torrent: Option<String> = None;

    egui::Window::new("Add download")
        .open(&mut open)
        .collapsible(false)
        .resizable(false)
        .default_width(540.0)
        .show(ctx, |ui| {
            ui.label(RichText::new("LINKS").size(10.5).strong().color(MUTED));
            ui.add(
                egui::TextEdit::multiline(&mut app.add_urls)
                    .desired_rows(4)
                    .desired_width(f32::INFINITY)
                    .hint_text(
                        "https://example.com/file.zip\nmagnet:?xt=urn:btih:…\nOne link per line",
                    ),
            );
            ui.add_space(10.0);

            ui.label(RichText::new("SAVE TO").size(10.5).strong().color(MUTED));
            ui.horizontal(|ui| {
                let width = (ui.available_width() - 92.0).max(120.0);
                ui.add(egui::TextEdit::singleline(&mut app.add_dir).desired_width(width));
                if ui.button("Browse…").clicked() {
                    if let Some(path) = rfd::FileDialog::new()
                        .set_directory(app.add_dir.as_str())
                        .pick_folder()
                    {
                        app.add_dir = path.to_string_lossy().to_string();
                    }
                }
            });
            ui.add_space(8.0);

            ui.horizontal(|ui| {
                ui.label(RichText::new("Filename").small().color(MUTED));
                ui.add(
                    egui::TextEdit::singleline(&mut app.add_name)
                        .hint_text("optional, single link only")
                        .desired_width(240.0),
                );
            });
            ui.add_space(4.0);
            ui.horizontal(|ui| {
                ui.label(RichText::new("Connections").small().color(MUTED));
                ui.scope(|ui| {
                    ui.spacing_mut().slider_width = 160.0;
                    ui.visuals_mut().slider_trailing_fill = true;
                    ui.visuals_mut().widgets.inactive.fg_stroke.color = HIGHLIGHT;
                    ui.visuals_mut().widgets.hovered.fg_stroke.color = HIGHLIGHT;
                    ui.visuals_mut().widgets.active.fg_stroke.color = HIGHLIGHT;
                    ui.add(egui::Slider::new(&mut app.add_conn, 1..=16));
                });
            });
            ui.add_space(12.0);
            ui.separator();
            ui.add_space(8.0);

            ui.horizontal(|ui| {
                if ui.button("Open .torrent…").clicked() {
                    if let Some(path) = rfd::FileDialog::new()
                        .add_filter("Torrent", &["torrent"])
                        .pick_file()
                    {
                        torrent = Some(path.to_string_lossy().to_string());
                    }
                }
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if ui.add(theme::primary_button("Add")).clicked() {
                        add = true;
                    }
                    if ui.button("Cancel").clicked() {
                        cancel = true;
                    }
                });
            });
        });

    if cancel {
        open = false;
    }
    if let Some(path) = torrent {
        app.add_torrent_file(&path);
        open = app.show_add;
    }
    if add {
        let name = app.add_name.trim().to_string();
        let out = if name.is_empty() { None } else { Some(name) };
        let urls = app.add_urls.clone();
        let dir = app.add_dir.clone();
        let conn = app.add_conn;
        app.add_links(&urls, &dir, out.as_deref(), conn);
        open = app.show_add;
    }
    app.show_add = open;
}
