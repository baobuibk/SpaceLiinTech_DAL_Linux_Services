//! Serial links. Only the Console (UART2, plain-text CLI) link talks to hardware in v1;
//! XCLI (xTP), RS485 and CAN (CSP) need framing that this tool does not implement yet.
use std::io::{Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::Sender;
use std::sync::Arc;
use std::thread;
use std::time::Duration;

pub const LINKS: [(&str, &str, &str); 4] = [
    ("Console (UART2)", "UART2", "115200"),
    ("XCLI (UART0)", "XCLI", "230400"),
    ("RS485 (UART4)", "RS485", "115200"),
    ("USB-CAN (SLCAN)", "CAN", "115200"),
];

pub struct Link {
    pub port: String,
    pub baud: String,
    pub open: bool,
    pub status: String,
    writer: Option<Box<dyn serialport::SerialPort>>,
    stop: Arc<AtomicBool>,
}

impl Link {
    pub fn new(baud: &str) -> Self {
        Link {
            port: String::new(),
            baud: baud.into(),
            open: false,
            status: "CLOSED".into(),
            writer: None,
            stop: Arc::new(AtomicBool::new(false)),
        }
    }

    pub fn open_serial(&mut self, index: usize, lines: Sender<(usize, String)>) -> Result<(), String> {
        if index != 0 {
            return Err("only the Console (UART2) link is implemented in exp_tui v1".into());
        }
        let baud: u32 = self.baud.trim().parse().map_err(|_| "baud must be a number".to_string())?;
        if self.port.trim().is_empty() {
            return Err("enter a serial device first (e.g. /dev/ttyUSB0)".into());
        }
        let port = serialport::new(self.port.trim(), baud)
            .timeout(Duration::from_millis(30))
            .open()
            .map_err(|e| format!("open {}: {e}", self.port))?;
        let mut reader = port.try_clone().map_err(|e| e.to_string())?;
        self.writer = Some(port);
        self.stop = Arc::new(AtomicBool::new(false));
        let stop = self.stop.clone();
        thread::spawn(move || {
            let mut buf = [0u8; 256];
            let mut line = Vec::<u8>::new();
            while !stop.load(Ordering::Relaxed) {
                match reader.read(&mut buf) {
                    Ok(n) if n > 0 => {
                        for &b in &buf[..n] {
                            if b == b'\n' || b == b'\r' {
                                if !line.is_empty() {
                                    let _ = lines.send((index, String::from_utf8_lossy(&line).into_owned()));
                                    line.clear();
                                }
                            } else if line.len() < 4096 {
                                line.push(b);
                            }
                        }
                    }
                    Ok(_) => {}
                    Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
                    Err(e) => {
                        let _ = lines.send((index, format!("link error: {e}")));
                        break;
                    }
                }
            }
        });
        self.open = true;
        self.status = format!("OPEN {} @ {}", self.port.trim(), baud);
        Ok(())
    }

    pub fn close(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
        self.writer = None;
        self.open = false;
        self.status = "CLOSED".into();
    }

    pub fn send_line(&mut self, text: &str) -> Result<(), String> {
        let w = self.writer.as_mut().ok_or("link is not open")?;
        let mut bytes = text.as_bytes().to_vec();
        bytes.push(b'\r');
        let mut sent = 0;
        while sent < bytes.len() {
            match w.write(&bytes[sent..]) {
                Ok(0) => {
                    self.close();
                    return Err(format!("write stalled after {sent} bytes; link closed (outcome unknown)"));
                }
                Ok(n) => sent += n,
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {
                    self.close();
                    return Err(format!("write timeout after {sent} bytes; link closed (outcome unknown)"));
                }
                Err(e) => {
                    self.close();
                    return Err(format!("write failed: {e}; link closed"));
                }
            }
        }
        let _ = w.flush();
        Ok(())
    }
}
