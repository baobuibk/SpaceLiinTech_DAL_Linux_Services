mod app;
mod catalog;
mod link;
mod sim;
mod theme;
mod ui;

use app::App;
use crossterm::event::{self, Event, KeyEventKind};
use crossterm::terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen};
use crossterm::ExecutableCommand;
use ratatui::backend::CrosstermBackend;
use ratatui::Terminal;
use std::io::{self, Write};
use std::time::Duration;

const USAGE: &str = "exp_tui [--sim] [--port DEVICE] [--baud RATE] [--dump PAGE [WxH]] [--help]
  --sim            run against the built-in simulator (no hardware)
  --port DEVICE    Console (UART2) serial device, opened at start
  --baud RATE      Console baud (default 115200)
  --dump PAGE WxH  print one rendered page as text and exit (e.g. --dump heaters 120x40)
Keys: Tab/Shift-Tab top menu (HOME..EXPERIMENT, PORT SETTINGS), Enter into left menu,
      Up/Down choose, Right to page then console, Left back, Enter press/edit, Space pick,
      / console, p ports, a auto-refresh, 1-4 link, Ctrl-L clear, q quit";

fn main() -> io::Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut sim = false;
    let mut port: Option<String> = None;
    let mut baud: Option<String> = None;
    let mut dump: Option<(String, String)> = None;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--sim" => sim = true,
            "--port" => {
                i += 1;
                port = args.get(i).cloned();
            }
            "--baud" => {
                i += 1;
                baud = args.get(i).cloned();
            }
            "--dump" => {
                i += 1;
                let page = args.get(i).cloned().unwrap_or_default();
                let size = args.get(i + 1).filter(|s| s.contains('x')).cloned();
                if size.is_some() {
                    i += 1;
                }
                dump = Some((page, size.unwrap_or_else(|| "120x40".into())));
            }
            "-h" | "--help" => {
                println!("{USAGE}");
                return Ok(());
            }
            other => {
                eprintln!("unknown argument: {other}\n{USAGE}");
                std::process::exit(2);
            }
        }
        i += 1;
    }

    if let Some((page, size)) = dump {
        let mut app = App::new(true);
        let needle = page.to_lowercase();
        match catalog::PAGES.iter().position(|p| p.name.to_lowercase().contains(&needle)) {
            Some(p) => {
                app.page = p;
                app.section = catalog::PAGES[p].section;
            }
            None => {
                eprintln!("no page matches '{page}'");
                std::process::exit(2);
            }
        }
        let (w, h) = size.split_once('x').unwrap_or(("120", "40"));
        print!("{}", ui::dump(&app, w.parse().unwrap_or(120), h.parse().unwrap_or(40)));
        return Ok(());
    }

    let mut app = App::new(sim);
    if let Some(b) = baud {
        app.links[0].baud = b;
    }
    if let Some(p) = port {
        if sim {
            app.info("--port ignored in simulation mode");
        } else {
            app.links[0].port = p;
            let tx = app.tx.clone();
            match app.links[0].open_serial(0, tx) {
                Ok(()) => {
                    let s = app.links[0].status.clone();
                    app.info(&s);
                }
                Err(e) => app.info(&format!("open failed: {e}")),
            }
        }
    }

    enable_raw_mode()?;
    io::stdout().execute(EnterAlternateScreen)?;
    let _ = io::stdout().flush();
    let mut term = Terminal::new(CrosstermBackend::new(io::stdout()))?;
    let result = run(&mut term, &mut app);
    disable_raw_mode()?;
    io::stdout().execute(LeaveAlternateScreen)?;
    result
}

fn run(term: &mut Terminal<CrosstermBackend<io::Stdout>>, app: &mut App) -> io::Result<()> {
    while !app.quit {
        app.drain_rx();
        app.tick();
        term.draw(|f| ui::draw(f, app))?;
        if event::poll(Duration::from_millis(50))? {
            if let Event::Key(k) = event::read()? {
                if k.kind == KeyEventKind::Press {
                    app.on_key(k);
                }
            }
        }
    }
    Ok(())
}
