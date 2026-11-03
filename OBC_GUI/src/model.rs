use serde_json::Value;
use std::{io::{Read,Write}, os::unix::net::UnixStream,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH}};

pub const RAILS: [&str; 8] = [
    "5V_SAMA7",
    "5V_FPGA",
    "3V3",
    "5V_AI",
    "PMIC_3V3",
    "PMIC_DDR_1V35",
    "PMIC_CORE_1V15",
    "PMIC_CPU_1V15",
];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tab {
    Home,
    ObcSensors,
    ObcPower,
    Peripherals,
    MipiCameras,
    ExpStatus,
    ExpControl,
    ExpDevices,
}

impl Tab {
    pub const ALL: [Self; 8] = [
        Self::Home,
        Self::ObcSensors,
        Self::ObcPower,
        Self::Peripherals,
        Self::MipiCameras,
        Self::ExpStatus,
        Self::ExpControl,
        Self::ExpDevices,
    ];
    pub fn title(self) -> &'static str {
        match self {
            Self::Home => "HOME",
            Self::ObcSensors => "OBC SENSORS",
            Self::ObcPower => "OBC POWER",
            Self::Peripherals => "PERIPHERALS",
            Self::MipiCameras => "MIPI CAMERAS",
            Self::ExpStatus => "EXP STATUS",
            Self::ExpControl => "EXP CONTROL",
            Self::ExpDevices => "EXP",
        }
    }
    pub fn next(self) -> Self {
        Self::ALL[(self.index() + 1) % Self::ALL.len()]
    }
    pub fn previous(self) -> Self {
        Self::ALL[(self.index() + Self::ALL.len() - 1) % Self::ALL.len()]
    }
    pub fn index(self) -> usize {
        Self::ALL.iter().position(|t| *t == self).unwrap()
    }
}

pub fn flight_socket() -> String {
    std::env::var("OBC_GUI_FLIGHT_SOCKET")
        .unwrap_or_else(|_| "/run/dal/dal_obc_fsw/console.sock".into())
}

pub fn unix_now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

pub fn flag(v: &Value, key: &str) -> bool {
    v[key].as_i64() == Some(1)
}
fn group_valid(v: &Value, group: usize) -> bool {
    let bit = 1_u64 << group;
    v["sensor_supported_mask"]
        .as_u64()
        .is_some_and(|mask| mask & bit != 0)
        && v["sensor_valid_mask"]
            .as_u64()
            .is_some_and(|mask| mask & bit != 0)
}
pub fn age(v: &Value, key: &str) -> Option<u64> {
    v[key].as_u64().map(|t| unix_now().saturating_sub(t))
}
pub fn reading(v: &Value, flag_key: &str, key: &str, unit: &str, digits: usize) -> String {
    if !flag(v, flag_key) {
        return "--".into();
    }
    v[key]
        .as_f64()
        .map(|n| format!("{n:.digits$}{unit}"))
        .unwrap_or_else(|| "--".into())
}
pub fn sample(v: &Value, valid_key: &str, time_key: &str) -> String {
    let state = if flag(v, valid_key) {
        "VALID"
    } else {
        "UNAVAILABLE"
    };
    match age(v, time_key) {
        Some(n) => format!("{state}, age {n}s"),
        None => state.into(),
    }
}
fn array_num(v: &Value, index: usize, digits: usize, unit: &str) -> String {
    v[index]
        .as_f64()
        .map(|n| format!("{n:.digits$}{unit}"))
        .unwrap_or_else(|| "--".into())
}
fn bool_state(on: Option<bool>) -> &'static str {
    match on {
        Some(true) => "ON",
        Some(false) => "OFF",
        None => "UNKNOWN",
    }
}
fn exp_reading(v: &Value, group: usize, key: &str, unit: &str, digits: usize) -> String {
    if !group_valid(v, group) {
        return "--".into();
    }
    v[key]
        .as_f64()
        .map(|n| format!("{n:.digits$}{unit}"))
        .unwrap_or_else(|| "--".into())
}
fn exp_rail_state(v: &Value, i: usize) -> &'static str {
    if let Some(switch) = [Some(2), Some(0), None, Some(1)].get(i).copied().flatten() {
        return match v["rail_enabled"][switch].as_u64() {
            Some(1) => "ON",
            Some(0) => "OFF",
            _ => "UNKNOWN",
        };
    }
    if !group_valid(v, 7) {
        return "UNKNOWN";
    }
    match v["pwr_volt_mv"][i].as_u64() {
        Some(n) if n >= 500 => "ON",
        Some(_) => "OFF",
        None => "UNKNOWN",
    }
}
fn flight_state() -> Result<Value,String> {
    let path=flight_socket();
    let mut socket=UnixStream::connect(&path).map_err(|e|format!("Flight socket {path}: {e}"))?;
    socket.set_read_timeout(Some(Duration::from_secs(2))).map_err(|e|e.to_string())?;
    socket.write_all(b"gui_state\n").map_err(|e|e.to_string())?;
    socket.shutdown(std::net::Shutdown::Write).map_err(|e|e.to_string())?;
    let mut bytes=Vec::new();
    socket.take(131072).read_to_end(&mut bytes).map_err(|e|e.to_string())?;
    let state:Value=serde_json::from_slice(&bytes).map_err(|e|format!("Flight JSON: {e}"))?;
    if state["ok"]!=true {return Err("Flight software rejected gui_state".into());}
    Ok(state)
}

pub struct Snapshots {
    pub obc: Result<Value, String>,
    pub exp: Result<Value, String>,
    pub exp_uart: Result<Value, String>,
    pub pdu: Result<Value, String>,
    pub rtc: String,
    pub simulated: bool,
    last_read: Option<Instant>,
}
impl Default for Snapshots {
    fn default() -> Self {
        Self {
            obc: Err("Waiting for OBC data".into()),
            exp: Err("Waiting for EXP data".into()),
            exp_uart: Err("Waiting for EXP UART data".into()),
            pdu: Err("Waiting for PDU poll".into()),
            rtc: "--".into(),
            simulated: false,
            last_read: None,
        }
    }
}
impl Snapshots {
    pub fn gpio_state(&self, name: &str) -> &'static str {
        bool_state(self.obc.as_ref().ok().and_then(|v|v["gpio_states"][name].as_bool()))
    }
    pub fn pwm_duty(&self) -> String {
        self.obc.as_ref().ok().and_then(|v|v["camera_led_duty_pct"].as_u64())
            .map(|n|format!("{} {n}%{}",if n==0 {"OFF"} else {"ON"},if self.simulated {" (simulated)"} else {""}))
            .unwrap_or_else(||"unavailable".into())
    }
    pub fn refresh(&mut self, force: bool) {
        if self.simulated {
            return;
        }
        if !force
            && self
                .last_read
                .is_some_and(|t| t.elapsed() < Duration::from_secs(1))
        {
            return;
        }
        self.last_read = Some(Instant::now());
        match flight_state() {
            Ok(mut state) => {
                let mut obc=state["obc"].take();
                let hk_valid=obc["board"]=="obc" && obc["schema_version"]==1;
                if !hk_valid {obc=serde_json::json!({"board":"obc","schema_version":1});}
                if obc.is_object() {
                    obc["gpio_states"]=state["gpio"].take();
                    obc["camera_led_duty_pct"]=state["camera_led_duty_pct"].take();
                    obc["devices"]=state["devices"].take();
                    obc["mipi_route_board"]=state["mipi"]["board"].take();
                    obc["mipi_route_lane"]=state["mipi"]["lane"].take();
                    obc["mipi_route_sensor"]=state["mipi"]["sensor"].take();
                    obc["hk_unavailable"]=Value::Bool(!hk_valid);
                    self.obc=Ok(obc);
                } else {self.obc=Err("Flight OBC telemetry unavailable".into());}
                let exp=state["exp"].take();
                self.exp=if exp["board"]=="exp" && exp["schema_version"]==1 {Ok(exp)}
                    else {Err("Flight EXP telemetry unavailable".into())};
                let uart=state["exp_uart"].take();
                self.exp_uart=if uart.is_object() {Ok(uart)}
                    else {Err("Flight EXP UART telemetry unavailable".into())};
                let pdu=state["pdu"].take();
                self.pdu=if pdu["board"]=="pdu" && pdu["schema_version"]==1 {Ok(pdu)}
                    else {Err("Flight PDU telemetry unavailable".into())};
                self.rtc=state["rtc"].as_str().unwrap_or("unavailable").into();
            }
            Err(e) => {self.obc=Err(e.clone());self.exp=Err(e.clone());
                self.exp_uart=Err(e.clone());self.pdu=Err(e);self.rtc="unavailable".into();}
        }
    }
    pub fn set_simulated(&mut self, obc: Value, exp: Value, pdu: Value) {
        self.simulated = true;
        self.obc = Ok(obc);
        self.exp = Ok(exp);
        self.exp_uart = Ok(serde_json::json!({
            "system":{"schema_version":2,"section":"system","data":{"simulated":true}},
            "sensors":{"schema_version":2,"section":"sensors","data":{"simulated":true}},
            "power":{"schema_version":2,"section":"power","data":{"simulated":true}}
        }));
        self.pdu = Ok(pdu);
        self.rtc = format!(
            "Unix {} UTC (simulated)",
            self.obc
                .as_ref()
                .ok()
                .and_then(|v| v["rtc_epoch"].as_u64())
                .unwrap_or(0)
        );
    }
    pub fn status(&self, exp: bool) -> String {
        let value = if exp { &self.exp } else { &self.obc };
        let uart_live = exp && self.exp_uart.as_ref().ok().is_some_and(|v|
            !v["system"].is_null() &&
            v["system_age_s"].as_i64().is_some_and(|age| (0..=30).contains(&age)));
        match value {
            Ok(v) => {
                let stamp = age(v, "obc_timestamp");
                let live = uart_live ||
                    (stamp.is_some_and(|n| n <= 30) && (!exp || flag(v, "online")));
                format!(
                    "{} {}",
                    if exp {
                        if self.simulated {
                            "EXP 11 SIM"
                        } else {
                            "EXP 11"
                        }
                    } else if self.simulated {
                        "OBC SIM"
                    } else {
                        "OBC"
                    },
                    if live { "ONLINE" } else { "OFFLINE" }
                )
            }
            Err(_) => format!("{} {}", if exp { "EXP 11" } else { "OBC" },
                             if uart_live { "ONLINE" } else { "OFFLINE" }),
        }
    }
    pub fn pdu_status(&self) -> String {
        match &self.pdu {
            Ok(v) if age(v, "obc_timestamp").is_some_and(|n| n <= 15) => "PDU ONLINE".into(),
            _ => "PDU OFFLINE".into(),
        }
    }
    pub fn lines(&self, tab: Tab) -> Vec<String> {
        match tab {
            Tab::Home => self.home(),
            Tab::ObcSensors => self.obc_sensors(),
            Tab::ObcPower => self.obc_power(),
            Tab::Peripherals => self.peripherals(),
            Tab::MipiCameras => self.mipi_cameras(1, 0),
            Tab::ExpStatus => self.exp_status(),
            Tab::ExpControl => self.exp_control(),
            Tab::ExpDevices => vec![
                "EXP device controls copied from exp_test_gui.".into(),
                "Select a command at right. Actions with arguments open an editor.".into(),
                "PgUp/PgDn moves ten actions; Home/End jumps to the list ends.".into(),
                "The flight software sends the command on the EXP UART and waits for its reply.".into(),
            ],
        }
    }
    /// EXP CAN RPARAM snapshot from flight software, one text line per entry.
    pub fn exp_rparam_lines(&self) -> Vec<String> {
        let mut v = vec![format!("EXP {}", self.status(true))];
        match &self.exp {
            Ok(snapshot) => {
                if let Ok(pretty) = serde_json::to_string_pretty(snapshot) {
                    v.extend(pretty.lines().map(str::to_owned));
                }
            }
            Err(error) => v.push(format!("Unavailable: {error}")),
        }
        v
    }
    pub fn home_cards(&self) -> [Vec<String>; 3] {
        let mut obc = vec![self.status(false).replace("LIVE", "ONLINE")];
        match &self.obc {
            Ok(v) => {
                obc.push(format!(
                    "Board: {}",
                    reading(v, "temp_valid", "temp_onboard_c", " C", 1)
                ));
                obc.push(format!(
                    "BME: {} / {} %RH",
                    reading(v, "bme_valid", "bme_temp_c", " C", 1),
                    reading(v, "bme_valid", "bme_humidity_rh", "", 1)
                ));
                obc.push(format!(
                    "Pressure: {}",
                    reading(v, "bme_valid", "bme_pressure_pa", " Pa", 0)
                ));
                obc.push(format!(
                    "Gas: {}",
                    if flag(v, "bme_valid") && flag(v, "bme_gas_valid") {
                        v["bme_gas_resistance_ohm"]
                            .as_f64()
                            .map(|n| format!("{n:.0} ohm"))
                            .unwrap_or_else(|| "--".into())
                    } else {
                        "--".into()
                    }
                ));
                obc.push(format!(
                    "NTC 1/2: {} / {} C",
                    if flag(v, "temp_valid") {
                        array_num(&v["temp_ntc_c"], 0, 1, "")
                    } else {
                        "--".into()
                    },
                    if flag(v, "temp_valid") {
                        array_num(&v["temp_ntc_c"], 1, 1, "")
                    } else {
                        "--".into()
                    }
                ));
                obc.push(format!(
                    "PAC U6/U2: {} / {}",
                    if flag(v, "pac_u6_valid") {
                        "VALID"
                    } else {
                        "--"
                    },
                    if flag(v, "pac_u2_valid") {
                        "VALID"
                    } else {
                        "--"
                    }
                ));
                obc.push(format!("AI eFuse: {}", self.gpio_state("pow_onoff_ai")));
                obc.push(format!("FPGA eFuse: {}", self.gpio_state("pow_onoff_fpga")));
                for n in 1..=4 {
                    obc.push(format!(
                        "Camera {n} USB: {}",
                        self.gpio_state(&format!("usb_{n}_pwr_en"))
                    ));
                }
                obc.push(format!("RTC: {}", self.rtc));
            }
            Err(e) => obc.push(e.clone()),
        }
        let mut exp = vec![self.status(true).replace("LIVE", "ONLINE")];
        match &self.exp {
            Ok(v) if flag(v, "data_valid") && age(v, "obc_timestamp").is_some_and(|n| n <= 30) => {
                exp.push(format!(
                    "Board: {}",
                    exp_reading(v, 4, "board_temp_c", " C", 1)
                ));
                exp.push(format!(
                    "BME: {} / {} %RH",
                    exp_reading(v, 5, "bme_temp_c", " C", 1),
                    exp_reading(v, 5, "bme_hum_rh", "", 1)
                ));
                exp.push(format!(
                    "Flow 1/2: {} / {} uL/min",
                    if group_valid(v, 8) {
                        array_num(&v["flow_ul_min"], 0, 1, "")
                    } else {
                        "--".into()
                    },
                    if group_valid(v, 9) {
                        array_num(&v["flow_ul_min"], 1, 1, "")
                    } else {
                        "--".into()
                    }
                ));
                exp.push(format!(
                    "NTC 1/2: {} / {} C",
                    if group_valid(v, 0) {
                        array_num(&v["ntc_c"], 0, 1, "")
                    } else {
                        "--".into()
                    },
                    if group_valid(v, 1) {
                        array_num(&v["ntc_c"], 1, 1, "")
                    } else {
                        "--".into()
                    }
                ));
                exp.push(format!(
                    "IMU: {}",
                    if group_valid(v, 6) { "VALID" } else { "--" }
                ));
                for i in 0..4 {
                    exp.push(format!(
                        "{}: {} / {} mV",
                        v["pwr_rail_names"][i].as_str().unwrap_or("rail"),
                        exp_rail_state(v, i),
                        if group_valid(v, 7) {
                            array_num(&v["pwr_volt_mv"], i, 0, "")
                        } else {
                            "--".into()
                        }
                    ));
                }
                for (name, index) in [("12V_EXP", 0), ("5V_SLN_HD", 1), ("12V_MOTOR", 2)] {
                    exp.push(format!(
                        "{name} switch: {}",
                        match v["rail_enabled"][index].as_u64() {
                            Some(1) => "ON",
                            Some(0) => "OFF",
                            _ => "UNKNOWN",
                        }
                    ));
                }
            }
            Ok(_) => exp.push("RPARAM sensor data unavailable".into()),
            Err(e) => exp.push(e.clone()),
        }
        if let Ok(uart) = &self.exp_uart {
            exp.push(format!("UART telemetry: system {} / sensors {} / power {}",
                if uart["system"].is_null() {"--"} else {"OK"},
                if uart["sensors"].is_null() {"--"} else {"OK"},
                if uart["power"].is_null() {"--"} else {"OK"}));
        }
        let mut pdu = vec![self.pdu_status()];
        match &self.pdu {
            Ok(v) if age(v, "obc_timestamp").is_some_and(|n| n <= 15) => {
                pdu.push(format!(
                    "Board: {} C",
                    v["board_temp_c"]
                        .as_f64()
                        .map(|n| format!("{n:.1}"))
                        .unwrap_or_else(|| "--".into())
                ));
                pdu.push(format!("Mode: {}", v["mode"].as_str().unwrap_or("--")));
                for (name, key, volts, amps) in [
                    ("VOUT", "vout_on", "vout_mv", "vout_ma"),
                    ("+5_TRANS", "trans_on", "trans_mv", "trans_ma"),
                ] {
                    pdu.push(format!(
                        "{name}: {} / {} mV / {} mA",
                        bool_state(v[key].as_bool()),
                        v[volts]
                            .as_u64()
                            .map(|n| n.to_string())
                            .unwrap_or_else(|| "--".into()),
                        v[amps]
                            .as_u64()
                            .map(|n| n.to_string())
                            .unwrap_or_else(|| "--".into())
                    ));
                }
                pdu.push(format!(
                    "VOUT set: {} mV",
                    v["vout_set_mv"]
                        .as_u64()
                        .map(|n| n.to_string())
                        .unwrap_or_else(|| "--".into())
                ));
                pdu.push(format!(
                    "+12/+5 in: {} / {} mV",
                    v["input_12v_mv"]
                        .as_u64()
                        .map(|n| n.to_string())
                        .unwrap_or_else(|| "--".into()),
                    v["input_5v_mv"]
                        .as_u64()
                        .map(|n| n.to_string())
                        .unwrap_or_else(|| "--".into())
                ));
                if let Some(flags) = v["status_flags"].as_u64() {
                    pdu.push(format!(
                        "Faults: {}",
                        if flags & 0x7c == 0 {
                            "NONE"
                        } else {
                            "CHECK STATUS"
                        }
                    ));
                }
            }
            Ok(_) => pdu.push("Telemetry stale".into()),
            Err(e) => pdu.push(e.clone()),
        }
        [obc, exp, pdu]
    }
    fn home(&self) -> Vec<String> {
        let mut lines=vec![if self.simulated {"SIMULATED FLIGHT TELEMETRY".into()}
            else {"FLIGHT SOFTWARE TELEMETRY".into()}];
        lines.push(String::new());
        lines.push(self.status(false).replace("LIVE", "ONLINE"));
        match &self.obc {
            Ok(v) => {
                lines.push(format!("  Snapshot: {}", self.status(false)));
                lines.push(format!("  RTC: {}", self.rtc));
                lines.push(format!(
                    "  ADC: {}",
                    sample(v, "temp_valid", "temp_sample_unix_s")
                ));
                lines.push(format!(
                    "  BME688: {}",
                    sample(v, "bme_valid", "bme_sample_unix_s")
                ));
                lines.push(format!(
                    "  PAC U6: {}",
                    sample(v, "pac_u6_valid", "pac_sample_unix_s")
                ));
                lines.push(format!(
                    "  PAC U2: {}",
                    sample(v, "pac_u2_valid", "pac_sample_unix_s")
                ));
                lines.push(format!(
                    "  Onboard: {}",
                    reading(v, "temp_valid", "temp_onboard_c", " C", 1)
                ));
                lines.push(format!(
                    "  BME688: {} / {} / {}",
                    reading(v, "bme_valid", "bme_temp_c", " C", 1),
                    reading(v, "bme_valid", "bme_pressure_pa", " Pa", 0),
                    reading(v, "bme_valid", "bme_humidity_rh", " %RH", 1)
                ));
                for (name, key) in [
                    ("AI eFuse", "pow_onoff_ai"),
                    ("FPGA eFuse", "pow_onoff_fpga"),
                ] {
                    lines.push(format!("  {name:<18} {}", self.gpio_state(key)));
                }
            }
            Err(e) => lines.push(format!("  OBC: {e}")),
        }
        lines.push(String::new());
        lines.push(self.status(true).replace("LIVE", "ONLINE"));
        if let Ok(v) = &self.exp {
            if flag(v, "data_valid") && age(v, "obc_timestamp").is_some_and(|n| n <= 30) {
                lines.push(format!(
                    "  Board: {}  BME: {} / {} / {}",
                    exp_reading(v, 4, "board_temp_c", " C", 1),
                    exp_reading(v, 5, "bme_temp_c", " C", 1),
                    exp_reading(v, 5, "bme_press_pa", " Pa", 0),
                    exp_reading(v, 5, "bme_hum_rh", " %RH", 1)
                ));
                for i in 0..4 {
                    lines.push(format!(
                        "  {:<18} {}  {}",
                        v["pwr_rail_names"][i].as_str().unwrap_or("EXP rail"),
                        exp_rail_state(v, i),
                        if group_valid(v, 7) {
                            format!(
                                "{} mV / {} mA",
                                array_num(&v["pwr_volt_mv"], i, 0, ""),
                                array_num(&v["pwr_curr_ma"], i, 0, "")
                            )
                        } else {
                            "--".into()
                        }
                    ));
                }
                for (name, index) in [
                    ("12V_EXP switch", 0),
                    ("5V_SLN_HD switch", 1),
                    ("12V_MOTOR switch", 2),
                ] {
                    lines.push(format!(
                        "  {name:<18} {}",
                        match v["rail_enabled"][index].as_u64() {
                            Some(1) => "ON",
                            Some(0) => "OFF",
                            _ => "UNKNOWN",
                        }
                    ));
                }
                lines.push(format!(
                    "  Flow 1/2: {} / {} uL/min; IMU: {}",
                    array_num(&v["flow_ul_min"], 0, 1, ""),
                    array_num(&v["flow_ul_min"], 1, 1, ""),
                    if group_valid(v, 6) { "VALID" } else { "--" }
                ));
            } else {
                lines.push("  RPARAM sensor data unavailable".into());
            }
        }
        lines.push(String::new());
        lines.push(self.pdu_status());
        if let Ok(v) = &self.pdu {
            if age(v, "obc_timestamp").is_some_and(|n| n <= 15) {
                lines.push(format!(
                    "  Board temperature: {} C  Mode: {}",
                    v["board_temp_c"]
                        .as_f64()
                        .map(|n| format!("{n:.1}"))
                        .unwrap_or_else(|| "--".into()),
                    v["mode"].as_str().unwrap_or("--")
                ));
                for (name, key, volts, amps) in [
                    ("VOUT", "vout_on", "vout_mv", "vout_ma"),
                    ("+5_TRANS", "trans_on", "trans_mv", "trans_ma"),
                ] {
                    lines.push(format!(
                        "  {name:<18} {}  {} mV / {} mA",
                        bool_state(v[key].as_bool()),
                        v[volts]
                            .as_u64()
                            .map(|n| n.to_string())
                            .unwrap_or_else(|| "--".into()),
                        v[amps]
                            .as_u64()
                            .map(|n| n.to_string())
                            .unwrap_or_else(|| "--".into())
                    ));
                }
                lines.push(format!(
                    "  VOUT setpoint: {} mV; +12 input: {} mV; +5 input: {} mV",
                    v["vout_set_mv"]
                        .as_u64()
                        .map(|n| n.to_string())
                        .unwrap_or_else(|| "--".into()),
                    v["input_12v_mv"]
                        .as_u64()
                        .map(|n| n.to_string())
                        .unwrap_or_else(|| "--".into()),
                    v["input_5v_mv"]
                        .as_u64()
                        .map(|n| n.to_string())
                        .unwrap_or_else(|| "--".into())
                ));
                if let Some(flags) = v["status_flags"].as_u64() {
                    lines.push(format!(
                        "  Faults: TRANS={} MCU={} latched={} setpoint_mismatch={}",
                        bool_state(Some(flags & (1 << 2) != 0)),
                        bool_state(Some(flags & (1 << 3) != 0)),
                        bool_state(Some(flags & (3 << 4) != 0)),
                        bool_state(Some(flags & (1 << 6) != 0))
                    ));
                }
            }
        } else if let Err(e) = &self.pdu {
            lines.push(format!("  {e}"));
        }
        lines
    }
    fn obc_sensors(&self) -> Vec<String> {
        let Ok(v) = &self.obc else {
            return vec![format!(
                "OBC telemetry unavailable: {}",
                self.obc.as_ref().unwrap_err()
            )];
        };
        let mut lines = vec![
            format!("ADC  {}", sample(v, "temp_valid", "temp_sample_unix_s")),
            format!(
                "  TC1047A: {}",
                reading(v, "temp_valid", "temp_onboard_c", " C", 2)
            ),
        ];
        for i in 0..4 {
            let value = if flag(v, "temp_valid") {
                array_num(&v["temp_ntc_c"], i, 2, " C")
            } else {
                "--".into()
            };
            lines.push(format!("  NTC{}: {value}", i + 1));
        }
        lines.push(String::new());
        lines.push(format!(
            "BME688  {}",
            sample(v, "bme_valid", "bme_sample_unix_s")
        ));
        for (label, key, unit, digits) in [
            ("Temperature", "bme_temp_c", " C", 2),
            ("Pressure", "bme_pressure_pa", " Pa", 0),
            ("Humidity", "bme_humidity_rh", " %RH", 2),
        ] {
            lines.push(format!(
                "  {label}: {}",
                reading(v, "bme_valid", key, unit, digits)
            ));
        }
        let gas = if flag(v, "bme_valid") && flag(v, "bme_gas_valid") {
            v["bme_gas_resistance_ohm"]
                .as_f64()
                .map(|n| format!("{n:.0} ohm"))
                .unwrap_or_else(|| "--".into())
        } else {
            "INVALID".into()
        };
        lines.push(format!("  Gas resistance: {gas}"));
        lines.push(String::new());
        lines.push(format!("RTC /dev/rtc2: {}", self.rtc));
        lines
    }
    fn obc_power(&self) -> Vec<String> {
        let Ok(v) = &self.obc else {
            return vec![format!(
                "OBC telemetry unavailable: {}",
                self.obc.as_ref().unwrap_err()
            )];
        };
        let mut lines = vec![
            format!("PAC U6  {}", sample(v, "pac_u6_valid", "pac_sample_unix_s")),
            format!("PAC U2  {}", sample(v, "pac_u2_valid", "pac_sample_unix_s")),
            String::new(),
            format!(
                "{:<20} {:>7} {:>9} {:>9} {:>9}",
                "RAIL", "STATE", "V", "A", "W"
            ),
        ];
        for (i, name) in RAILS.iter().enumerate() {
            let valid = flag(
                v,
                if i < 4 {
                    "pac_u6_valid"
                } else {
                    "pac_u2_valid"
                },
            );
            let volts = v["pwr_volt_v"][i].as_f64();
            let amps = v["pwr_curr_a"][i].as_f64();
            let row = if valid {
                match (volts, amps) {
                    (Some(volts), Some(amps)) => format!(
                        "{:<20} {:>7} {:>9.3} {:>9.3} {:>9.3}",
                        name,
                        if volts >= 0.5 { "ON" } else { "OFF" },
                        volts,
                        amps,
                        volts * amps
                    ),
                    _ => format!("{name:<20} UNKNOWN --"),
                }
            } else {
                format!("{name:<20} UNKNOWN --")
            };
            lines.push(row);
        }
        lines.push(String::new());
        lines.push("OBC eFuse controls (readback from GPIO LEDs):".into());
        for (name, key) in [("AI", "pow_onoff_ai"), ("FPGA", "pow_onoff_fpga")] {
            lines.push(format!("  {name:<20} {}", self.gpio_state(key)));
        }
        lines
    }
    fn peripherals(&self) -> Vec<String> {
        if self.simulated {
            let mut lines = vec![
                "SIMULATED DEVICE NODES (no host hardware is accessed)".into(),
                "  I2C-0 sensors          PRESENT (simulated)".into(),
                "  CAT93 EEPROM SPI       PRESENT (simulated)".into(),
                "  S25 FPGA flash SPI     PRESENT (simulated)".into(),
                "  CAN 0 / CAN 1          PRESENT (simulated)".into(),
                String::new(),
                "USB camera and GPIO actions update the simulation only.".into(),
            ];
            for n in 1..=4 {
                lines.push(format!(
                    "Camera {n} USB power: {}",
                    self.gpio_state(&format!("usb_{n}_pwr_en"))
                ));
            }
            lines.push(format!("CAMERA_LED PWM duty: {}", self.pwm_duty()));
            return lines;
        }
        let mut lines = vec!["DEVICE NODES REPORTED BY FLIGHT SOFTWARE".into()];
        for (name, key) in [
            ("I2C-0 sensors", "i2c0"),
            ("CAT93 EEPROM SPI", "spi_cat93"),
            ("S25 FPGA flash SPI", "spi_s25"),
            ("CAN 0", "can0"),
            ("CAN 1", "can1"),
        ] {
            lines.push(format!(
                "  {:<21} {}",
                name,
                match self.obc.as_ref().ok().and_then(|v|v["devices"][key].as_bool()) {
                    Some(true)=>"PRESENT",Some(false)=>"ABSENT",None=>"UNKNOWN"}
            ));
        }
        lines.push(String::new());
        lines.push("USB cameras: capture uses usb_cam for SEL0/SEL1, power, active-low OE and enumeration.".into());
        lines.push("The capture result reports the saved JPEG path.".into());
        for n in 1..=4 {
            lines.push(format!(
                "Camera {n} USB power: {}",
                self.gpio_state(&format!("usb_{n}_pwr_en"))
            ));
        }
        lines.push(format!("CAMERA_LED PWM duty: {}", self.pwm_duty()));
        lines
    }
    pub fn mipi_cameras(&self, chosen_board: u8, chosen_sensor: u8) -> Vec<String> {
        let mut lines = vec![
            format!("Selected: MIPI camera {chosen_board}, sensor {chosen_sensor}"),
            "Two MIPI ports; each camera board has four AR2020 sensor slots (0..3).".into(),
            "Choose a camera and sensor, then Apply route or Capture RAW frame.".into(),
            String::new(),
        ];
        let route=self.obc.as_ref().ok();
        let board_select=route.and_then(|v|v["mipi_route_board"].as_u64()).map(|n|n as u8);
        let lane=route.and_then(|v|v["mipi_route_lane"].as_u64()).map(|n|n as u8);
        let mux=route.and_then(|v|v["mipi_route_sensor"].as_u64()).map(|n|n as u8);
        lines.push(format!(
            "Hardware route: board {}  lane {}  sensor mux {}",
            board_select
                .map(|n| n.to_string())
                .unwrap_or_else(|| "--".into()),
            lane.map(|n| n.to_string()).unwrap_or_else(|| "--".into()),
            mux.map(|n| n.to_string()).unwrap_or_else(|| "--".into())
        ));
        for board in 1..=2 {
            lines.push(String::new());
            lines.push(format!(
                "MIPI CAMERA {board}{}",
                if chosen_board == board {
                    "  [SELECTED]"
                } else {
                    ""
                }
            ));
            lines.push(format!(
                "  Power: {}  I2C: {}",
                self.gpio_state(&format!("pow_mipicam_{board}")),
                self.gpio_state(&format!("en_i2c_mipicam_{board}"))
            ));
            for sensor in 0..4 {
                let active = board_select == Some(board)
                    && lane == Some(sensor)
                    && mux == Some(sensor)
                    && self.gpio_state(&format!("pow_mipicam_{board}")) == "ON";
                lines.push(format!(
                    "  Sensor {sensor}: {}{}",
                    if active { "ACTIVE" } else { "IDLE" },
                    if chosen_board == board && chosen_sensor == sensor {
                        "  < chosen"
                    } else {
                        ""
                    }
                ));
            }
        }
        lines.push(String::new());
        lines.push("Capture saves a 5120x3840 BG10 RAW frame under /home/root.".into());
        lines
    }
    fn exp_status(&self) -> Vec<String> {
        let mut uart_lines = vec!["EXP UART tlm_json (flight software cache)".into()];
        if let Ok(uart) = &self.exp_uart {
            for section in ["system", "sensors", "power"] {
                uart_lines.push(format!("{section}:"));
                if uart[section].is_null() {
                    uart_lines.push("  unavailable or stale".into());
                } else if let Ok(pretty) = serde_json::to_string_pretty(&uart[section]) {
                    uart_lines.extend(pretty.lines().map(|line|format!("  {line}")));
                }
            }
        } else {
            uart_lines.push("  unavailable; check DAL_EXP_UART_PORT and wiring".into());
        }
        uart_lines.push(String::new());
        uart_lines.push("CAN RPARAM service snapshot".into());
        let Ok(v) = &self.exp else {
            uart_lines.push(format!(
                "EXP telemetry unavailable: {}",
                self.exp.as_ref().unwrap_err()
            ));
            return uart_lines;
        };
        let mut lines = uart_lines;
        lines.push(format!("NODE {}  {}", v["node_addr"], self.status(true)));
        if !flag(v, "data_valid") {
            lines.push("No valid RPARAM data; measurements hidden.".into());
            return lines;
        }
        lines.push(format!(
            "RPARAM schema {}  sequence {}  tick {} ms",
            v["rparam_schema"], v["sensor_seq"], v["sensor_tick_ms"]
        ));
        lines.push(format!(
            "Supported mask {}  valid mask {}",
            v["sensor_supported_mask"], v["sensor_valid_mask"]
        ));
        lines.push(String::new());
        for (label, key, unit, digits, group) in [
            ("Board temperature", "board_temp_c", " C", 2, 4),
            ("BME temperature", "bme_temp_c", " C", 2, 5),
            ("BME pressure", "bme_press_pa", " Pa", 0, 5),
            ("BME humidity", "bme_hum_rh", " %RH", 2, 5),
        ] {
            let value = if group_valid(v, group) {
                v[key]
                    .as_f64()
                    .map(|n| format!("{n:.digits$}{unit}"))
                    .unwrap_or_else(|| "--".into())
            } else {
                "--".into()
            };
            lines.push(format!("{label}: {value}"));
        }
        for i in 0..4 {
            lines.push(format!(
                "NTC{}: {}",
                i + 1,
                if group_valid(v, i) {
                    array_num(&v["ntc_c"], i, 2, " C")
                } else {
                    "--".into()
                }
            ));
        }
        for i in 0..2 {
            lines.push(format!(
                "Flow{}: {}",
                i + 1,
                if group_valid(v, 8 + i) {
                    array_num(&v["flow_ul_min"], i, 2, " uL/min")
                } else {
                    "--".into()
                }
            ));
        }
        lines.push(String::new());
        for i in 0..4 {
            let values = if group_valid(v, 7) {
                format!(
                    "{} mV, {} mA",
                    array_num(&v["pwr_volt_mv"], i, 0, ""),
                    array_num(&v["pwr_curr_ma"], i, 0, "")
                )
            } else {
                "--".into()
            };
            lines.push(format!(
                "{}: {values}",
                v["pwr_rail_names"][i].as_str().unwrap_or("rail")
            ));
        }
        lines.push("Check supported/valid masks before using individual EXP sensor values.".into());
        lines
    }
    fn exp_control(&self) -> Vec<String> {
        vec![
            "EXP UART text CLI through flight software.".into(),
            "Ping requests tlm_json system.".into(),
            "Read telemetry requests tlm_json sensors.".into(),
            "Send firmware command waits for the UART response.".into(),
            String::new(),
            "The last command result is shown below.".into(),
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn exp_uart_keeps_board_online_without_can_snapshot() {
        let mut state = Snapshots::default();
        state.exp_uart = Ok(serde_json::json!({
            "system":{"schema_version":2,"section":"system"},
            "system_age_s":1
        }));
        assert!(state.status(true).contains("ONLINE"));
        state.exp_uart = Ok(serde_json::json!({
            "system":{"schema_version":2,"section":"system"},
            "system_age_s":31
        }));
        assert!(state.status(true).contains("OFFLINE"));
    }
    #[test]
    fn invalid_pac_does_not_show_zero_as_measurement() {
        let mut state = Snapshots::default();
        state.obc = Ok(serde_json::json!({"pac_u6_valid":0,"pac_u2_valid":0,
            "pwr_volt_v":[0,0,0,0,0,0,0,0],"pwr_curr_a":[0,0,0,0,0,0,0,0]}));
        assert!(state
            .obc_power()
            .iter()
            .any(|l| l.contains("5V_SAMA7") && l.contains("--")));
    }
    #[test]
    fn invalid_gas_is_marked_invalid() {
        let mut state = Snapshots::default();
        state.obc = Ok(serde_json::json!({"bme_valid":1,"bme_gas_valid":0,
            "bme_gas_resistance_ohm":0}));
        assert!(state
            .obc_sensors()
            .iter()
            .any(|l| l.contains("Gas resistance: INVALID")));
    }
}
