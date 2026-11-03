use crate::{
    map::{self, Bank},
    rtu::Client,
};
use std::{
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc::{self, Receiver, SyncSender},
        Arc,
    },
    thread,
    time::{Duration, Instant},
};

#[derive(Clone)]
pub struct Config {
    pub port: String,
    pub baud: u32,
    pub unit: u8,
    pub timeout_ms: u64,
}
pub enum Command {
    Refresh,
    Write(usize, String),
    Console(String),
}
pub enum Event {
    Connected,
    Failed(String),
    Snapshot(Vec<Result<Vec<u16>, String>>, Instant),
    WriteResult(usize, Result<(), String>),
    ConsoleResult(Result<Vec<u8>, String>),
}
pub struct Worker {
    pub commands: SyncSender<Command>,
    pub events: Receiver<Event>,
    stop: Arc<AtomicBool>,
}
impl Drop for Worker {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
    }
}
impl Worker {
    pub fn start(config: Config) -> Self {
        let (tx, rx) = mpsc::sync_channel(1);
        let (events, output) = mpsc::channel();
        let stop = Arc::new(AtomicBool::new(false));
        let cancel = stop.clone();
        thread::spawn(move || {
            let mut client = match Client::open(
                &config.port,
                config.baud,
                config.unit,
                Duration::from_millis(config.timeout_ms),
            ) {
                Ok(c) => c,
                Err(e) => {
                    let _ = events.send(Event::Failed(e));
                    return;
                }
            };
            let rows = map::rows();
            let _ = events.send(Event::Connected);
            let mut refresh = true;
            let mut last_poll = Instant::now();
            while !cancel.load(Ordering::Relaxed) {
                if let Ok(command) = rx.recv_timeout(Duration::from_millis(50)) {
                    if cancel.load(Ordering::Relaxed) {
                        break;
                    }
                    match command {
                        Command::Refresh => refresh = true,
                        Command::Console(text) => {
                            let result = client.text_command(&text);
                            let _ = events.send(Event::ConsoleResult(result));
                            refresh = true;
                        }
                        Command::Write(index, text) => {
                            let result = rows
                                .get(index)
                                .ok_or_else(|| "Unknown row".to_string())
                                .and_then(|row| {
                                    let words = row.parse(&text)?;
                                    let (function, payload) = row.write_request(&words);
                                    client.write(function, &payload)
                                });
                            let _ = events.send(Event::WriteResult(index, result));
                            refresh = true;
                        }
                    }
                }
                if refresh || last_poll.elapsed() >= Duration::from_secs(1) {
                    let started = Instant::now();
                    let mut values = Vec::with_capacity(rows.len());
                    // One coherent input-register snapshot; never read through holding-map gaps.
                    let inputs = client.read(4, 19, 30);
                    for row in &rows {
                        if cancel.load(Ordering::Relaxed) {
                            return;
                        }
                        let result = if row.bank == Bank::Input {
                            inputs
                                .as_ref()
                                .map(|v| {
                                    let offset = (row.address - 30020) as usize;
                                    v[offset..offset + row.count as usize].to_vec()
                                })
                                .map_err(Clone::clone)
                        } else {
                            client.read(row.bank.function(), row.offset(), row.count)
                        };
                        values.push(result);
                    }
                    if events.send(Event::Snapshot(values, started)).is_err() {
                        break;
                    }
                    last_poll = Instant::now();
                    refresh = false;
                }
            }
        });
        Self {
            commands: tx,
            events: output,
            stop,
        }
    }
}
