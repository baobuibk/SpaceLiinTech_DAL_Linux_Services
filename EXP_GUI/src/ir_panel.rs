use crate::camera_data::field;
use crate::theme::{panel_frame, title, GREEN, MUTED, YELLOW};
use eframe::egui;
use std::time::Instant;

const HARD_DUTY: u32 = 25;
const HARD_MS: u32 = 100;
#[derive(Clone, Copy, Debug)]
struct Channel {
    active: bool,
    duty: u32,
    remaining: u32,
    hz: u32,
    max_duty: u32,
    max_ms: u32,
}
#[derive(Default)]
pub struct State {
    pending: String,
    channels: [Option<Channel>; 2],
    updated: [Option<Instant>; 2],
    owner: String,
    owner_updated: Option<Instant>,
    result: String,
    pub result_id: u32,
}
fn number(line: &str, key: &str) -> Option<u32> {
    field(line, key)?.parse().ok()
}
fn recent(at: Option<Instant>) -> bool {
    at.is_some_and(|t| t.elapsed().as_secs() < 5)
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
        for (i, prefix) in ["IR0 ", "IR1 "].iter().enumerate() {
            if let Some(start) = line.find(prefix) {
                let line = &line[start..];
                if let (
                    Some(active),
                    Some(duty),
                    Some(remaining),
                    Some(hz),
                    Some(max_duty),
                    Some(max_ms),
                ) = (
                    number(line, "active="),
                    number(line, "duty_pct="),
                    number(line, "remaining_ms="),
                    number(line, "pwm_hz="),
                    number(line, "max_duty_pct="),
                    number(line, "max_on_ms="),
                ) {
                    self.channels[i] = Some(Channel {
                        active: active != 0,
                        duty,
                        remaining,
                        hz,
                        max_duty,
                        max_ms,
                    });
                    self.updated[i] = Some(Instant::now());
                }
                return;
            }
        }
        if let Some(start) = line.find("IR active_id=") {
            self.owner = line[start..].into();
            self.owner_updated = Some(Instant::now());
            return;
        }
        if let Some(start) = line.find("IR ") {
            let line = &line[start..];
            if [
                "QUEUED",
                "ACCEPTED",
                "ACTIVE",
                "DONE",
                "INVALID",
                "BUSY",
                "NOT_READY",
                "REFUSED",
                "CANCELLED",
                "FAILED",
                "NOT_FOUND",
                "STOP_REQUESTED",
            ]
            .contains(&line.split_whitespace().nth(1).unwrap_or(""))
            {
                self.result = line.into();
                if let Some(id) = number(line, "id=").filter(|id| *id > 0) {
                    self.result_id = id;
                }
            }
        }
    }
    fn can_pulse(&self, index: usize, duty: u32, ms: u32) -> bool {
        if index >= 2
            || duty == 0
            || duty > HARD_DUTY
            || ms == 0
            || ms > HARD_MS
            || !recent(self.owner_updated)
            || field(&self.owner, "rail_requested=") != Some("1")
            || field(&self.owner, "stop_pending=") != Some("0")
            || number(&self.owner, "active_id=") != Some(0)
            || number(&self.owner, "stop_sequence=").is_none()
            || number(&self.owner, "stop_sequence=") != number(&self.owner, "reconciled=")
        {
            return false;
        }
        if !(0..2).all(|i| recent(self.updated[i]) && self.channels[i].is_some_and(|c| !c.active)) {
            return false;
        }
        self.channels[index].is_some_and(|c| duty <= c.max_duty && ms <= c.max_ms)
    }
}
#[derive(Clone, Copy)]
struct Draft {
    max_duty: u32,
    max_ms: u32,
    hz: u32,
    duty: u32,
    ms: u32,
}
impl Default for Draft {
    fn default() -> Self {
        Self {
            max_duty: 0,
            max_ms: 0,
            hz: 100000,
            duty: 5,
            ms: 10,
        }
    }
}
#[derive(Default)]
pub struct IrPanel {
    drafts: [Draft; 2],
}
fn action(ui: &mut egui::Ui, out: &mut Vec<String>, label: &str, command: impl Into<String>) {
    if ui.button(label).clicked() {
        out.push(command.into());
    }
}
impl IrPanel {
    pub fn show(&mut self, ui: &mut egui::Ui, state: &mut State) -> Vec<String> {
        let mut out = Vec::new();
        ui.label(title("IR LED CONTROL / CHANNELS 0 + 1"));
        ui.label("One channel at a time. Hard caps: 25% / 100 ms. Inputs below are drafts.");
        panel_frame().show(ui,|ui| {
            ui.set_min_width(ui.available_width());
            ui.strong("POWER & STOP");
            ui.horizontal_wrapped(|ui| {
                action(ui,&mut out,"EXP 12 V ON","power_exp12v_on");
                action(ui,&mut out,"Rail status","power_exp12v_get");
                action(ui,&mut out,"Refresh IR status","ir_status");
                action(ui,&mut out,"STOP BOTH","ir_off all");
                action(ui,&mut out,"EXP 12 V OFF","power_exp12v_off");
            });
            ui.label("EXP 12 V is shared with other peripherals. Stop IR outputs before switching the rail off.");
            ui.label(egui::RichText::new(if state.owner.is_empty(){"UNKNOWN / Refresh IR status"}else{&state.owner}).color(if recent(state.owner_updated){GREEN}else{MUTED}));
            if let Some(at)=state.owner_updated {ui.label(format!("{} / received {:.1}s ago",if recent(Some(at)){"RECENT"}else{"STALE"},at.elapsed().as_secs_f32()));}
            ui.label("Commanded state only; electrical and optical feedback unavailable.");
        });
        ui.add_space(8.0);
        ui.columns(2, |columns| {
        for (index, ui) in columns.iter_mut().enumerate() {
            let channel = index;
            panel_frame().show(ui,|ui| {
            ui.set_min_width(ui.available_width());
                ui.label(egui::RichText::new(format!("IR {channel} / PA{} / PWM0 CHANNEL {index}",index+11)).color(YELLOW).strong());
                if let Some(s)=state.channels[index] {
                    ui.label(egui::RichText::new(format!("{} | duty={} % | remaining={} ms | PWM={} Hz | configured caps={} % / {} ms",
                        if s.active{"ACTIVE"}else{"OFF"},s.duty,s.remaining,s.hz,s.max_duty,s.max_ms)).color(if recent(state.updated[index]){GREEN}else{MUTED}));
                    if let Some(at)=state.updated[index] {ui.label(format!("Received {:.1}s ago{}",at.elapsed().as_secs_f32(),if recent(Some(at)){""}else{" / STALE"}));}
                    if ui.button("Copy reported settings to draft").clicked() {
                        let d=&mut self.drafts[index]; d.max_duty=s.max_duty.min(HARD_DUTY);d.max_ms=s.max_ms.min(HARD_MS);d.hz=s.hz.clamp(100,100000);
                    }
                } else {ui.label("UNKNOWN / Request ir_status before exposure");}
                let d=&mut self.drafts[index];
                egui::Grid::new(("ir_channel_settings",index)).num_columns(3).spacing([10.0,8.0]).show(ui,|ui| {
                    ui.label("Max duty %");ui.add(egui::DragValue::new(&mut d.max_duty).range(0..=HARD_DUTY));
                    action(ui,&mut out,"Set duty cap",format!("ir_cfg_set {channel} max_duty_pct {}",d.max_duty));ui.end_row();
                    ui.label("Max pulse ms");ui.add(egui::DragValue::new(&mut d.max_ms).range(0..=HARD_MS));
                    action(ui,&mut out,"Set time cap",format!("ir_cfg_set {channel} max_on_ms {}",d.max_ms));ui.end_row();
                    ui.label("CTRL PWM Hz");ui.add(egui::DragValue::new(&mut d.hz).range(100..=100000));
                    action(ui,&mut out,"Set PWM",format!("ir_cfg_set {channel} pwm_hz {}",d.hz));ui.end_row();
                });
                ui.separator();
                egui::Grid::new(("ir_pulse_settings",index)).num_columns(2).spacing([10.0,8.0]).show(ui,|ui| {
                    ui.label("Pulse duty %");ui.add(egui::DragValue::new(&mut d.duty).range(1..=HARD_DUTY));ui.end_row();
                    ui.label("Duration ms");ui.add(egui::DragValue::new(&mut d.ms).range(1..=HARD_MS));ui.end_row();
                });
                ui.horizontal_wrapped(|ui| {
                    if ui.add_enabled(state.can_pulse(index,d.duty,d.ms),egui::Button::new(format!("Pulse IR {channel}"))).clicked() {out.push(format!("ir_set {channel} {} {}",d.duty,d.ms));}
                    action(ui,&mut out,&format!("STOP IR {channel}"),format!("ir_off {channel}"));
                });
            });
        }
        });
        ui.add_space(8.0);
        panel_frame().show(ui,|ui| {
            ui.set_min_width(ui.available_width());
            ui.strong("REQUEST / COMPLETION");
            ui.label(if state.result.is_empty(){"No IR result received on this link"}else{&state.result});
            ui.horizontal_wrapped(|ui| {
                ui.label("Request ID");ui.add(egui::DragValue::new(&mut state.result_id).range(1..=u32::MAX));
                action(ui,&mut out,"Poll result",format!("ir_result {}",state.result_id.max(1)));
                action(ui,&mut out,"Refresh status","ir_status");
            });
            ui.label("Wait for each configuration DONE / CONFIGURED, then refresh status before pulsing. Zero caps disable exposure; configuration resets at reboot.");
            ui.label("Pulse buttons require recent OFF status for both channels, rail_requested=1, stop_pending=0 and matching configured caps. Firmware validates every request again.");
            ui.label("QUEUED is not illumination. Poll the returned ID for DONE / EXPIRED; after STOP, wait for stop_pending=0 and reconciled=stop_sequence.");
        });
        out
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn ready() -> State {
        let mut s = State::default();
        s.feed("IR0 active=0 duty_pct=0 remaining_ms=0 pwm_hz=100000 max_duty_pct=10 max_on_ms=20\nIR1 active=0 duty_pct=0 remaining_ms=0 pwm_hz=100000 max_duty_pct=5 max_on_ms=10\nIR active_id=0 reason=NONE rail_requested=1 rail_feedback=UNAVAILABLE stop_pending=0 stop_sequence=0 reconciled=0 sequence=2\n");
        s
    }
    #[test]
    fn exposure_requires_verified_limits_and_both_channels_idle() {
        assert!(!State::default().can_pulse(0, 5, 10));
        let mut s = ready();
        assert!(s.can_pulse(0, 5, 10));
        assert!(s.can_pulse(1, 5, 10));
        assert!(!s.can_pulse(1, 6, 10));
        assert!(!s.can_pulse(0, 5, 21));
        assert!(!s.can_pulse(0, 26, 10));
        assert!(!s.can_pulse(0, 5, 101));
        assert!(!s.can_pulse(0, 0, 10));
        assert!(!s.can_pulse(0, 5, 0));
        s.channels[1].as_mut().unwrap().active = true;
        assert!(!s.can_pulse(0, 5, 10));
    }
    #[test]
    fn rail_stop_and_stale_data_disable_exposure() {
        let mut s = ready();
        s.owner = s.owner.replace("rail_requested=1", "rail_requested=0");
        assert!(!s.can_pulse(0, 5, 10));
        let mut s = ready();
        s.owner = s.owner.replace("stop_pending=0", "stop_pending=1");
        assert!(!s.can_pulse(0, 5, 10));
        let mut s = ready();
        s.updated[1] = Some(Instant::now() - std::time::Duration::from_secs(6));
        assert!(!s.can_pulse(0, 5, 10));
        let mut s = ready();
        s.owner = s.owner.replace("stop_sequence=0", "stop_sequence=3");
        assert!(!s.can_pulse(0, 5, 10));
        let mut s = ready();
        s.owner = s.owner.replace("active_id=0", "active_id=12");
        assert!(!s.can_pulse(0, 5, 10));
        let mut s = ready();
        s.channels[0].as_mut().unwrap().max_ms = 0;
        assert!(!s.can_pulse(0, 5, 10));
    }
    #[test]
    fn fragmented_replies_and_real_ir_ids_only() {
        let mut s = State::default();
        s.feed("IR0 active=0 duty_pct=0 remaining_ms=0 pwm_hz=100000 max_duty_pct=5 max_on_");
        assert!(s.channels[0].is_none());
        s.feed("ms=10\r\nIR QUEUED id=7\n");
        assert_eq!(s.result_id, 7);
        s.feed("ACCEPTED request=25 position=0\nIR INVALID id=0\n");
        assert_eq!(s.result_id, 7);
        s.feed("IR DONE id=7 channel=1 reason=EXPIRED started_ms=1 finished_ms=11\n");
        assert!(s.result.contains("reason=EXPIRED"));
        assert!(State::default().owner.is_empty());
    }
}
