//! Echidna theme.

use eframe::egui::{Color32, Visuals};

// Witch's Ink palette
pub const BG: Color32 = Color32::from_rgb(0x17, 0x15, 0x1c);
pub const PANEL: Color32 = Color32::from_rgb(0x25, 0x21, 0x2b);
pub const BORDER: Color32 = Color32::from_rgb(0xb9, 0xb2, 0xbc);
pub const ACCENT: Color32 = Color32::from_rgb(0x95, 0x84, 0x9b);
pub const SECONDARY: Color32 = Color32::from_rgb(0x59, 0x44, 0x5f);
pub const TEXT: Color32 = Color32::from_rgb(0xe9, 0xe4, 0xe3);
pub const MUTED: Color32 = Color32::from_rgb(0xb9, 0xb2, 0xbc);
pub const HIGHLIGHT: Color32 = Color32::from_rgb(0xcb, 0xae, 0xb9);
pub const GOLD: Color32 = Color32::from_rgb(0xb4, 0x9a, 0x70);
pub const SUCCESS: Color32 = Color32::from_rgb(0xb4, 0x9a, 0x70);
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
    v.widgets.hovered.bg_fill = SECONDARY;
    v.widgets.hovered.fg_stroke.color = TEXT;
    v.widgets.active.bg_fill = ACCENT;
    v.widgets.active.fg_stroke.color = Color32::WHITE;
    v.selection.bg_fill = ACCENT;
    v.selection.stroke.color = TEXT;
    v.hyperlink_color = ACCENT;
    v.warn_fg_color = Color32::from_rgb(0xfb, 0xbf, 0x24);
    v.error_fg_color = ERROR;
    ctx.set_visuals(v);

    let fonts = eframe::egui::FontDefinitions::default();
    // Use default fonts; no custom embed to keep binary small.
    ctx.set_fonts(fonts);
}

pub fn progress_gradient() -> (Color32, Color32) {
    (ACCENT, GOLD)
}
