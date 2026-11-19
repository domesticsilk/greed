//! Witch's Ink — dark Echidna theme.

use eframe::egui::style::WidgetVisuals;
use eframe::egui::{
    self, Color32, CornerRadius, FontId, Stroke, TextStyle, Theme, Visuals,
};

// Ink-black surfaces
pub const BG: Color32 = Color32::from_rgb(0x0e, 0x0c, 0x11);
pub const PANEL: Color32 = Color32::from_rgb(0x15, 0x12, 0x19);
pub const RAISED: Color32 = Color32::from_rgb(0x1d, 0x19, 0x22);
pub const LINE: Color32 = Color32::from_rgb(0x28, 0x23, 0x2e);

// Text
pub const TEXT: Color32 = Color32::from_rgb(0xe9, 0xe4, 0xe3);
pub const MUTED: Color32 = Color32::from_rgb(0x9e, 0x97, 0xa4);

// Witch's Ink accents
pub const ACCENT: Color32 = Color32::from_rgb(0x95, 0x84, 0x9b);
pub const SECONDARY: Color32 = Color32::from_rgb(0x59, 0x44, 0x5f);
pub const HIGHLIGHT: Color32 = Color32::from_rgb(0xcb, 0xae, 0xb9);
pub const GOLD: Color32 = Color32::from_rgb(0xb4, 0x9a, 0x70);
pub const ERROR: Color32 = Color32::from_rgb(0xf8, 0x71, 0x71);

pub fn apply(ctx: &egui::Context) {
    ctx.set_theme(Theme::Dark);

    let mut v = Visuals::dark();
    v.panel_fill = BG;
    v.window_fill = PANEL;
    v.window_stroke = Stroke::new(1.0, LINE);
    v.window_corner_radius = CornerRadius::same(10);
    v.menu_corner_radius = CornerRadius::same(8);
    v.faint_bg_color = RAISED;
    v.extreme_bg_color = Color32::from_rgb(0x12, 0x10, 0x17);
    v.text_edit_bg_color = Some(Color32::from_rgb(0x10, 0x0e, 0x13));
    v.code_bg_color = RAISED;
    v.hyperlink_color = HIGHLIGHT;
    v.warn_fg_color = GOLD;
    v.error_fg_color = ERROR;
    v.striped = true;
    v.selection.bg_fill = SECONDARY;
    v.selection.stroke = Stroke::new(1.0, HIGHLIGHT);

    let widget = |bg: Color32, fg: Color32, border: Stroke| WidgetVisuals {
        bg_fill: bg,
        weak_bg_fill: bg,
        bg_stroke: border,
        corner_radius: CornerRadius::same(6),
        fg_stroke: Stroke::new(1.0, fg),
        expansion: 0.0,
    };
    v.widgets.noninteractive = widget(PANEL, TEXT, Stroke::new(1.0, LINE));
    v.widgets.inactive = widget(RAISED, TEXT, Stroke::NONE);
    v.widgets.hovered = widget(SECONDARY, TEXT, Stroke::new(1.0, ACCENT));
    v.widgets.active = widget(ACCENT, Color32::WHITE, Stroke::NONE);
    v.widgets.open = widget(
        Color32::from_rgb(0x23, 0x1c, 0x28),
        TEXT,
        Stroke::new(1.0, LINE),
    );

    ctx.set_visuals_of(Theme::Dark, v);

    ctx.all_styles_mut(|style| {
        style.spacing.item_spacing = egui::vec2(8.0, 6.0);
        style.spacing.button_padding = egui::vec2(10.0, 5.0);
        style.spacing.interact_size = egui::vec2(36.0, 26.0);
        style.spacing.window_margin = egui::Margin::same(14);
        style.spacing.menu_margin = egui::Margin::same(6);
        style.text_styles = [
            (TextStyle::Small, FontId::proportional(11.5)),
            (TextStyle::Body, FontId::proportional(13.5)),
            (TextStyle::Button, FontId::proportional(13.0)),
            (TextStyle::Heading, FontId::proportional(19.0)),
            (TextStyle::Monospace, FontId::monospace(12.0)),
        ]
        .into();
    });
}

pub fn status_color(status: &str) -> Color32 {
    match status {
        "active" => HIGHLIGHT,
        "paused" => GOLD,
        "complete" => GOLD,
        "error" => ERROR,
        _ => MUTED,
    }
}

/// Progress bar without the minimum-width "dot" egui draws at zero progress.
pub fn progress_bar(ui: &mut egui::Ui, progress: f32, width: f32, height: f32, color: Color32) {
    if progress <= 0.0 {
        let (rect, _) = ui.allocate_exact_size(egui::vec2(width, height), egui::Sense::hover());
        ui.painter()
            .rect_filled(rect, CornerRadius::same(3), ui.visuals().extreme_bg_color);
    } else {
        ui.add(
            egui::ProgressBar::new(progress)
                .desired_width(width)
                .desired_height(height)
                .fill(color)
                .corner_radius(CornerRadius::same(3)),
        );
    }
}

pub fn primary_button(text: &str) -> egui::Button<'static> {
    egui::Button::new(egui::RichText::new(text).color(BG).strong())
        .fill(GOLD)
        .corner_radius(CornerRadius::same(6))
}
