use crate::pac_panel;
use crate::theme::{panel_frame, title, MUTED, YELLOW};
use eframe::egui;
use std::time::Instant;

const KEYS: [&str; 10] = [
    "duty_pct",
    "pulse_ms",
    "max_total_ms",
    "min_current_ma",
    "max_current_ma",
    "open_current_ma",
    "max_baseline_ma",
    "open_confirm_samples",
    "motor_12v_min_mv",
    "motor_12v_max_mv",
];
fn key_range(key: usize) -> std::ops::RangeInclusive<u32> {
    match key {
        0 => 1..=100,
        1 => 50..=1000,
        2 => 50..=60000,
        3..=5 => 1..=3000,
        6 => 1..=1000,
        7 => 2..=10,
        _ => 8000..=16000,
    }
}

#[derive(Default)]
pub struct State {
    pending: String,
    runtime: String,
    config: [String; 2],
    voltage: [String; 2],
    result: [String; 2],
    key: usize,
    value: u32,
    next_refresh: Option<Instant>,
}

impl State {
    pub fn feed(&mut self, text: &str) {
        self.pending.push_str(text);
        while let Some(end) = self.pending.find('\n') {
            let line: String = self.pending.drain(..=end).collect();
            let line = line.trim();
            if let Some(rest) = line.strip_prefix("burn state=") {
                self.runtime = format!("state={rest}");
            }
            for wire in 0..2 {
                if let Some(rest) = line.strip_prefix(&format!("wire{wire} configured=")) {
                    self.config[wire] = format!("configured={rest}");
                } else if let Some(rest) =
                    line.strip_prefix(&format!("wire{wire} motor_12v_min_mv="))
                {
                    self.voltage[wire] = format!("motor_12v_min_mv={rest}");
                } else if let Some(rest) = line.strip_prefix(&format!("wire{wire} last=")) {
                    self.result[wire] = format!("last={rest}");
                }
            }
        }
        if self.pending.len() > 8192 {
            self.pending.clear();
        }
    }

    pub fn render(
        &mut self,
        ui: &mut egui::Ui,
        ready: bool,
        pac: &pac_panel::State,
    ) -> Vec<String> {
        let mut commands = Vec::new();
        let now = Instant::now();
        if ready && self.next_refresh.map_or(true, |at| now >= at) {
            commands.push("burn_status".into());
            commands.push("pac1934_read".into());
            self.next_refresh = Some(now + std::time::Duration::from_secs(2));
        }
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(title("BURN-WIRE CONTROL / TWO CHANNELS"));
            ui.label("The shared motor supply must be on and healthy. PAC channel 1 measures the shared 12V_MOTOR feed, not individual wire current.");
            ui.horizontal_wrapped(|ui| {
                action(ui, &mut commands, ready, "Refresh status", "burn_status");
                action(ui, &mut commands, ready, "Refresh PAC", "pac1934_read");
                action(ui, &mut commands, ready, "STOP / Abort", "burn_abort");
            });
            ui.label(if self.runtime.is_empty() { "Burn state: UNKNOWN" } else { self.runtime.as_str() });
            ui.add_space(8.0);
            ui.columns(2, |columns| {
                for wire in 0..2 {
                    panel_frame().show(&mut columns[wire], |ui| {
                        ui.label(egui::RichText::new(format!("WIRE {} PROFILE", wire)).color(YELLOW).strong());
                        ui.label(if self.config[wire].is_empty() { "Configuration: UNKNOWN".to_owned() } else { format!("Configuration: {}", self.config[wire]) });
                        ui.label(if self.voltage[wire].is_empty() { "12 V limits: UNKNOWN".to_owned() } else { self.voltage[wire].clone() });
                        ui.label(if self.result[wire].is_empty() { "Last result: UNKNOWN".to_owned() } else { self.result[wire].clone() });
                        ui.horizontal_wrapped(|ui| {
                            egui::ComboBox::from_id_salt(("burn_key", wire)).selected_text(KEYS[self.key]).show_ui(ui, |ui| {
                                for (index, key) in KEYS.iter().enumerate() { ui.selectable_value(&mut self.key, index, *key); }
                            });
                            let range = key_range(self.key);
                            self.value = self.value.clamp(*range.start(), *range.end());
                            ui.add(egui::DragValue::new(&mut self.value).range(range));
                            action(ui, &mut commands, ready, "Persist setting", &format!("burn_cfg_set {wire} {} {}", KEYS[self.key], self.value));
                        });
                        ui.horizontal_wrapped(|ui| {
                            action(ui, &mut commands, ready, "ARM", &format!("burn_arm {wire}"));
                            let armed = self.runtime.contains(&format!("state=ARMED active_wire={wire}"));
                            action(ui, &mut commands, ready && armed, "START BURN", "burn_fire");
                        });
                    });
                }
            });
            panel_frame().show(ui, |ui| {
                ui.label(egui::RichText::new("PAC1934 / CHANNEL 1 / 12V_MOTOR").color(YELLOW).strong());
                if let Some([volts, amps, watts]) = pac.samples[0] {
                    ui.label(format!("Voltage {volts:.3} V    Current {amps:.3} A    Power {watts:.3} W"));
                } else {
                    ui.label("Voltage/current/power: UNKNOWN; refresh PAC to read the latest cached sample.");
                }
                ui.label(if pac.snapshot.is_empty() { "PAC sample age: UNKNOWN".to_owned() } else { pac.snapshot.clone() });
                ui.label(egui::RichText::new("PAC current is the shared 12V_MOTOR rail current; it does not measure the 5V_MOTOR output or one wire separately.").color(MUTED));
            });
            ui.label("Profile settings persist individually to FRAM. ARM reserves the motor channel; START BURN requires a fresh healthy PAC sample. STOP / Abort de-energizes both outputs.");
        });
        commands
    }
}

fn action(
    ui: &mut egui::Ui,
    commands: &mut Vec<String>,
    enabled: bool,
    label: &str,
    command: &str,
) {
    if ui.add_enabled(enabled, egui::Button::new(label)).clicked() {
        commands.push(command.to_owned());
    }
}
