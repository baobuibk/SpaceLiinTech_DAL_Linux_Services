mod exp;
mod host_sim;
mod mipi;
mod model;

use crossterm::{
    event::{self, Event, KeyCode, KeyEvent, KeyEventKind, KeyModifiers},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use exp::ExpPanel;
use host_sim::HostSim;
use model::{Snapshots, Tab};
use ratatui::{
    backend::CrosstermBackend,
    layout::{Constraint, Direction, Layout},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{Block, Borders, List, ListItem, ListState, Paragraph, Tabs, Wrap},
    Frame, Terminal,
};
use serde_json::Value;
use std::{
    collections::VecDeque,
    env,
    io::{self, Read},
    os::unix::process::CommandExt,
    path::PathBuf,
    process::{Command, Stdio},
    sync::mpsc::{self, Receiver},
    thread,
    time::{Duration, Instant},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Action {
    XioList,
    Efuse(&'static str, bool),
    UsbPower(u8, bool),
    Camera(u8),
    CameraLed,
    SetRtc,
    MipiBoard(u8),
    MipiSensor(u8),
    MipiPower(u8, bool),
    MipiApply,
    MipiCapture,
    ExpPing,
    ExpGetall,
    ExpPreset(&'static str),
    ExpRun,
}
impl Action {
    fn label(self) -> &'static str {
        match self {
            Self::XioList => "Show board GPIO states",
            Self::Efuse("pow_onoff_ai", true) => "AI eFuse [ON]",
            Self::Efuse("pow_onoff_ai", false) => "AI eFuse [OFF]",
            Self::Efuse(_, true) => "FPGA eFuse [ON]",
            Self::Efuse(_, false) => "FPGA eFuse [OFF]",
            Self::UsbPower(1, true) => "Camera 1 USB power [ON]",
            Self::UsbPower(1, false) => "Camera 1 USB power [OFF]",
            Self::UsbPower(2, true) => "Camera 2 USB power [ON]",
            Self::UsbPower(2, false) => "Camera 2 USB power [OFF]",
            Self::UsbPower(3, true) => "Camera 3 USB power [ON]",
            Self::UsbPower(3, false) => "Camera 3 USB power [OFF]",
            Self::UsbPower(4, true) => "Camera 4 USB power [ON]",
            Self::UsbPower(4, false) => "Camera 4 USB power [OFF]",
            Self::UsbPower(_, _) => "USB power",
            Self::Camera(1) => "Capture USB camera 1",
            Self::Camera(2) => "Capture USB camera 2",
            Self::Camera(3) => "Capture USB camera 3",
            Self::Camera(_) => "Capture USB camera 4",
            Self::CameraLed => "Set CAMERA_LED PWM duty 0-100%",
            Self::SetRtc => "Edit RTC date/time (UTC)",
            Self::MipiBoard(1) => "Choose MIPI camera 1",
            Self::MipiBoard(_) => "Choose MIPI camera 2",
            Self::MipiSensor(0) => "Choose sensor 0",
            Self::MipiSensor(1) => "Choose sensor 1",
            Self::MipiSensor(2) => "Choose sensor 2",
            Self::MipiSensor(_) => "Choose sensor 3",
            Self::MipiPower(1, true) => "MIPI camera 1 power [ON]",
            Self::MipiPower(1, false) => "MIPI camera 1 power [OFF]",
            Self::MipiPower(2, true) => "MIPI camera 2 power [ON]",
            Self::MipiPower(_, false) => "MIPI camera 2 power [OFF]",
            Self::MipiPower(_, true) => "MIPI power [ON]",
            Self::MipiApply => "Apply selected MIPI route",
            Self::MipiCapture => "Capture selected MIPI RAW image",
            Self::ExpPing => "Check EXP UART (system telemetry)",
            Self::ExpGetall => "Read EXP UART sensor telemetry",
            Self::ExpPreset("power_all_get") => "Read EXP power switches",
            Self::ExpPreset("sensor_status") => "Read EXP sensor status",
            Self::ExpPreset("pc_status") => "Read EXP PC experiment status",
            Self::ExpPreset("limit_status all") => "Read EXP optical limits",
            Self::ExpPreset("ir_status") => "Read EXP IR status",
            Self::ExpPreset(_) => "Read EXP status",
            Self::ExpRun => "Send EXP firmware command",
        }
    }
}
fn actions(tab: Tab) -> &'static [Action] {
    use Action::*;
    match tab {
        Tab::ObcSensors => &[SetRtc],
        Tab::ObcPower => &[
            Efuse("pow_onoff_ai", true),
            Efuse("pow_onoff_ai", false),
            Efuse("pow_onoff_fpga", true),
            Efuse("pow_onoff_fpga", false),
            XioList,
        ],
        Tab::Peripherals => &[
            UsbPower(1, true),
            UsbPower(1, false),
            Camera(1),
            UsbPower(2, true),
            UsbPower(2, false),
            Camera(2),
            UsbPower(3, true),
            UsbPower(3, false),
            Camera(3),
            UsbPower(4, true),
            UsbPower(4, false),
            Camera(4),
            CameraLed,
            XioList,
        ],
        Tab::MipiCameras => &[
            MipiBoard(1),
            MipiBoard(2),
            MipiSensor(0),
            MipiSensor(1),
            MipiSensor(2),
            MipiSensor(3),
            MipiPower(1, true),
            MipiPower(1, false),
            MipiPower(2, true),
            MipiPower(2, false),
            MipiApply,
            MipiCapture,
        ],
        Tab::ExpStatus => &[ExpPing, ExpGetall],
        Tab::ExpControl => &[
            ExpPing,
            ExpGetall,
            ExpPreset("power_all_get"),
            ExpPreset("sensor_status"),
            ExpPreset("pc_status"),
            ExpPreset("limit_status all"),
            ExpPreset("ir_status"),
            ExpRun,
        ],
        _ => &[],
    }
}

enum InputMode {
    Normal,
    ExpCommand(String),
    ConfirmBurn,
    Rtc(TextInput),
    Pwm(String),
}
struct TextInput {
    value: String,
    cursor: usize,
}
impl TextInput {
    fn new(value: String) -> Self {
        let cursor = value.len();
        Self { value, cursor }
    }
    fn edit(&mut self, key: KeyCode) {
        match key {
            KeyCode::Left => self.cursor = self.cursor.saturating_sub(1),
            KeyCode::Right => self.cursor = (self.cursor + 1).min(self.value.len()),
            KeyCode::Home => self.cursor = 0,
            KeyCode::End => self.cursor = self.value.len(),
            KeyCode::Backspace if self.cursor > 0 => {
                self.cursor -= 1;
                self.value.remove(self.cursor);
            }
            KeyCode::Delete if self.cursor < self.value.len() => {
                self.value.remove(self.cursor);
            }
            KeyCode::Char(c)
                if (c.is_ascii_digit() || matches!(c, '-' | ':' | ' ' | 'T' | 'Z'))
                    && self.value.len() < 20 =>
            {
                self.value.insert(self.cursor, c);
                self.cursor += 1;
            }
            _ => (),
        }
    }
    fn display(&self) -> String {
        let mut shown = self.value.clone();
        shown.insert(self.cursor, '|');
        shown
    }
}
fn parse_rtc_input(value: &str) -> Result<u32, String> {
    let text = value.trim();
    if text.bytes().all(|c| c.is_ascii_digit()) {
        let epoch: u32 = text
            .parse()
            .map_err(|_| "Enter a UTC date/time or Unix epoch")?;
        return (epoch >= 946684800)
            .then_some(epoch)
            .ok_or_else(|| "RTC must be year 2000 or later".into());
    }
    let text = text.strip_suffix('Z').unwrap_or(text);
    let bytes = text.as_bytes();
    if bytes.len() != 19
        || bytes[4] != b'-'
        || bytes[7] != b'-'
        || !matches!(bytes[10], b' ' | b'T')
        || bytes[13] != b':'
        || bytes[16] != b':'
    {
        return Err("Use YYYY-MM-DD HH:MM:SS UTC or a Unix epoch".into());
    }
    let number = |a: usize, b: usize| -> Result<i64, String> {
        text[a..b]
            .parse::<i64>()
            .map_err(|_| "Invalid UTC date/time".into())
    };
    let (year, month, day) = (number(0, 4)?, number(5, 7)?, number(8, 10)?);
    let (hour, minute, second) = (number(11, 13)?, number(14, 16)?, number(17, 19)?);
    let leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    let month_days = [
        31,
        if leap { 29 } else { 28 },
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
    ];
    if !(2000..=2106).contains(&year)
        || !(1..=12).contains(&month)
        || day < 1
        || day > month_days[(month - 1) as usize]
        || hour > 23
        || minute > 59
        || second > 59
    {
        return Err("Invalid UTC date/time".into());
    }
    // Gregorian civil date to days since 1970-01-01.
    let y = year - i64::from(month <= 2);
    let era = y.div_euclid(400);
    let yoe = y - era * 400;
    let mp = month + if month > 2 { -3 } else { 9 };
    let doy = (153 * mp + 2) / 5 + day - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    let epoch = (era * 146097 + doe - 719468) * 86400 + hour * 3600 + minute * 60 + second;
    u32::try_from(epoch).map_err(|_| "RTC date exceeds the supported 32-bit epoch".into())
}
struct Finished {
    label: String,
    output: String,
}
struct App {
    tab: Tab,
    selected: usize,
    exp: ExpPanel,
    snapshots: Snapshots,
    sim: Option<HostSim>,
    mode: InputMode,
    rx: Option<Receiver<Finished>>,
    log: VecDeque<String>,
    content_scroll: u16,
    log_scroll: u16,
    mipi_board: u8,
    mipi_sensor: u8,
    quit: bool,
}
impl Default for App {
    fn default() -> Self {
        Self::new(false)
    }
}
impl App {
    fn new(simulated: bool) -> Self {
        let mut snapshots = Snapshots::default();
        let sim = if simulated {
            Some(HostSim::default())
        } else {
            None
        };
        if let Some(sim) = &sim {
            let (obc, exp, pdu) = sim.snapshots();
            snapshots.set_simulated(obc, exp, pdu);
        } else {
            snapshots.refresh(true);
        }
        Self {
            tab: Tab::Home,
            selected: 0,
            exp: ExpPanel::new(simulated),
            snapshots,
            sim,
            mode: InputMode::Normal,
            rx: None,
            log: VecDeque::from([
                "Ready. Tab/1-8 select a page; Enter runs a selected action.".into(),
            ]),
            content_scroll: 0,
            log_scroll: 0,
            mipi_board: 1,
            mipi_sensor: 0,
            quit: false,
        }
    }
}
impl App {
    fn tick(&mut self) {
        if let Some(sim) = &mut self.sim {
            if sim.tick() {
                let (obc, exp, pdu) = sim.snapshots();
                self.snapshots.set_simulated(obc, exp, pdu);
            }
        } else {
            self.snapshots.refresh(false);
        }
        self.exp.tick(self.tab == Tab::ExpDevices);
        let lines: Vec<String> = self.exp.out.drain(..).collect();
        for line in lines {
            self.push_log(line);
        }
        if let Some(rx) = &self.rx {
            if let Ok(result) = rx.try_recv() {
                self.push_log(format!("{}: {}", result.label, result.output));
                self.rx = None;
                self.snapshots.refresh(true);
            }
        }
    }
    fn push_log(&mut self, text: String) {
        for line in text.lines() {
            self.log.push_back(line.chars().take(240).collect());
        }
        while self.log.len() > 250 {
            self.log.pop_front();
        }
        self.log_scroll = 0;
    }
    fn select_tab(&mut self, tab: Tab) {
        self.tab = tab;
        self.selected = 0;
        self.content_scroll = 0;
    }
    fn handle(&mut self, key: KeyEvent) {
        if key.kind != KeyEventKind::Press {
            return;
        }
        match &mut self.mode {
            InputMode::ExpCommand(input) => match key.code {
                KeyCode::Esc => self.mode = InputMode::Normal,
                KeyCode::Char('u') if key.modifiers.contains(KeyModifiers::CONTROL) => {
                    input.clear();
                }
                KeyCode::Enter => {
                    let command = input.trim().to_owned();
                    self.mode = InputMode::Normal;
                    if command.is_empty() {
                        self.push_log("EXP command is empty".into());
                    } else if command.len() > 95 {
                        self.push_log("EXP command exceeds 95 bytes".into());
                    } else if self.tab == Tab::ExpDevices {
                        self.exp.send_user(&command);
                    } else if command == "burn_fire" {
                        self.mode = InputMode::ConfirmBurn;
                    } else {
                        self.start(Action::ExpRun, Some(command));
                    }
                }
                KeyCode::Backspace => {
                    input.pop();
                }
                KeyCode::Char(c)
                    if !key.modifiers.contains(KeyModifiers::CONTROL) && input.len() < 95 =>
                {
                    input.push(c)
                }
                _ => (),
            },
            InputMode::ConfirmBurn => match key.code {
                KeyCode::Enter => {
                    self.mode = InputMode::Normal;
                    self.start(Action::ExpRun, Some("burn_fire".into()));
                }
                KeyCode::Esc => self.mode = InputMode::Normal,
                _ => (),
            },
            InputMode::Rtc(input) => match key.code {
                KeyCode::Esc => self.mode = InputMode::Normal,
                KeyCode::Char('u') if key.modifiers.contains(KeyModifiers::CONTROL) => {
                    input.value.clear();
                    input.cursor = 0;
                }
                KeyCode::Enter => match parse_rtc_input(&input.value) {
                    Ok(epoch) => {
                        self.mode = InputMode::Normal;
                        self.start(Action::SetRtc, Some(epoch.to_string()));
                    }
                    Err(error) => self.push_log(error),
                },
                _ => input.edit(key.code),
            },
            InputMode::Pwm(input) => match key.code {
                KeyCode::Esc => self.mode = InputMode::Normal,
                KeyCode::Enter => {
                    let value = input.trim().to_owned();
                    self.mode = InputMode::Normal;
                    self.start(Action::CameraLed, Some(value));
                }
                KeyCode::Backspace => {
                    input.pop();
                }
                KeyCode::Char(c) if c.is_ascii_digit() && input.len() < 3 => input.push(c),
                _ => (),
            },
            InputMode::Normal if self.tab == Tab::ExpDevices && self.exp.capturing()
                && !(key.code == KeyCode::Char('c') && key.modifiers.contains(KeyModifiers::CONTROL)) =>
            {
                self.exp.key(key);
            }
            InputMode::Normal if self.tab == Tab::ExpDevices
                && !matches!(key.code, KeyCode::Char('q' | 'r' | '/' | '[' | ']') | KeyCode::Tab | KeyCode::BackTab)
                && !matches!(key.code, KeyCode::Char('1'..='8'))
                && self.exp.key(key) => {}
            InputMode::Normal => match key.code {
                KeyCode::Char('q') | KeyCode::Esc => self.quit = true,
                KeyCode::Char('c') if key.modifiers.contains(KeyModifiers::CONTROL) => {
                    self.quit = true
                }
                KeyCode::Tab | KeyCode::Right => self.select_tab(self.tab.next()),
                KeyCode::BackTab | KeyCode::Left => self.select_tab(self.tab.previous()),
                KeyCode::Char(c @ '1'..='8') => {
                    self.select_tab(Tab::ALL[(c as u8 - b'1') as usize])
                }
                KeyCode::Char('/') if self.tab == Tab::ExpDevices =>
                    self.mode = InputMode::ExpCommand(String::new()),
                KeyCode::Up | KeyCode::Char('k') => self.selected = self.selected.saturating_sub(1),
                KeyCode::Down | KeyCode::Char('j') => {
                    let count = actions(self.tab).len();
                    self.selected = (self.selected + 1).min(count.saturating_sub(1));
                }
                KeyCode::Enter => {
                    let selected = actions(self.tab).get(self.selected).copied();
                    if let Some(action) = selected {
                        match action {
                            Action::ExpRun => self.mode = InputMode::ExpCommand(String::new()),
                            Action::SetRtc => {
                                let prefill = self
                                    .snapshots
                                    .rtc
                                    .get(..19)
                                    .filter(|s| s.as_bytes().get(4) == Some(&b'-'))
                                    .unwrap_or("");
                                self.mode = InputMode::Rtc(TextInput::new(prefill.to_owned()));
                            }
                            Action::CameraLed => self.mode = InputMode::Pwm(String::new()),
                            _ => self.start(action, None),
                        }
                    }
                }
                KeyCode::Char('r') => {
                    self.exp.refresh_now();
                    if let Some(sim) = &mut self.sim {
                        sim.advance();
                        let (obc, exp, pdu) = sim.snapshots();
                        self.snapshots.set_simulated(obc, exp, pdu);
                    } else {
                        self.snapshots.refresh(true);
                    }
                    self.push_log("Snapshots refreshed".into());
                }
                KeyCode::Char('f') if self.sim.is_some() => {
                    let sim = self.sim.as_mut().expect("host simulation active");
                    let message = sim.cycle_fault().to_string();
                    let (obc, exp, pdu) = sim.snapshots();
                    self.snapshots.set_simulated(obc, exp, pdu);
                    self.push_log(message);
                }
                KeyCode::PageDown => self.content_scroll = self.content_scroll.saturating_add(5),
                KeyCode::PageUp => self.content_scroll = self.content_scroll.saturating_sub(5),
                KeyCode::Char('[') => self.log_scroll = self.log_scroll.saturating_add(3),
                KeyCode::Char(']') => self.log_scroll = self.log_scroll.saturating_sub(3),
                _ => (),
            },
        }
    }
    fn start(&mut self, action: Action, command: Option<String>) {
        if let Action::MipiBoard(board) = action {
            self.mipi_board = board;
            self.push_log(format!("MIPI camera {board} chosen"));
            return;
        }
        if let Action::MipiSensor(sensor) = action {
            self.mipi_sensor = sensor;
            self.push_log(format!("MIPI sensor {sensor} chosen"));
            return;
        }
        let command = if matches!(action, Action::MipiApply | Action::MipiCapture) {
            Some(format!("{}:{}", self.mipi_board, self.mipi_sensor))
        } else {
            command
        };
        if let Some(sim) = &mut self.sim {
            let reply = sim.apply(action, command.as_deref());
            let (obc, exp, pdu) = sim.snapshots();
            self.snapshots.set_simulated(obc, exp, pdu);
            self.push_log(format!("{}: {reply}", action.label()));
            return;
        }
        if self.rx.is_some() {
            self.push_log("Another action is still running".into());
            return;
        }
        let spec = match command_spec(action, command) {
            Ok(spec) => spec,
            Err(error) => {
                self.push_log(error);
                return;
            }
        };
        self.push_log(format!("Running {}...", action.label()));
        let (tx, rx) = mpsc::channel();
        self.rx = Some(rx);
        thread::spawn(move || {
            let _ = tx.send(run_command(spec));
        });
    }
}

struct CommandSpec {
    label: String,
    program: PathBuf,
    args: Vec<String>,
    timeout: Duration,
}
fn tool(var: &str, default: &str) -> PathBuf {
    env::var_os(var)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(default))
}
fn mipi_selection(value: Option<String>) -> Result<(u8, u8), String> {
    let value = value.ok_or("MIPI camera/sensor selection missing")?;
    let (board, sensor) = value.split_once(':').ok_or("Invalid MIPI selection")?;
    let board: u8 = board.parse().map_err(|_| "Invalid MIPI camera")?;
    let sensor: u8 = sensor.parse().map_err(|_| "Invalid MIPI sensor")?;
    mipi::select_gpio(board)?;
    mipi::check_sensor(sensor)?;
    Ok((board, sensor))
}
fn command_spec(action: Action, command: Option<String>) -> Result<CommandSpec, String> {
    let label = match (action, command.as_ref()) {
        (Action::ExpRun, Some(text)) => format!("EXP command: {text}"),
        _ => action.label().to_string(),
    };
    let (program, args, timeout) = match action {
        Action::XioList | Action::Efuse(_, _) | Action::UsbPower(_, _) => {
            let args = match action {
                Action::XioList => vec!["--gui-hw".into(), "gpio-list".into()],
                Action::Efuse(name, on) => vec!["--gui-hw".into(), "gpio".into(),
                    name.into(), if on { "on" } else { "off" }.into()],
                Action::UsbPower(n, on) => vec!["--gui-hw".into(), "gpio".into(),
                    format!("usb_{n}_pwr_en"), if on { "on" } else { "off" }.into()],
                _ => unreachable!(),
            };
            (tool("OBC_GUI_FLIGHT_FSW", "/home/root/tools/bin/dal_obc_fsw"),
             args, Duration::from_secs(8))
        }
        Action::SetRtc => {
            let epoch = command.ok_or("Enter a UTC Unix epoch")?;
            let parsed: u32 = epoch
                .parse()
                .map_err(|_| "RTC epoch must be a positive 32-bit Unix timestamp")?;
            if parsed < 946684800 {
                return Err("RTC epoch must be year 2000 or later".into());
            }
            (
                tool(
                    "OBC_GUI_FLIGHT_CONSOLE",
                    "/home/root/tools/bin/dal_flight_console",
                ),
                vec!["sys_rtc".into(), epoch],
                Duration::from_secs(8),
            )
        }
        Action::CameraLed => {
            let duty: u8 = command
                .ok_or("Enter PWM duty 0..100")?
                .parse()
                .map_err(|_| "PWM duty must be 0..100")?;
            if duty > 100 {
                return Err("PWM duty must be 0..100".into());
            }
            (
                tool("OBC_GUI_FLIGHT_FSW", "/home/root/tools/bin/dal_obc_fsw"),
                vec!["--gui-hw".into(), "camera-led".into(), duty.to_string()],
                Duration::from_secs(8),
            )
        }
        Action::Camera(n) => {
            let name = format!("/home/root/camera{n}-{}.jpg", model::unix_now());
            (
                tool("OBC_GUI_USB_CAM", "/home/root/tools/bin/usb_cam"),
                vec![
                    n.to_string(),
                    "--width".into(),
                    "1280".into(),
                    "--height".into(),
                    "720".into(),
                    "-o".into(),
                    name,
                ],
                Duration::from_secs(30),
            )
        }
        Action::MipiPower(board, on) => {
            mipi::select_gpio(board)?;
            (
                tool("OBC_GUI_FLIGHT_FSW", "/home/root/tools/bin/dal_obc_fsw"),
                vec![
                    "--gui-hw".into(),
                    "mipi-power".into(),
                    board.to_string(),
                    if on { "on" } else { "off" }.into(),
                ],
                Duration::from_secs(10),
            )
        }
        Action::MipiApply | Action::MipiCapture => {
            let (board, sensor) = mipi_selection(command)?;
            (
                tool("OBC_GUI_FLIGHT_FSW", "/home/root/tools/bin/dal_obc_fsw"),
                vec![
                    "--gui-hw".into(),
                    if action == Action::MipiApply {
                        "mipi-route"
                    } else {
                        "mipi-capture"
                    }
                    .into(),
                    board.to_string(),
                    sensor.to_string(),
                ],
                Duration::from_secs(if action == Action::MipiApply { 15 } else { 90 }),
            )
        }
        Action::MipiBoard(_) | Action::MipiSensor(_) => {
            return Err("Choose a MIPI camera/sensor in the UI".into())
        }
        Action::ExpPing | Action::ExpGetall | Action::ExpPreset(_) |
        Action::ExpRun => {
            let args = match action {
                Action::ExpPing => vec!["exp_ping".into()],
                Action::ExpGetall => vec!["exp_tlm".into(), "sensors".into()],
                Action::ExpPreset(text) =>
                    vec!["exp_run".into(), text.trim().into()],
                Action::ExpRun => vec!["exp_run".into(), command.ok_or("EXP command missing")?],
                _ => unreachable!(),
            };
            (tool("OBC_GUI_FLIGHT_CONSOLE", "/home/root/tools/bin/dal_flight_console"),
             args, Duration::from_secs(65))
        }
    };
    if action==Action::SetRtc || matches!(action,Action::ExpPing | Action::ExpGetall |
        Action::ExpPreset(_) | Action::ExpRun) {
        let mut cli_args=vec!["--socket".into(),model::flight_socket(),"--json".into()];
        cli_args.extend(args);
        return Ok(CommandSpec {label,program:tool("OBC_GUI_FLIGHT_CONSOLE","/home/root/tools/bin/dal_flight_console"),
            args:cli_args,
            timeout:timeout+Duration::from_secs(3)});
    }
    let mut shell=shell_quote(&program.to_string_lossy());
    for arg in &args {shell.push(' ');shell.push_str(&shell_quote(arg));}
    flight_shell_spec(label,shell,timeout)
}
fn shell_quote(value: &str) -> String {
    format!("'{}'",value.replace('\'',"'\"'\"'"))
}
fn flight_shell_spec(label:String,shell:String,timeout:Duration) -> Result<CommandSpec,String> {
    if shell.len()>2048 {return Err("Command exceeds flight software limit".into());}
    Ok(CommandSpec {label,program:tool("OBC_GUI_FLIGHT_CONSOLE","/home/root/tools/bin/dal_flight_console"),
        args:vec!["--socket".into(),model::flight_socket(),"gui_shell".into(),
                  timeout.as_secs().min(120).to_string(),shell],
        timeout:timeout+Duration::from_secs(3)})
}
fn run_command(spec: CommandSpec) -> Finished {
    let mut child = match Command::new(&spec.program)
        .args(&spec.args)
        .process_group(0)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
    {
        Ok(child) => child,
        Err(e) => {
            return Finished {
                label: spec.label,
                output: format!("FAILED: {e}"),
            }
        }
    };
    let stdout = child.stdout.take().expect("piped stdout");
    let stderr = child.stderr.take().expect("piped stderr");
    let out_reader = thread::spawn(move || drain(stdout, 50000));
    let err_reader = thread::spawn(move || drain(stderr, 2000));
    let deadline = Instant::now() + spec.timeout;
    let mut timed_out = false;
    let mut error = None;
    let status = loop {
        match child.try_wait() {
            Ok(Some(status)) => break Some(status),
            Ok(None) if Instant::now() < deadline => thread::sleep(Duration::from_millis(50)),
            Ok(None) => {
                timed_out = true;
                unsafe {
                    libc::kill(-(child.id() as i32), libc::SIGKILL);
                }
                let _ = child.kill();
                break child.wait().ok();
            }
            Err(e) => {
                error = Some(e.to_string());
                let _ = child.kill();
                break child.wait().ok();
            }
        }
    };
    let stdout = out_reader.join().unwrap_or_default();
    let stderr = err_reader.join().unwrap_or_default();
    let output = if timed_out {
        "TIMEOUT; child process stopped".into()
    } else if let Some(e) = error {
        format!("FAILED: {e}")
    } else if let Ok(value)=serde_json::from_slice::<Value>(&stdout) {
        let ok=status.is_some_and(|s|s.success()) && value["ok"].as_bool()==Some(true);
        format!("{}: {}",if ok {"OK"} else {"FAILED"},
            value["text"].as_str().or_else(||value["error"].as_str()).unwrap_or("No flight response text"))
    } else {
        format!("FAILED: invalid flight response: {}{}",String::from_utf8_lossy(&stdout),String::from_utf8_lossy(&stderr))
    };
    Finished {
        label: spec.label,
        output,
    }
}
fn drain(mut reader: impl Read, limit: usize) -> Vec<u8> {
    let mut saved = Vec::new();
    let mut chunk = [0_u8; 4096];
    loop {
        match reader.read(&mut chunk) {
            Ok(0) => break,
            Ok(n) => {
                let keep = n.min(limit.saturating_sub(saved.len()));
                saved.extend_from_slice(&chunk[..keep]);
            }
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(_) => break,
        }
    }
    saved
}
struct TerminalGuard;
impl TerminalGuard {
    fn enter() -> io::Result<Self> {
        enable_raw_mode()?;
        if let Err(e) = execute!(io::stdout(), EnterAlternateScreen) {
            let _ = disable_raw_mode();
            return Err(e);
        }
        Ok(Self)
    }
}
impl Drop for TerminalGuard {
    fn drop(&mut self) {
        let _ = disable_raw_mode();
        let _ = execute!(io::stdout(), LeaveAlternateScreen);
    }
}
fn styled_status_lines(lines: &[String]) -> Vec<Line<'static>> {
    lines
        .iter()
        .map(|s| {
            let mut spans = Vec::new();
            let mut rest = s.as_str();
            while let Some((offset, token)) = ["OFFLINE", "UNKNOWN", "ONLINE", "OFF", "ON"]
                .iter()
                .filter_map(|t| rest.find(t).map(|at| (at, *t)))
                .min_by_key(|(at, _)| *at)
            {
                spans.push(Span::raw(rest[..offset].to_owned()));
                spans.push(Span::styled(
                    token.to_owned(),
                    Style::default()
                        .fg(match token {
                            "ON" | "ONLINE" => Color::Green,
                            "OFF" | "OFFLINE" => Color::Gray,
                            _ => Color::Yellow,
                        })
                        .add_modifier(Modifier::BOLD),
                ));
                rest = &rest[offset + token.len()..];
            }
            spans.push(Span::raw(rest.to_owned()));
            Line::from(spans)
        })
        .collect()
}

fn draw(frame: &mut Frame, app: &App) {
    let area = frame.area();
    let rows = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3),
            Constraint::Length(3),
            Constraint::Min(6),
            Constraint::Length(if app.tab == Tab::Home { 5 } else { 7 }),
            Constraint::Length(2),
        ])
        .split(area);
    let header = Paragraph::new(Line::from(vec![
        Span::styled(
            if app.sim.is_some() {
                " DAL OBC / EXP / PDU  HOST SIM "
            } else {
                " DAL OBC / EXP / PDU "
            },
            Style::default()
                .fg(Color::Yellow)
                .add_modifier(Modifier::BOLD),
        ),
        Span::raw(format!(
            "  {}  |  {}  |  {}",
            app.snapshots.status(false),
            app.snapshots.status(true),
            app.snapshots.pdu_status()
        )),
    ]))
    .block(
        Block::default()
            .borders(Borders::ALL)
            .title("SPACE LIIN TECH"),
    );
    frame.render_widget(header, rows[0]);
    let tabs = Tabs::new(
        Tab::ALL
            .iter()
            .enumerate()
            .map(|(i, tab)| Line::from(format!("{} {}", i + 1, tab.title())))
            .collect::<Vec<_>>(),
    )
    .select(app.tab.index())
    .style(Style::default().fg(Color::Gray))
    .highlight_style(
        Style::default()
            .fg(Color::Black)
            .bg(Color::Yellow)
            .add_modifier(Modifier::BOLD),
    )
    .block(Block::default().borders(Borders::ALL));
    frame.render_widget(tabs, rows[1]);
    if app.tab == Tab::ExpDevices {
        let extra = if app.exp.is_rparam() { app.snapshots.exp_rparam_lines() } else { Vec::new() };
        app.exp.draw(frame, rows[2], &extra);
    } else {
    let choices = actions(app.tab);
    let content_area = if choices.is_empty() {
        rows[2]
    } else {
        let split = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([Constraint::Percentage(70), Constraint::Percentage(30)])
            .split(rows[2]);
        let list = List::new(
            choices
                .iter()
                .map(|a| ListItem::new(a.label()))
                .collect::<Vec<_>>(),
        )
        .block(
            Block::default()
                .borders(Borders::ALL)
                .title("ACTIONS  Up/Down + Enter"),
        )
        .highlight_style(Style::default().fg(Color::Black).bg(Color::Cyan));
        let mut state = ListState::default();
        state.select(Some(app.selected));
        frame.render_stateful_widget(list, split[1], &mut state);
        split[0]
    };
    if app.tab == Tab::Home && content_area.width >= 75 {
        let cards = app.snapshots.home_cards();
        let areas = Layout::default()
            .direction(Direction::Horizontal)
            .constraints([
                Constraint::Percentage(33),
                Constraint::Percentage(34),
                Constraint::Percentage(33),
            ])
            .split(content_area);
        for (i, title) in ["OBC", "EXP", "PDU"].iter().enumerate() {
            frame.render_widget(
                Paragraph::new(styled_status_lines(&cards[i]))
                    .block(Block::default().borders(Borders::ALL).title(*title))
                    .wrap(Wrap { trim: false })
                    .scroll((app.content_scroll, 0)),
                areas[i],
            );
        }
    } else {
        let mut lines = if app.tab == Tab::MipiCameras {
            app.snapshots.mipi_cameras(app.mipi_board, app.mipi_sensor)
        } else {
            app.snapshots.lines(app.tab)
        };
        if app.tab == Tab::ObcSensors {
            let editor = match &app.mode {
                InputMode::Rtc(input) => format!("EDIT RTC UTC: [{}]", input.display()),
                _ => "SET RTC UTC: [select Edit RTC, then Enter]".into(),
            };
            lines.splice(
                0..0,
                [
                    editor,
                    "Format: YYYY-MM-DD HH:MM:SS or Unix epoch; arrows edit, Ctrl-U clears.".into(),
                    String::new(),
                ],
            );
        }
        let max_scroll = lines
            .len()
            .saturating_sub(content_area.height.saturating_sub(2) as usize)
            as u16;
        let body = Paragraph::new(styled_status_lines(&lines))
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .title(app.tab.title()),
            )
            .wrap(Wrap { trim: false })
            .scroll((app.content_scroll.min(max_scroll), 0));
        frame.render_widget(body, content_area);
    }
    }
    let log_height = rows[3].height.saturating_sub(2) as usize;
    let end = app.log.len().saturating_sub(app.log_scroll as usize);
    let start = end.saturating_sub(log_height);
    let log_text = app
        .log
        .iter()
        .skip(start)
        .take(end - start)
        .cloned()
        .collect::<Vec<_>>()
        .join("\n");
    frame.render_widget(
        Paragraph::new(log_text)
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .title("COMMAND RESULT  [ / ] scroll"),
            )
            .wrap(Wrap { trim: false }),
        rows[3],
    );
    let footer = match &app.mode {
        InputMode::Normal => {
            if app.tab == Tab::ExpDevices {
                app.exp.footer()
            } else if app.sim.is_some() {
                "HOST SIM | q quit | Tab/1-8 pages | Up/Down + Enter action | r refresh | f cycle sensor fault".into()
            } else {
                "q quit | Tab/1-8 pages | Up/Down action | Enter execute | r refresh | PgUp/PgDn scroll".into()
            }
        }
        InputMode::ExpCommand(s) => format!("EXP command > {s}_   Enter send | Esc cancel"),
        InputMode::ConfirmBurn => "BURN FIRE: press Enter again to fire | Esc cancel".into(),
        InputMode::Rtc(s) => format!("RTC UTC [{}]  Enter save | Esc cancel", s.display()),
        InputMode::Pwm(s) => format!("CAMERA_LED PWM duty 0..100% > {s}_   Enter set | Esc cancel"),
    };
    frame.render_widget(
        Paragraph::new(footer).style(Style::default().fg(Color::Black).bg(Color::Yellow)),
        rows[4],
    );
}

fn run_tui(simulated: bool) -> io::Result<()> {
    let _guard = TerminalGuard::enter()?;
    let mut terminal = Terminal::new(CrosstermBackend::new(io::stdout()))?;
    let mut app = App::new(simulated);
    while !app.quit {
        app.tick();
        terminal.draw(|frame| draw(frame, &app))?;
        if event::poll(Duration::from_millis(200))? {
            if let Event::Key(key) = event::read()? {
                app.handle(key);
            }
        }
    }
    Ok(())
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    match env::args().nth(1).as_deref() {
        None | Some("tui") => run_tui(false)?,
        Some("host-sim" | "--host-sim") => run_tui(true)?,
        Some("host-sim-status") => {
            let app = App::new(true);
            for line in app.snapshots.lines(Tab::Home) {
                println!("{line}");
            }
        }
        Some("status") => {
            let mut data = Snapshots::default();
            data.refresh(true);
            for line in data.lines(Tab::Home) {
                println!("{line}");
            }
        }
        Some("--help" | "-h" | "help") => {
            println!(
                "Usage: obc_gui [tui|status|host-sim|host-sim-status|--help]\n\
                TUI: Tab or 1-8 changes page; Up/Down chooses action; Enter runs it; q quits.\n\
                host-sim: run with simulated OBC/EXP/PDU data and controls; no hardware access.\n\
                Environment: OBC_GUI_USB_CAM,\n\
                OBC_GUI_FLIGHT_SOCKET, OBC_GUI_FLIGHT_CONSOLE, OBC_GUI_FLIGHT_FSW."
            );
        }
        Some(other) => return Err(format!("Unknown argument: {other}").into()),
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ratatui::backend::TestBackend;
    #[test]
    fn exp_control_passes_firmware_command_as_one_argument() {
        let spec = command_spec(Action::ExpRun, Some("pc_status".into())).unwrap();
        assert!(spec.program.ends_with("dal_flight_console"));
        assert_eq!(spec.args[2],"--json");
        assert_eq!(spec.args[3],"exp_run");
        assert_eq!(spec.args[4],"pc_status");
    }
    #[test]
    fn exp_pages_send_through_flight_console_and_fill_readouts() {
        let mut app = App::new(true);
        app.select_tab(Tab::ExpDevices);
        for _ in 0..3 {
            app.tick();
        }
        assert!(app.exp.reads.keys().any(|k| k.starts_with("Power")), "overview auto-poll did not fill rails");
        let spec = command_spec(Action::ExpRun, Some("power_all_get".into())).unwrap();
        assert!(spec.program.ends_with("dal_flight_console"));
        assert_eq!(&spec.args[2..4], ["--json", "exp_run"]);
    }
    #[test]
    fn exp_tab_navigation_and_button_press() {
        let k = |c| KeyEvent::new(c, KeyModifiers::NONE);
        let mut app = App::new(true);
        app.select_tab(Tab::ExpDevices);
        app.handle(k(KeyCode::Right)); // section POWER
        app.handle(k(KeyCode::Enter)); // into peripheral list
        app.handle(k(KeyCode::Enter)); // into page
        assert!(app.exp.zone == exp::panel::Zone::Page);
        app.handle(k(KeyCode::Enter)); // first control
        app.tick();
        assert!(app.log.iter().any(|l| l.contains("EXP >")));
        app.handle(k(KeyCode::Tab)); // global tab still works
        assert!(app.tab != Tab::ExpDevices);
    }
    #[test]
    fn burn_fire_requires_a_second_enter() {
        let enter = KeyEvent::new(KeyCode::Enter, KeyModifiers::NONE);
        let mut app = App::new(true);
        app.select_tab(Tab::ExpDevices);
        app.exp.send_user("burn_fire");
        assert!(app.exp.confirm.is_some());
        app.handle(KeyEvent::new(KeyCode::Esc, KeyModifiers::NONE));
        assert!(app.exp.confirm.is_none());
        app.tick();
        assert!(!app.log.iter().any(|l| l.contains("burn_fire")));
        app.exp.send_user("burn_fire");
        app.handle(enter);
        app.tick();
        assert!(app.log.iter().any(|l| l.contains("burn_fire")));
    }
    #[test]
    fn camera_capture_keeps_usb_switch_logic_in_usb_cam() {
        let spec = command_spec(Action::Camera(2), None).unwrap();
        assert!(spec.program.ends_with("dal_flight_console"));
        assert!(spec.args[4].contains("/usb_cam' '2' '--width' '1280' '--height' '720'"));
    }
    #[test]
    fn board_controls_target_only_verified_interfaces() {
        let ai = command_spec(Action::Efuse("pow_onoff_ai", true), None).unwrap();
        assert!(ai.args[4].contains("'--gui-hw' 'gpio' 'pow_onoff_ai' 'on'"));
        let fpga = command_spec(Action::Efuse("pow_onoff_fpga", true), None).unwrap();
        assert!(fpga.args[4].contains("'--gui-hw' 'gpio' 'pow_onoff_fpga' 'on'"));
        let usb = command_spec(Action::UsbPower(4, false), None).unwrap();
        assert!(usb.args[4].contains("'--gui-hw' 'gpio' 'usb_4_pwr_en' 'off'"));
        let rtc = command_spec(Action::SetRtc, Some("1792939200".into())).unwrap();
        assert_eq!(&rtc.args[2..],["--json","sys_rtc","1792939200"]);
        let led = command_spec(Action::CameraLed, Some("45".into())).unwrap();
        assert!(led.args[4].contains("/dal_obc_fsw' '--gui-hw' 'camera-led' '45'"));
        assert!(command_spec(Action::CameraLed, Some("101".into())).is_err());
        assert!(!actions(Tab::ObcPower)
            .iter()
            .any(|a| a.label().contains("EXP")));
    }
    #[test]
    fn rtc_editor_accepts_calendar_time_and_rejects_bad_dates() {
        assert_eq!(parse_rtc_input("2026-10-25 12:00:00").unwrap(), 1792929600);
        assert_eq!(parse_rtc_input("2026-10-25T12:00:00Z").unwrap(), 1792929600);
        assert!(parse_rtc_input("2026-02-29 12:00:00").is_err());
        let mut input = TextInput::new("2026-10-25 12:00:00".into());
        input.edit(KeyCode::Home);
        input.edit(KeyCode::Right);
        input.edit(KeyCode::Delete);
        assert_eq!(input.value, "226-10-25 12:00:00");
    }
    #[test]
    fn mipi_actions_use_two_boards_and_four_sensor_slots() {
        let spec = command_spec(Action::MipiCapture, Some("2:3".into())).unwrap();
        assert_eq!(spec.args[2],"gui_shell");
        assert!(spec.args[4].contains("'--gui-hw' 'mipi-capture' '2' '3'"));
        let power = command_spec(Action::MipiPower(1, true), None).unwrap();
        assert!(power.args[4].contains("'--gui-hw' 'mipi-power' '1' 'on'"));
        assert!(command_spec(Action::MipiApply, Some("2:4".into())).is_err());
        let mut app = App::new(true);
        app.start(Action::MipiBoard(2), None);
        app.start(Action::MipiSensor(3), None);
        app.start(Action::MipiCapture, None);
        let lines = app.snapshots.mipi_cameras(app.mipi_board, app.mipi_sensor);
        assert!(lines.iter().any(|s| s.contains("MIPI CAMERA 2")));
        assert!(lines.iter().any(|s| s.contains("Sensor 3: ACTIVE")));
    }
    #[test]
    fn home_shows_three_boards_and_simulated_power_state() {
        let mut app = App::new(true);
        let cards = app.snapshots.home_cards();
        assert!(cards[0][0].contains("OBC"));
        assert!(cards[1][0].contains("EXP"));
        assert!(cards[2][0].contains("PDU ONLINE"));
        app.start(Action::UsbPower(2, true), None);
        assert!(app.snapshots.home_cards()[0]
            .iter()
            .any(|s| s == "Camera 2 USB: ON"));
    }
    #[test]
    fn all_pages_render_in_a_standard_terminal() {
        let mut terminal = Terminal::new(TestBackend::new(100, 28)).unwrap();
        for simulated in [false, true] {
            let mut app = App::new(simulated);
            for tab in Tab::ALL {
                app.select_tab(tab);
                terminal.draw(|frame| draw(frame, &app)).unwrap();
            }
            app.select_tab(Tab::ExpDevices);
            for page in 0..exp::catalog::PAGES.len() {
                app.exp.section = exp::catalog::PAGES[page].section;
                app.exp.page = page;
                terminal.draw(|frame| draw(frame, &app)).unwrap();
            }
        }
    }
    #[test]
    fn host_sim_actions_do_not_start_board_processes() {
        let mut app = App::new(true);
        app.start(Action::Efuse("pow_onoff_ai", false), None);
        assert!(app.rx.is_none());
        assert_eq!(
            app.snapshots.obc.as_ref().unwrap()["gpio_states"]["pow_onoff_ai"],
            false
        );
        assert_eq!(app.snapshots.exp.as_ref().unwrap()["online"], 1);
        app.start(Action::Camera(2), None);
        assert!(app.rx.is_none());
        assert!(app
            .log
            .iter()
            .any(|line| line.contains("No JPEG file was written")));
    }
}
