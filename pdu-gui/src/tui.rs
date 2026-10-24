use crossterm::{
    cursor::Show,
    event::{self, Event as Input, KeyCode, KeyEvent, KeyEventKind, KeyModifiers},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use dal_pdu_gui::{
    map::{self, Bank, Row as Parameter},
    worker::{Command, Config, Event, Worker},
};
use ratatui::{
    backend::CrosstermBackend,
    layout::{Constraint, Layout, Rect},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{Block, Borders, Clear, Paragraph, Row, Table, TableState, Tabs, Wrap},
    Frame, Terminal,
};
use std::{
    collections::VecDeque,
    io,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

const BANKS: [Bank; 4] = [Bank::Input, Bank::Holding, Bank::Coil, Bank::Discrete];
pub fn validate_config(c: &Config) -> Result<(), String> {
    if c.port.trim().is_empty() {
        return Err("Enter a serial port".into());
    }
    if ![1200, 2400, 4800, 9600, 19200, 38400, 115200].contains(&c.baud) {
        return Err("Baud: 1200/2400/4800/9600/19200/38400/115200".into());
    }
    if !(1..=247).contains(&c.unit) {
        return Err("Slave must be 1–247".into());
    }
    if !(100..=5000).contains(&c.timeout_ms) {
        return Err("Timeout must be 100–5000 ms".into());
    }
    Ok(())
}
#[derive(Clone)]
enum Dialog {
    Edit { index: usize, text: String },
    Confirm { index: usize, text: String },
    Settings { fields: [String; 4], active: usize },
    Log,
}
struct App {
    config: Config,
    worker: Option<Worker>,
    connected: bool,
    writing: bool,
    rows: Vec<Parameter>,
    values: Vec<Result<Vec<u16>, String>>,
    tab: usize,
    state: TableState,
    message: String,
    log: VecDeque<String>,
    last_scan: Option<Instant>,
    dialog: Option<Dialog>,
    quit: bool,
}
impl App {
    fn new(config: Config) -> Self {
        let rows = map::rows();
        Self {
            config,
            worker: None,
            connected: false,
            writing: false,
            values: vec![Err("Not read".into()); rows.len()],
            rows,
            tab: 0,
            state: TableState::default().with_selected(0),
            message: "Press s to configure the link, then c to connect".into(),
            log: VecDeque::new(),
            last_scan: None,
            dialog: None,
            quit: false,
        }
    }
    fn indices(&self) -> Vec<usize> {
        self.rows
            .iter()
            .enumerate()
            .filter(|(_, r)| r.bank == BANKS[self.tab])
            .map(|(i, _)| i)
            .collect()
    }
    fn selected(&self) -> usize {
        self.indices()[self.state.selected().unwrap_or(0)]
    }
    fn note(&mut self, s: String) {
        self.message = s.clone();
        self.log.push_front(s);
        self.log.truncate(50);
    }
    fn connect(&mut self) {
        if self.worker.is_some() {
            self.worker = None;
            self.connected = false;
            self.writing = false;
            self.values.fill(Err("Disconnected".into()));
            self.last_scan = None;
            self.note("Disconnected".into());
        } else {
            self.values.fill(Err("Waiting for reply".into()));
            self.last_scan = None;
            self.worker = Some(Worker::start(self.config.clone()));
            self.note("Opening serial port…".into());
        }
    }
    fn send(&mut self, c: Command) -> bool {
        if self
            .worker
            .as_ref()
            .is_some_and(|w| w.commands.try_send(c).is_ok())
        {
            true
        } else {
            self.note("Request not queued: link busy or disconnected".into());
            false
        }
    }
    fn receive(&mut self) {
        let events: Vec<_> = self
            .worker
            .as_ref()
            .map(|w| w.events.try_iter().collect())
            .unwrap_or_default();
        for event in events {
            match event {
                Event::Connected => {
                    self.connected = true;
                    self.note("Serial port open; reading tables…".into());
                }
                Event::Failed(e) => {
                    self.worker = None;
                    self.connected = false;
                    self.note(format!("Connection failed: {e}"));
                }
                Event::Snapshot(v, time) => {
                    let errors = v.iter().filter(|r| r.is_err()).count();
                    self.values = v;
                    self.last_scan = Some(time);
                    self.message = format!(
                        "Scan complete: {errors} unavailable rows. l: write results / activity"
                    );
                }
                Event::WriteResult(index, result) => {
                    self.writing = false;
                    match result {
                        Ok(()) => self.note(format!(
                            "{}: write acknowledged; refreshing",
                            self.rows[index].name
                        )),
                        Err(e) => self.note(format!(
                            "Write failed: {e}. Outcome may be unknown; refresh before retry."
                        )),
                    }
                }
            }
        }
    }
    fn edit(&mut self, clock: bool) {
        if !self.connected || self.writing {
            self.note("Connect first and wait for any pending write".into());
            return;
        }
        let index = if clock {
            self.rows.iter().position(|r| r.address == 40040).unwrap()
        } else {
            self.selected()
        };
        let row = &self.rows[index];
        if !row.writable() {
            self.note("This parameter is read only".into());
            return;
        }
        let text = if clock {
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|t| t.as_secs().to_string())
                .unwrap_or_default()
        } else {
            String::new()
        };
        self.dialog = Some(Dialog::Edit { index, text });
    }
    fn key(&mut self, key: KeyEvent) {
        if key.kind != KeyEventKind::Press {
            return;
        }
        if key.code == KeyCode::Char('c') && key.modifiers.contains(KeyModifiers::CONTROL) {
            self.quit = true;
            return;
        }
        if let Some(dialog) = self.dialog.take() {
            if key.code == KeyCode::Esc {
                return;
            }
            match dialog {
                Dialog::Log => {
                    if !matches!(
                        key.code,
                        KeyCode::Char('q') | KeyCode::Char('l') | KeyCode::Enter
                    ) {
                        self.dialog = Some(Dialog::Log);
                    }
                }
                Dialog::Confirm { index, text } => {
                    if key.code == KeyCode::Char('y') {
                        if self.connected && !self.writing {
                            self.writing = self.send(Command::Write(index, text));
                        } else {
                            self.note("Write cancelled: connection unavailable".into());
                        }
                    } else if !matches!(key.code, KeyCode::Char('n')) {
                        self.dialog = Some(Dialog::Confirm { index, text });
                    }
                }
                Dialog::Edit { index, mut text } => {
                    if key.code == KeyCode::Enter {
                        match self.rows[index].parse(&text) {
                            Ok(_) => self.dialog = Some(Dialog::Confirm { index, text }),
                            Err(e) => {
                                self.note(e);
                                self.dialog = Some(Dialog::Edit { index, text });
                            }
                        }
                    } else {
                        edit_text(&mut text, key);
                        self.dialog = Some(Dialog::Edit { index, text });
                    }
                }
                Dialog::Settings {
                    mut fields,
                    mut active,
                } => {
                    match key.code {
                        KeyCode::Tab | KeyCode::Down => active = (active + 1) % 4,
                        KeyCode::BackTab | KeyCode::Up => active = (active + 3) % 4,
                        KeyCode::Enter => {
                            let parsed = (|| -> Result<Config, String> {
                                let c = Config {
                                    port: fields[0].trim().into(),
                                    baud: fields[1].parse().map_err(|_| "Invalid baud")?,
                                    unit: fields[2].parse().map_err(|_| "Invalid slave")?,
                                    timeout_ms: fields[3].parse().map_err(|_| "Invalid timeout")?,
                                };
                                validate_config(&c)?;
                                Ok(c)
                            })();
                            match parsed {
                                Ok(c) => {
                                    self.config = c;
                                    self.note("Settings saved; press c to connect".into());
                                    return;
                                }
                                Err(e) => self.note(e),
                            }
                        }
                        _ => edit_text(&mut fields[active], key),
                    }
                    self.dialog = Some(Dialog::Settings { fields, active });
                }
            }
            return;
        }
        match key.code {
            KeyCode::Char('q') => self.quit = true,
            KeyCode::Char('c') => self.connect(),
            KeyCode::Char('s') => {
                if self.worker.is_none() {
                    self.dialog = Some(Dialog::Settings {
                        fields: [
                            self.config.port.clone(),
                            self.config.baud.to_string(),
                            self.config.unit.to_string(),
                            self.config.timeout_ms.to_string(),
                        ],
                        active: 0,
                    });
                } else {
                    self.note("Disconnect before changing settings".into());
                }
            }
            KeyCode::Char('l') => self.dialog = Some(Dialog::Log),
            KeyCode::Tab | KeyCode::Right => {
                self.tab = (self.tab + 1) % 4;
                self.state = TableState::default().with_selected(0);
            }
            KeyCode::BackTab | KeyCode::Left => {
                self.tab = (self.tab + 3) % 4;
                self.state = TableState::default().with_selected(0);
            }
            KeyCode::Char('1'..='4') => {
                if let KeyCode::Char(c) = key.code {
                    self.tab = (c as u8 - b'1') as usize;
                    self.state = TableState::default().with_selected(0);
                }
            }
            KeyCode::Up | KeyCode::Char('k') => self.move_selection(-1),
            KeyCode::Down | KeyCode::Char('j') => self.move_selection(1),
            KeyCode::PageUp => self.move_selection(-10),
            KeyCode::PageDown => self.move_selection(10),
            KeyCode::Home => self.state.select(Some(0)),
            KeyCode::End => self.state.select(Some(self.indices().len() - 1)),
            KeyCode::Enter | KeyCode::Char('e') => self.edit(false),
            KeyCode::Char('t') => self.edit(true),
            KeyCode::Char('r') if self.connected => {
                self.send(Command::Refresh);
            }
            _ => {}
        }
    }
    fn move_selection(&mut self, delta: isize) {
        let last = self.indices().len() - 1;
        let next =
            (self.state.selected().unwrap_or(0) as isize + delta).clamp(0, last as isize) as usize;
        self.state.select(Some(next));
    }
    fn draw(&mut self, f: &mut Frame) {
        let area = f.area();
        if area.width < 70 || area.height < 20 {
            f.render_widget(
                Paragraph::new("DAL PDU — enlarge terminal to at least 70×20. q or Ctrl-C exits.")
                    .wrap(Wrap { trim: false }),
                area,
            );
            return;
        }
        let panels = Layout::vertical([
            Constraint::Length(3),
            Constraint::Length(3),
            Constraint::Min(5),
            Constraint::Length(4),
            Constraint::Length(4),
        ])
        .split(area);
        let status = if self.connected {
            "CONNECTED"
        } else if self.worker.is_some() {
            "CONNECTING"
        } else {
            "DISCONNECTED"
        };
        let age = self
            .last_scan
            .map(|t| format!(" · sample age ≤ {:.1}s", t.elapsed().as_secs_f32()))
            .unwrap_or_default();
        f.render_widget(
            Paragraph::new(format!(
                "{status} · {} · {} baud · slave {} · 8N1{age}",
                self.config.port, self.config.baud, self.config.unit
            ))
            .block(Block::bordered().title(" DAL PDU / HW1.1 — Terminal Monitor "))
            .style(Style::default().fg(Color::Cyan)),
            panels[0],
        );
        f.render_widget(
            Tabs::new(BANKS.map(|b| b.label()))
                .select(self.tab)
                .highlight_style(
                    Style::default()
                        .fg(Color::Black)
                        .bg(Color::Cyan)
                        .add_modifier(Modifier::BOLD),
                )
                .block(Block::bordered().title(" Tables  [Tab / ← → / 1–4] ")),
            panels[1],
        );
        let indices = self.indices();
        let data = indices.iter().map(|&i| {
            let r = &self.rows[i];
            let (value, raw) = match &self.values[i] {
                Ok(v) => (
                    format!("{} {}", r.display(v), r.unit),
                    v.iter()
                        .map(|w| format!("{w:04X}"))
                        .collect::<Vec<_>>()
                        .join(" "),
                ),
                Err(_) => ("Unavailable".into(), "—".into()),
            };
            let address = if r.count == 2 {
                format!("{:05}–{:05}", r.address, r.address + 1)
            } else {
                format!("{:05}", r.address)
            };
            Row::new(vec![
                address,
                r.name.to_string(),
                value,
                raw,
                if r.writable() { "R/W" } else { "R" }.into(),
            ])
        });
        let table = Table::new(
            data,
            [
                Constraint::Length(12),
                Constraint::Percentage(32),
                Constraint::Min(20),
                Constraint::Length(10),
                Constraint::Length(4),
            ],
        )
        .header(
            Row::new(["Address", "Parameter", "Current value", "Raw hex", "Mode"])
                .style(Style::default().fg(Color::Yellow))
                .bottom_margin(1),
        )
        .row_highlight_style(
            Style::default()
                .bg(Color::DarkGray)
                .add_modifier(Modifier::BOLD),
        )
        .highlight_symbol("▶ ")
        .block(Block::bordered().title(" Live values · Enter/e to edit · writes require review "));
        f.render_stateful_widget(table, panels[2], &mut self.state);
        let i = self.selected();
        let r = &self.rows[i];
        let detail = match &self.values[i] {
            Ok(_) => r.hint.to_string(),
            Err(e) => format!("{} | {e}", r.hint),
        };
        f.render_widget(
            Paragraph::new(detail)
                .wrap(Wrap { trim: true })
                .block(Block::bordered().title(format!(" {} ", r.name))),
            panels[3],
        );
        f.render_widget(
            Paragraph::new(vec![
                Line::from(self.message.as_str()),
                Line::from(Span::styled(
                    "c link  s settings  ↑↓ select  e edit  r refresh  t clock  l log  q quit",
                    Style::default().fg(Color::Cyan),
                )),
            ])
            .wrap(Wrap { trim: true })
            .block(Block::bordered().title(if self.writing {
                " Write pending "
            } else {
                " Status "
            })),
            panels[4],
        );
        if let Some(dialog) = &self.dialog {
            let rect = centered(area, 78, 15);
            f.render_widget(Clear, rect);
            let (title, lines) = match dialog {
                Dialog::Edit { index, text } => {
                    let r = &self.rows[*index];
                    (
                        " Edit value ",
                        vec![
                            format!("{} · {:05} · {}", r.name, r.address, r.unit),
                            r.hint.into(),
                            String::new(),
                            format!("> {text}▏"),
                            String::new(),
                            "Type value · Backspace · Ctrl-U clear · Enter review · Esc cancel"
                                .into(),
                        ],
                    )
                }
                Dialog::Confirm { index, text } => {
                    let r = &self.rows[*index];
                    (
                        " Review write ",
                        vec![
                            format!("{} · slave {}", self.config.port, self.config.unit),
                            format!("{} ({:05}) → {} {}", r.name, r.address, text, r.unit),
                            r.hint.into(),
                            String::new(),
                            "y: Apply write    n / Esc: Cancel".into(),
                            "A timed-out write is never automatically repeated.".into(),
                        ],
                    )
                }
                Dialog::Settings { fields, active } => {
                    let mut lines = vec![
                        "RS232: 115200. RS485: /dev/ttyS2 or auto-direction adapter.".into(),
                        String::new(),
                    ];
                    for (i, name) in ["Port", "Baud", "Slave", "Timeout ms"].iter().enumerate() {
                        lines.push(format!(
                            "{} {name:12} {}{}",
                            if i == *active { "▶" } else { " " },
                            fields[i],
                            if i == *active { "▏" } else { "" }
                        ));
                    }
                    lines.extend([
                        String::new(),
                        "Tab / ↑↓ field · Ctrl-U clear · Enter save · Esc cancel".into(),
                    ]);
                    (" Connection settings ", lines)
                }
                Dialog::Log => (
                    " Recent activity · Esc closes ",
                    self.log.iter().take(10).cloned().collect(),
                ),
            };
            f.render_widget(
                Paragraph::new(lines.join("\n"))
                    .wrap(Wrap { trim: false })
                    .block(
                        Block::default()
                            .borders(Borders::ALL)
                            .title(title)
                            .border_style(Style::default().fg(Color::Cyan)),
                    ),
                rect,
            );
        }
    }
}
fn edit_text(text: &mut String, key: KeyEvent) {
    match key.code {
        KeyCode::Char('u') if key.modifiers.contains(KeyModifiers::CONTROL) => text.clear(),
        KeyCode::Backspace => {
            text.pop();
        }
        KeyCode::Char(c)
            if !key
                .modifiers
                .intersects(KeyModifiers::CONTROL | KeyModifiers::ALT)
                && text.len() < 256 =>
        {
            text.push(c)
        }
        _ => {}
    }
}
fn centered(area: Rect, width: u16, height: u16) -> Rect {
    let w = width.min(area.width);
    let h = height.min(area.height);
    Rect::new(
        area.x + (area.width - w) / 2,
        area.y + (area.height - h) / 2,
        w,
        h,
    )
}
fn restore() {
    let _ = disable_raw_mode();
    let _ = execute!(io::stdout(), LeaveAlternateScreen, Show);
}
struct Restore;
impl Drop for Restore {
    fn drop(&mut self) {
        restore();
    }
}
pub fn run(config: Config, connect: bool) -> io::Result<()> {
    let old_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        restore();
        old_hook(info);
    }));
    enable_raw_mode()?;
    let _restore = Restore;
    execute!(io::stdout(), EnterAlternateScreen)?;
    let mut terminal = Terminal::new(CrosstermBackend::new(io::stdout()))?;
    let mut app = App::new(config);
    if connect {
        app.connect();
    }
    while !app.quit {
        app.receive();
        terminal.draw(|f| app.draw(f))?;
        if event::poll(Duration::from_millis(100))? {
            if let Input::Key(key) = event::read()? {
                app.key(key);
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ratatui::backend::TestBackend;
    fn app() -> App {
        App::new(Config {
            port: "/dev/test".into(),
            baud: 115200,
            unit: 1,
            timeout_ms: 500,
        })
    }
    fn key(c: KeyCode) -> KeyEvent {
        KeyEvent::new(c, KeyModifiers::NONE)
    }
    #[test]
    fn all_tables_dialogs_and_small_terminals_render() {
        let mut app = app();
        let mut terminal = Terminal::new(TestBackend::new(120, 35)).unwrap();
        for tab in 0..4 {
            app.tab = tab;
            terminal.draw(|f| app.draw(f)).unwrap();
        }
        for dialog in [
            Dialog::Edit {
                index: 0,
                text: "1".into(),
            },
            Dialog::Confirm {
                index: 0,
                text: "1".into(),
            },
            Dialog::Settings {
                fields: [
                    "/dev/test".into(),
                    "115200".into(),
                    "1".into(),
                    "500".into(),
                ],
                active: 0,
            },
            Dialog::Log,
        ] {
            app.dialog = Some(dialog);
            terminal.draw(|f| app.draw(f)).unwrap();
        }
        let mut small = Terminal::new(TestBackend::new(40, 10)).unwrap();
        small.draw(|f| app.draw(f)).unwrap();
    }
    #[test]
    fn edit_validation_and_confirmation_never_write_implicitly() {
        let mut app = app();
        app.connected = true;
        app.tab = 1;
        app.key(key(KeyCode::Enter));
        assert!(matches!(app.dialog, Some(Dialog::Edit { .. })));
        for c in "805".chars() {
            app.key(key(KeyCode::Char(c)));
        }
        app.key(key(KeyCode::Enter));
        assert!(matches!(app.dialog, Some(Dialog::Edit { .. })));
        assert!(!app.writing);
        app.key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL));
        for c in "12000".chars() {
            app.key(key(KeyCode::Char(c)));
        }
        app.key(key(KeyCode::Enter));
        assert!(matches!(app.dialog, Some(Dialog::Confirm { .. })));
        assert!(!app.writing);
        app.key(key(KeyCode::Enter));
        assert!(matches!(app.dialog, Some(Dialog::Confirm { .. })));
        app.key(key(KeyCode::Char('n')));
        assert!(app.dialog.is_none());
        assert!(!app.writing);
    }
    #[test]
    fn navigation_settings_readonly_and_quit() {
        let mut app = app();
        app.key(key(KeyCode::End));
        assert!(app.state.selected().unwrap() > 0);
        app.key(key(KeyCode::Tab));
        assert_eq!(app.tab, 1);
        assert_eq!(app.state.selected(), Some(0));
        app.key(key(KeyCode::Char('s')));
        app.key(key(KeyCode::Enter));
        assert!(app.dialog.is_none());
        app.tab = 0;
        app.connected = true;
        app.key(key(KeyCode::Enter));
        assert!(app.dialog.is_none());
        app.key(key(KeyCode::Char('q')));
        assert!(app.quit);
    }
}
