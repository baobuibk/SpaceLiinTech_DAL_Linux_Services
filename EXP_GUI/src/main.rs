mod bubble_panel;
mod burn_panel;
mod camera_data;
mod camera_files;
mod camera_panel;
mod flow_panel;
mod heater_panel;
mod ir_panel;
mod limit_panel;
mod motor_panel;
mod motor_telemetry;
mod pac_panel;
mod power_status;
mod protocol;
mod sensor_panels;
mod serial_io;
mod theme;
mod transport;
mod uart_text;
use theme::*;
use transport::{worker, Link, Notice, Request};
#[cfg(test)]
use transport::save_download;

use eframe::egui;
use std::{sync::mpsc::{self, Receiver, Sender}, thread, time::Duration};

#[derive(Clone, Copy, PartialEq, Eq)]
enum Section {
    Home,
    Power,
    Pumps,
    Sensors,
    Actuators,
    Experiment,
}
impl Section {
    const ALL: [Self; 6] = [
        Self::Home,
        Self::Power,
        Self::Pumps,
        Self::Sensors,
        Self::Actuators,
        Self::Experiment,
    ];
    fn label(self) -> &'static str {
        match self {
            Self::Home => "HOME",
            Self::Power => "POWER",
            Self::Pumps => "PUMPS",
            Self::Sensors => "SENSORS",
            Self::Actuators => "ACTUATORS",
            Self::Experiment => "EXPERIMENT",
        }
    }
}
#[derive(Clone, Copy, PartialEq, Eq)]
enum Page {
    Overview,
    Rparam,
    Rails,
    Pac,
    U11,
    U15,
    Ntc,
    Bme,
    Imu,
    Flow,
    Rtc,
    Heaters,
    Motors,
    Limits,
    Ir,
    Camera0,
    Camera1,
    Bubble,
    Burn,
    Pc,
}
impl Page {
    const ALL: [Self; 20] = [
        Self::Overview,
        Self::Rparam,
        Self::Rails,
        Self::Pac,
        Self::U11,
        Self::U15,
        Self::Ntc,
        Self::Bme,
        Self::Imu,
        Self::Flow,
        Self::Rtc,
        Self::Heaters,
        Self::Motors,
        Self::Limits,
        Self::Ir,
        Self::Camera0,
        Self::Camera1,
        Self::Bubble,
        Self::Burn,
        Self::Pc,
    ];
    fn section(self) -> Section {
        match self {
            Self::Overview | Self::Rparam => Section::Home,
            Self::Rails | Self::Pac => Section::Power,
            Self::U11 | Self::U15 => Section::Pumps,
            Self::Ntc | Self::Bme | Self::Imu | Self::Flow | Self::Rtc => Section::Sensors,
            Self::Heaters
            | Self::Motors
            | Self::Limits
            | Self::Ir
            | Self::Camera0
            | Self::Camera1
            | Self::Bubble
            | Self::Burn => Section::Actuators,
            Self::Pc => Section::Experiment,
        }
    }
    fn label(self) -> &'static str {
        match self {
            Self::Overview => "Overview",
            Self::Rparam => "RPARAM tables",
            Self::Rails => "Switched rails",
            Self::Pac => "PAC1934",
            Self::U11 => "U11 single pump",
            Self::U15 => "U15 four-channel",
            Self::Ntc => "NTC",
            Self::Bme => "BME688",
            Self::Imu => "IMU",
            Self::Flow => "Flow sensors",
            Self::Rtc => "RTC",
            Self::Heaters => "Heaters",
            Self::Motors => "Motors / DRV8910",
            Self::Limits => "Optical limits",
            Self::Ir => "IR LEDs",
            Self::Camera0 => "IR camera 0",
            Self::Camera1 => "IR camera 1",
            Self::Bubble => "Bubble inputs",
            Self::Burn => "Burn wire",
            Self::Pc => "PC experiment",
        }
    }
}

struct Connection {
    port: String,
    baud: String,
    status: String,
    tx: Option<Sender<Request>>,
    rx: Option<Receiver<Notice>>,
    connected: bool,
}
impl Connection {
    fn new(link: Link) -> Self {
        Self {
            port: if cfg!(windows) {
                format!("COM{}", link.index() + 4)
            } else {
                format!("/dev/ttyUSB{}", link.index())
            },
            baud: if link == Link::Xcli { "230400" } else { "115200" }.into(),
            status: "Closed".into(),
            tx: None,
            rx: None,
            connected: false,
        }
    }
}

struct App {
    section: Section,
    page: Page,
    active_link: Link,
    links: [Connection; 4],
    available_ports: Vec<String>,
    settings_open: bool,
    console_command: String,
    xcli_command: String,
    remote_command: String,
    can_command: String,
    pump_type: Option<&'static str>,
    pump_freq: String,
    pump_volt: String,
    channel: String,
    table: String,
    motor_panel: motor_panel::MotorPanel,
    motor_status: [motor_telemetry::Telemetry; 4],
    limit_states: [limit_panel::State; 4],
    camera_panel: camera_panel::CameraPanel,
    camera_states: [camera_panel::State; 4],
    ir_panel: ir_panel::IrPanel,
    ir_states: [ir_panel::State; 4],
    bubble_states: [bubble_panel::State; 4],
    burn_states: [burn_panel::State; 4],
    heater_panel: heater_panel::HeaterPanel,
    heater_states: [heater_panel::State; 4],
    power_states: [power_status::State; 4],
    pac_states: [pac_panel::State; 4],
    sensor_states: [sensor_panels::State; 4],
    flow_states: [flow_panel::State; 4],
    uart_lines: [uart_text::Lines; 4],
    preview_frames: Option<usize>,
    log: Vec<String>,
}
impl Default for App {
    fn default() -> Self {
        Self {
            section: Section::Home,
            page: Page::Overview,
            active_link: Link::Console,
            links: std::array::from_fn(|i| Connection::new(Link::ALL[i])),
            available_ports: available_ports(),
            settings_open: false,
            console_command: String::new(),
            xcli_command: String::new(),
            remote_command: String::new(),
            can_command: String::new(),
            pump_type: None,
            pump_freq: "200".into(),
            pump_volt: "30".into(),
            channel: "0".into(),
            table: "0".into(),
            motor_panel: Default::default(),
            motor_status: std::array::from_fn(|_| Default::default()),
            limit_states: std::array::from_fn(|_| Default::default()),
            camera_panel: Default::default(),
            camera_states: std::array::from_fn(|_| Default::default()),
            ir_panel: Default::default(),
            ir_states: std::array::from_fn(|_| Default::default()),
            bubble_states: std::array::from_fn(|_| Default::default()),
            burn_states: std::array::from_fn(|_| Default::default()),
            heater_panel: Default::default(),
            heater_states: std::array::from_fn(|_| Default::default()),
            power_states: std::array::from_fn(|_| Default::default()),
            pac_states: std::array::from_fn(|_| Default::default()),
            sensor_states: std::array::from_fn(|_| Default::default()),
            flow_states: std::array::from_fn(|_| Default::default()),
            uart_lines: std::array::from_fn(|_| Default::default()),
            preview_frames: None,
            log: Vec::new(),
        }
    }
}
fn available_ports() -> Vec<String> {
    let mut ports: Vec<_> = serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .map(|port| port.port_name)
        .collect();
    #[cfg(target_os = "linux")]
    for directory in ["/dev/serial/by-id", "/dev"] {
        if let Ok(entries) = std::fs::read_dir(directory) {
            for entry in entries.flatten() {
                let name = entry.file_name().to_string_lossy().into_owned();
                if directory == "/dev" &&
                   !(name.starts_with("ttyUSB") || name.starts_with("ttyACM")) {
                    continue;
                }
                ports.push(entry.path().to_string_lossy().into_owned());
            }
        }
    }
    ports.sort();
    ports.dedup();
    ports
}
impl App {
    fn log(&mut self, msg: impl Into<String>) {
        self.log.push(msg.into());
        if self.log.len() > 1500 {
            self.log.drain(..500);
        }
    }
    fn connect(&mut self, link: Link) {
        if self.preview_frames.is_some() {
            return;
        }
        let connection = &mut self.links[link.index()];
        if let Some(tx) = connection.tx.take() {
            let _ = tx.send(Request::Stop);
        }
        let (tx, requests) = mpsc::channel();
        let (notices, rx) = mpsc::channel();
        let port = connection.port.clone();
        let baud = match connection.baud.parse::<u32>() {
            Ok(baud) if baud > 0 => baud,
            _ => {
                connection.status = "Invalid baud rate".into();
                return;
            }
        };
        thread::spawn(move || worker(link, port, baud, requests, notices));
        connection.tx = Some(tx);
        connection.rx = Some(rx);
        connection.status = format!("Opening {}", connection.port);
    }
    fn disconnect(&mut self, link: Link) {
        let connection = &mut self.links[link.index()];
        if let Some(tx) = connection.tx.take() {
            let _ = tx.send(Request::Stop);
        }
        connection.rx = None;
        connection.connected = false;
        connection.status = "Closed".into();
        self.motor_status[link.index()] = Default::default();
        self.camera_states[link.index()] = Default::default();
        self.ir_states[link.index()] = Default::default();
        self.heater_states[link.index()] = Default::default();
        self.bubble_states[link.index()] = Default::default();
        self.power_states[link.index()] = Default::default();
        self.pac_states[link.index()] = Default::default();
        self.sensor_states[link.index()] = Default::default();
        self.flow_states[link.index()] = Default::default();
        self.uart_lines[link.index()] = Default::default();
    }
    fn send_to(&mut self, link: Link, command: String) {
        if self.preview_frames.is_some() {
            return;
        }
        if self.camera_states[link.index()].busy {
            self.log(format!("{}: XMODEM active; command refused", link.label()));
            return;
        }
        if command.trim().is_empty() {
            return;
        }
        if !self.links[link.index()].connected {
            self.log(format!("{}: Open the port first", link.label()));
            return;
        }
        self.log(format!("QUEUE {} > {}", link.label(), command));
        if let Some(tx) = &self.links[link.index()].tx {
            if tx.send(Request::Command(command)).is_err() {
                self.log(format!("{}: Connection closed", link.label()));
            }
        } else {
            self.log(format!("{}: Connect first", link.label()));
        }
    }
    fn send(&mut self, command: String) {
        self.send_to(self.active_link, command);
    }
    fn camera_action(&mut self, link: Link, action: camera_panel::Action) {
        match action {
            camera_panel::Action::Command(command) => self.send_to(link, command),
            camera_panel::Action::Download(metadata, folder) => {
                if self.preview_frames.is_some() {
                    return;
                }
                if link != Link::Console || !self.links[link.index()].connected {
                    self.log("Open Console / UART2 before downloading images");
                    return;
                }
                let folder = match camera_files::folder(&folder) {
                    Ok(Some(path)) => path,
                    Ok(None) => {
                        self.camera_states[link.index()].message =
                            "Download cancelled; image retained on target".into();
                        return;
                    }
                    Err(error) => {
                        self.camera_states[link.index()].message = error;
                        return;
                    }
                };
                if let Some(tx) = &self.links[link.index()].tx {
                    if tx.send(Request::Download(metadata, folder)).is_ok() {
                        self.camera_states[link.index()].busy = true;
                        self.camera_states[link.index()].saved = None;
                        self.camera_states[link.index()].message =
                            "Starting XMODEM receiver...".into();
                    }
                }
            }
        }
    }
    fn button(&mut self, ui: &mut egui::Ui, label: &str, command: String) {
        let control = egui::Button::new(label);
        if ui.add(control).clicked() {
            self.send(command);
        }
    }
    fn group(&mut self, ui: &mut egui::Ui, title: &str, actions: &[(&str, &str)]) {
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(egui::RichText::new(title).color(YELLOW).strong());
            ui.add_space(8.0);
            ui.horizontal_wrapped(|ui| {
                for &(label, command) in actions {
                    self.button(ui, label, command.into());
                }
            });
        });
    }
    fn render_pump(&mut self, ui: &mut egui::Ui, four_channel: bool) {
        let prefix = if four_channel { "pump4" } else { "pump" };
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(title("01 / DRIVER SETUP"));
            if four_channel {
                ui.label("U15 / mp-Highdriver4 / fixed driver type");
            } else {
                egui::Grid::new("u11_driver_setup").num_columns(4).spacing([12.0,8.0]).show(ui,|ui| {
                    ui.label("Fitted driver");
                    egui::ComboBox::from_id_salt("u11_pump_type")
                        .selected_text(self.pump_type.unwrap_or("Select driver"))
                        .show_ui(ui,|ui| {
                            ui.selectable_value(&mut self.pump_type,Some("lowdriver"),"mp-Lowdriver (0x59)");
                            ui.selectable_value(&mut self.pump_type,Some("highdriver"),"mp-Highdriver (0x78)");
                        });
                    if ui.add_enabled(self.pump_type.is_some(),egui::Button::new("Set type")).clicked() {
                        if let Some(kind)=self.pump_type { self.send(format!("pump_type_set {kind}")); }
                    }
                    self.button(ui,"Read type","pump_type_get".into());
                    ui.end_row();
                });
                ui.label(egui::RichText::new("Select the fitted module with SLN/HD rail OFF. Dropdown values are drafts; Read type checks the board.").color(MUTED));
            }
            ui.horizontal_wrapped(|ui| {
                self.button(ui,"Detect",format!("{prefix}_detect"));
                self.button(ui,"Initialize",format!("{prefix}_init"));
            });
        });
        ui.add_space(12.0);
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(title("02 / OUTPUT SETTINGS"));
            egui::Grid::new(("pump_settings", four_channel))
                .num_columns(3)
                .spacing([16.0, 12.0])
                .show(ui, |ui| {
                    if four_channel {
                        ui.label("Output channel");
                        egui::ComboBox::from_id_salt("pump4_output_channel")
                            .selected_text(format!("Channel {}", self.channel))
                            .show_ui(ui, |ui| {
                                for channel in 0..=3 {
                                    ui.selectable_value(
                                        &mut self.channel,
                                        channel.to_string(),
                                        format!("Channel {channel}"),
                                    );
                                }
                            });
                        ui.label("Voltage target");
                        ui.end_row();
                    }
                    ui.label("Frequency / Hz");
                    ui.add(
                        egui::TextEdit::singleline(&mut self.pump_freq)
                            .desired_width(105.0)
                            .min_size(egui::vec2(105.0, 28.0)),
                    );
                    self.button(
                        ui,
                        "Set frequency",
                        format!("{prefix}_set_freq {}", self.pump_freq),
                    );
                    ui.end_row();
                    ui.label("Voltage / Vpp");
                    ui.add(
                        egui::TextEdit::singleline(&mut self.pump_volt)
                            .desired_width(105.0)
                            .min_size(egui::vec2(105.0, 28.0)),
                    );
                    let command = if four_channel {
                        format!("pump4_set_volt {} {}", self.channel, self.pump_volt)
                    } else {
                        format!("pump_set_volt {}", self.pump_volt)
                    };
                    self.button(ui, "Set voltage", command);
                    ui.end_row();
                });
            if four_channel {
                ui.label(
                    egui::RichText::new(
                        "Frequency and enable are shared; voltage applies to the selected channel.",
                    )
                    .color(MUTED),
                );
            }
        });
        ui.add_space(12.0);
        panel_frame().show(ui,|ui| {
            ui.set_min_width(ui.available_width());
            ui.label(title("03 / OPERATION & READBACK"));
            ui.horizontal_wrapped(|ui| {
                self.button(ui,"Enable",format!("{prefix}_enable"));
                self.button(ui,"Disable",format!("{prefix}_disable"));
                ui.separator();
                self.button(ui,"Read state",format!("{prefix}_state"));
                self.button(ui,"Hardware status",format!("{prefix}_status"));
            });
            ui.label(egui::RichText::new("Commands and replies appear in the console. Set values do not indicate measured output.").color(MUTED));
        });
    }
    fn render_page(&mut self, ui: &mut egui::Ui) {
        match self.page {
            Page::Overview => {
                ui.label(
                    "Select a peripheral on the left. Responses appear in the log on the right.",
                );
                self.group(
                    ui,
                    "System checks",
                    &[
                        ("Help", "help"),
                        ("Power", "power_all_get"),
                        ("Sensors", "sensor_status"),
                        ("RS485", "rs485_status"),
                        ("PC", "pc_status"),
                    ],
                );
            }
            Page::Rparam => {
                ui.label("Read an RPARAM table over RS485 or USB-CAN.");
                ui.horizontal(|ui| {
                    ui.label("Table 0-10");
                    ui.add(egui::TextEdit::singleline(&mut self.table).desired_width(50.0));
                    if ui
                        .add_enabled(
                            matches!(self.active_link, Link::Rs485 | Link::Can)
                                && self.links[self.active_link.index()].connected,
                            egui::Button::new("Read table"),
                        )
                        .clicked()
                    {
                        if let Ok(table) = self.table.parse::<u8>() {
                            if table <= 10 {
                                if let Some(tx) = &self.links[self.active_link.index()].tx {
                                    let _ = tx.send(Request::Table(table));
                                }
                            } else {
                                self.log("RPARAM table must be 0-10");
                            }
                        } else {
                            self.log("Enter a numeric RPARAM table");
                        }
                    }
                });
            }
            Page::Rails => {
                panel_frame().show(ui, |ui| {
                    ui.set_min_width(ui.available_width());
                    ui.label(title("SWITCHED POWER RAILS"));
                    self.button(ui, "Read all rails", "power_all_get".into());
                    ui.label(egui::RichText::new("Last reported ON/OFF command state; rail voltage is not measured. Read status to refresh.").color(MUTED));
                    ui.separator();
                    egui::Grid::new("power_rails")
                        .num_columns(5)
                        .spacing([16.0, 12.0])
                        .show(ui, |ui| {
                            for (i, (name, status, on, off)) in [
                                (
                                    "SLN / HD",
                                    "power_sln_hd_get",
                                    "power_sln_hd_on",
                                    "power_sln_hd_off",
                                ),
                                (
                                    "MOTOR",
                                    "power_motor_get",
                                    "power_motor_on",
                                    "power_motor_off",
                                ),
                                (
                                    "EXP 12 V",
                                    "power_exp12v_get",
                                    "power_exp12v_on",
                                    "power_exp12v_off",
                                ),
                            ].into_iter().enumerate() {
                                ui.label(egui::RichText::new(name).color(YELLOW));
                                let state = self.power_states[self.active_link.index()].rails[i];
                                let (label, color) = match state {
                                    Some(true) => ("ON", GREEN),
                                    Some(false) => ("OFF", MUTED),
                                    None => ("UNKNOWN", YELLOW),
                                };
                                ui.label(egui::RichText::new(label).color(color).strong());
                                self.button(ui, "Read status", status.into());
                                if ui.button("ON").clicked() {
                                    self.power_states[self.active_link.index()].rails[i] = None;
                                    self.send(on.into());
                                }
                                if ui.button("OFF").clicked() {
                                    self.power_states[self.active_link.index()].rails[i] = None;
                                    self.send(off.into());
                                }
                                ui.end_row();
                            }
                        });
                });
            }
            Page::Pac => {
                panel_frame().show(ui, |ui| {
                    ui.label(title("PAC1934"));
                    let index = self.active_link.index();
                    let ready = self.links[index].connected && !self.camera_states[index].busy;
                    ui.horizontal(|ui| {
                        if ui.add_enabled(ready, egui::Button::new("Detect")).clicked() {
                            self.pac_states[index].detect();
                            self.send("pac1934_detect".into());
                        }
                        let detection = self.pac_states[index].detection;
                        ui.label(egui::RichText::new(if detection.is_empty() { "Not checked" } else { detection })
                            .color(if detection == "Detected" { GREEN } else { YELLOW }));
                        if ui.add_enabled(ready, egui::Button::new("Refresh")).clicked() {
                            self.pac_states[index].refresh();
                            self.send("pac1934_read".into());
                        }
                    });
                    ui.label(&self.pac_states[index].message);
                    ui.separator();
                    egui::Grid::new("pac_readings").num_columns(4).spacing([24.0, 12.0]).show(ui, |ui| {
                        for label in ["Channel / rail", "Voltage (V)", "Current (A)", "Power (W)"] {
                            ui.label(egui::RichText::new(label).color(YELLOW));
                        }
                        ui.end_row();
                        for (i, name) in pac_panel::CHANNELS.iter().enumerate() {
                            ui.label(*name);
                            for n in 0..3 {
                                ui.label(self.pac_states[index].samples[i].map(|v| format!("{:.3}", v[n])).unwrap_or_else(|| "--".into()));
                            }
                            ui.end_row();
                        }
                    });
                    ui.label(egui::RichText::new(&self.pac_states[index].snapshot).color(MUTED));
                    ui.label(egui::RichText::new("Refresh reads the sensor cache. MOTOR measures the 12 V feed, not 5V_MOTOR.").color(MUTED));
                });
            }
            Page::U11 => self.render_pump(ui, false),
            Page::U15 => self.render_pump(ui, true),
            Page::Ntc | Page::Bme | Page::Imu | Page::Rtc => {
                let label = match self.page {
                    Page::Ntc => "NTC",
                    Page::Bme => "BME688",
                    Page::Imu => "IMU",
                    _ => "RTC",
                };
                let i = self.active_link.index();
                let ready = self.links[i].connected && !self.camera_states[i].busy;
                for command in self.sensor_states[i].render(ui, label, ready) {
                    self.send(command);
                }
            }
            Page::Flow => {
                let i = self.active_link.index();
                let ready = self.links[i].connected && !self.camera_states[i].busy;
                for command in self.flow_states[i].render(ui, ready) {
                    self.send(command);
                }
            }
            Page::Heaters => {
                for command in self
                    .heater_panel
                    .show(ui, &self.heater_states[self.active_link.index()])
                {
                    self.send(command);
                }
            }
            Page::Motors => {
                for command in self
                    .motor_panel
                    .show(ui, &self.motor_status[self.active_link.index()])
                {
                    self.send(command);
                }
            }
            Page::Limits => {
                let index = self.active_link.index();
                let ready = self.links[index].connected;
                for command in self.limit_states[index].render(ui, ready) {
                    self.send(command);
                }
            }
            Page::Ir => {
                for command in self
                    .ir_panel
                    .show(ui, &mut self.ir_states[self.active_link.index()])
                {
                    self.send(command);
                }
            }
            Page::Camera0 | Page::Camera1 => {
                let link = self.active_link;
                for action in self.camera_panel.show(
                    ui,
                    &mut self.camera_states[link.index()],
                    link == Link::Console,
                    if self.page == Page::Camera0 { 0 } else { 1 },
                ) {
                    self.camera_action(link, action);
                }
            }
            Page::Bubble => {
                for command in
                    bubble_panel::show(ui, &mut self.bubble_states[self.active_link.index()])
                {
                    self.send(command);
                }
            }
            Page::Burn => {
                let index = self.active_link.index();
                let ready = self.links[index].connected;
                for command in self.burn_states[index].render(ui, ready, &self.pac_states[index]) {
                    self.send(command);
                }
            }
            Page::Pc => self.group(ui, "PC experiment", &[("Status", "pc_status")]),
        }
    }
}
impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _: &mut eframe::Frame) {
        let quit_requested = ctx.memory(|memory| memory.focused().is_none())
            && ctx.input(|input| input.key_pressed(egui::Key::Q));
        if quit_requested {
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
            return;
        }
        if let Some(frames) = &mut self.preview_frames {
            if let Some(image) = ctx.input(|i| {
                i.events.iter().find_map(|event| {
                    if let egui::Event::Screenshot { image, .. } = event {
                        Some(image.clone())
                    } else {
                        None
                    }
                })
            }) {
                let mut bytes =
                    format!("P6\n{} {}\n255\n", image.size[0], image.size[1]).into_bytes();
                for pixel in &image.pixels {
                    bytes.extend_from_slice(&pixel.to_array()[..3]);
                }
                std::fs::write(
                    if matches!(self.page, Page::Camera0 | Page::Camera1) {
                        "camera-control-preview.ppm"
                    } else if self.page == Page::Heaters {
                        "heater-control-preview.ppm"
                    } else if self.page == Page::Ir {
                        "ir-control-preview.ppm"
                    } else if self.page == Page::Bubble {
                        "bubble-control-preview.ppm"
                    } else if self.page == Page::U11 {
                        "pump-control-preview.ppm"
                    } else if self.page == Page::U15 {
                        "pump4-control-preview.ppm"
                    } else if self.page == Page::Rails {
                        "power-control-preview.ppm"
                    } else if self.settings_open {
                        "settings-control-preview.ppm"
                    } else {
                        "motor-control-preview.ppm"
                    },
                    bytes,
                )
                .expect("write preview");
                ctx.send_viewport_cmd(egui::ViewportCommand::Close);
            }
            *frames += 1;
            if *frames == 8 {
                ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot(Default::default()));
            }
        }
        for link in Link::ALL {
            let messages: Vec<_> = self.links[link.index()]
                .rx
                .as_ref()
                .map(|rx| rx.try_iter().collect())
                .unwrap_or_default();
            for message in messages {
                match message {
                    Notice::Text(text) => {
                        self.motor_status[link.index()].feed(&text);
                        self.limit_states[link.index()].feed(&text);
                        self.camera_states[link.index()].feed(&text);
                        self.ir_states[link.index()].feed(&text);
                        self.heater_states[link.index()].feed(&text);
                        self.bubble_states[link.index()].feed(&text);
                        self.burn_states[link.index()].feed(&text);
                        self.power_states[link.index()].feed(&text);
                        self.pac_states[link.index()].feed(&text);
                        self.sensor_states[link.index()].feed(&text);
                        self.flow_states[link.index()].feed(&text);
                        for line in self.uart_lines[link.index()].feed(&text) {
                            self.log(format!("{}: RX < {line}", link.label()));
                        }
                    }
                    Notice::Downloaded(meta, bytes, path) => {
                        self.log(format!(
                            "{}: Saved {} bytes to {}",
                            link.label(),
                            bytes.len(),
                            path.display()
                        ));
                        self.camera_states[link.index()].received(meta, bytes, path);
                    }
                    Notice::DownloadFailed(error) => {
                        self.log(format!("{}: Download failed: {error}", link.label()));
                        self.camera_states[link.index()].busy = false;
                        self.camera_states[link.index()].message = error;
                    }
                    Notice::Line(line) => self.log(format!("{}: {line}", link.label())),
                    Notice::Connected(ok) => {
                        self.uart_lines[link.index()] = Default::default();
                        self.motor_status[link.index()] = Default::default();
                        self.limit_states[link.index()] = Default::default();
                        self.camera_states[link.index()] = Default::default();
                        self.ir_states[link.index()] = Default::default();
                        self.heater_states[link.index()] = Default::default();
                        self.bubble_states[link.index()] = Default::default();
                        self.burn_states[link.index()] = Default::default();
                        self.power_states[link.index()] = Default::default();
                        self.pac_states[link.index()] = Default::default();
                        self.sensor_states[link.index()] = Default::default();
                        self.flow_states[link.index()] = Default::default();
                        self.links[link.index()].connected = ok;
                        if ok {
                            self.send_to(link, "power_all_get".into());
                            self.send_to(link, "limit_status all".into());
                        }
                        if !ok {
                            self.links[link.index()].tx = None;
                        }
                    }
                    Notice::Status(s) => self.links[link.index()].status = s,
                }
            }
            if let Some(text) = self.uart_lines[link.index()].idle() {
                self.log(format!("{}: RX < {text}", link.label()));
            }
            if self.links[link.index()].connected && !self.camera_states[link.index()].busy {
                if let Some(action) = self.camera_states[link.index()].poll(link == Link::Console) {
                    self.camera_action(link, action);
                }

                if let Some(command) = self.flow_states[link.index()].poll() {
                    self.send_to(link, command);
                }
                for command in self.sensor_states[link.index()].poll() {
                    self.send_to(link, command);
                }
                if let Some(command) = self.pac_states[link.index()].poll() {
                    self.send_to(link, command);
                }
            }
        }
        egui::TopBottomPanel::top("header")
            .frame(egui::Frame::default().fill(BLUE).inner_margin(10.0))
            .show(ctx, |ui| {
                ui.horizontal(|ui| {
                    ui.label(
                        egui::RichText::new(if self.preview_frames.is_some() {
                            "EXP / TEST PANEL / DEMO DATA / NO HARDWARE"
                        } else {
                            "EXP  /  TEST GUI"
                        })
                        .color(egui::Color32::WHITE)
                        .strong()
                        .size(19.0),
                    );
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        ui.label(egui::RichText::new("Q: QUIT").color(egui::Color32::WHITE));
                        ui.label(
                            egui::RichText::new(format!(
                                "{} / {} LINKS OPEN",
                                self.links.iter().filter(|link| link.connected).count(),
                                self.links.len()
                            ))
                            .color(
                                if self.links.iter().any(|link| link.connected) {
                                    GREEN
                                } else {
                                    egui::Color32::WHITE
                                },
                            ),
                        );
                    });
                });
            });
        egui::TopBottomPanel::top("sections")
            .frame(egui::Frame::default().fill(BG).inner_margin(7.0))
            .show(ctx, |ui| {
                ui.horizontal_wrapped(|ui| {
                    for section in Section::ALL {
                        let selected = self.section == section;
                        let button = egui::Button::new(
                            egui::RichText::new(section.label())
                                .color(if selected {
                                    egui::Color32::WHITE
                                } else {
                                    MUTED
                                })
                                .strong(),
                        )
                        .fill(if selected { BLUE } else { PANEL });
                        if ui.add(button).clicked() {
                            self.section = section;
                            self.page = Page::ALL
                                .into_iter()
                                .find(|page| page.section() == section)
                                .unwrap();
                        }
                    }
                });
            });
        egui::TopBottomPanel::top("connection")
            .frame(egui::Frame::default().fill(BG).inner_margin(8.0))
            .show(ctx, |ui| {
                ui.horizontal_wrapped(|ui| {
                    if ui
                        .add(egui::Button::new("PORT SETTINGS").fill(BLUE))
                        .clicked()
                    {
                        self.settings_open = true;
                    }
                    for link in Link::ALL {
                        let connection = &self.links[link.index()];
                        let label = if connection.connected {
                            format!("{} {} OPEN", link.short(), connection.port)
                        } else {
                            format!("{} CLOSED", link.short())
                        };
                        ui.label(egui::RichText::new(label).color(if connection.connected {
                            GREEN
                        } else {
                            MUTED
                        }));
                        ui.separator();
                    }
                });
            });
        if self.settings_open {
            let mut settings_open = self.settings_open;
            egui::Window::new("Serial port settings")
                .open(&mut settings_open)
                .resizable(true)
                .default_width(920.0)
                .show(ctx, |ui| {
                    ui.horizontal(|ui| {
                        ui.label("Select a serial port for each independent interface.");
                        if ui.button("Refresh UART list").clicked() {
                            self.available_ports = available_ports();
                        }
                    });
                    ui.separator();
                    egui::Grid::new("serial_settings_grid")
                        .num_columns(7)
                        .spacing([12.0, 10.0])
                        .show(ui, |ui| {
                            for label in
                                ["INTERFACE", "PORT", "DISCOVERY", "BAUD", "", "", "STATUS"]
                            {
                                ui.label(egui::RichText::new(label).color(MUTED));
                            }
                            ui.end_row();
                            for link in Link::ALL {
                                let connection = &mut self.links[link.index()];
                                ui.label(egui::RichText::new(link.label()).color(YELLOW));
                                ui.add_enabled(
                                    connection.tx.is_none(),
                                    egui::TextEdit::singleline(&mut connection.port)
                                        .desired_width(180.0)
                                        .min_size(egui::vec2(180.0, 28.0)),
                                );
                                ui.add_enabled_ui(connection.tx.is_none(), |ui| {
                                    egui::ComboBox::from_id_salt(("port", link.index()))
                                        .selected_text("Choose UART")
                                        .show_ui(ui, |ui| {
                                            for port in &self.available_ports {
                                                if ui
                                                    .selectable_label(
                                                        connection.port == *port,
                                                        port,
                                                    )
                                                    .clicked()
                                                {
                                                    connection.port = port.clone();
                                                }
                                            }
                                        });
                                });
                                ui.add_enabled(
                                    connection.tx.is_none(),
                                    egui::TextEdit::singleline(&mut connection.baud)
                                        .desired_width(90.0)
                                        .min_size(egui::vec2(90.0, 28.0)),
                                );
                                let open = ui
                                    .add_enabled(
                                        connection.tx.is_none(),
                                        egui::Button::new("Open").fill(BLUE),
                                    )
                                    .clicked();
                                let close = ui
                                    .add_enabled(
                                        connection.tx.is_some(),
                                        egui::Button::new("Close"),
                                    )
                                    .clicked();
                                ui.label(
                                    egui::RichText::new(&connection.status)
                                        .color(if connection.connected { GREEN } else { MUTED }),
                                );
                                ui.end_row();
                                if open {
                                    self.connect(link);
                                }
                                if close {
                                    self.disconnect(link);
                                }
                            }
                        });
                });
            self.settings_open = settings_open;
        }
        egui::SidePanel::left("peripherals")
            .resizable(false)
            .default_width(205.0)
            .frame(egui::Frame::default().fill(BG).inner_margin(6.0))
            .show(ctx, |ui| {
                panel_frame().show(ui, |ui| {
                    ui.set_min_width(178.0);
                    ui.label(title("PERIPHERALS"));
                    ui.separator();
                    for page in Page::ALL
                        .into_iter()
                        .filter(|page| page.section() == self.section)
                    {
                        let selected = self.page == page;
                        let button = egui::Button::new(
                            egui::RichText::new(page.label()).color(if selected {
                                BG
                            } else {
                                egui::Color32::WHITE
                            }),
                        )
                        .fill(if selected { YELLOW } else { PANEL })
                        .min_size(egui::vec2(ui.available_width(), 31.0));
                        if ui.add(button).clicked() {
                            self.page = page;
                        }
                    }
                });
            });
        egui::SidePanel::right("command_log")
            .resizable(true)
            .default_width(540.0)
            .min_width(480.0)
            .frame(egui::Frame::default().fill(BG).inner_margin(6.0))
            .show(ctx, |ui| {
                panel_frame().show(ui, |ui| {
                    ui.set_min_width(ui.available_width());
                    ui.set_min_height(ui.available_height());
                    ui.label(title("CONSOLE / TX + RX"));
                    ui.label("QUEUE = requested | TX = written to link | RX = received");
                    for (link, label) in [
                        (Link::Console, "UART2"),
                        (Link::Xcli, "XCLI"),
                        (Link::Rs485, "RS485"),
                        (Link::Can, "CAN"),
                    ] {
                        if !self.links[link.index()].connected && link != self.active_link {
                            continue;
                        }
                        ui.add_enabled_ui(self.links[link.index()].connected, |ui| {
                            let input = match link {
                                Link::Console => &mut self.console_command,
                                Link::Xcli => &mut self.xcli_command,
                                Link::Rs485 => &mut self.remote_command,
                                Link::Can => &mut self.can_command,
                            };
                            let mut send = false;
                            let response = ui
                                .horizontal(|ui| {
                                    ui.label(label);
                                    let width = (ui.available_width() - 70.0).max(80.0);
                                    let response = ui.add_sized(
                                        [width, 30.0],
                                        egui::TextEdit::singleline(input)
                                            .hint_text("Enter command"),
                                    );
                                    send = ui.button("Send").clicked();
                                    response
                                })
                                .inner;
                            if send
                                || (response.lost_focus()
                                    && ui.input(|i| i.key_pressed(egui::Key::Enter)))
                            {
                                let command = std::mem::take(input);
                                self.send_to(link, command);
                            }
                        });
                    }
                    if ui.button("Clear log").clicked() {
                        self.log.clear();
                    }
                    ui.separator();
                    egui::ScrollArea::vertical()
                        .stick_to_bottom(true)
                        .auto_shrink([false, false])
                        .show(ui, |ui| {
                            for line in &self.log {
                                let color = if line.contains(": TX ") {
                                    YELLOW
                                } else if line.contains(": RX ") || line.contains(": Shell output:")
                                {
                                    GREEN
                                } else {
                                    MUTED
                                };
                                ui.label(egui::RichText::new(line).color(color).monospace());
                            }
                        });
                });
            });
        egui::CentralPanel::default()
            .frame(egui::Frame::default().fill(BG).inner_margin(6.0))
            .show(ctx, |ui| {
                panel_frame().show(ui, |ui| {
                    ui.set_min_width(ui.available_width());
                    ui.set_min_size(ui.available_size());
                    ui.label(title(self.page.label()));
                    ui.horizontal(|ui| {
                        ui.label("Peripheral commands via");
                        egui::ComboBox::from_id_salt("peripheral_route")
                            .selected_text(self.active_link.label())
                            .show_ui(ui, |ui| {
                                for link in Link::ALL {
                                    ui.selectable_value(&mut self.active_link, link, link.label());
                                }
                            });
                        let connected = self.links[self.active_link.index()].connected;
                        ui.label(
                            egui::RichText::new(if connected { "OPEN" } else { "CLOSED" })
                                .color(if connected { GREEN } else { MUTED }),
                        );
                    });
                    ui.separator();
                    egui::ScrollArea::vertical().show(ui, |ui| self.render_page(ui));
                });
            });
        ctx.request_repaint_after(Duration::from_millis(100));
    }
}

fn main() -> eframe::Result<()> {
    let pump_preview = std::env::args().any(|arg| arg == "--pump-preview");
    let pump4_preview = std::env::args().any(|arg| arg == "--pump4-preview");
    let power_preview = std::env::args().any(|arg| arg == "--power-preview");
    let settings_preview = std::env::args().any(|arg| arg == "--settings-preview");
    let bubble_preview = std::env::args().any(|arg| arg == "--bubble-preview");
    let heater_preview = std::env::args().any(|arg| arg == "--heater-preview");
    let ir_preview = std::env::args().any(|arg| arg == "--ir-preview");
    let camera_preview = std::env::args().any(|arg| arg == "--camera-preview");
    let preview = heater_preview
        || pump_preview
        || pump4_preview
        || power_preview
        || settings_preview
        || bubble_preview
        || ir_preview
        || camera_preview
        || std::env::args().any(|arg| arg == "--motor-preview" || arg.contains("motor_preview"));
    eframe::run_native(
        "EXP Peripheral Test GUI",
        eframe::NativeOptions {
            viewport: egui::ViewportBuilder::default()
                .with_inner_size([1550.0, 1000.0])
                .with_visible(!preview),
            ..Default::default()
        },
        Box::new(|cc| {
            configure_theme(&cc.egui_ctx);
            let mut app = App::default();
            if preview {
                cc.egui_ctx.set_pixels_per_point(1.0);
                app.section = Section::Actuators;
                app.page = Page::Motors;
                app.preview_frames = Some(0);
                app.motor_status[0].feed("DRV8910 initialized=1 asleep=0 faulted=0 diag=0 status=0xC1 age_ms=1\nMOTOR0 mode=MANUAL STOPPED position=10 reference_valid=0 dir=1 remaining=0 speed_sps=0 requested_sps=10 request=12 reason=completed\nMOTOR1 mode=MANUAL STOPPED position=0 reference_valid=0 dir=0 remaining=0 speed_sps=0 requested_sps=0 request=0 reason=initialized\n");
                for line in [
                    "DEMO TRANSCRIPT / illustrative replies only",
                    "Console (UART2): TX > drv8910_status",
                    "Console (UART2): RX < DRV8910 initialized=1 asleep=0 faulted=0",
                    "Console (UART2): TX > motor5_run 0 cw 20",
                    "Console (UART2): RX < ACCEPTED request=12 position=0",
                    "Console (UART2): TX > motor5_result 12",
                    "Console (UART2): RX < RUNNING request=12",
                    "Console (UART2): TX > motor5_stop 0",
                    "Console (UART2): RX < STOP_REQUESTED",
                    "Console (UART2): TX > motor5_status all",
                    "Console (UART2): RX < MOTOR0 STOPPED position=10 remaining=0",
                    "Console (UART2): RX < MOTOR0 STOPPED position=0 remaining=0",
                ] {
                    app.log(line);
                }
            }
            if heater_preview {
                app.page = Page::Heaters;
                app.section = Section::Actuators;
                app.log.clear();
                app.log("LAYOUT PREVIEW / illustrative values / no hardware connected");
                app.heater_states[0].feed("Heater duty: H0=5% H1=0% H2=0% H3=0%\nNTC[0] : 25.30 C age=52 ms raw=2045\nNTC[1] : 24.80 C age=52 ms raw=2031\nNTC[2] : INVALID age=52 ms raw=0\nNTC[3] : NO_SAMPLE\nBOARD_TEMP : 27.10 C age=52 ms raw=2110\n");
            }
            if camera_preview {
                app.page = Page::Camera1;
                app.log.clear();
                app.camera_states[0].feed("CAMERA buffer=0 DISABLED result=0\nCAMERA buffer=1 ENABLED result=0\nCAMERA camera=1 model=MLX90640 address=0x33 format=float32 hz=2 emissivity_milli=1000 initialized=1 data_exist=1 request=4 op=2 result=OK error=0 object=2 seq=1\n");
                let bytes: Vec<u8> = (0..768)
                    .flat_map(|i| {
                        let x = (i % 32) as f32;
                        let y = (i / 32) as f32;
                        let value =
                            23.0 + 14.0 * (-((x - 17.0).powi(2) + (y - 11.0).powi(2)) / 40.0).exp();
                        value.to_le_bytes()
                    })
                    .collect();
                let meta = camera_data::Metadata {
                    boot: 7,
                    object: 2,
                    kind: 3,
                    source: 1,
                    encoding: 2,
                    width: 32,
                    height: 24,
                    length: bytes.len(),
                    crc: camera_data::crc32(&bytes),
                    run: 0,
                    sequence: 0,
                    timestamp_ms: 0,
                };
                app.camera_states[0].metadata = Some(meta.clone());
                app.camera_states[0].received(
                    meta,
                    bytes,
                    std::path::PathBuf::from("DEMO / camera1.bin"),
                );
                for line in [
                    "DEMO TRANSCRIPT / illustrative replies only",
                    "Console (UART2): TX > camera_buffer_enable 1",
                    "Console (UART2): RX < CAMERA buffer=1 ENABLED result=0",
                    "Console (UART2): TX > camera_capture 1",
                    "Console (UART2): RX < CAMERA camera=1 request=4 result=QUEUED",
                    "Console (UART2): RX < XMODEM 3072 bytes; CRC32 verified",
                ] {
                    app.log(line);
                }
            }
            if ir_preview {
                app.page = Page::Ir;
                app.log.clear();
                app.ir_states[0].feed("IR0 active=0 duty_pct=0 remaining_ms=0 pwm_hz=100000 max_duty_pct=10 max_on_ms=20\nIR1 active=0 duty_pct=0 remaining_ms=0 pwm_hz=100000 max_duty_pct=5 max_on_ms=10\nIR active_id=0 reason=EXPIRED rail_requested=1 rail_feedback=UNAVAILABLE stop_pending=0 stop_sequence=1 reconciled=1 sequence=42\nIR DONE id=4 channel=0 reason=EXPIRED started_ms=200 finished_ms=210\n");
                for line in [
                    "DEMO TRANSCRIPT / illustrative replies only",
                    "Console (UART2): TX > ir_cfg_set 0 max_duty_pct 10",
                    "Console (UART2): RX < IR QUEUED id=1",
                    "Console (UART2): TX > ir_result 1",
                    "Console (UART2): RX < IR DONE id=1 channel=1 reason=CONFIGURED",
                    "Console (UART2): TX > ir_set 0 5 10",
                    "Console (UART2): RX < IR QUEUED id=4",
                    "Console (UART2): TX > ir_result 4",
                    "Console (UART2): RX < IR DONE id=4 channel=0 reason=EXPIRED",
                    "Console (UART2): TX > ir_status",
                    "Console (UART2): RX < IR0 active=0 duty_pct=0 remaining_ms=0",
                ] {
                    app.log(line);
                }
            }
            if bubble_preview {
                app.page = Page::Bubble;
                app.log.clear();
                app.bubble_states[0].feed("bubble0 OUT1=0 OUT2=1 (raw; polarity unqualified)\nbubble1 OUT1=1 OUT2=0 (raw; polarity unqualified)\n");
                for line in [
                    "DEMO TRANSCRIPT / illustrative replies only",
                    "Console (UART2): TX > bubble_raw 0",
                    "Console (UART2): RX < bubble0 OUT1=0 OUT2=1 (raw; polarity unqualified)",
                    "Console (UART2): TX > bubble_raw 1",
                    "Console (UART2): RX < bubble1 OUT1=1 OUT2=0 (raw; polarity unqualified)",
                ] {
                    app.log(line);
                }
            }
            if pump_preview || pump4_preview || power_preview || settings_preview {
                app.section = if power_preview {
                    Section::Power
                } else if settings_preview {
                    Section::Home
                } else {
                    Section::Pumps
                };
                app.page = if pump4_preview {
                    Page::U15
                } else if power_preview {
                    Page::Rails
                } else if settings_preview {
                    Page::Overview
                } else {
                    Page::U11
                };
                app.settings_open = settings_preview;
                app.log.clear();
                app.log("LAYOUT PREVIEW / disconnected / no commands sent");
            }
            Ok(Box::new(app))
        }),
    )
}

#[cfg(test)]
mod download_tests {
    use super::*;
    #[test]
    fn save_preserves_verified_bytes_and_metadata_without_overwriting() {
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let folder = std::env::temp_dir().join(format!("exp_camera_download_{stamp}"));
        let bytes = b"test camera object";
        let meta = camera_data::Metadata {
            boot: 7,
            object: 2,
            kind: 1,
            source: 0,
            encoding: 0,
            width: 0,
            height: 0,
            length: bytes.len(),
            crc: camera_data::crc32(bytes),
            run: 0,
            sequence: 0,
            timestamp_ms: 1234,
        };
        assert!(save_download(&folder, &meta, b"wrong").is_err());
        assert!(!folder.exists());
        let first = save_download(&folder, &meta, bytes).unwrap();
        let second = save_download(&folder, &meta, bytes).unwrap();
        assert_ne!(first, second);
        assert_eq!(std::fs::read(&first).unwrap(), bytes);
        let parsed = camera_data::Metadata::parse(
            &std::fs::read_to_string(first.with_extension("txt")).unwrap(),
        )
        .unwrap();
        assert_eq!(parsed, meta);
        for path in [first, second] {
            std::fs::remove_file(path.with_extension("txt")).unwrap();
            std::fs::remove_file(path).unwrap();
        }
        std::fs::remove_dir(folder).unwrap();
    }
}
