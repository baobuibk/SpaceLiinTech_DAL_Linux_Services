use crate::theme::{panel_frame, MUTED, YELLOW};
use eframe::egui;
use std::time::Instant;

#[derive(Default)]
pub struct State {
    pending: String,
    ntc: [Option<(String, Instant)>; 5],
    duty: [Option<(u8, Instant)>; 4],
    owner: [String; 4],
    rail: Option<(String, Instant)>,
}
impl State {
    pub fn feed(&mut self, text: &str) {
        self.pending.push_str(text);
        while let Some(end) = self.pending.find('\n') {
            let line: String = self.pending.drain(..=end).collect();
            self.line(line.trim());
        }
        if self.pending.len() > 8192 {
            self.pending.clear();
        }
    }
    fn line(&mut self, line: &str) {
        for i in 0..5 {
            let prefix = if i == 4 {
                "BOARD_TEMP : ".into()
            } else {
                format!("NTC[{i}] : ")
            };
            if let Some((_, value)) = line.split_once(&prefix) {
                self.ntc[i] = Some((value.into(), Instant::now()));
            }
        }
        if let Some(start) = line.find("Power 12V_EXP requested=") {
            self.rail = Some((line[start..].into(), Instant::now()));
        }
        for i in 0..4 {
            let prefix = format!("Heater[{}] ", i);
            if let Some((_, value)) = line.split_once(&prefix) {
                if value.starts_with("owner=") {
                    self.owner[i] = value.into();
                }
                // Only published readback updates the card; set acknowledgements are in the console.
                if line.starts_with(&prefix) {
                    if let Some(value) = value
                        .strip_prefix("Duty = ")
                        .and_then(|v| v.strip_suffix('%'))
                        .and_then(|v| v.parse::<u8>().ok())
                        .filter(|v| *v <= 100)
                    {
                        self.duty[i] = Some((value, Instant::now()));
                    }
                }
            }
            if line.starts_with("Heater duty: ") {
                let key = format!("H{}=", i);
                if let Some(value) = line
                    .split_once(&key)
                    .and_then(|(_, v)| v.split('%').next())
                    .and_then(|v| v.parse::<u8>().ok())
                    .filter(|v| *v <= 100)
                {
                    self.duty[i] = Some((value, Instant::now()));
                }
            }
        }
    }
}
pub struct HeaterPanel {
    duty: [u32; 4],
    duration: [u32; 4],
    result: u32,
}
impl Default for HeaterPanel {
    fn default() -> Self {
        Self {
            duty: [5; 4],
            duration: [30000; 4],
            result: 1,
        }
    }
}
fn reading(ui: &mut egui::Ui, text: &str, at: Instant) {
    let age = at.elapsed().as_secs_f32();
    ui.label(egui::RichText::new(text).color(if age < 5.0 { YELLOW } else { MUTED }));
    ui.small(format!(
        "{} / received {age:.1}s ago",
        if age < 5.0 { "RECENT" } else { "STALE" }
    ));
}
impl HeaterPanel {
    pub fn show(&mut self, ui: &mut egui::Ui, state: &State) -> Vec<String> {
        let mut commands = Vec::new();
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(egui::RichText::new("HEATER SUPPLY / 12V_EXP").color(YELLOW));
            ui.horizontal_wrapped(|ui| {
                for (label, command) in [("Power ON", "power_exp12v_on"), ("Power OFF", "power_exp12v_off"), ("Refresh power", "power_exp12v_get"), ("Refresh heaters", "heater_get"), ("Stop all heaters", "heater_stop all")] {
                    if ui.button(label).clicked() { commands.push(command.into()); }
                }
            });
            if let Some((value, at)) = &state.rail { reading(ui, value, *at); } else { ui.label("Supply status: UNKNOWN / refresh power"); }
            ui.small("Shared with IR LEDs. Power OFF can be refused while consumers own the rail; stop consumers first.");
        });
        ui.add_space(8.0);
        for row in 0..2 {
            ui.columns(2, |columns| {
                for (column, ui) in columns.iter_mut().enumerate() {
                    let i = row * 2 + column;
                    let channel = i;
                    panel_frame().show(ui, |ui| {
                        ui.set_min_width(ui.available_width());
                        ui.label(
                            egui::RichText::new(format!("MANUAL HEATER {channel}")).color(YELLOW),
                        );
                        egui::Grid::new(format!("heater_inputs_{i}")).show(ui, |ui| {
                            ui.label("Duty %");
                            ui.add(egui::DragValue::new(&mut self.duty[i]).range(0..=100));
                            ui.end_row();
                            ui.label("ON time ms");
                            ui.add(egui::DragValue::new(&mut self.duration[i]).range(1..=600000));
                            ui.end_row();
                        });
                        ui.horizontal_wrapped(|ui| {
                            if ui.button("ON / Apply duty").clicked() {
                                commands.push(format!(
                                    "heater_set {channel} {} {}",
                                    self.duty[i], self.duration[i]
                                ));
                            }
                            if ui.button("OFF").clicked() {
                                commands.push(format!("heater_stop {channel}"));
                            }
                            if ui.button("Read status").clicked() {
                                commands.push(format!("heater_get {channel}"));
                            }
                        });
                        if let Some((duty, at)) = state.duty[i] {
                            reading(ui, &format!("Applied duty: {duty}%"), at);
                        } else {
                            ui.label("Applied duty: UNKNOWN");
                        }
                        if !state.owner[i].is_empty() {
                            ui.small(&state.owner[i]);
                        }
                    });
                }
            });
            ui.add_space(8.0);
        }
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.horizontal(|ui| {
                ui.label(egui::RichText::new("NTC TEMPERATURES").color(YELLOW));
                if ui.button("Refresh NTC").clicked() { commands.push("ntc_read_all".into()); }
            });
            egui::Grid::new("heater_ntc").show(ui, |ui| {
                for i in 0..5 {
                    ui.label(if i == 4 { "Board temperature".into() } else { format!("NTC {i}") });
                    if let Some((value, at)) = &state.ntc[i] { ui.vertical(|ui| reading(ui,value,*at)); } else { ui.label("UNKNOWN / no sample received"); }
                    ui.end_row();
                }
            });
            ui.small("NTC indexes are independent of heater indexes; verify the configured sensor mapping. Firmware age/INVALID/STALE/NO_SAMPLE are shown as received.");
        });
        ui.add_space(8.0);
        ui.horizontal_wrapped(|ui| {
            ui.label("Request ID");
            ui.add(egui::DragValue::new(&mut self.result).range(1..=u32::MAX));
            if ui.button("Read result").clicked() {
                commands.push(format!("heater_result {}", self.result));
            }
        });
        ui.small("Manual PWM does not check NTCs or profile thermal settings. Enable 12V_EXP first. Active profile ownership, duty limits, duration timeout and power cutoffs still apply. Check outcomes in TX/RX; poll a TIMEOUT ticket before retrying. Read status after changes. Applied duty is software readback.");
        commands
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fragmented_reads_and_invalid_samples() {
        let mut s = State::default();
        s.feed("NTC[0] : 24.");
        assert!(s.ntc[0].is_none());
        s.feed("50 C age=20 ms raw=1234\nNTC[1] : INVALID age=4 ms raw=0\nBOARD_TEMP : NO_SAMPLE\nHeater duty: H0=5% H1=0% H2=8% H3=0%\n");
        assert!(s.ntc[0].as_ref().unwrap().0.starts_with("24.50"));
        assert!(s.ntc[1].as_ref().unwrap().0.starts_with("INVALID"));
        assert_eq!(s.duty[2].unwrap().0, 8);
        s.feed("Heater[1] owner=MANUAL profile=-1 generation=1\nHeater[1] Duty = 7%\n--> OK: Heater[1] Duty = 30% (requested 30%)\n");
        assert_eq!(s.duty[1].unwrap().0, 7);
        assert!(s.owner[1].starts_with("owner=MANUAL"));
        s.feed("Power 12V_EXP requested=ON observed=UNKNOWN phase=ON_REQUESTED last=OK seq=1\n");
        assert!(s.rail.is_some());
    }
}
