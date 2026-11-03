use eframe::egui;
pub const BG: egui::Color32 = egui::Color32::from_rgb(9, 10, 12);
pub const PANEL: egui::Color32 = egui::Color32::from_rgb(15, 17, 20);
pub const BLUE: egui::Color32 = egui::Color32::from_rgb(50, 108, 225);
pub const YELLOW: egui::Color32 = egui::Color32::from_rgb(245, 221, 112);
pub const MUTED: egui::Color32 = egui::Color32::from_rgb(143, 148, 155);
pub const GREEN: egui::Color32 = egui::Color32::from_rgb(109, 222, 132);

pub fn panel_frame() -> egui::Frame {
    egui::Frame::default()
        .fill(PANEL)
        .stroke(egui::Stroke::new(1.0_f32, YELLOW))
        .inner_margin(12.0)
}
pub fn title(text: &str) -> egui::RichText {
    egui::RichText::new(text).color(YELLOW).strong().size(18.0)
}

pub fn configure_theme(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();
    visuals.panel_fill = BG;
    visuals.window_fill = PANEL;
    visuals.extreme_bg_color = BG;
    visuals.code_bg_color = BG;
    visuals.faint_bg_color = PANEL;
    visuals.override_text_color = Some(egui::Color32::from_rgb(231, 234, 239));
    visuals.selection.bg_fill = BLUE;
    visuals.selection.stroke = egui::Stroke::new(1.0_f32, egui::Color32::WHITE);
    visuals.hyperlink_color = YELLOW;
    visuals.window_corner_radius = egui::CornerRadius::ZERO;
    visuals.menu_corner_radius = egui::CornerRadius::ZERO;
    visuals.window_stroke = egui::Stroke::new(1.0_f32, YELLOW);
    for widget in [
        &mut visuals.widgets.noninteractive,
        &mut visuals.widgets.inactive,
        &mut visuals.widgets.hovered,
        &mut visuals.widgets.active,
        &mut visuals.widgets.open,
    ] {
        widget.corner_radius = egui::CornerRadius::ZERO;
        widget.bg_fill = PANEL;
        widget.weak_bg_fill = PANEL;
        widget.bg_stroke = egui::Stroke::new(1.0_f32, MUTED);
        widget.fg_stroke = egui::Stroke::new(1.0_f32, egui::Color32::from_rgb(231, 234, 239));
        widget.expansion = 0.0;
    }
    for widget in [
        &mut visuals.widgets.hovered,
        &mut visuals.widgets.active,
        &mut visuals.widgets.open,
    ] {
        widget.bg_fill = BLUE;
        widget.weak_bg_fill = BLUE;
        widget.bg_stroke = egui::Stroke::new(1.0_f32, YELLOW);
    }
    visuals.widgets.noninteractive.bg_stroke =
        egui::Stroke::new(1.0_f32, egui::Color32::from_rgb(56, 60, 68));
    ctx.set_visuals(visuals);
    let mut style = (*ctx.style()).clone();
    for (kind, size) in [
        (egui::TextStyle::Small, 12.0),
        (egui::TextStyle::Body, 14.0),
        (egui::TextStyle::Button, 14.0),
        (egui::TextStyle::Heading, 18.0),
        (egui::TextStyle::Monospace, 13.0),
    ] {
        style
            .text_styles
            .insert(kind, egui::FontId::new(size, egui::FontFamily::Monospace));
    }
    style.spacing.item_spacing = egui::vec2(10.0, 8.0);
    style.spacing.button_padding = egui::vec2(10.0, 5.0);
    style.spacing.interact_size = egui::vec2(44.0, 28.0);
    style.spacing.text_edit_width = 150.0;
    style.spacing.combo_width = 150.0;
    style.spacing.window_margin = egui::Margin::same(12);
    ctx.set_style(style);
}
