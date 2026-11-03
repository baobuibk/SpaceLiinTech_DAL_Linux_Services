use crate::catalog::{self, Page, PAGES, W};
use crate::link::{Link, LINKS};
use crate::sim::Sim;
use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};
use std::collections::HashMap;
use std::time::{Duration, Instant};
use std::sync::mpsc::{Receiver, Sender};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Zone {
    /// Section tabs + PORT SETTINGS.
    Top,
    /// Left peripheral list.
    Periph,
    /// Centre page (buttons/fields, or the port list).
    Page,
    /// Right command console (typing).
    Console,
}

/// Index of PORT SETTINGS in the top menu (after the six sections).
pub const TOP_PORT: usize = 6;
const POLL_EVERY: Duration = Duration::from_secs(2);
/// Replies arriving this soon after an automatic poll stay out of the log.
const QUIET: Duration = Duration::from_millis(700);

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Info,
    Queue,
    Tx,
    Rx,
    Err,
}

pub struct LogLine {
    pub kind: Kind,
    pub text: String,
}

/// What an in-place text edit is writing to.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Edit {
    None,
    Value,
    Port,
    Baud,
    /// Choosing a serial device from `port_list`.
    Pick,
}

pub struct App {
    pub top: usize,
    pub section: usize,
    pub page: usize, // index into PAGES
    pub auto: bool,
    last_poll: Option<Instant>,
    quiet_until: Option<Instant>,
    pub zone: Zone,
    pub cursor: usize,
    pub edit: Edit,
    pub values: Vec<Vec<String>>,
    pub active: usize,
    pub links: [Link; 4],
    pub log: Vec<LogLine>,
    pub log_scroll: usize,
    pub input: String,
    pub reads: HashMap<&'static str, String>,
    pub needles: Vec<&'static str>,
    pub sim: Option<Sim>,
    pub image: Option<Vec<f32>>,
    pub port_view: bool,
    pub port_cursor: usize,
    pub port_list: Vec<String>,
    pub port_sel: usize,
    pub quit: bool,
    pub tx: Sender<(usize, String)>,
    pub rx: Receiver<(usize, String)>,
}

impl App {
    pub fn new(sim: bool) -> Self {
        let (tx, rx) = std::sync::mpsc::channel();
        let mut app = App {
            top: 0,
            section: 0,
            page: 0,
            auto: true,
            last_poll: None,
            quiet_until: None,
            zone: Zone::Top,
            cursor: 0,
            edit: Edit::None,
            values: PAGES.iter().map(catalog::defaults).collect(),
            active: 0,
            links: std::array::from_fn(|i| Link::new(LINKS[i].2)),
            log: Vec::new(),
            log_scroll: 0,
            input: String::new(),
            reads: HashMap::new(),
            needles: catalog::all_needles(),
            sim: if sim { Some(Sim::new()) } else { None },
            image: None,
            port_view: false,
            port_cursor: 0,
            port_list: Vec::new(),
            port_sel: 0,
            quit: false,
            tx,
            rx,
        };
        if sim {
            for l in app.links.iter_mut() {
                l.open = true;
                l.status = "SIMULATED".into();
            }
            app.info("SIMULATED EXP: replies come from the built-in model; no hardware is accessed.");
            app.info("Status refreshes automatically; the console is for debugging.");
        } else {
            app.info("Press p for port settings, enter a device, then o to open the Console link.");
        }
        app
    }

    pub fn page_def(&self) -> &'static Page {
        &PAGES[self.page]
    }

    pub fn info(&mut self, t: &str) {
        self.push(Kind::Info, t.into());
    }

    fn push(&mut self, kind: Kind, text: String) {
        self.log.push(LogLine { kind, text });
        if self.log.len() > 2000 {
            self.log.drain(..500);
        }
        self.log_scroll = 0;
    }

    pub fn open_count(&self) -> usize {
        self.links.iter().filter(|l| l.open).count()
    }

    /// Number of focusable items on the current page.
    pub fn item_count(&self) -> usize {
        self.values[self.page].len()
    }

    pub fn select_top(&mut self, t: usize) {
        self.top = t % (catalog::SECTIONS.len() + 1);
        if self.top == TOP_PORT {
            self.port_view = true;
            self.port_cursor = self.active;
        } else {
            self.section = self.top;
            self.page = catalog::pages_in(self.section)[0];
            self.cursor = 0;
            self.port_view = false;
        }
        self.last_poll = None;
    }

    fn select_page(&mut self, p: usize) {
        self.page = p;
        self.cursor = 0;
        self.last_poll = None;
    }

    /// Periodic read-only refresh of the status shown on the current page.
    pub fn tick(&mut self) {
        if !self.auto || self.port_view || self.edit != Edit::None {
            return;
        }
        let due = self.last_poll.map_or(true, |t| t.elapsed() >= POLL_EVERY);
        if !due || !(self.sim.is_some() || self.links[self.active].open) {
            return;
        }
        self.last_poll = Some(Instant::now());
        let cmds = self.page_def().poll;
        if cmds.is_empty() {
            return;
        }
        for c in cmds {
            self.dispatch(c, true);
        }
        self.quiet_until = Some(Instant::now() + QUIET);
    }

    pub fn drain_rx(&mut self) {
        while let Ok((link, line)) = self.rx.try_recv() {
            self.on_line(link, line);
        }
    }

    fn on_line(&mut self, link: usize, line: String) {
        for n in self.needles.clone() {
            if let Some(pos) = line.find(n) {
                self.reads.insert(n, line[pos + n.len()..].trim().to_string());
            }
        }
        if self.quiet_until.is_some_and(|t| Instant::now() < t) {
            return;
        }
        self.push(Kind::Rx, format!("{}: RX < {}", LINKS[link].1, line));
    }

    pub fn send(&mut self, cmd: &str) {
        self.quiet_until = None;
        self.dispatch(cmd, false);
    }

    /// Send one command; `quiet` (automatic poll) keeps it out of the log.
    fn dispatch(&mut self, cmd: &str, quiet: bool) {
        let cmd = cmd.trim().to_string();
        if cmd.is_empty() {
            return;
        }
        let a = self.active;
        if !quiet {
            self.push(Kind::Queue, format!("{}: QUEUE {cmd}", LINKS[a].1));
        }
        if self.sim.is_some() {
            if !quiet {
                self.push(Kind::Tx, format!("{}: TX > {cmd}", LINKS[a].1));
            }
            let was = self.quiet_until;
            if quiet {
                self.quiet_until = Some(Instant::now() + QUIET);
            }
            let sim = self.sim.as_mut().unwrap();
            let replies = sim.reply(&cmd);
            if let Some(f) = sim.frame.clone() {
                self.image = Some(f);
            }
            for r in replies {
                self.on_line(a, r);
            }
            if quiet {
                self.quiet_until = was;
            }
            return;
        }
        match self.links[a].send_line(&cmd) {
            Ok(()) if quiet => {}
            Ok(()) => self.push(Kind::Tx, format!("{}: TX > {cmd}", LINKS[a].1)),
            Err(e) => {
                self.auto = false;
                self.push(Kind::Err, format!("{}: {e} (auto refresh off, press a)", LINKS[a].1));
            }
        }
    }

    fn press_item(&mut self) {
        let page = self.page_def();
        let mut i = 0;
        for g in page.groups {
            for w in g.widgets {
                match w {
                    W::Btn(_, cmd) => {
                        if i == self.cursor {
                            let c = catalog::expand(page, &self.values[self.page], cmd);
                            self.send(&c);
                            return;
                        }
                        i += 1;
                    }
                    W::Field(..) => {
                        if i == self.cursor {
                            self.edit = Edit::Value;
                            return;
                        }
                        i += 1;
                    }
                    W::Pick(_, _, opts) => {
                        if i == self.cursor {
                            let cur = &self.values[self.page][i];
                            let n = opts.iter().position(|o| o == cur).map(|p| (p + 1) % opts.len()).unwrap_or(0);
                            self.values[self.page][i] = opts[n].to_string();
                            return;
                        }
                        i += 1;
                    }
                    W::Row(_, _, btns) => {
                        for (_, cmd) in btns.iter() {
                            if i == self.cursor {
                                let c = catalog::expand(page, &self.values[self.page], cmd);
                                self.send(&c);
                                return;
                            }
                            i += 1;
                        }
                    }
                }
            }
        }
    }

    fn toggle_link(&mut self) {
        let i = self.port_cursor;
        if self.sim.is_some() {
            self.info("Simulation mode: links are always open.");
            return;
        }
        if self.links[i].open {
            self.links[i].close();
            self.info(&format!("{} closed", LINKS[i].0));
        } else {
            let tx = self.tx.clone();
            match self.links[i].open_serial(i, tx) {
                Ok(()) => {
                    let s = self.links[i].status.clone();
                    self.info(&format!("{}: {s}", LINKS[i].0));
                }
                Err(e) => {
                    self.links[i].status = format!("ERROR {e}");
                    self.push(Kind::Err, format!("{}: {e}", LINKS[i].0));
                }
            }
        }
    }

    pub fn on_key(&mut self, key: KeyEvent) {
        if key.modifiers.contains(KeyModifiers::CONTROL) {
            match key.code {
                KeyCode::Char('c') => self.quit = true,
                KeyCode::Char('l') => self.log.clear(),
                _ => {}
            }
            return;
        }
        if self.edit == Edit::Pick {
            self.pick_key(key);
            return;
        }
        // text editing modes
        if self.edit != Edit::None {
            let buf: &mut String = match self.edit {
                Edit::Value => &mut self.values[self.page][self.cursor],
                Edit::Port => &mut self.links[self.port_cursor].port,
                _ => &mut self.links[self.port_cursor].baud,
            };
            match key.code {
                KeyCode::Enter | KeyCode::Esc => self.edit = Edit::None,
                KeyCode::Backspace => {
                    buf.pop();
                }
                KeyCode::Char(c) if self.edit != Edit::Baud || c.is_ascii_digit() => buf.push(c),
                _ => {}
            }
            return;
        }
        if self.zone == Zone::Console {
            match key.code {
                KeyCode::Esc | KeyCode::Tab | KeyCode::BackTab => {
                    self.zone = Zone::Page;
                    if key.code != KeyCode::Esc {
                        self.cycle_top(key.code == KeyCode::Tab);
                    }
                }
                KeyCode::Left if self.input.is_empty() => self.zone = Zone::Page,
                KeyCode::Enter => {
                    let c = std::mem::take(&mut self.input);
                    self.send(&c);
                }
                KeyCode::Backspace => {
                    self.input.pop();
                }
                KeyCode::Char(c) => self.input.push(c),
                KeyCode::PageUp => self.log_scroll += 5,
                KeyCode::PageDown => self.log_scroll = self.log_scroll.saturating_sub(5),
                _ => {}
            }
            return;
        }
        match key.code {
            KeyCode::Char('q') => self.quit = true,
            KeyCode::Char('/') => self.zone = Zone::Console,
            KeyCode::Char('a') => {
                self.auto = !self.auto;
                self.last_poll = None;
            }
            KeyCode::F(n) if (1..=4).contains(&n) => self.active = n as usize - 1,
            KeyCode::Char(c @ '1'..='4') => self.active = c as usize - '1' as usize,
            KeyCode::Char('p') => {
                self.select_top(TOP_PORT);
                self.zone = Zone::Page;
            }
            KeyCode::PageUp => self.log_scroll += 5,
            KeyCode::PageDown => self.log_scroll = self.log_scroll.saturating_sub(5),
            KeyCode::Tab => self.cycle_top(true),
            KeyCode::BackTab => self.cycle_top(false),
            _ => match self.zone {
                Zone::Top => self.top_key(key),
                Zone::Periph => self.periph_key(key),
                Zone::Page if self.port_view => self.port_key(key),
                Zone::Page => self.page_key(key),
                Zone::Console => {}
            },
        }
    }

    /// Tab / Shift-Tab: move along the top menu from anywhere.
    fn cycle_top(&mut self, fwd: bool) {
        let n = catalog::SECTIONS.len() + 1;
        self.zone = Zone::Top;
        self.select_top(if fwd { self.top + 1 } else { self.top + n - 1 });
    }

    fn top_key(&mut self, key: KeyEvent) {
        match key.code {
            KeyCode::Right => self.cycle_top(true),
            KeyCode::Left => self.cycle_top(false),
            KeyCode::Enter | KeyCode::Down => {
                self.zone = if self.top == TOP_PORT { Zone::Page } else { Zone::Periph };
            }
            _ => {}
        }
    }

    fn periph_key(&mut self, key: KeyEvent) {
        match key.code {
            KeyCode::Up | KeyCode::Down => {
                let pages = catalog::pages_in(self.section);
                let pos = pages.iter().position(|&p| p == self.page).unwrap_or(0);
                let n = pages.len();
                let np = if key.code == KeyCode::Down { (pos + 1) % n } else { (pos + n - 1) % n };
                self.select_page(pages[np]);
            }
            KeyCode::Right | KeyCode::Enter => {
                self.zone = if self.item_count() > 0 { Zone::Page } else { Zone::Console };
            }
            KeyCode::Left | KeyCode::Esc => self.zone = Zone::Top,
            _ => {}
        }
    }

    fn page_key(&mut self, key: KeyEvent) {
        let n = self.item_count();
        match key.code {
            KeyCode::Up if n > 0 => self.cursor = (self.cursor + n - 1) % n,
            KeyCode::Down if n > 0 => self.cursor = (self.cursor + 1) % n,
            KeyCode::Enter | KeyCode::Char(' ') if n > 0 => self.press_item(),
            KeyCode::Right => self.zone = Zone::Console,
            KeyCode::Left | KeyCode::Esc => self.zone = Zone::Periph,
            _ => {}
        }
    }

    /// Rescan serial devices and open the chooser on the selected link.
    fn open_pick(&mut self) {
        self.port_list = scan_ports();
        let cur = self.links[self.port_cursor].port.clone();
        self.port_sel = self.port_list.iter().position(|p| *p == cur).unwrap_or(0);
        if self.port_list.is_empty() {
            self.info("No serial devices found in /dev (press t to type a path).");
        } else {
            self.edit = Edit::Pick;
        }
    }

    fn pick_key(&mut self, key: KeyEvent) {
        let n = self.port_list.len().max(1);
        match key.code {
            KeyCode::Up => self.port_sel = (self.port_sel + n - 1) % n,
            KeyCode::Down => self.port_sel = (self.port_sel + 1) % n,
            KeyCode::Char('r') => {
                self.port_list = scan_ports();
                self.port_sel = 0;
            }
            KeyCode::Enter => {
                if let Some(p) = self.port_list.get(self.port_sel) {
                    self.links[self.port_cursor].port = p.clone();
                }
                self.edit = Edit::None;
            }
            KeyCode::Esc | KeyCode::Left => self.edit = Edit::None,
            _ => {}
        }
    }

    fn port_key(&mut self, key: KeyEvent) {
        match key.code {
            KeyCode::Esc | KeyCode::Left => self.zone = Zone::Top,
            KeyCode::Right => self.zone = Zone::Console,
            KeyCode::Up => self.port_cursor = (self.port_cursor + 3) % 4,
            KeyCode::Down => self.port_cursor = (self.port_cursor + 1) % 4,
            KeyCode::Char('e') => self.open_pick(),
            KeyCode::Char('t') => self.edit = Edit::Port,
            KeyCode::Char('b') => self.edit = Edit::Baud,
            KeyCode::Char('o') | KeyCode::Enter => self.toggle_link(),
            _ => {}
        }
    }
}

/// Serial device nodes present in /dev, sorted.
fn scan_ports() -> Vec<String> {
    const PFX: [&str; 7] = ["ttyUSB", "ttyACM", "ttyS", "ttyAMA", "ttyAT", "ttyO", "ttymxc"];
    let mut v: Vec<String> = std::fs::read_dir("/dev")
        .map(|d| {
            d.filter_map(|e| e.ok())
                .filter_map(|e| e.file_name().into_string().ok())
                .filter(|n| PFX.iter().any(|p| n.starts_with(p)))
                .map(|n| format!("/dev/{n}"))
                .collect()
        })
        .unwrap_or_default();
    v.sort_by(|a, b| (a.len(), a).cmp(&(b.len(), b)));
    v
}

#[cfg(test)]
mod tests {
    use super::*;
    use crossterm::event::{KeyEventKind, KeyEventState};

    fn key(c: KeyCode) -> KeyEvent {
        KeyEvent { code: c, modifiers: KeyModifiers::NONE, kind: KeyEventKind::Press, state: KeyEventState::NONE }
    }

    #[test]
    fn button_sends_through_simulator_and_updates_readout() {
        let mut a = App::new(true);
        a.select_top(1); // POWER > Switched rails
        a.zone = Zone::Page;
        a.cursor = 4; // SLN/HD "ON"
        a.on_key(key(KeyCode::Enter));
        assert_eq!(a.reads.get("Power SLN HD requested=").map(String::as_str), Some("ON observed=UNKNOWN"));
    }

    #[test]
    fn template_uses_field_values() {
        let mut a = App::new(true);
        a.select_top(2);
        a.zone = Zone::Periph;
        a.on_key(key(KeyCode::Down)); // U15
        a.zone = Zone::Page;
        a.cursor = 6; // "Set voltage"
        a.on_key(key(KeyCode::Enter));
        assert!(a.log.iter().any(|l| l.text.contains("QUEUE pump4_set_volt 0 30")));
    }

    #[test]
    fn real_mode_without_port_reports_error() {
        let mut a = App::new(false);
        a.send("help");
        assert!(a.log.last().unwrap().text.contains("not open"));
    }

    #[test]
    fn navigation_follows_menu_levels() {
        let mut a = App::new(true);
        a.on_key(key(KeyCode::Tab)); // HOME -> POWER
        assert_eq!((a.zone, a.top), (Zone::Top, 1));
        a.on_key(key(KeyCode::Enter));
        assert_eq!(a.zone, Zone::Periph);
        a.on_key(key(KeyCode::Down)); // PAC1934
        assert_eq!(a.page_def().name, "PAC1934");
        a.on_key(key(KeyCode::Right));
        assert_eq!(a.zone, Zone::Page);
        a.on_key(key(KeyCode::Right));
        assert_eq!(a.zone, Zone::Console);
        a.on_key(key(KeyCode::Left));
        a.on_key(key(KeyCode::Left));
        assert_eq!(a.zone, Zone::Periph);
        a.on_key(key(KeyCode::Left));
        assert_eq!(a.zone, Zone::Top);
    }

    #[test]
    fn tab_reaches_port_settings_and_wraps() {
        let mut a = App::new(true);
        for _ in 0..TOP_PORT {
            a.on_key(key(KeyCode::Tab));
        }
        assert!(a.port_view && a.top == TOP_PORT);
        a.on_key(key(KeyCode::Enter));
        assert_eq!(a.zone, Zone::Page);
        a.on_key(key(KeyCode::Tab));
        assert!(!a.port_view && a.top == 0);
    }

    #[test]
    fn auto_poll_fills_status_without_logging_traffic() {
        let mut a = App::new(true);
        a.select_top(3); // SENSORS > NTC
        let before = a.log.len();
        a.tick();
        assert_eq!(a.log.len(), before);
        assert!(a.reads.get("NTC[0] : ").is_some_and(|v| v.starts_with("25.30")));
    }

    #[test]
    fn port_picker_selects_from_list_and_baud_is_numeric() {
        let mut a = App::new(false);
        a.select_top(TOP_PORT);
        a.zone = Zone::Page;
        a.port_list = vec!["/dev/ttyUSB0".into(), "/dev/ttyUSB1".into()];
        a.edit = Edit::Pick;
        a.on_key(key(KeyCode::Down));
        a.on_key(key(KeyCode::Enter));
        assert_eq!(a.links[0].port, "/dev/ttyUSB1");
        assert!(a.edit == Edit::None);
        a.on_key(key(KeyCode::Char('b')));
        a.on_key(key(KeyCode::Char('x')));
        for c in "9600".chars() {
            a.on_key(key(KeyCode::Char(c)));
        }
        assert!(a.links[0].baud.ends_with("9600") && !a.links[0].baud.contains('x'));
    }
}
