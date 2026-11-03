//! Transports to `dal_obc_fsw` and the worker thread that owns them.
//!
//! * `Console`: one connection per request to the flight console socket, line
//!   `json <cmd>`, reply is one JSON object whose `payload_hex` is the TM payload.
//! * `Rs422`: binary `A5…CRC16` frames, either over the flight software's
//!   optional frame socket (`DAL_OBC_RS422_SOCKET`) or a serial device.
//!
//! The UI never touches a transport: it queues `Job`s and drains `Done`s.

use crate::json::Json;
use crate::proto::{self, Cmd, Decoder, Fed};
use std::collections::VecDeque;
use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::sync::mpsc::{channel, Receiver, Sender};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

pub const DEFAULT_CONSOLE: &str = "/run/dal/dal_obc_fsw/console.sock";
pub const DEFAULT_RS422: &str = "/run/dal/dal_obc_fsw/rs422.sock";

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mode {
    Sim,
    Console,
    Rs422,
}

impl Mode {
    pub const ALL: [Mode; 3] = [Mode::Sim, Mode::Console, Mode::Rs422];
    pub fn label(self) -> &'static str {
        match self {
            Mode::Sim => "SIM",
            Mode::Console => "CONSOLE",
            Mode::Rs422 => "RS-422",
        }
    }
}

/// What a transport hands back for one command.
#[derive(Debug, Clone)]
pub struct Reply {
    pub ack: u8,
    pub code: u8,
    pub payload: Vec<u8>,
    /// Console `text` field (empty over RS-422).
    pub text: String,
    /// Console reported `ok`; over RS-422 true unless the frame is an error frame.
    pub ok: bool,
    pub tx: String,
    pub rx: String,
    pub ms: u64,
}

impl Reply {
    pub fn is_err(&self) -> bool {
        self.ack == proto::ERR_MARK
    }
}

pub trait Transport: Send {
    fn exec(&mut self, cmd: &Cmd) -> Result<Reply, String>;
    /// `gui_state` snapshot; only the console has it.
    fn gui_state(&mut self) -> Option<Result<Json, String>> {
        None
    }
}

// ── Console ───────────────────────────────────────────────────────────────────

pub struct Console {
    pub path: String,
}

impl Console {
    fn request(&self, line: &str, timeout: Duration) -> Result<String, String> {
        let mut s =
            UnixStream::connect(&self.path).map_err(|e| format!("connect {}: {e}", self.path))?;
        s.set_read_timeout(Some(timeout)).ok();
        s.set_write_timeout(Some(Duration::from_secs(5))).ok();
        s.write_all(format!("{line}\n").as_bytes())
            .map_err(|e| format!("write: {e}"))?;
        let mut out = Vec::new();
        let deadline = Instant::now() + timeout;
        let mut buf = [0u8; 8192];
        loop {
            match s.read(&mut buf) {
                Ok(0) => break,
                Ok(n) => out.extend_from_slice(&buf[..n]),
                Err(e)
                    if matches!(
                        e.kind(),
                        std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut
                    ) =>
                {
                    return Err(format!("no reply within {} s", timeout.as_secs()));
                }
                Err(e) => return Err(format!("read: {e}")),
            }
            if Instant::now() > deadline {
                return Err("reply timeout".into());
            }
        }
        String::from_utf8(out).map_err(|e| format!("reply is not UTF-8: {e}"))
    }
}

impl Transport for Console {
    fn exec(&mut self, cmd: &Cmd) -> Result<Reply, String> {
        let t0 = Instant::now();
        let local = cmd.ty == 0xFE;
        let line = if local {
            cmd.text.clone()
        } else {
            format!("json {}", cmd.text)
        };
        let raw = self.request(&line, cmd.timeout)?;
        let j = Json::parse(raw.trim()).map_err(|e| {
            format!(
                "bad JSON ({e}): {}",
                raw.chars().take(80).collect::<String>()
            )
        })?;
        if j.get("ok").is_null() {
            return Err(j
                .get("error")
                .str()
                .unwrap_or("unexpected reply")
                .to_string());
        }
        if local && !j.get("ok").bool().unwrap_or(false) {
            return Err(format!(
                "{}: {}",
                if cmd.text.starts_with("port_cfg ") {
                    "port_cfg"
                } else {
                    "gui_shell"
                },
                j.get("text")
                    .str()
                    .or_else(|| j.get("error").str())
                    .unwrap_or("command failed")
                    .trim()
            ));
        }
        let payload =
            proto::from_hex(j.get("payload_hex").str().unwrap_or("")).ok_or("bad payload_hex")?;
        Ok(Reply {
            ack: if local {
                0xFE
            } else {
                j.get("ack").i64().unwrap_or(0) as u8
            },
            code: j.get("code").i64().unwrap_or(0) as u8,
            payload,
            text: j.get("text").str().unwrap_or("").to_string(),
            ok: j.get("ok").bool().unwrap_or(false),
            tx: line,
            rx: raw.trim().chars().take(200).collect(),
            ms: t0.elapsed().as_millis() as u64,
        })
    }

    fn gui_state(&mut self) -> Option<Result<Json, String>> {
        let r = self
            .request("gui_state", Duration::from_secs(5))
            .and_then(|raw| Json::parse(raw.trim()).map_err(|e| format!("gui_state JSON: {e}")));
        Some(r)
    }
}

// ── RS-422 ────────────────────────────────────────────────────────────────────

enum Port {
    Sock(UnixStream),
    Tty(File),
}

impl Port {
    fn write_all(&mut self, b: &[u8]) -> std::io::Result<()> {
        match self {
            Port::Sock(s) => s.write_all(b),
            Port::Tty(f) => f.write_all(b),
        }
    }
    fn read(&mut self, b: &mut [u8]) -> std::io::Result<usize> {
        match self {
            Port::Sock(s) => s.read(b),
            Port::Tty(f) => f.read(b),
        }
    }
}

pub struct Rs422 {
    pub path: String,
    pub baud: u32,
    port: Option<Port>,
}

impl Rs422 {
    pub fn new(path: &str, baud: u32) -> Rs422 {
        Rs422 {
            path: path.to_string(),
            baud,
            port: None,
        }
    }

    fn open(&self) -> Result<Port, String> {
        if self.path.starts_with("/dev/") {
            // Raw 8N1 through stty; VMIN=0 VTIME=2 gives a 200 ms read tick.
            let st = std::process::Command::new("stty")
                .args([
                    "-F",
                    &self.path,
                    &self.baud.to_string(),
                    "raw",
                    "-echo",
                    "-crtscts",
                    "cs8",
                    "-cstopb",
                    "-parenb",
                    "min",
                    "0",
                    "time",
                    "2",
                ])
                .output()
                .map_err(|e| format!("stty: {e}"))?;
            if !st.status.success() {
                return Err(format!(
                    "stty {}: {}",
                    self.path,
                    String::from_utf8_lossy(&st.stderr).trim()
                ));
            }
            let f = OpenOptions::new()
                .read(true)
                .write(true)
                .open(&self.path)
                .map_err(|e| format!("open {}: {e}", self.path))?;
            Ok(Port::Tty(f))
        } else {
            let s = UnixStream::connect(&self.path)
                .map_err(|e| format!("connect {}: {e}", self.path))?;
            s.set_read_timeout(Some(Duration::from_millis(200))).ok();
            s.set_write_timeout(Some(Duration::from_secs(5))).ok();
            Ok(Port::Sock(s))
        }
    }

    fn txrx(&mut self, cmd: &Cmd) -> Result<Reply, String> {
        if self.port.is_none() {
            self.port = Some(self.open()?);
        }
        let t0 = Instant::now();
        let frame = proto::encode_tc(cmd.ty, cmd.addr, &cmd.payload);
        let port = self.port.as_mut().unwrap();
        // Drop anything left over from a timed-out earlier exchange.
        port.write_all(&frame).map_err(|e| format!("write: {e}"))?;

        let mut dec = Decoder::new();
        let mut idle = Duration::ZERO;
        let mut rx = Vec::new();
        let deadline = t0 + cmd.timeout;
        let mut b = [0u8; 4096];
        while Instant::now() < deadline {
            match port.read(&mut b) {
                Ok(0) if matches!(port, Port::Sock(_)) => {
                    return Err("fsw closed the frame socket".into())
                }
                Ok(0) => idle += Duration::from_millis(200),
                Ok(n) => {
                    idle = Duration::ZERO;
                    for &x in &b[..n] {
                        rx.push(x);
                        match dec.feed(x) {
                            Fed::Frame(tm) => {
                                return Ok(Reply {
                                    ack: tm.ack,
                                    code: tm.code,
                                    ok: !tm.is_err(),
                                    payload: tm.payload,
                                    text: String::new(),
                                    tx: proto::to_hex(&frame),
                                    rx: proto::to_hex(&rx),
                                    ms: t0.elapsed().as_millis() as u64,
                                })
                            }
                            Fed::CrcError => {
                                return Err(format!("CRC error in reply ({} bytes)", rx.len()))
                            }
                            Fed::More => {}
                        }
                    }
                }
                Err(e)
                    if matches!(
                        e.kind(),
                        std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut
                    ) =>
                {
                    idle += Duration::from_millis(200)
                }
                Err(e) => {
                    self.port = None;
                    return Err(format!("read: {e}"));
                }
            }
            if dec.busy() && idle >= Duration::from_millis(proto::INTER_BYTE_MS) {
                dec.reset();
                return Err("inter-byte timeout: reply truncated".into());
            }
        }
        Err(format!("no reply within {} s", cmd.timeout.as_secs()))
    }
}

impl Transport for Rs422 {
    fn exec(&mut self, cmd: &Cmd) -> Result<Reply, String> {
        if cmd.ty == 0xFE {
            return Err("gui-hw requires the local console".into());
        }
        let r = self.txrx(cmd);
        if r.is_err() {
            // A half-read frame would corrupt the next exchange.
            self.port = None;
        }
        r
    }
}

// ── Worker ────────────────────────────────────────────────────────────────────

/// Why a command was sent; decides how the UI treats the result.
#[derive(Clone, Debug, PartialEq)]
pub enum Why {
    /// Periodic telemetry refresh.
    Poll,
    /// Operator action, labelled with the control id.
    Action(String),
    /// Follow-up of an action (status re-read).
    Refresh,
}

pub enum Job {
    Cmd {
        cmd: Cmd,
        why: Why,
    },
    /// `gui_state` snapshot (console only).
    State,
    Quit,
}

pub struct Done {
    pub cmd: Option<Cmd>,
    pub why: Why,
    pub result: Result<Reply, String>,
    /// For `Job::State`.
    pub state: Option<Result<Json, String>>,
}

pub struct Link {
    tx: Sender<Job>,
    rx: Receiver<Done>,
    handle: Option<JoinHandle<()>>,
}

impl Link {
    pub fn start(mode: Mode, target: &str, baud: u32) -> Link {
        let (jtx, jrx) = channel::<Job>();
        let (dtx, drx) = channel::<Done>();
        let mut transport: Box<dyn Transport> = match mode {
            Mode::Rs422 => Box::new(Rs422::new(target, baud)),
            _ => Box::new(Console {
                path: target.to_string(),
            }),
        };
        let handle = thread::spawn(move || {
            let mut queued = VecDeque::new();
            loop {
                if queued.is_empty() {
                    let Ok(job) = jrx.recv() else { break };
                    queued.push_back(job);
                }
                queued.extend(jrx.try_iter());
                // A telemetry round queues many reads at once. Let an operator
                // action and its follow-up run after the current I/O finishes.
                let next = queued
                    .iter()
                    .position(|job| {
                        matches!(
                            job,
                            Job::Quit
                                | Job::Cmd {
                                    why: Why::Action(_) | Why::Refresh,
                                    ..
                                }
                        )
                    })
                    .unwrap_or(0);
                let job = queued.remove(next).expect("queued job");
                let done = match job {
                    Job::Quit => break,
                    Job::State => Done {
                        cmd: None,
                        why: Why::Poll,
                        result: Err(String::new()),
                        state: transport.gui_state(),
                    },
                    Job::Cmd { cmd, why } => {
                        let result = transport.exec(&cmd);
                        Done {
                            cmd: Some(cmd),
                            why,
                            result,
                            state: None,
                        }
                    }
                };
                if dtx.send(done).is_err() {
                    break;
                }
            }
        });
        Link {
            tx: jtx,
            rx: drx,
            handle: Some(handle),
        }
    }

    pub fn send(&self, cmd: Cmd, why: Why) {
        let _ = self.tx.send(Job::Cmd { cmd, why });
    }

    pub fn request_state(&self) {
        let _ = self.tx.send(Job::State);
    }

    pub fn drain(&self) -> Vec<Done> {
        let mut v = Vec::new();
        while let Ok(d) = self.rx.try_recv() {
            v.push(d);
        }
        v
    }
}

impl Drop for Link {
    fn drop(&mut self) {
        let _ = self.tx.send(Job::Quit);
        if let Some(h) = self.handle.take() {
            // The worker may be inside a long command; do not hang the UI on exit.
            let t0 = Instant::now();
            while !h.is_finished() && t0.elapsed() < Duration::from_millis(300) {
                thread::sleep(Duration::from_millis(10));
            }
            if h.is_finished() {
                let _ = h.join();
            }
        }
    }
}
