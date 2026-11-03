use crate::theme::{panel_frame, title, MUTED, YELLOW};
use eframe::egui;
use std::time::Instant;

struct Channel {
    status: String,
    raw: String,
    updated: Option<Instant>,
    active_low: bool,
}

impl Default for Channel {
    fn default() -> Self {
        Self {
            status: String::new(),
            raw: String::new(),
            updated: None,
            active_low: true,
        }
    }
}

#[derive(Default)]
pub struct State {
    pending: String,
    channels: [Channel; 4],
}

impl State {
    pub fn feed(&mut self, text: &str) {
        self.pending.push_str(text);
        while let Some(end) = self.pending.find('\n') {
            let line: String = self.pending.drain(..=end).collect();
            let line = line.trim();
            let Some(rest) = line.strip_prefix("LIMIT") else {
                continue;
            };
            let Some((id, details)) = rest.split_once(' ') else {
                continue;
            };
            let Ok(id) = id.parse::<usize>() else {
                continue;
            };
            let Some(channel) = self.channels.get_mut(id) else {
                continue;
            };
            if details.contains("powered=") {
                channel.raw = details.into();
            } else {
                channel.status = details.into();
                channel.updated = Some(Instant::now());
                if let Some(level) = field(details, "active_level=") {
                    channel.active_low = level.eq_ignore_ascii_case("LOW");
                }
                if let (Some(light), Some(dark)) =
                    (field(details, "light="), field(details, "dark="))
                {
                    channel.raw = format!("light={light} dark={dark}");
                }
            }
        }
        if self.pending.len() > 8192 {
            self.pending.clear();
        }
    }

    pub fn render(&mut self, ui: &mut egui::Ui, ready: bool) -> Vec<String> {
        let mut commands = Vec::new();
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(title("OPTICAL LIMITS / ALL CHANNELS"));
            ui.horizontal_wrapped(|ui| {
                for (label, command) in [
                    ("Refresh status", "limit_status all"),
                    ("Refresh raw", "limit_raw all"),
                    ("Get all", "limit_get all"),
                ] {
                    if ui.add_enabled(ready, egui::Button::new(label)).clicked() {
                        commands.push(command.to_owned());
                    }
                }
            });
            ui.add_space(8.0);
            ui.columns(2, |columns| {
                for (index, channel) in self.channels.iter_mut().enumerate() {
                    let ui = &mut columns[index % 2];
                    if index % 2 == 1 {
                        ui.add_space(4.0);
                    }
                    panel_frame().show(ui, |ui| {
                        ui.label(
                            egui::RichText::new(format!("LIMIT {index}"))
                                .color(YELLOW)
                                .strong(),
                        );
                        ui.label(sensor_location(index));
                        ui.label(if channel.status.is_empty() {
                            "Status: UNKNOWN".to_owned()
                        } else {
                            format!("Status: {}", channel.status)
                        });
                        ui.label(if channel.raw.is_empty() {
                            "Raw: UNKNOWN".to_owned()
                        } else {
                            format!("Raw: {}", channel.raw)
                        });
                        ui.horizontal(|ui| {
                            egui::ComboBox::from_id_salt(("limit_active_level", index))
                                .selected_text(if channel.active_low {
                                    "Active LOW"
                                } else {
                                    "Active HIGH"
                                })
                                .show_ui(ui, |ui| {
                                    ui.selectable_value(
                                        &mut channel.active_low,
                                        true,
                                        "Active LOW",
                                    );
                                    ui.selectable_value(
                                        &mut channel.active_low,
                                        false,
                                        "Active HIGH",
                                    );
                                });
                            if ui
                                .add_enabled(ready, egui::Button::new("Apply level"))
                                .clicked()
                            {
                                commands.push(format!(
                                    "limit_cfg_set {index} active_level {}",
                                    if channel.active_low { "low" } else { "high" }
                                ));
                            }
                        });
                        if let Some(updated) = channel.updated {
                            let age = updated.elapsed().as_secs_f32();
                            ui.label(
                                egui::RichText::new(format!(
                                    "{} / received {age:.1}s ago",
                                    if age < 5.0 { "RECENT" } else { "STALE" }
                                ))
                                .color(MUTED),
                            );
                        }
                    });
                }
            });
        });
        commands
    }
}

fn field<'a>(line: &'a str, key: &str) -> Option<&'a str> {
    line.split_whitespace()
        .find_map(|token| token.strip_prefix(key))
}

fn sensor_location(id: usize) -> &'static str {
    match id {
        0 => "PM-U25 #1 / PD11",
        1 => "PM-U25 #2 / PC7",
        2 => "PM-L25 #1 / PA24",
        _ => "PM-L25 #2 / PD13",
    }
}
