use dal_pdu_gui::{
    console::{same_port, Console, Event as ConsoleEvent},
    map::{self, Bank, Row},
    rtu::Client,
    worker::{Command, Config, Event, Worker},
};
use eframe::egui;
use std::{
    collections::VecDeque,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

struct App {
    config: Config,
    ports: Vec<String>,
    worker: Option<Worker>,
    connected: bool,
    rows: Vec<Row>,
    values: Vec<Result<Vec<u16>, String>>,
    edits: Vec<String>,
    bank: Bank,
    filter: String,
    last_update: Option<Instant>,
    message: String,
    log: VecDeque<String>,
    pending: Option<(usize, String)>,
    writing: bool,
    console: Option<Console>,
    console_connected: bool,
    console_shared: bool,
    console_port: String,
    console_baud: u32,
    console_input: String,
    console_output: String,
    console_status: String,
    console_view: bool,
}
impl Default for App {
    fn default() -> Self {
        let rows = map::rows();
        Self {
            config: Config {
                port: if cfg!(windows) {
                    "COM3"
                } else if cfg!(all(target_os = "linux", target_arch = "arm")) {
                    "/dev/ttyS3"
                } else {
                    "/dev/ttyUSB0"
                }
                .into(),
                baud: 115200,
                unit: 1,
                timeout_ms: 500,
            },
            ports: Vec::new(),
            worker: None,
            connected: false,
            values: vec![Err("Not read".into()); rows.len()],
            edits: vec![String::new(); rows.len()],
            rows,
            bank: Bank::Input,
            filter: String::new(),
            last_update: None,
            message: "Disconnected — choose a serial port to begin".into(),
            log: VecDeque::new(),
            pending: None,
            writing: false,
            console: None,
            console_connected: false,
            console_shared: false,
            console_port: if cfg!(windows) {
                "COM3"
            } else if cfg!(all(target_os = "linux", target_arch = "arm")) {
                "/dev/ttyS3"
            } else {
                "/dev/ttyUSB0"
            }
            .into(),
            console_baud: 115200,
            console_input: String::new(),
            console_output: String::new(),
            console_status: "Disconnected".into(),
            console_view: false,
        }
    }
}
impl App {
    fn note(&mut self, text: String) {
        self.message = text.clone();
        self.log.push_front(text);
        self.log.truncate(50);
    }
    fn append_console_output(&mut self, bytes: &[u8]) {
        self.console_output
            .push_str(&String::from_utf8_lossy(bytes).replace('\r', ""));
        if self.console_output.len() > 65536 {
            let mut start = self.console_output.len() - 65536;
            while !self.console_output.is_char_boundary(start) {
                start += 1;
            }
            self.console_output.drain(..start);
        }
    }
    fn send(&mut self, command: Command) -> bool {
        if let Some(worker) = &self.worker {
            if worker.commands.try_send(command).is_ok() {
                return true;
            }
        }
        self.note("Request not queued: connection busy or closed".into());
        false
    }
    fn receive(&mut self) {
        let events: Vec<_> = self
            .worker
            .as_ref()
            .map(|w| w.events.try_iter().collect())
            .unwrap_or_default();
        for event in events {
            match event {
                Event::Connected => {
                    self.connected = true;
                    self.note("Serial port open; reading PDU tables…".into());
                }
                Event::Failed(e) => {
                    self.worker = None;
                    self.connected = false;
                    if self.console_shared {
                        self.console_shared = false;
                        self.console_connected = false;
                        self.console_status = "Modbus link failed; console disconnected".into();
                    }
                    self.note(format!("Connection failed: {e}"));
                }
                Event::Snapshot(values, started) => {
                    let errors = values.iter().filter(|v| v.is_err()).count();
                    self.values = values;
                    self.last_update = Some(started);
                    if errors == self.rows.len() {
                        self.message =
                            "No valid replies — check link, slave address and baud".into();
                    } else {
                        self.message =
                            format!("Table refresh complete · {errors} unavailable rows");
                    }
                }
                Event::WriteResult(index, result) => {
                    self.writing = false;
                    match result {
                        Ok(()) => { self.edits[index].clear(); self.note(format!("{}: write acknowledged; refreshing values", self.rows[index].name)); }
                        Err(e) => self.note(format!("{}: write failed ({e}). Outcome may be unknown; inspect refreshed value before retrying.", self.rows[index].name)),
                    }
                }
                Event::ConsoleResult(result) => match result {
                    Ok(bytes) => self.append_console_output(&bytes),
                    Err(error) => self.console_status = format!("Text CLI: {error}"),
                },
            }
        }
        let console_events: Vec<_> = self
            .console
            .as_ref()
            .map(|c| c.events.try_iter().collect())
            .unwrap_or_default();
        for event in console_events {
            match event {
                ConsoleEvent::Connected => {
                    self.console_connected = true;
                    self.console_status = "Connected".into();
                }
                ConsoleEvent::Output(bytes) => self.append_console_output(&bytes),
                ConsoleEvent::Failed(error) => {
                    self.console = None;
                    self.console_connected = false;
                    self.console_status = format!("Console error: {error}");
                }
            }
        }
    }
    fn console_ui(&mut self, ui: &mut egui::Ui) {
        ui.horizontal_wrapped(|ui| {
            ui.add_enabled_ui(self.console.is_none() && !self.console_shared, |ui| {
                ui.label("Console port");
                ui.add(egui::TextEdit::singleline(&mut self.console_port).desired_width(180.0));
                ui.label("Baud");
                egui::ComboBox::from_id_salt("console_baud")
                    .selected_text(self.console_baud.to_string())
                    .show_ui(ui, |ui| {
                        ui.selectable_value(&mut self.console_baud, 115200, "115200 · UART0 RS232");
                        ui.selectable_value(&mut self.console_baud, 9600, "9600 · local PE3/PE4");
                    });
            });
            if self.console.is_none() && !self.console_shared {
                if ui.button("Open console").clicked() {
                    if self.console_port.trim().is_empty() {
                        self.console_status = "Enter a console port".into();
                    } else if self.worker.is_some()
                        && same_port(&self.console_port, &self.config.port)
                    {
                        if self.console_baud != self.config.baud {
                            self.console_status = "Shared port must use the Modbus baud".into();
                        } else if self.connected {
                            self.console_shared = true;
                            self.console_connected = true;
                            self.console_status = "Attached to Modbus UART0 link".into();
                        } else {
                            self.console_status =
                                "Wait for Modbus connection before attaching".into();
                        }
                    } else {
                        self.console =
                            Some(Console::start(self.console_port.clone(), self.console_baud));
                        self.console_status = "Opening console…".into();
                    }
                }
            } else if ui.button("Close console").clicked() {
                self.console = None;
                self.console_shared = false;
                self.console_connected = false;
                self.console_status = "Disconnected".into();
            }
            if ui.button("Clear output").clicked() {
                self.console_output.clear();
            }
        });
        ui.small("Text CLI: UART0 RS232 at 115200, or local PE3/PE4 console at 9600. Commands are sent with Enter.");
        ui.label(&self.console_status);
        ui.separator();
        let output_height = (ui.available_height() - 40.0).max(80.0);
        let output_rows = (output_height / 18.0).max(4.0) as usize;
        egui::ScrollArea::vertical()
            .stick_to_bottom(true)
            .auto_shrink([false, false])
            .max_height(output_height)
            .show(ui, |ui| {
                ui.add(
                    egui::TextEdit::multiline(&mut self.console_output)
                        .font(egui::TextStyle::Monospace)
                        .desired_width(f32::INFINITY)
                        .desired_rows(output_rows)
                        .interactive(false),
                );
            });
        ui.horizontal(|ui| {
            ui.label(">");
            let response = ui.add_enabled(
                self.console_connected,
                egui::TextEdit::singleline(&mut self.console_input).desired_width(f32::INFINITY),
            );
            let submit =
                response.lost_focus() && ui.input(|input| input.key_pressed(egui::Key::Enter));
            if submit
                || ui
                    .add_enabled(self.console_connected, egui::Button::new("Send"))
                    .clicked()
            {
                if self.console_shared {
                    let command = self.console_input.clone();
                    if command.len() > 63
                        || !command.bytes().all(|b| b.is_ascii_graphic() || b == b' ')
                    {
                        self.console_status = "Use at most 63 printable ASCII characters".into();
                    } else if self.send(Command::Console(command)) {
                        self.console_input.clear();
                    }
                } else if let Some(console) = &self.console {
                    match console.send(&self.console_input) {
                        Ok(()) => self.console_input.clear(),
                        Err(error) => self.console_status = error,
                    }
                }
            }
        });
    }
    fn content(&mut self, ctx: &egui::Context) {
        self.receive();
        egui::TopBottomPanel::top("connection").show(ctx, |ui| {
            ui.add_space(8.0);
            ui.horizontal(|ui| { ui.heading("DAL PDU"); ui.label("HW 1.1  /  Modbus RTU"); });
            ui.horizontal_wrapped(|ui| {
                ui.add_enabled_ui(self.worker.is_none(), |ui| {
                    ui.label("Port"); ui.add(egui::TextEdit::singleline(&mut self.config.port).desired_width(160.0));
                    egui::ComboBox::from_id_salt("ports").selected_text("Available ports").show_ui(ui, |ui| {
                        for port in &self.ports { ui.selectable_value(&mut self.config.port, port.clone(), port); }
                    });
                    if ui.button("Scan").clicked() {
                        match serialport::available_ports() {
                            Ok(ports) => self.ports = ports.into_iter().map(|p| p.port_name).collect(),
                            Err(e) => self.note(format!("Port scan: {e}. You can enter a port manually.")),
                        }
                    }
                    ui.label("Baud");
                    egui::ComboBox::from_id_salt("baud").selected_text(self.config.baud.to_string()).show_ui(ui, |ui| {
                        for baud in [1200,2400,4800,9600,19200,38400,115200] { ui.selectable_value(&mut self.config.baud, baud, baud.to_string()); }
                    });
                    ui.label("Slave"); ui.add(egui::DragValue::new(&mut self.config.unit).range(1..=247));
                    ui.label("Timeout ms"); ui.add(egui::DragValue::new(&mut self.config.timeout_ms).range(100..=5000));
                });
                if self.worker.is_none() {
                    if ui.button("Connect").clicked() {
                        if self.console.is_some() && same_port(&self.console_port, &self.config.port) {
                            self.note("Close the console before opening Modbus on the same port".into());
                        } else {
                            self.values.fill(Err("Waiting for first read".into())); self.edits.fill(String::new()); self.last_update = None;
                            self.worker = Some(Worker::start(self.config.clone())); self.note("Opening serial port…".into());
                        }
                    }
                } else if ui.button("Disconnect").clicked() {
                    self.worker = None; self.connected = false; self.writing = false; self.pending = None;
                    if self.console_shared { self.console_shared = false; self.console_connected = false; self.console_status = "Modbus link closed; console disconnected".into(); }
                    self.values.fill(Err("Disconnected".into())); self.last_update = None; self.note("Disconnected".into());
                }
                if ui.add_enabled(self.connected, egui::Button::new("Refresh")).clicked() { self.send(Command::Refresh); }
            });
            ui.small("8N1 · RS232: fixed 115200 · RS485: /dev/ttyS2 or auto-direction USB adapter · Polls about once per second after each scan");
            ui.add_space(6.0);
        });
        egui::TopBottomPanel::bottom("status").show(ctx, |ui| {
            ui.label(&self.message);
            if let Some(last) = self.last_update {
                let age = last.elapsed().as_secs_f32();
                let color = if age > 3.0 {
                    egui::Color32::YELLOW
                } else {
                    egui::Color32::LIGHT_GREEN
                };
                ui.colored_label(
                    color,
                    format!(
                        "Oldest sample up to {age:.1}s old{}",
                        if age > 3.0 {
                            " — values may be stale"
                        } else {
                            ""
                        }
                    ),
                );
            }
            ui.collapsing("Activity", |ui| {
                for message in &self.log {
                    ui.label(message);
                }
            });
        });
        egui::CentralPanel::default().show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.selectable_value(&mut self.console_view, false, "Registers");
                ui.selectable_value(&mut self.console_view, true, "Text console");
            });
            ui.separator();
            if self.console_view {
                self.console_ui(ui);
                return;
            }
            ui.horizontal(|ui| {
                for bank in [Bank::Input, Bank::Holding, Bank::Coil, Bank::Discrete] { ui.selectable_value(&mut self.bank, bank, bank.label()); }
                ui.separator(); ui.label("Filter"); ui.text_edit_singleline(&mut self.filter);
            });
            ui.add_space(8.0);
            ui.label("Addresses use the firmware's one-based notation. Hover a parameter for units, limits and behavior.");
            if self.bank == Bank::Holding && ui.button("Prepare PC time in clock editor").clicked() {
                if let Ok(t) = SystemTime::now().duration_since(UNIX_EPOCH) {
                    let index = self.rows.iter().position(|r| r.address == 40040).unwrap();
                    self.edits[index] = t.as_secs().to_string();
                }
            }
            egui::ScrollArea::both().auto_shrink([false,false]).show(ui, |ui| {
                egui::Grid::new("registers").num_columns(6).striped(true).spacing([18.0,10.0]).show(ui, |ui| {
                    for label in ["Address", "Parameter", "Current value", "Raw words", "New value", ""] { ui.strong(label); } ui.end_row();
                    for (index,row) in self.rows.iter().enumerate() {
                        if row.bank != self.bank || !format!("{} {}",row.address,row.name).to_lowercase().contains(&self.filter.to_lowercase()) { continue; }
                        ui.monospace(if row.count == 2 { format!("{:05}–{:05}",row.address,row.address+1) } else {format!("{:05}",row.address)});
                        ui.label(row.name).on_hover_text(row.hint);
                        match &self.values[index] {
                            Ok(words) => {
                                ui.label(format!("{} {}",row.display(words),row.unit));
                                ui.monospace(words.iter().map(|w| format!("{w:04X}")).collect::<Vec<_>>().join(" "));
                            }
                            Err(e) => { ui.colored_label(egui::Color32::LIGHT_RED,"Unavailable").on_hover_text(e); ui.label("—"); }
                        }
                        if row.writable() {
                            ui.add_enabled(self.connected && !self.writing,egui::TextEdit::singleline(&mut self.edits[index]).desired_width(120.0)).on_hover_text(row.hint);
                            if ui.add_enabled(self.connected && !self.writing && !self.edits[index].trim().is_empty(),egui::Button::new("Write…")).clicked() {
                                match row.parse(&self.edits[index]) {
                                    Ok(_) => self.pending = Some((index,self.edits[index].clone())),
                                    Err(e) => self.message = format!("{}: {e}",row.name),
                                }
                            }
                        } else { ui.label("Read only"); ui.label(""); }
                        ui.end_row();
                    }
                });
            });
        });
        if let Some((index, text)) = self.pending.clone() {
            let row = &self.rows[index];
            let mut apply = false;
            let mut cancel = false;
            egui::Window::new("Write PDU value")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(format!("Slave {} · {}", self.config.unit, self.config.port));
                    ui.strong(format!(
                        "{} ({:05}) → {} {}",
                        row.name, row.address, text, row.unit
                    ));
                    ui.label(row.hint);
                    ui.horizontal(|ui| {
                        apply = ui.button("Apply write").clicked();
                        cancel = ui.button("Cancel").clicked();
                    });
                });
            if apply {
                self.writing = self.send(Command::Write(index, text));
                self.pending = None;
            }
            if cancel {
                self.pending = None;
            }
        }
        ctx.request_repaint_after(Duration::from_millis(100));
    }
}
impl eframe::App for App {
    fn update(&mut self, ctx: &egui::Context, _: &mut eframe::Frame) {
        self.content(ctx);
    }
}
fn main() -> eframe::Result {
    let args: Vec<String> = std::env::args().collect();
    if args.get(1).map(String::as_str) == Some("--probe") {
        let result = (|| -> Result<(), String> {
            let port = args
                .get(2)
                .ok_or("Usage: dal-pdu-gui --probe PORT [BAUD] [SLAVE]")?;
            let baud = args
                .get(3)
                .map_or(Ok(115200), |v| v.parse::<u32>().map_err(|e| e.to_string()))?;
            let unit = args
                .get(4)
                .map_or(Ok(1), |v| v.parse::<u8>().map_err(|e| e.to_string()))?;
            let mut client = Client::open(port, baud, unit, Duration::from_millis(500))?;
            for row in map::rows() {
                let words = client.read(row.bank.function(), row.offset(), row.count)?;
                println!(
                    "{:05} {} = {} {}",
                    row.address,
                    row.name,
                    row.display(&words),
                    row.unit
                );
            }
            Ok(())
        })();
        if let Err(e) = result {
            eprintln!("{e}");
            std::process::exit(1);
        }
        return Ok(());
    }
    eframe::run_native(
        "DAL PDU Monitor",
        eframe::NativeOptions {
            viewport: egui::ViewportBuilder::default()
                .with_inner_size([1200.0, 780.0])
                .with_min_inner_size([900.0, 500.0]),
            ..Default::default()
        },
        Box::new(|cc| {
            cc.egui_ctx.set_visuals(egui::Visuals::dark());
            Ok(Box::<App>::default())
        }),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn gui_renders_every_table_without_a_display() {
        let ctx = egui::Context::default();
        let mut app = App::default();
        for bank in [Bank::Coil, Bank::Discrete, Bank::Input, Bank::Holding] {
            app.bank = bank;
            let output = ctx.run(egui::RawInput::default(), |ctx| app.content(ctx));
            assert!(!output.shapes.is_empty());
        }
        app.console_view = true;
        let output = ctx.run(egui::RawInput::default(), |ctx| app.content(ctx));
        assert!(!output.shapes.is_empty());
        app.console_view = false;
        app.pending = Some((0, "1".into()));
        let output = ctx.run(egui::RawInput::default(), |ctx| app.content(ctx));
        assert!(!output.shapes.is_empty());
    }
}
