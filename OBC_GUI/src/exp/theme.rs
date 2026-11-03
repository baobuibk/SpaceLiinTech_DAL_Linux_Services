//! Colours taken from the EXP test GUI screenshots (dark canvas, blue bar, yellow cards).
use ratatui::style::{Color, Modifier, Style};

pub const BG: Color = Color::Rgb(15, 17, 22);
pub const BLUE: Color = Color::Rgb(48, 112, 224);
pub const YELLOW: Color = Color::Rgb(247, 220, 111);
pub const BORDER: Color = Color::Rgb(205, 170, 40);
pub const GREEN: Color = Color::Rgb(70, 225, 120);
pub const MUTED: Color = Color::Rgb(130, 136, 148);
pub const FG: Color = Color::Rgb(235, 238, 245);

pub fn base() -> Style {
    Style::default().fg(FG).bg(BG)
}
pub fn muted() -> Style {
    Style::default().fg(MUTED).bg(BG)
}
pub fn title() -> Style {
    Style::default().fg(YELLOW).bg(BG).add_modifier(Modifier::BOLD)
}
pub fn border() -> Style {
    Style::default().fg(BORDER).bg(BG)
}
pub fn button() -> Style {
    Style::default().fg(FG).bg(BG)
}
/// Focused or selected item: yellow fill, dark text.
pub fn selected() -> Style {
    Style::default().fg(BG).bg(YELLOW).add_modifier(Modifier::BOLD)
}
/// Active tab: blue fill.
pub fn tab_active() -> Style {
    Style::default().fg(Color::White).bg(BLUE).add_modifier(Modifier::BOLD)
}
