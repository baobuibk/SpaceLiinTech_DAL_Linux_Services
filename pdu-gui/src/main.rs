mod tui;
use dal_pdu_gui::{map, rtu::Client, worker::Config};
use std::{
    io::{self, IsTerminal},
    time::Duration,
};

fn main() {
    if let Err(error) = run() {
        eprintln!("PDU: {error}");
        std::process::exit(1);
    }
}
fn run() -> Result<(), String> {
    let mut config = Config {
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
    };
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--help" || a == "-h") {
        println!("DAL PDU terminal monitor (no desktop display required)\n\nUsage: dal-pdu-gui [--port PORT] [--baud BAUD] [--slave ID] [--timeout MS] [--connect]\n       dal-pdu-gui --probe PORT [BAUD] [SLAVE]\n\nKeys: c connect/disconnect; s settings; Tab/Left/Right tabs; Up/Down select;\n      Enter/e edit; r refresh; t prepare PC clock; l activity; q quit on table tabs; Ctrl-C quit.\nText console tab: F2 port/baud; F3 open/close; F4 clear; Enter sends a command.\nWrites require Enter to review and y to apply. --probe reads once without a TTY.");
        return Ok(());
    }
    if args.first().map(String::as_str) == Some("--probe") {
        config.port = args
            .get(1)
            .ok_or("--probe requires PORT [BAUD] [SLAVE]")?
            .clone();
        if let Some(v) = args.get(2) {
            config.baud = v.parse().map_err(|_| "Invalid baud")?;
        }
        if let Some(v) = args.get(3) {
            config.unit = v.parse().map_err(|_| "Invalid slave")?;
        }
        let mut client = Client::open(
            &config.port,
            config.baud,
            config.unit,
            Duration::from_millis(config.timeout_ms),
        )?;
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
        return Ok(());
    }
    let mut connect = false;
    let mut i = 0;
    while i < args.len() {
        let arg = &args[i];
        if arg == "--connect" {
            connect = true;
            i += 1;
            continue;
        }
        let value = args
            .get(i + 1)
            .ok_or_else(|| format!("Missing value for {arg}; see --help"))?;
        match arg.as_str() {
            "--port" => config.port = value.clone(),
            "--baud" => config.baud = value.parse().map_err(|_| "Invalid baud")?,
            "--slave" => config.unit = value.parse().map_err(|_| "Invalid slave")?,
            "--timeout" => config.timeout_ms = value.parse().map_err(|_| "Invalid timeout")?,
            _ => return Err(format!("Unknown option {arg}; see --help")),
        }
        i += 2;
    }
    tui::validate_config(&config)?;
    if !io::stdin().is_terminal() || !io::stdout().is_terminal() {
        return Err("Interactive mode needs a terminal. Run directly in a shell or use ssh -t. For redirected output use --probe PORT [BAUD] [SLAVE].".into());
    }
    tui::run(config, connect).map_err(|e| e.to_string())
}
