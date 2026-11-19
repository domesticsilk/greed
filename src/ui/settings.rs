//! Settings dialog.

use crate::app::GreedApp;
use crate::theme::{self, HIGHLIGHT, MUTED};
use eframe::egui::{self, Align, Layout, RichText};

pub fn show(app: &mut GreedApp, ctx: &egui::Context) {
    if !app.show_settings {
        return;
    }
    let mut open = true;
    let mut cancel = false;
    let mut save = false;

    egui::Window::new("Settings")
        .open(&mut open)
        .collapsible(false)
        .resizable(false)
        .default_width(430.0)
        .show(ctx, |ui| {
            ui.label(RichText::new("SAVE TO").size(10.5).strong().color(MUTED));
            ui.horizontal(|ui| {
                let width = (ui.available_width() - 92.0).max(120.0);
                ui.add(egui::TextEdit::singleline(&mut app.config.download_dir).desired_width(width));
                if ui.button("Browse…").clicked() {
                    if let Some(path) = rfd::FileDialog::new()
                        .set_directory(app.config.download_dir.as_str())
                        .pick_folder()
                    {
                        app.config.download_dir = path.to_string_lossy().to_string();
                    }
                }
            });
            ui.add_space(10.0);

            ui.label(RichText::new("MAX CONCURRENT").size(10.5).strong().color(MUTED));
            ui.scope(|ui| {
                ui.spacing_mut().slider_width = 180.0;
                ui.visuals_mut().slider_trailing_fill = true;
                ui.visuals_mut().widgets.inactive.fg_stroke.color = HIGHLIGHT;
                ui.visuals_mut().widgets.hovered.fg_stroke.color = HIGHLIGHT;
                ui.visuals_mut().widgets.active.fg_stroke.color = HIGHLIGHT;
                ui.add(egui::Slider::new(&mut app.config.max_concurrent, 1..=16));
            });
            ui.add_space(4.0);

            ui.label(RichText::new("RPC PORT").size(10.5).strong().color(MUTED));
            ui.add(egui::DragValue::new(&mut app.config.rpc_port).range(1024..=65535));
            ui.label(
                RichText::new("Port changes apply after restarting Greed.")
                    .small()
                    .color(MUTED),
            );
            ui.add_space(8.0);

            ui.checkbox(&mut app.config.auto_start_aria2, "Start aria2 automatically");
            ui.checkbox(
                &mut app.config.clipboard_monitor,
                "Watch clipboard for links",
            );
            ui.checkbox(
                &mut app.config.notify_on_complete,
                "Notify when a download completes",
            );

            ui.add_space(10.0);
            ui.separator();
            ui.add_space(6.0);
            ui.horizontal(|ui| {
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if ui.add(theme::primary_button("Save")).clicked() {
                        save = true;
                    }
                    if ui.button("Close").clicked() {
                        cancel = true;
                    }
                });
            });
        });

    if cancel {
        open = false;
    }
    if save {
        app.save_settings();
        open = false;
    }
    app.show_settings = open;
}
