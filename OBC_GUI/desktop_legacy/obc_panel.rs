//! Local DAL OBC monitoring and explicit board utility actions.
use crate::{Page, GREEN, MUTED, YELLOW};
use eframe::egui;
use serde_json::Value;
use std::{
    fs,
    path::PathBuf,
    process::Command,
    sync::mpsc::{self, Receiver},
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

const RAILS: [&str; 8] = [
    "5V_SAMA7",
    "5V_FPGA",
    "3V3",
    "5V_AI",
    "PMIC_3V3",
    "PMIC_DDR_1V35",
    "PMIC_CORE_1V15",
    "PMIC_CPU_1V15",
];

pub struct ObcPanel {
    snapshot: Option<Value>,
    error: String,
    last_poll: Option<Instant>,
    command_result: String,
    command_rx: Option<Receiver<String>>,
    gpio_list: String,
    camera: usize,
    width: String,
    height: String,
    output: String,
    confirm_exp_off: bool,
}

impl Default for ObcPanel {
    fn default() -> Self {
        Self {
            snapshot: None,
            error: "Waiting for OBC telemetry".into(),
            last_poll: None,
            command_result: String::new(),
            command_rx: None,
            gpio_list: String::new(),
            camera: 1,
            width: "1280".into(),
            height: "720".into(),
            output: String::new(),
            confirm_exp_off: false,
        }
    }
}

fn run_dir() -> PathBuf {
    std::env::var_os("OBC_GUI_RUN_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/run/dal/hk_service"))
}

fn now_unix() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn age(snapshot: &Value, key: &str) -> Option<u64> {
    snapshot[key]
        .as_u64()
        .map(|time| now_unix().saturating_sub(time))
}

fn valid(snapshot: &Value, key: &str) -> bool {
    snapshot[key].as_i64() == Some(1)
}

fn number(value: &Value, unit: &str, digits: usize) -> String {
    value
        .as_f64()
        .map(|v| format!("{v:.digits$} {unit}"))
        .unwrap_or_else(|| "--".into())
}

fn measured(snapshot: &Value, validity: &str, key: &str, unit: &str, digits: usize) -> String {
    if valid(snapshot, validity) {
        number(&snapshot[key], unit, digits)
    } else {
        "--".into()
    }
}

fn field(ui: &mut egui::Ui, label: &str, value: String) {
    ui.label(label);
    ui.label(value);
    ui.end_row();
}

fn group(ui: &mut egui::Ui, title: &str, add: impl FnOnce(&mut egui::Ui)) {
    egui::Frame::group(ui.style()).show(ui, |ui| {
        ui.set_min_width(ui.available_width());
        ui.label(egui::RichText::new(title).strong().color(YELLOW));
        ui.separator();
        add(ui);
    });
    ui.add_space(8.0);
}

fn status(ui: &mut egui::Ui, name: &str, ok: bool, stamp: Option<u64>) {
    ui.horizontal(|ui| {
        ui.label(name);
        ui.label(
            egui::RichText::new(if ok { "VALID" } else { "UNAVAILABLE" })
                .color(if ok { GREEN } else { YELLOW })
                .strong(),
        );
        if let Some(seconds) = stamp {
            ui.label(format!("sample age {seconds}s"));
        }
    });
}

fn proc_value(path: &str) -> Option<String> {
    fs::read_to_string(path).ok()
}

impl ObcPanel {
    pub fn headline(&self) -> (&'static str, egui::Color32) {
        match self.snapshot.as_ref() {
            Some(snapshot) if age(snapshot, "obc_timestamp").is_some_and(|s| s <= 30) => {
                ("OBC TELEMETRY LIVE", GREEN)
            }
            Some(_) => ("OBC TELEMETRY STALE", YELLOW),
            None => ("OBC TELEMETRY OFFLINE", MUTED),
        }
    }

    pub fn tick(&mut self) {
        if self
            .last_poll
            .map_or(true, |at| at.elapsed() >= Duration::from_secs(1))
        {
            self.last_poll = Some(Instant::now());
            match fs::read(run_dir().join("obc_data.json")) {
                Ok(data) => match serde_json::from_slice::<Value>(&data) {
                    Ok(value) if value["board"] == "obc" && value["schema_version"] == 1 => {
                        self.snapshot = Some(value);
                        self.error.clear();
                    }
                    Ok(_) => {
                        self.snapshot = None;
                        self.error = "Unsupported OBC snapshot schema".into();
                    }
                    Err(error) => {
                        self.snapshot = None;
                        self.error = format!("Invalid OBC snapshot: {error}");
                    }
                },
                Err(error) => {
                    self.snapshot = None;
                    self.error = format!("OBC snapshot unavailable: {error}");
                }
            }
        }
        if let Some(rx) = &self.command_rx {
            if let Ok(result) = rx.try_recv() {
                if result.starts_with("xio list\n") {
                    self.gpio_list = result.clone();
                }
                self.command_result = result;
                self.command_rx = None;
            }
        }
    }

    fn execute(&mut self, label: &'static str, program: PathBuf, args: Vec<String>) {
        if self.command_rx.is_some() {
            return;
        }
        self.command_result = format!("Running {label}...");
        let (tx, rx) = mpsc::channel();
        self.command_rx = Some(rx);
        thread::spawn(move || {
            let result = Command::new(&program).args(&args).output();
            let message = match result {
                Ok(output) => {
                    let stdout = String::from_utf8_lossy(&output.stdout);
                    let stderr = String::from_utf8_lossy(&output.stderr);
                    let state = if output.status.success() {
                        "OK"
                    } else {
                        "FAILED"
                    };
                    format!("{label}\n{state} ({})\n{}{}", output.status, stdout, stderr)
                }
                Err(error) => format!("{label}\nFAILED: {error}"),
            };
            let _ = tx.send(message.chars().take(12_000).collect());
        });
    }

    fn xio(&mut self, args: &[&str]) {
        let path = std::env::var_os("OBC_GUI_XIO")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("/home/root/tools/bin/xio"));
        let label = if args == ["list"] {
            "xio list"
        } else {
            "xio control"
        };
        self.execute(label, path, args.iter().map(|s| s.to_string()).collect());
    }

    fn capture(&mut self) {
        let width = self
            .width
            .parse::<u32>()
            .ok()
            .filter(|n| (1..=4096).contains(n));
        let height = self
            .height
            .parse::<u32>()
            .ok()
            .filter(|n| (1..=4096).contains(n));
        let (Some(width), Some(height)) = (width, height) else {
            self.command_result = "Camera dimensions must be 1..4096".into();
            return;
        };
        let output = if self.output.trim().is_empty() {
            format!("/home/bee/data/camera{}-{}.jpg", self.camera, now_unix())
        } else {
            self.output.trim().to_string()
        };
        if !PathBuf::from(&output).is_absolute() {
            self.command_result = "Camera output must be an absolute path".into();
            return;
        }
        let path = std::env::var_os("OBC_GUI_USB_CAM")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("/home/root/tools/bin/usb_cam"));
        self.execute(
            "USB camera capture",
            path,
            vec![
                self.camera.to_string(),
                "--width".into(),
                width.to_string(),
                "--height".into(),
                height.to_string(),
                "-o".into(),
                output,
            ],
        );
    }

    pub fn show(&mut self, ui: &mut egui::Ui, page: Page) {
        let snapshot = self.snapshot.clone();
        if let Some(s) = &snapshot {
            let seconds = age(s, "obc_timestamp").unwrap_or(u64::MAX);
            status(ui, "OBC housekeeping", seconds <= 30, Some(seconds));
            if seconds > 30 {
                ui.colored_label(
                    YELLOW,
                    "Telemetry is stale; displayed values may no longer reflect the board.",
                );
            }
        } else {
            ui.colored_label(YELLOW, &self.error);
        }
        ui.add_space(8.0);
        match page {
            Page::ObcOverview => self.overview(ui, snapshot.as_ref()),
            Page::ObcSensors => self.sensors(ui, snapshot.as_ref()),
            Page::ObcPower => self.power(ui, snapshot.as_ref()),
            Page::ObcPeripherals => self.peripherals(ui),
            _ => unreachable!(),
        }
        if !self.command_result.is_empty() {
            group(ui, "LAST OBC ACTION", |ui| {
                ui.label(egui::RichText::new(&self.command_result).monospace());
            });
        }
    }

    fn overview(&mut self, ui: &mut egui::Ui, snapshot: Option<&Value>) {
        group(ui, "LINUX SYSTEM", |ui| {
            egui::Grid::new("obc_system").num_columns(2).show(ui, |ui| {
                let uptime = proc_value("/proc/uptime")
                    .and_then(|s| s.split_whitespace().next()?.parse::<f64>().ok())
                    .map(|s| format!("{:.1} h", s / 3600.0))
                    .unwrap_or_else(|| "--".into());
                field(ui, "Uptime", uptime);
                let load = proc_value("/proc/loadavg")
                    .map(|s| s.split_whitespace().take(3).collect::<Vec<_>>().join(" / "))
                    .unwrap_or_else(|| "--".into());
                field(ui, "Load (1 / 5 / 15 min)", load);
                let available = proc_value("/proc/meminfo").and_then(|s| {
                    s.lines()
                        .find(|line| line.starts_with("MemAvailable:"))
                        .and_then(|line| line.split_whitespace().nth(1))
                        .and_then(|v| v.parse::<u64>().ok())
                });
                field(
                    ui,
                    "Memory available",
                    available
                        .map(|kb| format!("{} MiB", kb / 1024))
                        .unwrap_or_else(|| "--".into()),
                );
                if let Some(s) = snapshot {
                    let boot_count = s["boot_count"]
                        .as_u64()
                        .map(|n| n.to_string())
                        .or_else(|| s["boot_count"].as_str().map(str::to_string))
                        .unwrap_or_else(|| "--".into());
                    field(ui, "Boot count", boot_count);
                }
            });
        });
        group(ui, "OBC HEALTH", |ui| {
            if let Some(s) = snapshot {
                status(
                    ui,
                    "Temperature / ADC",
                    valid(s, "temp_valid"),
                    age(s, "temp_sample_unix_s"),
                );
                status(
                    ui,
                    "BME688",
                    valid(s, "bme_valid"),
                    age(s, "bme_sample_unix_s"),
                );
                status(
                    ui,
                    "PAC1934 U6",
                    valid(s, "pac_u6_valid"),
                    age(s, "pac_sample_unix_s"),
                );
                status(
                    ui,
                    "PAC1934 U2",
                    valid(s, "pac_u2_valid"),
                    age(s, "pac_sample_unix_s"),
                );
                ui.separator();
                ui.label(format!(
                    "Onboard: {}",
                    measured(s, "temp_valid", "temp_onboard_c", "°C", 1)
                ));
                ui.label(format!(
                    "BME688: {}  /  {}  /  {}",
                    measured(s, "bme_valid", "bme_temp_c", "°C", 1),
                    measured(s, "bme_valid", "bme_pressure_pa", "Pa", 0),
                    measured(s, "bme_valid", "bme_humidity_rh", "%RH", 1)
                ));
            } else {
                ui.label("Start hk_service, temp_service and bme_service to populate telemetry.");
            }
        });
        group(ui, "BOARDS", |ui| {
            ui.label("OBC: local service snapshot");
            let exp = fs::read_to_string(run_dir().join("exp_status"))
                .unwrap_or_else(|_| "unknown".into());
            ui.label(format!("EXP 11: {}", exp.trim()));
            ui.label(
                egui::RichText::new("Use the EXP tabs for its UART/RS485/CAN controls.")
                    .color(MUTED),
            );
        });
    }

    fn sensors(&mut self, ui: &mut egui::Ui, snapshot: Option<&Value>) {
        let Some(s) = snapshot else {
            return;
        };
        group(ui, "ONBOARD + EXTERNAL TEMPERATURE", |ui| {
            status(
                ui,
                "ADC",
                valid(s, "temp_valid"),
                age(s, "temp_sample_unix_s"),
            );
            egui::Grid::new("obc_temp").num_columns(2).show(ui, |ui| {
                field(
                    ui,
                    "Onboard TC1047A",
                    measured(s, "temp_valid", "temp_onboard_c", "°C", 2),
                );
                for n in 0..4 {
                    let value = if valid(s, "temp_valid") {
                        number(&s["temp_ntc_c"][n], "°C", 2)
                    } else {
                        "--".into()
                    };
                    field(ui, &format!("NTC {}", n + 1), value);
                }
            });
        });
        group(ui, "BME688 ENVIRONMENT", |ui| {
            status(
                ui,
                "BME688",
                valid(s, "bme_valid"),
                age(s, "bme_sample_unix_s"),
            );
            egui::Grid::new("obc_bme").num_columns(2).show(ui, |ui| {
                field(
                    ui,
                    "Temperature",
                    measured(s, "bme_valid", "bme_temp_c", "°C", 2),
                );
                field(
                    ui,
                    "Pressure",
                    measured(s, "bme_valid", "bme_pressure_pa", "Pa", 0),
                );
                field(
                    ui,
                    "Humidity",
                    measured(s, "bme_valid", "bme_humidity_rh", "%RH", 2),
                );
                field(
                    ui,
                    "Gas resistance",
                    if valid(s, "bme_valid") && valid(s, "bme_gas_valid") {
                        number(&s["bme_gas_resistance_ohm"], "Ω", 0)
                    } else {
                        "INVALID / heater or sample unavailable".into()
                    },
                );
            });
        });
    }

    fn power(&mut self, ui: &mut egui::Ui, snapshot: Option<&Value>) {
        if let Some(s) = snapshot {
            group(ui, "PAC1934 RAILS", |ui| {
                ui.label("U6: first four rails. U2: last four rails. Values are measured, not switch states.");
                egui::Grid::new("obc_rails")
                    .num_columns(4)
                    .striped(true)
                    .show(ui, |ui| {
                        for head in ["Rail", "Voltage", "Current", "Power"] {
                            ui.strong(head);
                        }
                        ui.end_row();
                        for (i, rail) in RAILS.iter().enumerate() {
                            let chip_valid = valid(
                                s,
                                if i < 4 {
                                    "pac_u6_valid"
                                } else {
                                    "pac_u2_valid"
                                },
                            );
                            ui.label(*rail);
                            let volts = s["pwr_volt_v"][i].as_f64();
                            let amps = s["pwr_curr_a"][i].as_f64();
                            if chip_valid {
                                ui.label(number(&s["pwr_volt_v"][i], "V", 3));
                                ui.label(number(&s["pwr_curr_a"][i], "A", 3));
                                ui.label(match (volts, amps) {
                                    (Some(v), Some(a)) => format!("{:.3} W", v * a),
                                    _ => "--".into(),
                                });
                            } else {
                                for _ in 0..3 {
                                    ui.label("--");
                                }
                            }
                            ui.end_row();
                        }
                    });
            });
        }
        group(ui, "EXP POWER (OBC GPIO)", |ui| {
            ui.label("Controls pow_onoff_5v_exp1 through the installed xio utility.");
            ui.horizontal(|ui| {
                if ui
                    .add_enabled(self.command_rx.is_none(), egui::Button::new("Power ON"))
                    .clicked()
                {
                    self.xio(&["on", "pow_onoff_5v_exp1"]);
                }
                ui.checkbox(&mut self.confirm_exp_off, "Confirm power off");
                if ui
                    .add_enabled(
                        self.command_rx.is_none() && self.confirm_exp_off,
                        egui::Button::new("Power OFF"),
                    )
                    .clicked()
                {
                    self.confirm_exp_off = false;
                    self.xio(&["off", "pow_onoff_5v_exp1"]);
                }
            });
        });
    }

    fn peripherals(&mut self, ui: &mut egui::Ui) {
        group(ui, "GPIO AND SWITCH STATUS", |ui| {
            if ui
                .add_enabled(
                    self.command_rx.is_none(),
                    egui::Button::new("Refresh xio list"),
                )
                .clicked()
            {
                self.xio(&["list"]);
            }
            if !self.gpio_list.is_empty() {
                egui::ScrollArea::vertical()
                    .max_height(240.0)
                    .show(ui, |ui| {
                        ui.label(egui::RichText::new(&self.gpio_list).monospace());
                    });
            }
        });
        group(ui, "USB CAMERA", |ui| {
            ui.label("usb_cam handles power, SEL1/SEL0, active-low OE, enumeration and one JPEG capture.");
            ui.horizontal(|ui| {
                ui.label("Camera");
                for n in 1..=4 {
                    ui.selectable_value(&mut self.camera, n, n.to_string());
                }
            });
            ui.horizontal(|ui| {
                ui.label("Width");
                ui.text_edit_singleline(&mut self.width);
                ui.label("Height");
                ui.text_edit_singleline(&mut self.height);
            });
            ui.horizontal(|ui| {
                ui.label("Output (optional)");
                ui.text_edit_singleline(&mut self.output);
            });
            ui.label(
                egui::RichText::new("Default: /home/bee/data/cameraN-<unix time>.jpg").color(MUTED),
            );
            if ui
                .add_enabled(self.command_rx.is_none(), egui::Button::new("Capture JPEG"))
                .clicked()
            {
                self.capture();
            }
        });
        group(ui, "AVAILABLE DEVICE NODES", |ui| {
            for (name, path) in [
                ("I²C-0 sensors", "/dev/i2c-0"),
                ("CAT93 EEPROM SPI", "/dev/spidev0.0"),
                ("S25 FPGA flash SPI", "/dev/spidev2.0"),
                ("CAN 0", "/sys/class/net/can0"),
                ("CAN 1", "/sys/class/net/can1"),
            ] {
                ui.horizontal(|ui| {
                    ui.label(name);
                    ui.label(
                        egui::RichText::new(if std::path::Path::new(path).exists() {
                            "PRESENT"
                        } else {
                            "ABSENT"
                        })
                        .color(if std::path::Path::new(path).exists() {
                            GREEN
                        } else {
                            MUTED
                        }),
                    );
                });
            }
            ui.label(
                egui::RichText::new("Node presence does not prove a device responds.").color(MUTED),
            );
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn invalid_sensor_value_is_not_displayed_as_zero() {
        let sample = serde_json::json!({"bme_gas_valid":0,"bme_gas_resistance_ohm":null});
        assert!(!valid(&sample, "bme_gas_valid"));
        assert_eq!(number(&sample["bme_gas_resistance_ohm"], "Ω", 0), "--");
    }
}
