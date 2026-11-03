//! Raw text CLI session for the PDU's software console or UART0.
use std::{
    io::{Read, Write},
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc::{self, Receiver, SyncSender},
        Arc,
    },
    thread,
    time::Duration,
};

pub enum Event {
    Connected,
    Output(Vec<u8>),
    Failed(String),
}

/// Treat /dev/serial/by-id aliases and their tty device as the same port.
pub fn same_port(a: &str, b: &str) -> bool {
    if a == b || (cfg!(windows) && a.eq_ignore_ascii_case(b)) {
        return true;
    }
    match (std::fs::canonicalize(a), std::fs::canonicalize(b)) {
        (Ok(a), Ok(b)) => a == b,
        _ => false,
    }
}

pub struct Console {
    pub commands: SyncSender<String>,
    pub events: Receiver<Event>,
    stop: Arc<AtomicBool>,
}

impl Drop for Console {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
    }
}

impl Console {
    pub fn start(path: String, baud: u32) -> Self {
        let (commands, input) = mpsc::sync_channel::<String>(8);
        let (output, events) = mpsc::channel();
        let stop = Arc::new(AtomicBool::new(false));
        let cancel = stop.clone();
        thread::spawn(move || {
            let mut port = match serialport::new(path, baud)
                .data_bits(serialport::DataBits::Eight)
                .parity(serialport::Parity::None)
                .stop_bits(serialport::StopBits::One)
                .flow_control(serialport::FlowControl::None)
                .timeout(Duration::from_millis(50))
                .open()
            {
                Ok(port) => port,
                Err(error) => {
                    let _ = output.send(Event::Failed(error.to_string()));
                    return;
                }
            };
            if output.send(Event::Connected).is_err() {
                return;
            }
            let mut buffer = [0; 512];
            while !cancel.load(Ordering::Relaxed) {
                while let Ok(command) = input.try_recv() {
                    if let Err(error) = port
                        .write_all(command.as_bytes())
                        .and_then(|_| port.write_all(b"\r"))
                    {
                        let _ = output.send(Event::Failed(error.to_string()));
                        return;
                    }
                }
                match port.read(&mut buffer) {
                    Ok(0) => {
                        let _ = output.send(Event::Failed("Serial link closed".into()));
                        return;
                    }
                    Ok(size) => {
                        if output.send(Event::Output(buffer[..size].to_vec())).is_err() {
                            return;
                        }
                    }
                    Err(error)
                        if matches!(
                            error.kind(),
                            std::io::ErrorKind::TimedOut
                                | std::io::ErrorKind::WouldBlock
                                | std::io::ErrorKind::Interrupted
                        ) => {}
                    Err(error) => {
                        let _ = output.send(Event::Failed(error.to_string()));
                        return;
                    }
                }
            }
        });
        Self {
            commands,
            events,
            stop,
        }
    }

    pub fn send(&self, command: &str) -> Result<(), String> {
        if command.len() > 63
            || !command
                .bytes()
                .all(|byte| byte.is_ascii_graphic() || byte == b' ')
        {
            return Err("Use at most 63 printable ASCII characters".into());
        }
        self.commands
            .try_send(command.to_owned())
            .map_err(|_| "Console busy or closed".into())
    }
}
