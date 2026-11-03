use crate::{camera_data, protocol, serial_io, uart_text};
use std::{
    io::{Read, Write},
    sync::mpsc::{Receiver, Sender},
    time::{Duration, Instant},
};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Link {
    Console,
    Xcli,
    Rs485,
    Can,
}
impl Link {
    pub const ALL: [Self; 4] = [Self::Console, Self::Xcli, Self::Rs485, Self::Can];
    pub fn index(self) -> usize {
        match self {
            Self::Console => 0,
            Self::Xcli => 1,
            Self::Rs485 => 2,
            Self::Can => 3,
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Self::Console => "Console (UART2)",
            Self::Xcli => "XCLI (UART0)",
            Self::Rs485 => "RS485 RPARAM (UART4)",
            Self::Can => "USB-CAN RPARAM (SLCAN)",
        }
    }
    pub fn short(self) -> &'static str {
        match self {
            Self::Console => "Console",
            Self::Xcli => "XCLI",
            Self::Rs485 => "RS485",
            Self::Can => "CAN",
        }
    }
}
pub enum Request {
    Command(String),
    Table(u8),
    Download(camera_data::Metadata, std::path::PathBuf),
    Stop,
}
pub enum Notice {
    Line(String),
    Text(String),
    Downloaded(camera_data::Metadata, Vec<u8>, std::path::PathBuf),
    DownloadFailed(String),
    Connected(bool),
    Status(String),
}

pub fn save_download(
    folder: &std::path::Path,
    meta: &camera_data::Metadata,
    bytes: &[u8],
) -> Result<std::path::PathBuf, String> {
    meta.verify(bytes)?;
    std::fs::create_dir_all(folder).map_err(|e| e.to_string())?;
    let stamp = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_err(|e| e.to_string())?
        .as_nanos();
    let path = folder.join(format!(
        "boot{}_object{}_type{}_source{}_{}.bin",
        meta.boot, meta.object, meta.kind, meta.source, stamp
    ));
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&path)
        .map_err(|e| e.to_string())?;
    file.write_all(bytes)
        .and_then(|_| file.sync_all())
        .map_err(|e| e.to_string())?;
    let mut sidecar = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(path.with_extension("txt"))
        .map_err(|e| e.to_string())?;
    let info=format!("DATA boot={} object={} type={} source={} encoding={} width={} height={} length={} crc32=0x{:08X} run={} seq={} ms={}\n",meta.boot,meta.object,meta.kind,meta.source,meta.encoding,meta.width,meta.height,meta.length,meta.crc,meta.run,meta.sequence,meta.timestamp_ms);
    sidecar
        .write_all(info.as_bytes())
        .and_then(|_| sidecar.sync_all())
        .map_err(|e| e.to_string())?;
    Ok(path)
}

fn report_tx_failure(notices: &Sender<Notice>, name: &str, error: &str) {
    let message = format!("TX error on {name}: {error}. Link closed; check the serial adapter, then reopen in Port settings. Command outcome may be unknown; read status before retrying.");
    let _ = notices.send(Notice::Line(message));
    let _ = notices.send(Notice::Status(
        "TX failed / disconnected; reopen port".into(),
    ));
    let _ = notices.send(Notice::Connected(false));
}

pub fn worker(
    link: Link,
    name: String,
    baud: u32,
    requests: Receiver<Request>,
    notices: Sender<Notice>,
) {
    let port = serialport::new(&name, baud)
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .timeout(serial_io::READ_TIMEOUT)
        .open();
    let mut port = match port {
        Ok(port) => port,
        Err(e) => {
            let _ = notices.send(Notice::Status(format!("Open failed: {e}")));
            let _ = notices.send(Notice::Connected(false));
            return;
        }
    };
    let _ = notices.send(Notice::Connected(true));
    let _ = notices.send(Notice::Status(format!("Connected: {name}")));
    if link == Link::Can {
        if let Err(error) = serial_io::write_packet(&mut *port, b"C\rS8\rO\r") {
            report_tx_failure(&notices, &name, &error);
            return;
        }
    }
    let mut stream = Vec::new();
    let mut seq = 0u8;
    let mut can_id = 0u16;
    let mut can_line = Vec::new();
    let mut can_rx = CanRx::default();
    let mut console_decoder = uart_text::Decoder::default();
    let mut shell_pending = false;
    let mut next_poll = Instant::now();
    let mut xcli_ticket: Option<(u32, u32)> = None;
    loop {
        while let Ok(request) = requests.try_recv() {
            let mut transmitted_command = None;
            let packet = match request {
                Request::Stop => {
                    let _ = notices.send(Notice::Connected(false));
                    return;
                }
                Request::Command(command) => {
                    if (link == Link::Xcli && xcli_ticket.is_some())
                        || (matches!(link, Link::Rs485 | Link::Can) && shell_pending)
                    {
                        let _ = notices.send(Notice::Line(
                            "Previous remote command still pending; wait for its result".into(),
                        ));
                        continue;
                    }
                    transmitted_command = Some(command.clone());
                    match link {
                        Link::Console => {
                            let mut bytes = command.into_bytes();
                            bytes.push(b'\r');
                            Ok(bytes)
                        }
                        Link::Xcli => {
                            let result = protocol::xcli(&command, seq);
                            seq = seq.wrapping_add(1);
                            result
                        }
                        Link::Rs485 | Link::Can => {
                            let result = protocol::rparam_shell(&command)
                                .map(|p| remote_packet(link, &p, &mut can_id));
                            if result.is_ok() {
                                shell_pending = true;
                                next_poll = Instant::now() + Duration::from_millis(250);
                            }
                            result
                        }
                    }
                }
                Request::Download(meta, folder) => {
                    if link != Link::Console {
                        let _ = notices.send(Notice::DownloadFailed(
                            "XMODEM requires Console / UART2".into(),
                        ));
                        continue;
                    }
                    let mut stopped = false;
                    let result = camera_data::receive(
                        &mut *port,
                        &meta,
                        &mut || {
                            while let Ok(request) = requests.try_recv() {
                                if matches!(request, Request::Stop) {
                                    stopped = true;
                                    return true;
                                }
                                let _ = notices.send(Notice::Line(
                                    "Refused: XMODEM owns UART2 during download".into(),
                                ));
                            }
                            false
                        },
                        &mut |line| {
                            let _ = notices.send(Notice::Line(line));
                        },
                    );
                    match result.and_then(|bytes| {
                        save_download(&folder, &meta, &bytes).map(|path| (bytes, path))
                    }) {
                        Ok((bytes, path)) => {
                            let _ = notices.send(Notice::Downloaded(meta, bytes, path));
                        }
                        Err(error) => {
                            let _ = notices.send(Notice::DownloadFailed(error));
                        }
                    }
                    if stopped {
                        let _ = notices.send(Notice::Connected(false));
                        return;
                    }
                    continue;
                }
                Request::Table(table) => Ok(remote_packet(
                    link,
                    &protocol::rparam_get(table),
                    &mut can_id,
                )),
            };
            match packet {
                Ok(bytes) => {
                    if let Err(e) = serial_io::write_packet(&mut *port, &bytes) {
                        report_tx_failure(&notices, &name, &e);
                        return;
                    } else if let Some(command) = transmitted_command {
                        let _ = notices.send(Notice::Line(format!("TX > {command}")));
                    }
                }
                Err(e) => {
                    let _ = notices.send(Notice::Line(format!("Refused: {e}")));
                }
            }
        }
        if shell_pending && Instant::now() >= next_poll {
            let bytes = remote_packet(link, &protocol::rparam_get(1), &mut can_id);
            if let Err(e) = serial_io::write_packet(&mut *port, &bytes) {
                report_tx_failure(&notices, &name, &e);
                return;
            }
            next_poll = Instant::now() + Duration::from_millis(500);
        }
        if link == Link::Xcli && xcli_ticket.is_some() && Instant::now() >= next_poll {
            let (ticket, offset) = xcli_ticket.unwrap();
            let command = format!("obc_result {ticket} {offset}");
            if let Ok(bytes) = protocol::xcli(&command, seq) {
                seq = seq.wrapping_add(1);
                if let Err(e) = serial_io::write_packet(&mut *port, &bytes) {
                    report_tx_failure(&notices, &name, &e);
                    return;
                }
            }
            next_poll = Instant::now() + Duration::from_millis(500);
        }
        let mut buf = [0u8; 512];
        match port.read(&mut buf) {
            Ok(n) if n > 0 => match link {
                Link::Console => {
                    let _ = notices.send(Notice::Text(console_decoder.feed(&buf[..n])));
                }
                Link::Xcli => {
                    stream.extend_from_slice(&buf[..n]);
                    for (id, _, data) in protocol::parse_xtp(&mut stream) {
                        if id != 0x101 {
                            continue;
                        }
                        if let Some(text) = protocol::xcli_field(&data, "text") {
                            let _ = notices.send(Notice::Text(
                                String::from_utf8_lossy(text).trim_end_matches('\0').into(),
                            ));
                        }
                        let _ = notices
                            .send(Notice::Line(format!("RX {}", protocol::xcli_text(&data))));
                        if data.get(1) == Some(&0x30) {
                            if let Some(ticket) = protocol::xcli_field(&data, "ticket")
                                .and_then(|v| v.try_into().ok())
                                .map(u32::from_le_bytes)
                            {
                                xcli_ticket = Some((ticket, 0));
                                next_poll = Instant::now() + Duration::from_millis(250);
                            }
                        }
                        if data.get(1) == Some(&0x33) {
                            let state = protocol::xcli_field(&data, "state")
                                .and_then(|v| v.first())
                                .copied()
                                .unwrap_or(0);
                            let total = protocol::xcli_field(&data, "length")
                                .and_then(|v| v.try_into().ok())
                                .map(u32::from_le_bytes)
                                .unwrap_or(0);
                            let text_len = protocol::xcli_field(&data, "text")
                                .map(|v| v.len() as u32)
                                .unwrap_or(0);
                            if let Some((ticket, offset)) = xcli_ticket {
                                if state >= 3 && offset + text_len >= total {
                                    xcli_ticket = None;
                                } else if state >= 3 && text_len > 0 {
                                    xcli_ticket = Some((ticket, offset + text_len));
                                    next_poll = Instant::now();
                                }
                            }
                        }
                    }
                }
                Link::Rs485 => {
                    stream.extend_from_slice(&buf[..n]);
                    for frame in protocol::parse_kiss(&mut stream) {
                        if let Some(done) = log_rparam(&notices, &frame[4..]) {
                            shell_pending &= !done;
                        }
                    }
                }
                Link::Can => {
                    for &b in &buf[..n] {
                        if b == b'\r' {
                            if let Some((id, data)) = parse_slcan(&can_line) {
                                if let Some(payload) = can_rx.feed(id, &data) {
                                    if let Some(done) = log_rparam(&notices, &payload) {
                                        shell_pending &= !done;
                                    }
                                }
                            }
                            can_line.clear();
                        } else if can_line.len() < 64 {
                            can_line.push(b);
                        }
                    }
                }
            },
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
            Err(e) => {
                let _ = notices.send(Notice::Status(format!("Read failed: {e}")));
                break;
            }
            _ => {}
        }
    }
    let _ = notices.send(Notice::Connected(false));
}

fn remote_packet(link: Link, payload: &[u8], id: &mut u16) -> Vec<u8> {
    let source = if link == Link::Can { 1 } else { 2 };
    let header = protocol::csp_header(source, 42);
    if link == Link::Rs485 {
        return protocol::kiss(&header, payload);
    }
    let mut out = Vec::new();
    let frames = 1 + (payload.len().saturating_sub(2) + 7) / 8;
    let ident = *id & 0x3ff;
    *id = id.wrapping_add(1);
    let mut pos = 0usize;
    for part in 0..frames {
        let remaining = if part == 0 {
            frames - 1
        } else {
            (payload.len() - pos + 7) / 8 - 1
        };
        let can_id = (1u32 << 24)
            | (11u32 << 19)
            | ((part != 0) as u32) << 18
            | ((remaining as u32) << 10)
            | ident as u32;
        let mut data = Vec::new();
        if part == 0 {
            data.extend_from_slice(&header);
            data.extend_from_slice(&(payload.len() as u16).to_be_bytes());
        }
        let take = (8 - data.len()).min(payload.len() - pos);
        data.extend_from_slice(&payload[pos..pos + take]);
        pos += take;
        out.extend_from_slice(
            format!(
                "T{can_id:08X}{:X}{}\r",
                data.len(),
                data.iter().map(|b| format!("{b:02X}")).collect::<String>()
            )
            .as_bytes(),
        );
        if pos == payload.len() {
            break;
        }
    }
    out
}
fn parse_slcan(line: &[u8]) -> Option<(u32, Vec<u8>)> {
    let text = std::str::from_utf8(line).ok()?;
    if !text.starts_with('T') || text.len() < 10 {
        return None;
    }
    let id = u32::from_str_radix(&text[1..9], 16).ok()?;
    let len = usize::from_str_radix(&text[9..10], 16).ok()?;
    if text.len() < 10 + len * 2 {
        return None;
    }
    let data = (0..len)
        .map(|i| u8::from_str_radix(&text[10 + i * 2..12 + i * 2], 16).ok())
        .collect::<Option<Vec<_>>>()?;
    Some((id, data))
}
#[derive(Default)]
struct CanRx {
    key: u32,
    expected: usize,
    data: Vec<u8>,
}
impl CanRx {
    fn feed(&mut self, id: u32, frame: &[u8]) -> Option<Vec<u8>> {
        if (id >> 19) & 31 != 1 || (id >> 24) & 31 != 11 {
            return None;
        }
        let key = id & ((31 << 24) | (31 << 19) | 0x3ff);
        if (id >> 18) & 1 == 0 {
            if frame.len() < 6 {
                return None;
            }
            self.key = key;
            self.expected = u16::from_be_bytes([frame[4], frame[5]]) as usize;
            self.data.clear();
            self.data.extend_from_slice(&frame[6..]);
        } else if self.key == key && self.data.len() < self.expected {
            self.data.extend_from_slice(frame);
        } else {
            return None;
        }
        if self.data.len() == self.expected {
            return Some(std::mem::take(&mut self.data));
        }
        None
    }
}
fn log_rparam(notices: &Sender<Notice>, payload: &[u8]) -> Option<bool> {
    if payload.len() < 10 {
        let _ = notices.send(Notice::Line(format!("RX RPARAM short {payload:02X?}")));
        return None;
    }
    let table = payload[1];
    let action = payload[0];
    if action == 1 {
        let _ = notices.send(Notice::Line(format!("RX RPARAM table={table} SET_OK")));
        return None;
    }
    if action != 0x55 {
        let _ = notices.send(Notice::Line(format!(
            "RX RPARAM table={table} action=0x{action:02X} {payload:02X?}"
        )));
        return None;
    }
    let part = u16::from_be_bytes([payload[6], payload[7]]);
    let count = u16::from_be_bytes([payload[8], payload[9]]);
    let _ = notices.send(Notice::Line(format!(
        "RX RPARAM table={table} fragment={part}/{count} {payload:02X?}"
    )));
    if table != 1 {
        return None;
    }
    let mut cursor = 10;
    let mut done = false;
    while cursor + 2 <= payload.len() {
        let offset = u16::from_be_bytes([payload[cursor], payload[cursor + 1]]);
        cursor += 2;
        let size = match offset {
            0 => 64,
            64..=67 => 1,
            68 => 2,
            70 => 128,
            198 => 2,
            200 | 204 | 208 => 4,
            212 => 2,
            214 => 1,
            _ => break,
        };
        if cursor + size > payload.len() {
            break;
        }
        let value = &payload[cursor..cursor + size];
        cursor += size;
        match offset {
            64 => {
                done = matches!(value[0], 2 | 3 | 4);
                let _ = notices.send(Notice::Line(format!("Shell status={}", value[0])));
            }
            65 => {
                let _ = notices.send(Notice::Line(format!("Shell retcode={}", value[0] as i8)));
            }
            70 => {
                let output = String::from_utf8_lossy(value);
                let _ = notices.send(Notice::Text(output.trim_end_matches('\0').into()));
            }
            200 | 204 => {
                let id = u32::from_be_bytes(value.try_into().unwrap());
                let _ = notices.send(Notice::Line(format!(
                    "Shell {}={id}",
                    if offset == 200 {
                        "request_id"
                    } else {
                        "completed_id"
                    }
                )));
            }
            _ => {}
        }
    }
    Some(done)
}
