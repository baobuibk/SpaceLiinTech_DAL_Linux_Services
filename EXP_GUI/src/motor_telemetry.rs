use crate::theme::{panel_frame, GREEN, MUTED, YELLOW};
use eframe::egui;
use std::time::Instant;

#[derive(Default)]
pub struct Reading {
    pub text: String,
    pub updated: Option<Instant>,
}
impl Reading {
    fn set(&mut self, text: &str) {
        self.text = text.into();
        self.updated = Some(Instant::now());
    }
    fn show(&self, ui: &mut egui::Ui, title: &str) {
        ui.label(egui::RichText::new(title).color(YELLOW).strong());
        let fresh = self.updated.is_some_and(|t| t.elapsed().as_secs() < 5);
        let color = if !fresh {
            MUTED
        } else if self.text.contains("faulted=1") || self.text.contains("ERROR") {
            egui::Color32::from_rgb(255, 100, 100)
        } else {
            GREEN
        };
        let display = summary(&self.text);
        ui.label(egui::RichText::new(display).color(color).monospace());
        if let Some(t) = self.updated {
            ui.label(
                egui::RichText::new(format!(
                    "{} | received {:.1}s ago",
                    if fresh { "RECENT" } else { "STALE" },
                    t.elapsed().as_secs_f32()
                ))
                .color(MUTED),
            );
        }
    }
}
fn field<'a>(text: &'a str, key: &str) -> &'a str {
    text.split_whitespace()
        .find_map(|token| token.strip_prefix(key))
        .unwrap_or("?")
}
fn summary(text: &str) -> String {
    if text.is_empty() {
        return "UNKNOWN / Request status".into();
    }
    if text.starts_with("MOTOR") {
        let mode = field(text, "mode=");
        let state = text
            .split_whitespace()
            .find(|token| {
                ["STOPPED", "MOVING", "RUNNING", "HOMING", "HOLDING", "ERROR"].contains(token)
            })
            .unwrap_or("?");
        format!(
            "mode={} | {} | pos={}\nremaining={} speed={} steps/s\nreference={} request={}\nreason={}",
            mode,
            state,
            field(text, "position="),
            field(text, "remaining="),
            field(text, "speed_sps="),
            field(text, "reference_valid="),
            field(text, "request="),
            field(text, "reason=")
        )
    } else {
        let prefix = if text.starts_with("driver_") {
            "driver_"
        } else {
            ""
        };
        format!(
            "initialized={}\nsleep={} fault={}\ndiagnostic={}",
            field(text, &format!("{prefix}initialized=")),
            field(text, &format!("{prefix}asleep=")),
            field(text, &format!("{prefix}faulted=")),
            if prefix.is_empty() {
                field(text, "diag=")
            } else {
                field(text, "diagnostic=")
            }
        )
    }
}
#[derive(Default)]
pub struct Telemetry {
    pending: String,
    pub driver: Reading,
    pub motors: [Reading; 2],
}
impl Telemetry {
    pub fn feed(&mut self, text: &str) {
        self.pending.push_str(text);
        while let Some(end) = self.pending.find('\n') {
            let line: String = self.pending.drain(..=end).collect();
            self.parse(line.trim());
        }
        // An unframed prompt or unrelated long output must not grow without bound.
        if self.pending.len() > 8192 {
            self.pending.clear();
        }
    }
    fn parse(&mut self, line: &str) {
        if let Some(start) = line.find("DRV8910 initialized=") {
            self.driver.set(&line[start..]);
        } else if let Some(start) = line.find("driver_initialized=") {
            self.driver.set(&line[start..]);
        }
        for (index, prefix) in ["MOTOR0 ", "MOTOR1 "].iter().enumerate() {
            if let Some(start) = line.find(prefix) {
                let text = &line[start..];
                if text.split_whitespace().any(|token| {
                    ["STOPPED", "MOVING", "RUNNING", "HOMING", "HOLDING", "ERROR"].contains(&token)
                }) {
                    self.motors[index].set(text);
                }
            }
        }
    }
    pub fn show(&self, ui: &mut egui::Ui) {
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(egui::RichText::new("LIVE STATUS / SELECTED LINK").color(YELLOW).strong());
            ui.columns(3, |columns| {
                self.driver.show(&mut columns[0], "DRV8910");
                self.motors[0].show(&mut columns[1], "MOTOR 0 / J13");
                self.motors[1].show(&mut columns[2], "MOTOR 1 / J14");
            });
            ui.label(egui::RichText::new("Use Driver status and Motor status to refresh. Commanded position is not measured shaft feedback.").color(MUTED));
        });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn serial_fragments_form_complete_status_lines() {
        let mut status = Telemetry::default();
        status.feed("DEBUG@MCU:~ $ \r\nDRV8910 initial");
        assert!(status.driver.updated.is_none());
        status.feed("ized=1 asleep=0 faulted=0 diag=0 status=0xC1\r\nMOTOR0 MOV");
        assert!(status.driver.text.contains("faulted=0"));
        status.feed("ING position=3 remaining=7\r\nMOTOR1 STOPPED position=0\n");
        assert!(status.motors[0].text.contains("remaining=7"));
        assert!(status.motors[1].text.contains("STOPPED"));
    }
    #[test]
    fn mapping_output_cannot_replace_motion_status() {
        let mut status = Telemetry::default();
        status.feed(
            "MOTOR0 STOPPED position=10 remaining=0\nMOTOR0 home=optical1 Light-ON active_LOW\n",
        );
        assert!(status.motors[0].text.contains("position=10"));
    }
    #[test]
    fn unknown_until_reply_and_independent_links() {
        let a = Telemetry::default();
        let mut b = Telemetry::default();
        b.feed("driver_initialized=1 driver_asleep=0 driver_faulted=1 diagnostic=0\n");
        assert!(a.driver.updated.is_none());
        assert!(b.driver.text.contains("driver_faulted=1"));
        b.feed("echo drv8910_status\nERROR NOT_READY\n");
        assert!(b.motors[0].updated.is_none());
    }
}
