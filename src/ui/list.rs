//! Download table.

use crate::app::{Action, GreedApp};
use crate::model::{fmt_bytes, fmt_eta, fmt_speed, Download};
use crate::theme::{self, *};
use eframe::egui::{self, Align, Color32, Layout, RichText};
use egui_extras::{Column, TableBuilder};

pub fn table(app: &mut GreedApp, ui: &mut egui::Ui) {
    let rows = app.filtered();
    let selected = app.selected.clone();
    let mut action: Option<Action> = None;
    let height = ui.available_height();
    let width = ui.available_width();
    let show_eta = width >= 700.0;
    let show_speed = width >= 520.0;

    let mut table = TableBuilder::new(ui)
        .striped(true)
        .sense(egui::Sense::click())
        .cell_layout(Layout::left_to_right(Align::Center))
        .column(Column::remainder().at_least(120.0).clip(true))
        .column(Column::initial(85.0).at_least(55.0))
        .column(Column::initial(170.0).at_least(100.0));
    if show_speed {
        table = table.column(Column::initial(90.0).at_least(70.0));
    }
    if show_eta {
        table = table.column(Column::initial(75.0).at_least(55.0));
    }
    table = table.column(Column::initial(105.0).at_least(70.0));

    let mut headers = vec!["NAME", "SIZE", "PROGRESS"];
    if show_speed {
        headers.push("SPEED");
    }
    if show_eta {
        headers.push("ETA");
    }
    headers.push("STATUS");

    table
        .min_scrolled_height(0.0)
        .max_scroll_height(height)
        .auto_shrink([false, false])
        .header(26.0, |mut header| {
            for label in headers {
                header.col(|ui| {
                    ui.label(RichText::new(label).size(10.5).strong().color(MUTED));
                });
            }
        })
        .body(|body| {
            body.rows(40.0, rows.len(), |mut row| {
                let d = &rows[row.index()];
                row.set_selected(selected.as_deref() == Some(d.gid.as_str()));
                row.col(|ui| name_cell(ui, d));
                row.col(|ui| size_cell(ui, d));
                row.col(|ui| progress_cell(ui, d));
                if show_speed {
                    row.col(|ui| speed_cell(ui, d));
                }
                if show_eta {
                    row.col(|ui| eta_cell(ui, d));
                }
                row.col(|ui| status_cell(ui, d));

                let response = row.response();
                if response.clicked() {
                    action = Some(Action::Select(d.gid.clone()));
                }
                if response.double_clicked() {
                    action = Some(Action::OpenFolder(d.dir.clone()));
                }
                response.context_menu(|ui| {
                    let toggle = if d.status == "paused" {
                        "Resume"
                    } else {
                        "Pause"
                    };
                    if ui.button(toggle).clicked() {
                        action = Some(Action::Toggle(d.gid.clone()));
                        ui.close();
                    }
                    if ui.button("Open folder").clicked() {
                        action = Some(Action::OpenFolder(d.dir.clone()));
                        ui.close();
                    }
                    if ui.button("Remove").clicked() {
                        action = Some(Action::Remove(d.gid.clone()));
                        ui.close();
                    }
                });
            });
        });

    if let Some(action) = action {
        app.apply(action);
    }
}

fn name_cell(ui: &mut egui::Ui, d: &Download) {
    ui.horizontal(|ui| {
        dot(ui, theme::status_color(&d.status));
        ui.add(egui::Label::new(RichText::new(&d.name).size(13.0).color(TEXT)).truncate())
            .on_hover_text(&d.name);
    });
}

fn size_cell(ui: &mut egui::Ui, d: &Download) {
    let text = if d.total_length == 0 {
        "—".to_string()
    } else {
        fmt_bytes(d.total_length)
    };
    ui.label(RichText::new(text).small().color(MUTED));
}

fn progress_cell(ui: &mut egui::Ui, d: &Download) {
    ui.horizontal(|ui| {
        let width = (ui.available_width() - 42.0).max(50.0);
        theme::progress_bar(
            ui,
            d.progress(),
            width,
            6.0,
            theme::status_color(&d.status),
        );
        ui.label(
            RichText::new(format!("{:.0}%", d.progress() * 100.0))
                .size(11.0)
                .color(MUTED),
        );
    });
}

fn speed_cell(ui: &mut egui::Ui, d: &Download) {
    let text = if d.status == "active" {
        fmt_speed(d.download_speed)
    } else {
        "—".to_string()
    };
    ui.label(RichText::new(text).small().color(MUTED));
}

fn eta_cell(ui: &mut egui::Ui, d: &Download) {
    let text = d
        .eta_secs()
        .map(fmt_eta)
        .unwrap_or_else(|| "—".to_string());
    ui.label(RichText::new(text).small().color(MUTED));
}

fn status_cell(ui: &mut egui::Ui, d: &Download) {
    let label = ui.label(
        RichText::new(d.status_label())
            .small()
            .color(theme::status_color(&d.status)),
    );
    if !d.error_message.is_empty() {
        label.on_hover_text(&d.error_message);
    }
}

fn dot(ui: &mut egui::Ui, color: Color32) {
    let (rect, _) = ui.allocate_exact_size(egui::vec2(8.0, 8.0), egui::Sense::hover());
    ui.painter().circle_filled(rect.center(), 3.5, color);
}
