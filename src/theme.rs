//! Echidna theme.

use eframe::egui::{Color32, Visuals};

pub const BG: Color32 = Color32::from_rgb(0x0f, 0x0a, 0x1a);
pub const PANEL: Color32 = Color32::from_rgb(0x1a, 0x13, 0x28);
pub const BORDER: Color32 = Color32::from_rgb(0x3b, 0x2a, 0x5a);
pub const ACCENT: Color32 = Color32::from_rgb(0xa8, 0x55, 0xf7);
pub const GOLD: Color32 = Color32::from_rgb(0xfb, 0xbf, 0x24);
pub const TEXT: Color32 = Color32::from_rgb(0xf3, 0xe8, 0xff);
pub const MUTED: Color32 = Color32::from_rgb(0xa7, 0x8b, 0xfa);
pub const SUCCESS: Color32 = Color32::from_rgb(0xfb, 0xbf, 0x24);
pub const ERROR: Color32 = Color32::from_rgb(0xf8, 0x71, 0x71);

pub fn apply(ctx: &eframe::egui::Context) {
    let mut v = Visuals::dark();
    v.panel_fill = PANEL;
    v.window_fill = PANEL;
    v.faint_bg_color = BG;
    v.extreme_bg_color = BG;
    v.code_bg_color = BG;
    v.widgets.noninteractive.bg_fill = PANEL;
    v.widgets.noninteractive.fg_stroke.color = TEXT;
    v.widgets.inactive.bg_fill = PANEL;
    v.widgets.inactive.fg_stroke.color = TEXT;
    v.widgets.hovered.bg_fill = BORDER;
    v.widgets.hovered.fg_stroke.color = TEXT;
    v.widgets.active.bg_fill = ACCENT;
    v.widgets.active.fg_stroke.color = Color32::WHITE;
    v.selection.bg_fill = ACCENT;
    v.selection.stroke.color = TEXT;
    v.hyperlink_color = ACCENT;
    v.warn_fg_color = Color32::from_rgb(0xfb, 0xbf, 0x24);
    v.error_fg_color = ERROR;
    ctx.set_visuals(v);

    let mut fonts = eframe::egui::FontDefinitions::default();
    // Use default fonts; no custom embed to keep binary small.
    ctx.set_fonts(fonts);
}

pub fn progress_gradient() -> (Color32, Color32) {
    (ACCENT, GOLD)
}
