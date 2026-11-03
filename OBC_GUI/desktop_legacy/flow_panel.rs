use crate::theme::{panel_frame, title, MUTED, YELLOW};
use eframe::egui;
use std::time::{Duration, Instant};

#[derive(Default)]
struct Channel {
    detection: String,
    flow: Option<f64>,
    temperature: Option<f64>,
    info: String,
}
struct Pending {
    channel: usize,
    detect: bool,
    id: Option<u32>,
    deadline: Instant,
    next: Instant,
}
#[derive(Default)]
pub struct State {
    text: String,
    channels: [Channel; 2],
    pending: Option<Pending>,
}
fn field(line: &str, key: &str) -> Option<u32> {
    line.split_whitespace()
        .find_map(|v| v.strip_prefix(key)?.parse().ok())
}
impl State {
    fn start(&mut self, channel: usize, detect: bool) -> String {
        let now = Instant::now();
        self.pending = Some(Pending {
            channel,
            detect,
            id: None,
            deadline: now + Duration::from_secs(15),
            next: now + Duration::from_millis(500),
        });
        self.channels[channel].flow = None;
        self.channels[channel].temperature = None;
        self.channels[channel].info = "Waiting for sensor...".into();
        if detect {
            self.channels[channel].detection = "Detecting...".into();
        }
        format!(
            "{} {channel}",
            if detect { "flow_init" } else { "flow_read_all" }
        )
    }
    pub fn poll(&mut self) -> Option<String> {
        let p = self.pending.as_mut()?;
        let now = Instant::now();
        if now >= p.deadline {
            let c = &mut self.channels[p.channel];
            c.info = "Unavailable: operation timed out".into();
            if p.detect {
                c.detection = "Unavailable".into();
            }
            self.pending = None;
            return None;
        }
        if now >= p.next {
            p.next = now + Duration::from_millis(500);
            return p.id.map(|id| format!("flow_result {id}"));
        }
        None
    }
    pub fn feed(&mut self, text: &str) {
        self.text.push_str(text);
        while let Some(end) = self.text.find('\n') {
            let line: String = self.text.drain(..=end).collect();
            self.line(line.trim());
        }
        if self.text.len() > 8192 {
            self.text.clear();
        }
    }
    fn line(&mut self, line: &str) {
        if let Some((_, result)) = line.split_once("FLOW ") {
            if let Some(p) = self.pending.as_mut() {
                let code = result.split_whitespace().next().unwrap_or("");
                let id = field(result, "id=");
                let sensor = field(result, "sensor=");
                if sensor == Some(p.channel as u32) {
                    if code == "QUEUED" && p.id.is_none() && id.is_some_and(|v| v > 0) {
                        p.id = id;
                    } else if (p.id.is_some() && id == p.id) || (p.id.is_none() && id == Some(0)) {
                        if code != "QUEUED" && code != "ACCEPTED" {
                            let c = &mut self.channels[p.channel];
                            c.info = result.into();
                            if p.detect {
                                c.detection = if code == "DONE" {
                                    "Detected"
                                } else {
                                    "Unavailable"
                                }
                                .into();
                            }
                            self.pending = None;
                        }
                    }
                } else if code == "NOT_FOUND" && sensor.is_none() {
                    let c = &mut self.channels[p.channel];
                    c.info = "Unavailable: request expired or unknown".into();
                    if p.detect {
                        c.detection = "Unavailable".into();
                    }
                    self.pending = None;
                }
            }
        }
        for (i, c) in self.channels.iter_mut().enumerate() {
            if let Some((_, data)) = line.split_once(&format!("Sensor {i}: Flow=")) {
                let values = data.split_once(", Temp=").and_then(|(flow, rest)| {
                    let (temp, details) = rest.split_once(", Flags=")?;
                    let f = flow.strip_suffix(" uL/min")?.parse::<f64>().ok()?;
                    let t = temp.strip_suffix(" C")?.parse::<f64>().ok()?;
                    (f.is_finite() && t.is_finite()).then_some((f, t, details))
                });
                c.flow = values.map(|v| v.0);
                c.temperature = values.map(|v| v.1);
                c.info = values
                    .map(|v| format!("Flags={}", v.2))
                    .unwrap_or_else(|| "Unavailable: malformed sample".into());
            }
        }
    }
    pub fn render(&mut self, ui: &mut egui::Ui, ready: bool) -> Vec<String> {
        let mut commands = Vec::new();
        panel_frame().show(ui, |ui| {
            ui.label(title("FLOW SENSORS"));
            ui.label(
                egui::RichText::new(
                    "Detect initializes the sensor in water mode; Read requests a fresh sample.",
                )
                .color(MUTED),
            );
            for i in 0..2 {
                ui.separator();
                ui.label(egui::RichText::new(format!("Sensor {i}")).color(YELLOW));
                ui.horizontal(|ui| {
                    let enabled = ready && self.pending.is_none();
                    if ui
                        .add_enabled(enabled, egui::Button::new("Detect"))
                        .clicked()
                    {
                        commands.push(self.start(i, true));
                    }
                    if ui.add_enabled(enabled, egui::Button::new("Read")).clicked() {
                        commands.push(self.start(i, false));
                    }
                    ui.label(if self.channels[i].detection.is_empty() {
                        "Not checked"
                    } else {
                        &self.channels[i].detection
                    });
                });
                let c = &self.channels[i];
                ui.label(format!(
                    "Flow: {} uL/min",
                    c.flow
                        .map(|v| format!("{v:.3}"))
                        .unwrap_or_else(|| "--".into())
                ));
                ui.label(format!(
                    "Temperature: {} C",
                    c.temperature
                        .map(|v| format!("{v:.2}"))
                        .unwrap_or_else(|| "--".into())
                ));
                ui.label(&c.info);
            }
        });
        commands
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn detect_and_read_correlate_channel_and_id() {
        let mut s = State::default();
        assert_eq!(s.start(1, true), "flow_init 1");
        s.feed("FLOW QUEUED id=8 sensor=1\nFLOW DONE id=8 sensor=0 error=0\n");
        assert_eq!(s.channels[1].detection, "Detecting...");
        s.pending.as_mut().unwrap().next = Instant::now();
        assert_eq!(s.poll().as_deref(), Some("flow_result 8"));
        s.feed("FLOW DONE id=8 sensor=1 error=0\n");
        assert_eq!(s.channels[1].detection, "Detected");
        assert_eq!(s.start(1, false), "flow_read_all 1");
        s.feed("FLOW QUEUED id=9 sensor=1\nFLOW DONE id=9 sensor=1 error=0\nSensor 1: Flow=-12.");
        s.feed("345 uL/min, Temp=25.50 C, Flags=0x0000 AirInLine=0 sequence=1 age_ms=2\n");
        assert_eq!(s.channels[1].flow, Some(-12.345));
        assert!(s.channels[0].flow.is_none());
    }
    #[test]
    fn failure_and_timeout_are_unavailable_without_old_readings() {
        let mut s = State::default();
        s.channels[0].flow = Some(5.0);
        s.start(0, true);
        s.feed("FLOW QUEUED id=2 sensor=0\nFLOW FAILED id=2 sensor=0 error=6\n");
        assert_eq!(s.channels[0].detection, "Unavailable");
        assert!(s.channels[0].flow.is_none());
        s.start(0, true);
        s.pending.as_mut().unwrap().deadline = Instant::now();
        s.poll();
        assert_eq!(s.channels[0].detection, "Unavailable");
    }
}
