mod app;
mod controls;
mod data_transfer;
mod json;
mod link;
mod live;
mod proto;
mod sim;
mod ui;

use app::{State, Tab};
use crossterm::{
    event::{
        self, DisableMouseCapture, EnableMouseCapture, Event, KeyCode, KeyEvent, KeyEventKind,
        KeyModifiers,
    },
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use link::Mode;
use live::default_target;
use ratatui::{backend::TestBackend, prelude::*};
use std::io;
use std::time::{Duration, Instant};

const HELP: &str = "\
Usage: daltui [SOURCE] [--baud N] [--dump TAB [WxH] [KEYS]] [--help]

SOURCE (default: console when its socket exists, otherwise sim)
  --sim              built-in simulator, nothing is sent anywhere
  --console [PATH]   flight console text commands (default $DAL_FLIGHT_CONSOLE_SOCKET
                     or /run/dal/dal_obc_fsw/console.sock)
  --rs422 [PATH]     binary RS-422 frames, for protocol checks. PATH is the flight frame
                     socket ($DAL_OBC_RS422_SOCKET, /run/dal/dal_obc_fsw/rs422.sock)
                     or a serial device such as /dev/ttyUSB0 or COM3 (--baud, default 921600)
                     Windows: COM ports only; default COM1 (override with --rs422 COMn).
                     --console and --compare require Linux Unix sockets.
  The source can also be changed at run time on the CFG tab.

  --control-only     send only commands explicitly selected by the operator (default live mode)
  --poll             enable automatic housekeeping reads every 2 s
  --manual           enable reads only when UPDATE is requested

  --compare          send the read-only commands over both console and RS-422 and compare
                     the TM payloads byte by byte (volatile epoch/age fields are masked)
  --dump TAB         render one tab as plain text and exit (no tty needed)
                     TAB = home|obc|exp|pdu|log|cfg, default size 120x40
                     KEYS = comma-separated keys applied first, e.g. down,enter,right,right
";

struct Opts {
    mode: Option<Mode>,
    console: String,
    rs422: String,
    baud: u32,
    dump: Option<Vec<String>>,
    compare: bool,
    manual: bool,
    control_only: bool,
}

fn parse_args() -> Opts {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut o = Opts {
        mode: None,
        console: default_target(Mode::Console),
        rs422: default_target(Mode::Rs422),
        baud: 921_600,
        dump: None,
        compare: false,
        manual: false,
        control_only: true,
    };
    let mut i = 0;
    let path_arg = |i: &mut usize, dst: &mut String| {
        if let Some(n) = args.get(*i + 1).filter(|a| !a.starts_with("--")) {
            *dst = n.clone();
            *i += 1;
        }
    };
    while i < args.len() {
        match args[i].as_str() {
            "-h" | "--help" => {
                print!("{HELP}");
                std::process::exit(0);
            }
            "--sim" => o.mode = Some(Mode::Sim),
            "--console" => {
                o.mode = Some(Mode::Console);
                path_arg(&mut i, &mut o.console);
            }
            "--rs422" => {
                o.mode = Some(Mode::Rs422);
                path_arg(&mut i, &mut o.rs422);
            }
            "--baud" => {
                o.baud = args
                    .get(i + 1)
                    .and_then(|b| b.parse().ok())
                    .unwrap_or_else(|| {
                        eprintln!("--baud needs a number");
                        std::process::exit(2)
                    });
                i += 1;
            }
            "--compare" => o.compare = true,
            "--control-only" => {
                o.control_only = true;
                o.manual = false;
            }
            "--poll" => {
                o.control_only = false;
                o.manual = false;
            }
            "--manual" => {
                o.control_only = false;
                o.manual = true;
            }
            "--dump" => {
                o.dump = Some(args[i + 1..].to_vec());
                break;
            }
            other => {
                eprintln!("unknown argument '{other}'\n\n{HELP}");
                std::process::exit(2);
            }
        }
        i += 1;
    }
    o
}

fn main() -> io::Result<()> {
    let o = parse_args();
    #[cfg(windows)]
    if o.compare || o.mode == Some(Mode::Console) {
        eprintln!(
            "--console/--compare require Linux Unix sockets; on Windows use --rs422 COM3 or --sim"
        );
        std::process::exit(2);
    }
    if o.compare {
        std::process::exit(compare(&o.console, &o.rs422, o.baud));
    }
    let mut st = State::new();
    st.sim.live.console_path = o.console.clone();
    st.sim.live.rs422_path = o.rs422.clone();
    st.sim.live.baud = o.baud;
    st.sim.live.control_only = o.control_only;
    st.sim.live.auto = !o.manual && !o.control_only;
    let mode = match o.mode {
        Some(m) => m,
        None if std::path::Path::new(&o.console).exists() => Mode::Console,
        None => Mode::Sim,
    };
    st.set_mode(mode);
    if o.mode.is_none() && mode == Mode::Sim {
        st.note(&format!(
            "console socket {} not found: running the simulator (change on the CFG tab)",
            o.console
        ));
    }
    match o.dump {
        Some(a) => dump(st, &a),
        None => run_tui(st),
    }
}

/// Restores the terminal on drop, including on panic.
struct TermGuard;

impl TermGuard {
    fn enter() -> io::Result<Self> {
        enable_raw_mode()?;
        execute!(io::stdout(), EnterAlternateScreen, EnableMouseCapture)?;
        Ok(TermGuard)
    }
}

impl Drop for TermGuard {
    fn drop(&mut self) {
        let _ = disable_raw_mode();
        let _ = execute!(io::stdout(), LeaveAlternateScreen, DisableMouseCapture);
    }
}

fn run_tui(mut st: State) -> io::Result<()> {
    let _guard = TermGuard::enter()?;
    let mut term = Terminal::new(CrosstermBackend::new(io::stdout()))?;
    let mut last = Instant::now();

    while !st.quit {
        term.draw(|f| ui::draw(f, &mut st))?;
        if event::poll(Duration::from_millis(100))? {
            if let Event::Key(k) = event::read()? {
                if k.kind == KeyEventKind::Press {
                    st.on_key(k);
                }
            }
        }
        let now = Instant::now();
        let dt = now.duration_since(last).as_secs_f64();
        if dt >= 0.25 {
            st.tick(dt);
            last = now;
        }
    }
    Ok(())
}

fn parse_key(s: &str) -> Option<KeyEvent> {
    let none = KeyModifiers::NONE;
    let code = match s {
        "up" => KeyCode::Up,
        "down" => KeyCode::Down,
        "left" => KeyCode::Left,
        "right" => KeyCode::Right,
        "enter" => KeyCode::Enter,
        "space" => KeyCode::Char(' '),
        "tab" => KeyCode::Tab,
        s if s.chars().count() == 1 => KeyCode::Char(s.chars().next()?),
        _ => return None,
    };
    Some(KeyEvent::new(code, none))
}

/// With a live link, wait (bounded) for the worker to finish what is queued.
fn settle(st: &mut State) {
    if st.sim.live.mode == Mode::Sim {
        return;
    }
    let t0 = Instant::now();
    // First pass lets the poll timer fire; then wait until nothing is in flight.
    let mut idle_ticks = 0;
    while t0.elapsed() < Duration::from_secs(12) {
        std::thread::sleep(Duration::from_millis(40));
        st.tick(0.05);
        if st.sim.live.pending == 0 {
            idle_ticks += 1;
            let nothing_due = !st.sim.live.auto && !st.sim.live.update_req;
            if idle_ticks > 4 && (st.sim.live.ok_count + st.sim.live.err_count > 0 || nothing_due) {
                break;
            }
        } else {
            idle_ticks = 0;
        }
    }
}

/// Headless render for checking layouts: `daltui --dump exp 120x40 down,enter`.
fn dump(mut st: State, args: &[String]) -> io::Result<()> {
    let tab = match args.first().map(String::as_str) {
        Some("home") => Tab::Home,
        Some("obc") => Tab::Obc,
        Some("exp") => Tab::Exp,
        Some("pdu") => Tab::Pdu,
        Some("log") => Tab::Log,
        Some("cfg") => Tab::Cfg,
        _ => {
            eprint!("{HELP}");
            std::process::exit(2);
        }
    };
    let (w, h) = args
        .get(1)
        .and_then(|s| s.split_once('x'))
        .and_then(|(w, h)| Some((w.parse::<u16>().ok()?, h.parse::<u16>().ok()?)))
        .unwrap_or((120, 40));
    let keys = args.get(if args.get(1).map_or(false, |s| s.contains('x')) {
        2
    } else {
        1
    });

    for _ in 0..20 {
        st.tick(0.5);
    }
    settle(&mut st);
    st.tab = tab;
    for k in keys
        .map(|s| s.split(',').collect::<Vec<_>>())
        .unwrap_or_default()
    {
        match parse_key(k) {
            Some(ev) => {
                st.on_key(ev);
                st.tick(0.5);
                settle(&mut st);
            }
            None => eprintln!("ignoring unknown key '{k}'"),
        }
    }

    let mut term = Terminal::new(TestBackend::new(w, h))?;
    term.draw(|f| ui::draw(f, &mut st))?;
    let buf = term.backend().buffer();
    for y in 0..h {
        let row: String = (0..w).map(|x| buf[(x, y)].symbol()).collect();
        println!("{}", row.trim_end());
    }
    Ok(())
}

/// Protocol check: same request over the console and over RS-422 frames.
/// Returns the process exit code (0 = all payloads identical).
fn compare(console: &str, rs422: &str, baud: u32) -> i32 {
    use link::{Console, Rs422, Transport};
    use proto::Cmd;
    let mut con = Console {
        path: console.to_string(),
    };
    let mut rs = Rs422::new(rs422, baud);
    println!("console {console}\nrs422   {rs422}\n");
    let cmds: Vec<(Cmd, &str)> = vec![
        (proto::ping(), "uptime"),
        (proto::sys_info(), "epoch header"),
        (proto::sys_rtc_get(), "epoch"),
        (proto::tm_obc_get(), "epoch"),
        (proto::pwr_stat(), "availability flags"),
        (proto::pdu_vout_get(), ""),
        (proto::thm_stat(), ""),
        (proto::fld_stat(), ""),
        (proto::rba_stat(), ""),
        (proto::pc_stat(), ""),
    ];
    let mut bad = 0;
    for (c, masked) in &cmds {
        let a = con.exec(c);
        let b = rs.exec(c);
        let head = format!("{:<22} {:02X}/{:02X}", c.text, c.ty, c.addr);
        match (a, b) {
            (Ok(a), Ok(b)) => {
                let (mut pa, mut pb) = (a.payload.clone(), b.payload.clone());
                mask(c, &mut pa);
                mask(c, &mut pb);
                let same = pa == pb && a.ack == b.ack && a.code == b.code;
                if !same {
                    bad += 1;
                }
                println!(
                    "{head}  {}  ack/code {:02X}/{:02X} vs {:02X}/{:02X}  len {} vs {}{}",
                    if same { "SAME" } else { "DIFF" },
                    a.ack,
                    a.code,
                    b.ack,
                    b.code,
                    a.payload.len(),
                    b.payload.len(),
                    if masked.is_empty() {
                        String::new()
                    } else {
                        format!("  (masked: {masked})")
                    }
                );
                if !same {
                    println!(
                        "    console {}\n    rs422   {}",
                        proto::to_hex(&a.payload),
                        proto::to_hex(&b.payload)
                    );
                }
            }
            (a, b) => {
                bad += 1;
                println!(
                    "{head}  FAIL  console: {}  rs422: {}",
                    a.err().unwrap_or_else(|| "ok".into()),
                    b.err().unwrap_or_else(|| "ok".into())
                );
            }
        }
    }
    println!(
        "\n{} of {} commands identical",
        cmds.len() - bad,
        cmds.len()
    );
    (bad != 0) as i32
}

/// Zero the fields that change between two reads.
fn mask(c: &proto::Cmd, p: &mut [u8]) {
    let z = |p: &mut [u8], r: std::ops::Range<usize>| {
        for i in r {
            if i < p.len() {
                p[i] = 0;
            }
        }
    };
    match (c.ty, c.addr) {
        (0, 0) | (1, 1) => z(p, 0..p.len()),
        (1, 0) => z(p, 2..12),
        (4, 2) => z(p, 2..6),
        (1, 4) => {
            z(p, 2..6);
            let n = p.get(1).copied().unwrap_or(0) as usize;
            for i in 0..n {
                z(p, 6 + 12 * i + 2..6 + 12 * i + 4);
            }
        }
        _ => {}
    }
}
