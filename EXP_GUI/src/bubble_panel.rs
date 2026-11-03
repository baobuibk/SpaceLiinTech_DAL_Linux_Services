use crate::camera_data::field;
use crate::theme::{panel_frame, title, MUTED, YELLOW};
use eframe::egui;
use std::time::Instant;

#[derive(Default)]
struct Detector {
    levels: Option<[u8; 2]>,
    updated: Option<Instant>,
    reads: u64,
    changes: [u64; 2],
}
#[derive(Default)]
pub struct State {
    pending: String,
    detectors: [Detector; 2],
    error: String,
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
        if line.contains("bubble_raw: read error") || line.contains("Usage: bubble_raw") {
            self.error="Read/argument error reported; cards retain the last successful reads. See console.".into();
            return;
        }
        for (index, prefix) in ["bubble0 ", "bubble1 "].iter().enumerate() {
            if let Some(start) = line.find(prefix) {
                let line = &line[start..];
                let parse = |key| {
                    field(line, key)
                        .and_then(|v| v.parse::<u8>().ok())
                        .filter(|v| *v <= 1)
                };
                if let (Some(out1), Some(out2)) = (parse("OUT1="), parse("OUT2=")) {
                    let detector = &mut self.detectors[index];
                    let levels = [out1, out2];
                    if let Some(before) = detector.levels {
                        for i in 0..2 {
                            if before[i] != levels[i] {
                                detector.changes[i] += 1;
                            }
                        }
                    }
                    detector.levels = Some(levels);
                    detector.updated = Some(Instant::now());
                    detector.reads += 1;
                    self.error.clear();
                }
                return;
            }
        }
    }
}
pub fn show(ui: &mut egui::Ui, state: &mut State) -> Vec<String> {
    let mut commands = Vec::new();
    ui.label(title("BUBBLE DETECTORS / RAW INPUTS"));
    ui.label(
        "BE-A301 detector outputs. Raw electrical levels only; bubble polarity is unqualified.",
    );
    if !state.error.is_empty() {
        ui.label(egui::RichText::new(&state.error).color(YELLOW));
    }
    ui.add_space(8.0);
    ui.columns(2, |columns| {
        for (index, ui) in columns.iter_mut().enumerate() {
            let number = index;
            let pins = if index == 0 { [8, 9] } else { [6, 7] };
            panel_frame().show(ui, |ui| {
                ui.set_min_width(ui.available_width());
                ui.label(
                    egui::RichText::new(format!("DETECTOR {number}"))
                        .color(YELLOW)
                        .strong(),
                );
                ui.label(format!(
                    "OUT1: PD{} / BLACK\nOUT2: PD{} / WHITE",
                    pins[0], pins[1]
                ));
                ui.separator();
                let detector = &state.detectors[index];
                let fresh = detector
                    .updated
                    .is_some_and(|at| at.elapsed().as_secs() < 5);
                for output in 0..2 {
                    let level = detector.levels.map(|v| v[output]);
                    let label = match level {
                        Some(0) => "0 / LOW",
                        Some(1) => "1 / HIGH",
                        _ => "UNKNOWN",
                    };
                    ui.label(
                        egui::RichText::new(format!("OUT{}  {label}", output + 1))
                            .size(23.0)
                            .color(if fresh { YELLOW } else { MUTED }),
                    );
                }
                if let Some(at) = detector.updated {
                    ui.label(format!(
                        "{} / received {:.1}s ago",
                        if fresh { "RECENT" } else { "STALE" },
                        at.elapsed().as_secs_f32()
                    ));
                } else {
                    ui.label("No successful read on this link");
                }
                if ui.button(format!("Read detector {number}")).clicked() {
                    commands.push(format!("bubble_raw {number}"));
                }
                ui.label(format!("Valid replies: {}", detector.reads));
                ui.label(format!(
                    "Observed changes: OUT1={} OUT2={}",
                    detector.changes[0], detector.changes[1]
                ));
            });
        }
    });
    ui.add_space(8.0);
    panel_frame().show(ui,|ui| {
            ui.set_min_width(ui.available_width());
        ui.strong("READOUT & BOARD CHECK");
        ui.label("Each read samples both outputs from one PIOD register snapshot. Detectors are read separately; there is no automatic polling.");
        ui.label("Compare reads with measured GPIO levels for clear liquid, a known bubble, detector power removed and a disconnected cable. Record the qualified truth table for the fitted detector.");
        ui.label("A successful read does not confirm detector power or health. HIGH/LOW is not a bubble verdict. Firmware does not stop pumps or experiments from these inputs.");
        ui.label("Observed changes count differences between valid replies in this session; intermediate edges can be missed. Configuration, filtering, detector power control and bubble alarms are not implemented by the firmware.");
        if ui.button("Reset observation counters").clicked(){for detector in &mut state.detectors{detector.reads=0;detector.changes=[0;2];}}
        ui.label("Counters and levels belong to the selected interface and clear on reconnect. Commands and replies appear in the TX/RX console.");
    });
    commands
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fragmented_reads_and_each_detector_are_independent() {
        let mut s = State::default();
        s.feed("DEBUG@MCU:~ $ \r\nbubble0 OUT1=0 OUT");
        assert!(s.detectors[0].levels.is_none());
        s.feed("2=1 (raw; polarity unqualified)\r\nbubble1 OUT1=1 OUT2=0 (raw; polarity unqualified)\n");
        assert_eq!(s.detectors[0].levels, Some([0, 1]));
        assert_eq!(s.detectors[1].levels, Some([1, 0]));
        s.feed("bubble0 OUT1=1 OUT2=1 (raw; polarity unqualified)\n");
        assert_eq!(s.detectors[0].changes, [1, 0]);
        assert_eq!(s.detectors[1].changes, [0, 0]);
    }
    #[test]
    fn malformed_and_error_replies_do_not_invent_readings() {
        let mut s = State::default();
        s.feed("bubble0 OUT1=2 OUT2=0\nbubble3 OUT1=1 OUT2=1\nbubble_raw: read error\n");
        assert!(s.detectors[0].levels.is_none());
        assert!(!s.error.is_empty());
        s.feed("bubble0 OUT1=0 OUT2=0 (raw; polarity unqualified)\nUsage: bubble_raw <1|2>\n");
        assert_eq!(s.detectors[0].levels, Some([0, 0]));
        assert_eq!(s.detectors[0].reads, 1);
        assert!(!s.error.is_empty());
        assert!(State::default().detectors[0].levels.is_none());
    }
}
