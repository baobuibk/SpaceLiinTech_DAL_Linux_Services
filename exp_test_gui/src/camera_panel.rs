use crate::camera_data::{field, Metadata};
use crate::theme::{panel_frame, title, GREEN, MUTED, YELLOW};
use eframe::egui;
use std::path::PathBuf;
use std::time::{Duration, Instant};

struct Workflow {
    camera: usize,
    op: u8,
    capture: bool,
    request: Option<u32>,
    object: Option<u32>,
    folder: PathBuf,
    next: Instant,
    deadline: Instant,
}

pub enum Action {
    Command(String),
    Download(Metadata, PathBuf),
}
pub struct Saved {
    pub meta: Metadata,
    pub path: PathBuf,
}
pub struct Image {
    pub meta: Metadata,
    pub path: PathBuf,
    pub values: Vec<f32>,
    texture: Option<egui::TextureHandle>,
}
#[derive(Default)]
pub struct State {
    pending: String,
    pub statuses: [String; 2],
    address_status: [String; 2],
    updated: [Option<Instant>; 2],
    buffers: [Option<bool>; 2],
    scanning: Option<usize>,
    addresses: [Vec<u8>; 2],
    pub metadata: Option<Metadata>,
    catalogue: Vec<Metadata>,
    inventory: String,
    object_selection: String,
    page_after: u32,
    listing: bool,
    pub saved: Option<Saved>,
    pub images: [Option<Image>; 2],
    pub busy: bool,
    pub message: String,
    detection: [String; 2],
    initialized: [Option<bool>; 2],
    scan_status: [String; 2],
    workflow: Option<Workflow>,
    follow_up: Option<Action>,
    status_requested: bool,
    startup_status: Option<(usize, Instant)>,
    configuration: [String; 2],
    configuring: Option<(usize, Instant)>,
    capture_status: [String; 2],
}
impl State {
    fn begin(&mut self, camera: usize, op: u8, capture: bool, folder: PathBuf) -> Action {
        self.metadata = None;
        self.message = if capture {
            "Capturing image..."
        } else {
            "Checking camera..."
        }
        .into();
        if capture {
            self.images[camera] = None;
            self.capture_status[camera] = "Capturing...".into();
        }
        if op == 0 {
            self.detection[camera] = "Detecting...".into();
        }
        if op == 1 {
            self.initialized[camera] = None;
        }
        self.workflow = Some(Workflow {
            camera,
            op,
            capture,
            request: None,
            object: None,
            folder,
            next: Instant::now() + Duration::from_millis(500),
            deadline: Instant::now() + Duration::from_secs(120),
        });
        Action::Command(format!(
            "{} {camera}",
            ["camera_detect", "camera_init", "camera_capture"][op as usize]
        ))
    }
    pub fn poll(&mut self, uart2: bool) -> Option<Action> {
        if self.busy {
            return None;
        }
        if self.configuring.is_some_and(|(_, at)| Instant::now() >= at) {
            let (camera, _) = self.configuring.take().unwrap();
            self.configuration[camera] = "Unavailable: configuration response timed out".into();
        }
        if self.follow_up.is_some() {
            return self.follow_up.take();
        }
        if self.workflow.is_none()
            && self.configuring.is_none()
            && self
                .startup_status
                .is_some_and(|(_, at)| Instant::now() >= at)
        {
            let (camera, _) = self.startup_status.take().unwrap();
            return Some(Action::Command(format!("camera_status {camera}")));
        }
        let w = self.workflow.as_mut()?;
        let now = Instant::now();
        if now >= w.deadline {
            if w.op == 0 {
                self.detection[w.camera] = "Unavailable: timeout".into();
            }
            if w.capture {
                self.capture_status[w.camera] = "Timed out; read status before retrying".into();
            }
            self.message =
                "Camera operation timed out; inspect status before retrying. Data retained.".into();
            self.workflow = None;
            return None;
        }
        if let Some(object) = w.object {
            if !uart2 {
                self.message = "Image captured and retained. Use Console / UART2 to read metadata and download for display.".into();
                self.workflow = None;
                return Some(Action::Command(format!("data_ready_collect {object}")));
            }
            if let Some(meta) = self.metadata.as_ref() {
                if meta.kind == 3 && meta.source == w.camera as u32 && meta.object == object {
                    if uart2 {
                        let action = Action::Download(meta.clone(), w.folder.clone());
                        self.workflow = None;
                        return Some(action);
                    }
                } else {
                    self.message = "Waiting for the captured object metadata; stored logs and other images are retained.".into();
                    self.metadata = None;
                }
            }
        }
        if now < w.next {
            return None;
        }
        w.next = now + Duration::from_millis(500);
        Some(Action::Command(if let Some(id) = w.object {
            format!("data_ready_collect {id}")
        } else {
            format!("camera_status {}", w.camera)
        }))
    }
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
        if let Some((camera, _)) = self.configuring {
            if ["OK", "BUSY", "INVALID", "PERSIST_ERROR", "RESTART_REQUIRED"].contains(&line)
                || line.starts_with("ERROR ")
            {
                if line == "OK" {
                    self.initialized[camera] = Some(false);
                    self.follow_up = Some(Action::Command(format!("camera_status {camera}")));
                }
                self.configuration[camera] = if line == "OK" {
                    "Applied".into()
                } else {
                    format!("Not applied: {line}")
                };
                self.configuring = None;
            }
        }
        if line.starts_with("DATA inventory ") {
            self.inventory = line.into();
        }
        if let Some(m) = Metadata::parse(line) {
            self.catalogue
                .retain(|old| old.boot == m.boot && old.object != m.object);
            self.catalogue.push(m.clone());
            self.catalogue.sort_by_key(|item| item.object);
            self.object_selection = m.object.to_string();
            self.metadata = Some(m);
        }
        if line.starts_with("DATA page ") {
            self.page_after = field(line, "after=")
                .and_then(|s| s.parse().ok())
                .unwrap_or(0);
            if self.listing {
                if field(line, "count=") == Some("8") {
                    self.follow_up =
                        Some(Action::Command(format!("data_list {}", self.page_after)));
                } else {
                    self.listing = false;
                }
            }
        }
        if line.contains("DATA result=1 (1=EMPTY)") {
            self.metadata = None;
            self.catalogue.clear();
        }
        if let Some(start) = line.find("CAMERA camera=") {
            let line = &line[start..];
            if let Some(id) = field(line, "camera=")
                .and_then(|v| v.parse::<usize>().ok())
                .filter(|i| *i < 2)
            {
                if let Some(value) = field(line, "initialized=") {
                    self.initialized[id] = Some(value == "1");
                }
                if field(line, "initialized=").is_some() || field(line, "model=").is_some() {
                    self.statuses[id] = line.into();
                }
                if self.initialized[id] == Some(true) && self.detection[id].is_empty() {
                    self.detection[id] = "Detected".into();
                }
                self.updated[id] = Some(Instant::now());
                let result = field(line, "result=").unwrap_or("");
                let manual_status = self.workflow.as_ref().map_or(true, |w| w.camera != id);
                if manual_status && field(line, "op=") == Some("0") {
                    self.detection[id] = match result {
                        "OK" => format!(
                            "Detected at {}",
                            field(line, "address=").unwrap_or("unknown address")
                        ),
                        "QUEUED" | "RUNNING" => "Detecting...".into(),
                        "" => self.detection[id].clone(),
                        _ => format!("Unavailable: {result}"),
                    };
                }
                if manual_status && field(line, "op=") == Some("2") {
                    self.capture_status[id] = format!(
                        "{result}; image retained={}",
                        field(line, "data_exist=").unwrap_or("unknown")
                    );
                }
                if let Some(w) = self.workflow.as_mut().filter(|w| w.camera == id) {
                    let request = field(line, "request=").and_then(|v| v.parse::<u32>().ok());
                    if result == "QUEUED" && w.request.is_none() {
                        w.request = request;
                    }
                    let matching = w.request.is_some()
                        && request == w.request
                        && field(line, "op=").and_then(|v| v.parse::<u8>().ok()) == Some(w.op);
                    if matching && w.capture {
                        self.capture_status[id] = format!(
                            "{result}; image retained={}",
                            field(line, "data_exist=").unwrap_or("unknown")
                        );
                    }
                    if matching && result != "QUEUED" && result != "RUNNING" {
                        let camera = w.camera;
                        if result != "OK" {
                            if w.op == 0 {
                                self.detection[camera] = "Unavailable".into();
                                self.follow_up =
                                    Some(Action::Command(format!("camera_scan {camera}")));
                            }
                            self.message = format!(
                                "Camera {camera}: {result}; {}",
                                field(line, "error=").unwrap_or("")
                            );
                            if w.capture {
                                self.capture_status[camera] = format!("Failed: {result}");
                            }
                            self.workflow = None;
                        } else if w.op == 0 {
                            self.detection[camera] = format!(
                                "Detected at {}",
                                field(line, "address=").unwrap_or("unknown address")
                            );
                            self.message = self.detection[camera].clone();
                            self.workflow = None;
                            self.follow_up = Some(Action::Command(format!("camera_scan {camera}")));
                        } else if w.capture && w.op == 1 {
                            w.op = 2;
                            w.request = None;
                            self.follow_up =
                                Some(Action::Command(format!("camera_capture {camera}")));
                        } else if w.capture && w.op == 2 && field(line, "data_exist=") == Some("1")
                        {
                            w.object = field(line, "object=").and_then(|v| v.parse().ok());
                            w.next = Instant::now() + Duration::from_millis(500);
                            self.message = "Image captured; reading download metadata...".into();
                        } else {
                            self.message = format!("Camera {camera}: {result}");
                            self.workflow = None;
                        }
                    } else if w.request.is_none()
                        && !["QUEUED", "RUNNING", "OK", ""].contains(&result)
                    {
                        if w.capture && w.op == 2 && result == "NOT_INITIALIZED" {
                            self.initialized[id] = Some(false);
                            w.op = 1;
                            w.request = None;
                            self.follow_up = Some(Action::Command(format!("camera_init {id}")));
                            self.message = "Initializing before capture...".into();
                        } else {
                            self.message = format!("Camera {id}: {result}");
                            if w.capture {
                                self.capture_status[id] = format!("Failed: {result}");
                            }
                            if w.op == 0 {
                                self.detection[id] = "Unavailable".into();
                            }
                            self.workflow = None;
                        }
                    }
                }
            }
        }
        if line.contains("CAMERA address camera=") {
            if let Some(id) = field(line, "camera=")
                .and_then(|v| v.parse::<usize>().ok())
                .filter(|id| *id < 2)
            {
                self.address_status[id] = line.into();
            }
        }
        if line.contains("CAMERA buffer=") {
            if let Some(id) = field(line, "buffer=")
                .and_then(|v| v.parse::<usize>().ok())
                .filter(|i| *i < 2)
            {
                self.buffers[id] = if field(line, "result=") == Some("0") {
                    Some(line.contains(" ENABLED "))
                } else {
                    None
                };
            }
        }
        if line.contains("CAMERA scan camera=") {
            self.scanning = field(line, "camera=")
                .and_then(|v| v.parse::<usize>().ok())
                .filter(|i| *i < 2);
            if let Some(id) = self.scanning {
                self.addresses[id].clear();
                self.scan_status[id] = "Scanning...".into();
            }
        }
        if let Some(id) = self.scanning {
            if line.contains("scan complete") || line.contains("ERROR") {
                self.scan_status[id] = if line.contains("ERROR") {
                    line.into()
                } else {
                    format!(
                        "{} available I2C address(es); shared upstream devices may also appear",
                        self.addresses[id].len()
                    )
                };
                self.scanning = None;
            } else if line.as_bytes().get(2) == Some(&b':')
                && u8::from_str_radix(&line[..2], 16).is_ok()
            {
                for token in line[3..].split_whitespace() {
                    if let Ok(a) = u8::from_str_radix(token, 16) {
                        if (8..=0x77).contains(&a) && !self.addresses[id].contains(&a) {
                            self.addresses[id].push(a);
                        }
                    }
                }
            }
        }
    }
    pub fn received(&mut self, meta: Metadata, bytes: Vec<u8>, path: PathBuf) {
        self.busy = false;
        self.message = format!(
            "Saved {} bytes, CRC32 verified: {}",
            bytes.len(),
            path.display()
        );
        if meta.kind == 3 {
            match meta.temperatures(&bytes) {
                Ok(values) => {
                    self.images[meta.source as usize] = Some(Image {
                        meta: meta.clone(),
                        path: path.clone(),
                        values,
                        texture: None,
                    })
                }
                Err(error) => self
                    .message
                    .push_str(&format!("; image display refused: {error}")),
            }
        }
        self.saved = Some(Saved { meta, path });
    }
}
pub struct CameraPanel {
    cameras: [CameraControls; 2],
}
impl Default for CameraPanel {
    fn default() -> Self {
        Self {
            cameras: std::array::from_fn(|id| CameraControls {
                selected: id,
                ..Default::default()
            }),
        }
    }
}
impl CameraPanel {
    pub fn show(
        &mut self,
        ui: &mut egui::Ui,
        state: &mut State,
        uart2: bool,
        selected: usize,
    ) -> Vec<Action> {
        let mut actions = Vec::new();
        self.cameras[1].folder = self.cameras[0].folder.clone();
        if !state.status_requested {
            state.status_requested = true;
            state.startup_status =
                Some((1 - selected, Instant::now() + Duration::from_millis(500)));
            actions.push(Action::Command(format!("camera_status {selected}")));
        }
        ui.push_id(("camera_controls", selected), |ui| {
            actions.extend(self.cameras[selected].show(ui, state))
        });
        ui.separator();
        ui.push_id(("camera_image", selected), |ui| {
            self.cameras[selected].show_image(ui, state)
        });
        ui.add_space(8.0);
        actions.extend(self.cameras[selected].downloads(ui, state, uart2));
        actions
    }
}
struct CameraControls {
    selected: usize,
    model: usize,
    address: String,
    new_address: String,
    encoding: usize,
    hz: u8,
    emissivity: u16,
    folder: String,
    #[cfg(not(windows))]
    import_path: String,
    scale: f32,
}
impl Default for CameraControls {
    fn default() -> Self {
        Self {
            selected: 0,
            model: 0,
            address: "0x33".into(),
            new_address: "0x34".into(),
            encoding: 1,
            hz: 2,
            emissivity: 1000,
            folder: "camera_downloads".into(),
            #[cfg(not(windows))]
            import_path: String::new(),
            scale: 10.0,
        }
    }
}
fn parse_address(value: &str) -> Option<u8> {
    let value = value.trim();
    let address = if let Some(hex) = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
    {
        u8::from_str_radix(hex, 16).ok()
    } else {
        value.parse::<u8>().ok()
    };
    address.filter(|a| (8..=0x77).contains(a))
}
fn action(ui: &mut egui::Ui, out: &mut Vec<Action>, label: &str, cmd: String) {
    if ui.button(label).clicked() {
        out.push(Action::Command(cmd));
    }
}
pub(crate) fn color(t: f32) -> egui::Color32 {
    let stops = [
        [9.0, 12.0, 50.0],
        [40.0, 35.0, 160.0],
        [200.0, 30.0, 100.0],
        [255.0, 140.0, 20.0],
        [255.0, 245.0, 150.0],
    ];
    let v = t.clamp(0.0, 1.0) * 4.0;
    let index = (v as usize).min(3);
    let part = v - index as f32;
    egui::Color32::from_rgb(
        (stops[index][0] * (1.0 - part) + stops[index + 1][0] * part) as u8,
        (stops[index][1] * (1.0 - part) + stops[index + 1][1] * part) as u8,
        (stops[index][2] * (1.0 - part) + stops[index + 1][2] * part) as u8,
    )
}
impl CameraControls {
    pub fn show(&mut self, ui: &mut egui::Ui, state: &mut State) -> Vec<Action> {
        let mut out = Vec::new();
        let id = self.selected;
        ui.label(title(&format!(
            "CAMERA {id} / {}",
            if id == 0 { "J15" } else { "J17" }
        )));
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.label(egui::RichText::new(format!("SETTINGS / CAMERA {id}")).color(YELLOW));
            ui.add_enabled_ui(!state.busy && state.workflow.is_none() && state.configuring.is_none(), |ui| {
                ui.columns(2,|columns| {
                    egui::Grid::new("camera_identity_fields").num_columns(2).spacing([12.0,8.0]).show(&mut columns[0],|ui| {
                        ui.label("Model");
                        egui::ComboBox::from_id_salt("camera_model").selected_text(["MLX90640","MLX90642"][self.model]).show_ui(ui,|ui| {ui.selectable_value(&mut self.model,0,"MLX90640");ui.selectable_value(&mut self.model,1,"MLX90642");});ui.end_row();
                        ui.label("I2C address");ui.add(egui::TextEdit::singleline(&mut self.address).desired_width(90.0).min_size(egui::vec2(90.0,28.0)));ui.end_row();
                        ui.label("Encoding");
                        egui::ComboBox::from_id_salt("camera_format").selected_text(["int16","float32"][self.encoding]).show_ui(ui,|ui| {ui.selectable_value(&mut self.encoding,0,"int16");ui.selectable_value(&mut self.encoding,1,"float32");});ui.end_row();
                    });
                    egui::Grid::new("camera_capture_fields").num_columns(2).spacing([12.0,8.0]).show(&mut columns[1],|ui| {
                        ui.label("Refresh / Hz");
                        egui::ComboBox::from_id_salt("camera_hz").selected_text(format!("{} Hz",self.hz)).show_ui(ui,|ui| {for hz in [2,4,8,16] {ui.selectable_value(&mut self.hz,hz,format!("{hz} Hz"));}});ui.end_row();
                        ui.label("Emissivity /1000");ui.add(egui::DragValue::new(&mut self.emissivity).range(100..=1000));ui.end_row();
                    });
                });
            });
            ui.label("Edit this camera's settings, then Apply configuration. Applying settings does not program the sensor's I2C address.");
            ui.add_enabled_ui(!state.busy && state.workflow.is_none() && state.configuring.is_none(), |ui| {
                ui.horizontal_wrapped(|ui| {
                    action(ui, &mut out, "Enable buffer", format!("camera_buffer_enable {id}"));
                    action(ui, &mut out, "Disable buffer", format!("camera_buffer_disable {id}"));
                    if ui.button("Detect").clicked() { out.push(state.begin(id, 0, false, PathBuf::new())); }
                    if ui.button("Initialize").clicked() {out.push(state.begin(id,1,false,PathBuf::new()));}
                    action(ui, &mut out, "Read status", format!("camera_status {id}"));
                    if ui.add_enabled(parse_address(&self.address).is_some(), egui::Button::new("Apply configuration")).clicked() {
                        state.configuration[id] = "Applying...".into();
                        state.configuring = Some((id, Instant::now() + Duration::from_secs(15)));
                        out.push(Action::Command(format!("camera_config {id} {} {} {} {} {}", ["mlx90640", "mlx90642"][self.model], parse_address(&self.address).unwrap(), ["int16", "float32"][self.encoding], self.hz, self.emissivity)));
                    }
                    if ui.button("Capture image").clicked() {
                        let initialized = state.initialized[id]==Some(true);
                        out.push(state.begin(id, if initialized { 2 } else { 1 }, true, PathBuf::from(self.folder.trim())));
                    }
                });
            });
            ui.small(format!("Configuration draft: {} / {} / {} / {} Hz / emissivity {}", ["MLX90640", "MLX90642"][self.model], self.address, ["int16", "float32"][self.encoding], self.hz, self.emissivity));
            ui.label(if state.detection[id].is_empty() { "Detection: not checked" } else { &state.detection[id] });
            ui.label(format!("Initialized: {}",if state.workflow.as_ref().is_some_and(|w|w.camera==id && w.op==1) {"INITIALIZING..."} else {match state.initialized[id] {Some(true)=>"YES",Some(false)=>"NO",None=>"UNKNOWN"}}));
            ui.label(format!("Configuration: {}", if state.configuration[id].is_empty() { "not applied in this session" } else { &state.configuration[id] }));
            ui.label(format!("Capture: {}", if state.capture_status[id].is_empty() { "not requested" } else { &state.capture_status[id] }));
            ui.label(if state.statuses[id].is_empty() {
                "UNKNOWN / Request status"
            } else {
                &state.statuses[id]
            });
            if !state.address_status[id].is_empty() {
                ui.label(egui::RichText::new(&state.address_status[id]).color(YELLOW));
                if state.address_status[id].contains("restart_required=1") {
                    ui.label(egui::RichText::new("Camera power cycle required. Buffer OFF/ON does not power-cycle the sensor. Keep isolated, then Detect/Poll status at the new address.").color(YELLOW));
                }
            }
            if let Some(t) = state.updated[id] {
                ui.label(format!(
                    "Received {:.1}s ago{}",
                    t.elapsed().as_secs_f32(),
                    if t.elapsed().as_secs() >= 5 {
                        " / STALE"
                    } else {
                        ""
                    }
                ));
            }
            ui.horizontal_wrapped(|ui| {
                for camera in [id] {
                    ui.label(format!(
                        "Buffer {camera}: {}",
                        match state.buffers[camera] {
                            Some(true) => "ENABLED",
                            Some(false) => "DISABLED",
                            None => "UNKNOWN",
                        }
                    ));
                }
            });
        });
        ui.add_space(8.0);
        egui::CollapsingHeader::new("I2C scan / advanced address programming").show(ui, |ui| {
        ui.add_enabled_ui(!state.busy && state.workflow.is_none() && state.configuring.is_none(),|ui|{
            panel_frame().show(ui,|ui|{
            ui.set_min_width(ui.available_width());
                ui.strong("I2C1 BUFFER & DEVICE");
                ui.horizontal_wrapped(|ui|{
                    action(ui,&mut out,"Enable buffer",format!("camera_buffer_enable {id}"));
                    action(ui,&mut out,"Disable buffer",format!("camera_buffer_disable {id}"));
                    action(ui,&mut out,"Scan I2C",format!("camera_scan {id}"));
                    action(ui,&mut out,"Status",format!("camera_status {id}"));
                });
                ui.label("Both buffers may be ON after verifying distinct addresses. Isolate the selected branch before programming an address. Scan/capture never switch buffers.");
                ui.horizontal_wrapped(|ui|{ui.label("Scan ACKs (click address to configure):");for address in &state.addresses[id]{if ui.button(format!("0x{address:02X}")).clicked(){self.address=format!("0x{address:02X}");}}});
                ui.label(&state.scan_status[id]);
                ui.strong("PROGRAM SENSOR I2C ADDRESS / EEPROM");
                ui.horizontal_wrapped(|ui| {
                    ui.label("New sensor address");
                    ui.add(egui::TextEdit::singleline(&mut self.new_address).desired_width(70.0));
                    let address=parse_address(&self.new_address);
                    let isolated=state.buffers[id]==Some(true) && state.buffers[1-id]==Some(false);
                    if ui.add_enabled(isolated && address.is_some(),egui::Button::new("Program sensor address")).clicked() {
                        out.push(Action::Command(format!("camera_address_set {id} 0x{:02X}",address.unwrap())));
                    }
                });
                ui.label("Enable this buffer and disable the other, then program. Poll status: MLX90640 requires camera power cycle; MLX90642 switches after programming. Firmware saves the new address only after verification.");

                ui.horizontal_wrapped(|ui|{
                    if ui.button("Detect").clicked() { out.push(state.begin(id, 0, false, PathBuf::new())); }
                    if ui.button("Initialize").clicked() { out.push(state.begin(id, 1, false, PathBuf::new())); }
                    if ui.button("Capture / show image").clicked() {
                        let initialized = state.initialized[id]==Some(true);
                        out.push(state.begin(id, if initialized { 2 } else { 1 }, true, PathBuf::from(self.folder.trim())));
                    }
                    action(ui,&mut out,"Detect status",format!("camera_status {id}"));
                    action(ui,&mut out,"Inventory","data_ready_collect".into());
                });
                ui.label("Capture initializes if needed, polls completion and downloads/displays on UART2. Image remains retained until explicit release.");
            });
        });
        });
        ui.add_space(8.0);
        out
    }
    fn downloads(&mut self, ui: &mut egui::Ui, state: &mut State, uart2: bool) -> Vec<Action> {
        let mut out = Vec::new();
        panel_frame().show(ui,|ui|{
            ui.set_min_width(ui.available_width());
            ui.strong("IMAGES / DOWNLOAD / CONVERT");
            if state.workflow.is_none() && !state.metadata.as_ref().is_some_and(|m|m.kind==3 && m.source==self.selected as u32) {
                state.metadata=state.catalogue.iter().find(|m|m.kind==3 && m.source==self.selected as u32).cloned();
                state.object_selection=state.metadata.as_ref().map(|m|m.object.to_string()).unwrap_or_default();
            }
            if state.listing {ui.label("Reading image IDs from all catalogue pages...");}
            ui.horizontal_wrapped(|ui| {
                action(ui, &mut out, "Inventory", "data_ready_collect".into());
                if ui.button("Refresh object list").clicked() {
                    state.catalogue.clear(); state.page_after = 0; state.listing=true;
                    out.push(Action::Command("data_list".into()));
                }
                if ui.button("Next page").clicked() {
                    out.push(Action::Command(format!("data_list {}", state.page_after)));
                }
            });
            ui.label(&state.inventory);
            egui::ComboBox::from_id_salt(("stored_objects", self.selected))
                .selected_text(format!("Object {}", state.object_selection))
                .show_ui(ui, |ui| {
                    for m in state.catalogue.iter().filter(|m|m.kind==3 && m.source==self.selected as u32) {
                        let label = format!("Image object {}", m.object);
                        if ui.selectable_label(state.object_selection == m.object.to_string(), label).clicked() {
                            state.object_selection = m.object.to_string(); state.metadata = Some(m.clone());
                        }
                    }
                });
            ui.horizontal(|ui| {
                ui.label("Object ID"); ui.text_edit_singleline(&mut state.object_selection);
                if ui.button("Read selected metadata").clicked() {
                    if let Ok(id) = state.object_selection.parse::<u32>() {
                        out.push(Action::Command(format!("data_ready_collect {id}")));
                    }
                }
            });
            if let Some(m) = state.metadata.as_ref().filter(|m|m.kind==3 && m.source==self.selected as u32) {
                ui.label(format!("Selected: boot={} object={} type={} camera/source={} {}x{} encoding={} length={} CRC32=0x{:08X}",m.boot,m.object,m.kind,m.source,m.width,m.height,m.encoding,m.length,m.crc));
            }
            ui.horizontal_wrapped(|ui|{
                ui.label("Save folder");ui.text_edit_singleline(&mut self.folder);
                let download_label = if cfg!(windows) { "Get data / choose destination" } else { "Get data to save folder" };
                if ui.add_enabled(uart2&&!state.busy&&state.metadata.as_ref().is_some_and(|m|m.kind==3 && m.source==self.selected as u32 && state.object_selection.parse::<u32>()==Ok(m.object)),egui::Button::new(download_label)).clicked(){out.push(Action::Download(state.metadata.clone().unwrap(),PathBuf::from(self.folder.trim())));}
                if ui.add_enabled(uart2&&!state.busy&&state.saved.as_ref().is_some_and(|s|s.meta.kind==3 && s.meta.source==self.selected as u32),egui::Button::new(format!("Release saved object {}",state.saved.as_ref().map(|s|s.meta.object.to_string()).unwrap_or_default()))).clicked(){
                    let saved=state.saved.as_ref().unwrap();
                    match std::fs::read(&saved.path).map_err(|e|e.to_string()).and_then(|bytes|saved.meta.verify(&bytes)){
                        Ok(())=>out.push(Action::Command(saved.meta.ack())),Err(error)=>state.message=format!("Cannot release: {error}"),
                    }
                }
            });
            #[cfg(not(windows))]
            ui.horizontal(|ui| { ui.label("Import .bin path"); ui.text_edit_singleline(&mut self.import_path); });
            if ui.button("Convert image binary to BMP...").clicked() {
                match (|| -> Result<Option<(Metadata,Vec<f32>,PathBuf)>,String> {
                    #[cfg(windows)]
                    let selected=crate::camera_files::binary()?;
                    #[cfg(not(windows))]
                    let selected=crate::camera_files::binary_path(std::path::Path::new(self.import_path.trim()))?;
                    let Some(input)=selected else {return Ok(None);};
                    let (meta,values)=crate::camera_files::load(&input,self.selected)?;
                    let Some(output)=crate::camera_files::bitmap(&input.with_extension("bmp"))? else {return Ok(None);};
                    if !output.extension().is_some_and(|v|v.to_string_lossy().eq_ignore_ascii_case("bmp")) {return Err("Choose a .bmp destination; raw binary is preserved".into());}
                    std::fs::write(&output,crate::camera_files::bmp(&values)?).map_err(|e|e.to_string())?;
                    Ok(Some((meta,values,output)))
                })() {
                    Ok(Some((meta,values,path)))=>{
                        state.message=format!("BMP saved: {} (auto color scale)",path.display());
                        state.images[self.selected]=Some(Image{meta,path,values,texture:None});
                    }
                    Ok(None)=>state.message="Image conversion cancelled".into(),
                    Err(error)=>state.message=format!("Conversion failed: {error}"),
                }
            }
            ui.label("Convert a 32x24 float32 or int16 binary. A matching .txt sidecar verifies CRC; otherwise format is inferred from file length.");
            if !uart2{ui.label(egui::RichText::new("Image download requires Console / UART2. Other links support setup and capture only.").color(YELLOW));}
            if state.busy{ui.label("Receiving XMODEM... other commands on this UART are blocked until complete.");}
            ui.label(egui::RichText::new(&state.message).color(GREEN));
            ui.label("Files are saved and CRC32-checked before release is enabled. Release is explicit; no automatic discard.");
        });
        ui.add_space(8.0);
        out
    }
    pub fn show_image(&mut self, ui: &mut egui::Ui, state: &mut State) {
        let id = self.selected;
        panel_frame().show(ui,|ui|{
            ui.set_min_width(ui.available_width());
            ui.strong(format!("CAMERA {id} / THERMAL IMAGE"));
            if let Some(image)=&mut state.images[id]{
                let min=image.values.iter().copied().fold(f32::INFINITY,f32::min);let max=image.values.iter().copied().fold(f32::NEG_INFINITY,f32::max);
                let mean=image.values.iter().sum::<f32>()/image.values.len() as f32;
                if image.texture.is_none(){let mut pixels=egui::ColorImage::new([32,24],egui::Color32::BLACK);for (pixel,temp) in pixels.pixels.iter_mut().zip(&image.values){*pixel=color(if max>min{(*temp-min)/(max-min)}else{0.5});}image.texture=Some(ui.ctx().load_texture(format!("camera-{id}-{}-{}",image.meta.boot,image.meta.object),pixels,egui::TextureOptions::NEAREST));}
                ui.label(format!("32 x 24 | min={min:.2} C | mean={mean:.2} C | max={max:.2} C | auto color range"));
                ui.horizontal(|ui|{ui.label("Zoom");ui.add(egui::Slider::new(&mut self.scale,4.0..=16.0));});
                let width=(32.0*self.scale).min(ui.available_width());
                let response=ui.add(egui::Image::new(image.texture.as_ref().unwrap()).fit_to_exact_size(egui::vec2(width,width*0.75)).sense(egui::Sense::hover()));
                if let Some(pos)=response.hover_pos(){let x=(((pos.x-response.rect.left())/response.rect.width()*32.0)as usize).min(31);let y=(((pos.y-response.rect.top())/response.rect.height()*24.0)as usize).min(23);response.on_hover_text(format!("x={x} y={y}: {:.2} C",image.values[y*32+x]));}
                ui.label(egui::RichText::new(format!("Saved object {} | {}",image.meta.object,image.path.display())).color(MUTED));
            }else{ui.label("No downloaded image for this camera and link. Capture, poll status, read metadata, then download.");}
        });
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn address_programming_parser_and_restart_status() {
        assert_eq!(parse_address("0x34"), Some(0x34));
        assert_eq!(parse_address("52"), Some(0x34));
        assert_eq!(parse_address("0x78"), None);
        assert_eq!(parse_address("0"), None);
        let mut state = State::default();
        state.feed("CAMERA buffer=0 ENABLED result=0\nCAMERA buffer=1 ENABLED result=0\nCAMERA address camera=1 requested=0x34 restart_required=1\n");
        assert_eq!(state.buffers, [Some(true), Some(true)]);
        assert!(state.address_status[1].contains("restart_required=1"));
        assert!(state.address_status[0].is_empty());
    }
    #[test]
    fn scans_and_status_are_independent_per_camera() {
        let mut s = State::default();
        s.feed("CAMERA buffer=1 ENABLED result=0\nCAMERA scan camera=1 bus=1\n30: -- -- -- 33 --\nCAMERA scan complete camera=1 found=1 busy=0\nCAMERA camera=1 initialized=1 data_exist=0 result=OK\n");
        assert_eq!(s.buffers, [None, Some(true)]);
        assert_eq!(s.addresses[1], vec![0x33]);
        assert!(s.addresses[0].is_empty());
        assert!(s.statuses[0].is_empty());
        assert!(s.statuses[1].contains("initialized=1"));
    }
    #[test]
    fn detection_waits_for_matching_request_and_scans_addresses() {
        let mut s = State::default();
        s.begin(0, 0, false, PathBuf::new());
        s.feed("CAMERA camera=0 request=4 result=QUEUED\nCAMERA camera=0 request=3 op=0 result=OK address=0x33\n");
        assert_eq!(s.detection[0], "Detecting...");
        s.feed("CAMERA camera=0 request=4 op=0 result=OK address=0x33\n");
        assert_eq!(s.detection[0], "Detected at 0x33");
        assert!(matches!(s.poll(true), Some(Action::Command(c)) if c == "camera_scan 0"));
        s.feed("CAMERA scan camera=0 bus=1\n30: -- -- -- 33 --\nCAMERA scan complete camera=0 found=1 busy=0\n");
        assert_eq!(s.addresses[0], vec![0x33]);
        assert!(s.scan_status[0].starts_with("1 available"));
    }
    #[test]
    fn capture_initializes_then_downloads_exact_image_without_ack() {
        let mut s = State::default();
        s.begin(1, 1, true, PathBuf::from("test-output"));
        s.feed("CAMERA camera=1 request=7 result=QUEUED\nCAMERA camera=1 request=7 op=1 result=OK initialized=1\n");
        assert!(matches!(s.poll(true), Some(Action::Command(c)) if c == "camera_capture 1"));
        s.feed("CAMERA camera=1 request=8 result=QUEUED\nCAMERA camera=1 request=8 op=2 result=RUNNING\n");
        assert!(s.workflow.is_some());
        s.feed("CAMERA camera=1 request=8 op=2 result=OK data_exist=1 object=42\n");
        s.workflow.as_mut().unwrap().next = Instant::now();
        assert!(matches!(s.poll(true), Some(Action::Command(c)) if c == "data_ready_collect 42"));
        s.feed("DATA boot=1 object=42 type=3 source=1 encoding=2 width=32 height=24 length=3072 crc32=0x1234\n");
        assert!(
            matches!(s.poll(true), Some(Action::Download(m, _)) if m.object == 42 && m.source == 1)
        );
        assert!(s.poll(true).is_none()); // Never ACK/discard automatically.
    }
    #[test]
    fn older_objects_and_timeouts_do_not_display_wrong_image() {
        let mut s = State::default();
        s.begin(0, 2, true, PathBuf::new());
        s.feed("CAMERA camera=0 request=8 result=QUEUED\nCAMERA camera=0 request=8 op=2 result=OK data_exist=1 object=42\nDATA boot=1 object=41 type=1 source=0 encoding=0 width=0 height=0 length=10 crc32=0x1234\n");
        assert!(s.poll(true).is_none());
        assert!(s.message.contains("captured object metadata"));
        assert!(s.workflow.is_some());
        s.workflow.as_mut().unwrap().next = Instant::now();
        assert!(matches!(s.poll(true), Some(Action::Command(c)) if c == "data_ready_collect 42"));
        s.begin(0, 0, false, PathBuf::new());
        s.workflow.as_mut().unwrap().deadline = Instant::now();
        assert!(s.poll(true).is_none());
        assert!(s.detection[0].contains("Unavailable"));
    }
    #[test]
    fn camera_drafts_and_configuration_results_are_independent() {
        let mut panel = CameraPanel::default();
        panel.cameras[0].address = "0x34".into();
        panel.cameras[0].model = 1;
        assert_eq!(panel.cameras[0].selected, 0);
        assert_eq!(panel.cameras[1].selected, 1);
        assert_eq!(panel.cameras[1].address, "0x33");
        assert_eq!(panel.cameras[1].model, 0);
        let mut state = State::default();
        state.configuring = Some((1, Instant::now() + Duration::from_secs(10)));
        state.feed("OK\n");
        assert_eq!(state.configuration[1], "Applied");
        assert!(state.configuration[0].is_empty());
        state.configuring = Some((0, Instant::now() + Duration::from_secs(10)));
        state.feed("BUSY\n");
        assert_eq!(state.configuration[0], "Not applied: BUSY");
        assert_eq!(state.configuration[1], "Applied");
    }
    #[test]
    fn initialization_snapshot_and_refusal_recovery() {
        let mut s = State::default();
        s.feed("CAMERA camera=1 initialized=1 op=1 request=1 result=OK\n");
        s.feed("CAMERA camera=1 request=2 result=QUEUED\n");
        assert_eq!(s.initialized[1], Some(true));
        assert!(s.statuses[1].contains("initialized=1"));
        s.begin(1, 2, true, PathBuf::new());
        s.feed("CAMERA camera=1 request=0 result=NOT_INITIALIZED\n");
        assert_eq!(s.initialized[1], Some(false));
        assert!(matches!(s.poll(true),Some(Action::Command(c)) if c=="camera_init 1"));
        s.feed("CAMERA camera=1 request=3 result=QUEUED\nCAMERA camera=1 initialized=1 op=1 request=3 result=OK\n");
        assert!(matches!(s.poll(true),Some(Action::Command(c)) if c=="camera_capture 1"));
    }
    #[test]
    fn image_list_walks_all_pages() {
        let mut s = State::default();
        s.listing = true;
        s.feed("DATA page count=8 after=8\n");
        assert!(matches!(s.poll(true),Some(Action::Command(c)) if c=="data_list 8"));
        s.feed("DATA page count=2 after=10\n");
        assert!(!s.listing);
    }
    #[test]
    fn status_read_reports_each_camera_without_pending_probe() {
        let mut state = State::default();
        state.feed("CAMERA camera=0 request=5 op=0 result=OK address=0x33\nCAMERA camera=1 request=6 op=0 result=IO_ERROR address=0x34\n");
        assert_eq!(state.detection[0], "Detected at 0x33");
        assert_eq!(state.detection[1], "Unavailable: IO_ERROR");
    }
}
