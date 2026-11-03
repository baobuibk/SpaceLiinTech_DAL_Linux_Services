use crate::theme::{panel_frame, title, MUTED, YELLOW};
use eframe::egui;
use std::time::{Duration, Instant};

#[derive(Default)]
struct Probe {
    status: String,
    id: Option<u32>,
    deadline: Option<Instant>,
    next: Option<Instant>,
}
impl Probe {
    fn start(&mut self) {
        self.status = "Detecting...".into();
        self.id = None;
        self.deadline = Some(Instant::now() + Duration::from_secs(15));
        self.next = None;
    }
    fn feed(&mut self, line: &str, prefix: &str) {
        if self.deadline.is_none() {
            return;
        }
        let Some((_, rest)) = line.split_once(prefix) else {
            return;
        };
        let result = rest.split_whitespace().next().unwrap_or("");
        if ![
            "QUEUED",
            "ACCEPTED",
            "DONE",
            "FAILED",
            "BUSY",
            "NOT_READY",
            "DISABLED",
            "NOT_FOUND",
            "INVALID",
            "REFUSED",
            "IO_ERROR",
        ]
        .contains(&result)
        {
            return;
        }
        let id = rest.split_whitespace().find_map(|v| {
            v.strip_prefix("id=")?
                .trim_end_matches(';')
                .parse::<u32>()
                .ok()
        });
        if result == "QUEUED" && self.id.is_none() && id.is_some_and(|v| v > 0) {
            self.id = id;
            self.next = Some(Instant::now() + Duration::from_millis(500));
        } else if (self.id.is_some() && id == self.id) || (self.id.is_none() && result != "QUEUED")
        {
            if result != "QUEUED" && result != "ACCEPTED" {
                self.status = if result == "DONE" {
                    "Detected".into()
                } else {
                    format!("Unavailable: {rest}")
                };
                self.deadline = None;
            }
        }
    }
    fn poll(&mut self, command: &str) -> Option<String> {
        let now = Instant::now();
        if self.deadline.is_some_and(|end| now >= end) {
            self.status = "Unavailable: detection timed out".into();
            self.deadline = None;
        }
        if self.deadline.is_some() && self.next.is_some_and(|at| now >= at) {
            self.next = Some(now + Duration::from_millis(500));
            return self.id.map(|id| format!("{command} {id}"));
        }
        None
    }
}

#[derive(Default)]
pub struct State {
    pending: String,
    ntc: [String; 5],
    bme: [String; 3],
    imu: [String; 7],
    bme_probe: Probe,
    imu_probe: Probe,
    bme_info: String,
    imu_info: String,
    rtc_detection: String,
    rtc_info: String,
    rtc_time: String,
    rtc_date: String,
    rtc_fields: [String; 6], // hour, minute, second, day, month, YY
    rtc_set: bool,
    rtc_set_until: Option<Instant>,
    rtc_read_at: Option<Instant>,
    rtc_detect_until: Option<Instant>,
}
fn value(text: &str) -> &str {
    if text.is_empty() {
        "--"
    } else {
        text
    }
}
fn readings(ui: &mut egui::Ui, id: &str, names: &[&str], values: &[String]) {
    egui::Grid::new(id)
        .num_columns(2)
        .spacing([24.0, 10.0])
        .show(ui, |ui| {
            for (name, reading) in names.iter().zip(values) {
                ui.label(egui::RichText::new(*name).color(YELLOW));
                ui.label(value(reading));
                ui.end_row();
            }
        });
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
        self.bme_probe.feed(line, "BME688 ");
        self.imu_probe.feed(line, "LSM6DSOX ");
        for i in 0..5 {
            let prefix = if i == 4 {
                "BOARD_TEMP : ".into()
            } else {
                format!("NTC[{i}] : ")
            };
            if let Some((_, reading)) = line.split_once(&prefix) {
                self.ntc[i] = reading.into();
            }
        }
        if line.contains("Error: NTC database busy") {
            self.ntc = std::array::from_fn(|_| "Unavailable / database busy".into());
        }
        if let Some((_, sample)) = line.split_once("BME688 snapshot ") {
            self.bme_info = sample.into();
            self.bme = Default::default();
        }
        for (i, prefix) in ["[BME688] Temp :", "[BME688] Press:", "[BME688] Hum  :"]
            .iter()
            .enumerate()
        {
            if let Some((_, reading)) = line.split_once(prefix) {
                self.bme[i] = reading.trim().into();
            }
        }
        if line.contains("BME688 NOT_READY") {
            self.bme = Default::default();
            self.bme_info = "Unavailable / no valid fresh sample".into();
        }
        if line.starts_with("snapshot seq=") {
            self.imu_info = line.into();
            self.imu = Default::default();
        }
        for (base, prefix) in [(0, "Accel:"), (3, "Gyro:")] {
            if let Some((_, axes)) = line.split_once(prefix) {
                for (i, axis) in ["X=", "Y=", "Z="].iter().enumerate() {
                    if let Some((_, field)) = axes.split_once(axis) {
                        self.imu[base + i] = field
                            .split_whitespace()
                            .take(2)
                            .collect::<Vec<_>>()
                            .join(" ");
                    }
                }
            }
        }
        if let Some((_, reading)) = line.split_once("Temperature: ") {
            self.imu[6] = reading.into();
        }
        if line.contains("ERROR LSM6DSOX NOT_READY") {
            self.imu = Default::default();
            self.imu_info = line.into();
        }
        if self.rtc_detect_until.is_some() && (line == "RTC OK" || line == "RTC FAIL") {
            self.rtc_detection = if line == "RTC OK" {
                "Detected"
            } else {
                "Unavailable"
            }
            .into();
            self.rtc_detect_until = None;
        }
        if let Some((_, reading)) = line.split_once("TIME: ") {
            self.rtc_time = reading.into();
        }
        if let Some((_, reading)) = line.split_once("DATE: ") {
            self.rtc_date = reading.into();
        }
        if line.starts_with("RTC: ") {
            self.rtc_info = line.into();
            if self.rtc_set && line == "RTC: OK" {
                self.rtc_read_at = Some(Instant::now() + Duration::from_millis(500));
            } else if line != "RTC: OK" {
                self.rtc_time.clear();
                self.rtc_date.clear();
            }
            self.rtc_set = false;
            self.rtc_set_until = None;
        }
    }
    pub fn poll(&mut self) -> Vec<String> {
        let mut commands = Vec::new();
        if self.rtc_set_until.is_some_and(|at| Instant::now() >= at) {
            self.rtc_set_until = None;
            self.rtc_set = false;
            self.rtc_info = "Set response timed out; Read to verify hardware time".into();
        }
        if let Some(c) = self.bme_probe.poll("bme688_result") {
            commands.push(c);
        }
        if let Some(c) = self.imu_probe.poll("lsm6d_result") {
            commands.push(c);
        }
        if self.rtc_read_at.is_some_and(|at| Instant::now() >= at) {
            self.rtc_read_at = None;
            self.rtc_time.clear();
            self.rtc_date.clear();
            commands.push("rtc_read".into());
        }
        if self.rtc_detect_until.is_some_and(|at| Instant::now() >= at) {
            self.rtc_detect_until = None;
            self.rtc_detection = "Unavailable: detection timed out".into();
        }
        commands
    }
    pub fn render(&mut self, ui: &mut egui::Ui, page: &str, ready: bool) -> Vec<String> {
        let mut commands = Vec::new();
        panel_frame().show(ui, |ui| {
            ui.label(title(page));
            ui.add_enabled_ui(ready, |ui| {
                ui.horizontal(|ui| {
                    match page {
                        "NTC" => { if ui.button("Read all").clicked() { self.ntc = Default::default(); commands.push("ntc_read_all".into()); } }
                        "BME688" => {
                            if ui.button("Detect / initialize").clicked() { self.bme_probe.start(); commands.push("bme688_init".into()); }
                            if ui.button("Read").clicked() { self.bme = Default::default(); self.bme_info = "Waiting...".into(); commands.push("bme688_read_all".into()); }
                        }
                        "IMU" => {
                            if ui.button("Detect").clicked() { self.imu_probe.start(); commands.push("lsm6d_probe".into()); }
                            if ui.button("Initialize").clicked() { self.imu_probe.start(); commands.push("lsm6d_init".into()); }
                            if ui.button("Read").clicked() { self.imu = Default::default(); self.imu_info = "Waiting...".into(); commands.push("lsm6d_read_all".into()); }
                        }
                        "RTC" => {
                            if ui.button("Detect").clicked() { self.rtc_detection = "Detecting...".into(); self.rtc_detect_until = Some(Instant::now() + Duration::from_secs(10)); commands.push("rtc_ping".into()); }
                            if ui.button("Read").clicked() { self.rtc_time.clear(); self.rtc_date.clear(); commands.push("rtc_read".into()); }
                        }
                        _ => {}
                    }
                });
            });
            ui.separator();
            match page {
                "NTC" => readings(ui, "sensor_ntc", &["NTC 0", "NTC 1", "NTC 2", "NTC 3", "PCB temperature"], &self.ntc),
                "BME688" => {
                    ui.label(value(&self.bme_probe.status));
                    readings(ui, "sensor_bme", &["Temperature", "Pressure", "Humidity"], &self.bme);
                    ui.label(&self.bme_info);
                    ui.label(egui::RichText::new("Firmware exposes temperature, pressure and humidity; gas/IAQ is not available.").color(MUTED));
                }
                "IMU" => {
                    ui.label(value(&self.imu_probe.status));
                    readings(ui, "sensor_imu", &["Acceleration X", "Acceleration Y", "Acceleration Z", "Gyro X", "Gyro Y", "Gyro Z", "Temperature"], &self.imu);
                    ui.label(&self.imu_info);
                }
                "RTC" => {
                    ui.label(value(&self.rtc_detection));
                    readings(ui, "rtc_current", &["Time (UTC)", "Date"], &[self.rtc_time.clone(), self.rtc_date.clone()]);
                    ui.label(&self.rtc_info);
                    ui.separator();
                    ui.label("Set UTC datetime (year 00-99 means 2000-2099)");
                    egui::Grid::new("rtc_input").num_columns(2).show(ui, |ui| {
                        for (i, label) in ["Hour", "Minute", "Second", "Day", "Month", "Year (YY)"].iter().enumerate() {
                            ui.label(*label);
                            ui.add(egui::TextEdit::singleline(&mut self.rtc_fields[i]).desired_width(80.0));
                            ui.end_row();
                        }
                    });
                    if ui.add_enabled(ready, egui::Button::new("Set datetime")).clicked() {
                        match rtc_command(&self.rtc_fields) {
                            Ok(command) => { self.rtc_set = true; self.rtc_set_until = Some(Instant::now() + Duration::from_secs(10)); self.rtc_info = "Setting...".into(); commands.push(command); }
                            Err(error) => self.rtc_info = error.into(),
                        }
                    }
                    ui.label(egui::RichText::new("Last hardware read; Read refreshes it. Successful Set automatically reads back.").color(MUTED));
                }
                _ => {}
            }
        });
        commands
    }
}
fn rtc_command(fields: &[String; 6]) -> Result<String, &'static str> {
    let mut n = [0u32; 6];
    for i in 0..6 {
        n[i] = fields[i]
            .trim()
            .parse()
            .map_err(|_| "Enter all six numeric UTC fields")?;
    }
    let [h, m, s, d, month, y] = n;
    if h > 23 || m > 59 || s > 59 || !(1..=12).contains(&month) || y > 99 {
        return Err("Invalid UTC time/date");
    }
    let days = match month {
        2 => {
            if y % 4 == 0 {
                29
            } else {
                28
            }
        }
        4 | 6 | 9 | 11 => 30,
        _ => 31,
    };
    if d == 0 || d > days {
        return Err("Invalid calendar date");
    }
    Ok(format!("rtc_set {h} {m} {s} {d} {month} {y}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fragmented_sensor_fields_and_invalid_samples() {
        let mut state = State::default();
        state.feed("NTC[0] : 25.");
        state.feed("30 C age=1 ms raw=2048\nNTC[1] : INVALID\nNTC[2] : STALE\nNTC[3] : NO_SAMPLE\nBOARD_TEMP : 27.0 C\n");
        assert!(state.ntc.iter().all(|s| !s.is_empty()));
        state.feed("[BME688] Temp : 21 C\n[BME688] Press: 100000 Pa\n[BME688] Hum  : 40 %\nAccel: X=1 mg Y=2 mg Z=3 mg\nGyro: X=4 dps Y=5 dps Z=6 dps\nTemperature: 22 C\n");
        assert_eq!(state.bme[2], "40 %");
        assert_eq!(state.imu[5], "6 dps");
        state.feed("ERROR LSM6DSOX NOT_READY: stale\nBME688 NOT_READY: stale\n");
        assert!(state.imu.iter().all(String::is_empty));
        assert!(state.bme.iter().all(String::is_empty));
    }
    #[test]
    fn probe_requires_own_completion() {
        let mut p = Probe::default();
        p.start();
        p.feed("LSM6DSOX QUEUED id=4; query lsm6d_result", "LSM6DSOX ");
        p.feed("LSM6DSOX DONE id=3", "LSM6DSOX ");
        assert_eq!(p.status, "Detecting...");
        p.feed("LSM6DSOX DONE id=4 error=0", "LSM6DSOX ");
        assert_eq!(p.status, "Detected");
    }
    #[test]
    fn rtc_validation_and_readback() {
        let fields = ["12", "30", "0", "29", "2", "24"].map(String::from);
        assert_eq!(rtc_command(&fields).unwrap(), "rtc_set 12 30 0 29 2 24");
        let mut bad = fields;
        bad[5] = "25".into();
        assert!(rtc_command(&bad).is_err());
        let mut state = State::default();
        state.rtc_set = true;
        state.feed("RTC: OK\n");
        state.rtc_read_at = Some(Instant::now());
        assert_eq!(state.poll(), ["rtc_read"]);
        state.feed("TIME: 12:30:00\nDATE: 29/02/2024 weekday=4\n");
        assert_eq!(state.rtc_time, "12:30:00");
    }
}
