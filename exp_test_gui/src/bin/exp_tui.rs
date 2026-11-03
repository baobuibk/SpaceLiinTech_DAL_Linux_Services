// Image decoding in this shared module is used by the optional desktop frontend.
#[allow(dead_code)]
#[path = "../camera_data.rs"]
mod camera_data;
#[path = "../protocol.rs"]
mod protocol;
#[path = "../serial_io.rs"]
mod serial_io;
#[path = "../simulator.rs"]
mod simulator;
#[path = "../transport.rs"]
mod transport;
#[path = "../tui_catalog.rs"]
mod tui_catalog;
#[path = "../uart_text.rs"]
mod uart_text;

use crossterm::{
    event::{self, Event, KeyCode, KeyEvent, KeyEventKind},
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{
    backend::CrosstermBackend,
    layout::{Constraint, Direction, Layout, Rect},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{Block, Borders, List, ListItem, ListState, Paragraph, Wrap},
    Frame, Terminal,
};
use std::{
    collections::VecDeque,
    env,
    io::{self, IsTerminal},
    path::PathBuf,
    sync::{
        mpsc::{self, Receiver, Sender},
        Arc, Mutex,
    },
    thread,
    time::Duration,
};
use transport::{Link, Notice, Request};
use tui_catalog::{Command, COMMANDS, PAGES, SECTIONS};

const BLUE: Color = Color::Rgb(54, 138, 211);
const YELLOW: Color = Color::Rgb(255, 210, 70);
const GREEN: Color = Color::Rgb(67, 200, 115);

struct Connection {
    port: String,
    baud: u32,
    status: String,
    connected: bool,
    tx: Option<Sender<Request>>,
    rx: Option<Receiver<Notice>>,
    lines: uart_text::Lines,
}
impl Connection {
    fn new(link: Link) -> Self {
        let port = match link {
            Link::Console => env::var("EXP_TEST_GUI_CONSOLE_PORT"),
            Link::Xcli => env::var("EXP_TEST_GUI_XCLI_PORT"),
            Link::Rs485 => env::var("EXP_TEST_GUI_RS485_PORT"),
            Link::Can => env::var("EXP_TEST_GUI_CAN_PORT"),
        }
        .unwrap_or_default();
        Self {
            port,
            baud: if link == Link::Xcli { 230400 } else { 115200 },
            status: "Closed".into(),
            connected: false,
            tx: None,
            rx: None,
            lines: Default::default(),
        }
    }
}

enum Edit {
    Command(String),
    Port(String),
    Baud(String),
    Table(String),
    Confirm(String),
}
impl Edit {
    fn text(&mut self) -> Option<&mut String> {
        match self {
            Self::Command(s) | Self::Port(s) | Self::Baud(s) | Self::Table(s) => Some(s),
            Self::Confirm(_) => None,
        }
    }
    fn label(&self) -> &'static str {
        match self {
            Self::Command(_) => "Command",
            Self::Port(_) => "Serial device",
            Self::Baud(_) => "Baud rate",
            Self::Table(_) => "RPARAM table (0-10)",
            Self::Confirm(_) => "Confirm",
        }
    }
    fn value(&self) -> &str {
        match self {
            Self::Command(s)
            | Self::Port(s)
            | Self::Baud(s)
            | Self::Table(s)
            | Self::Confirm(s) => s,
        }
    }
}

struct App {
    links: [Connection; 4],
    active: Link,
    page: usize,
    selected: [usize; 20],
    logs: VecDeque<String>,
    edit: Option<Edit>,
    latest_meta: Option<camera_data::Metadata>,
    simulated: bool,
    sim_state: Arc<Mutex<simulator::State>>,
    quit: bool,
}
impl App {
    fn new() -> Self {
        Self {
            links: std::array::from_fn(|i| Connection::new(Link::ALL[i])),
            active: Link::Console,
            page: 0,
            selected: [0; 20],
            logs: VecDeque::new(),
            edit: None,
            latest_meta: None,
            simulated: false,
            sim_state: Arc::new(Mutex::new(simulator::State::default())),
            quit: false,
        }
    }
    fn log(&mut self, message: impl Into<String>) {
        for line in message.into().lines() {
            self.logs.push_back(line.to_owned());
        }
        while self.logs.len() > 500 {
            self.logs.pop_front();
        }
    }
    fn commands(&self) -> Vec<&'static Command> {
        COMMANDS
            .iter()
            .filter(|c| c.page == PAGES[self.page].title)
            .collect()
    }
    fn selected_command(&self) -> Option<&'static Command> {
        self.commands().get(self.selected[self.page]).copied()
    }
    fn change_section(&mut self, step: isize) {
        let current = SECTIONS.iter().position(|s| *s == PAGES[self.page].section).unwrap_or(0);
        let next = (current as isize + step).rem_euclid(SECTIONS.len() as isize) as usize;
        if let Some(page) = PAGES.iter().position(|p| p.section == SECTIONS[next]) {
            self.page = page;
        }
    }
    fn open(&mut self) {
        let link = self.active;
        let connection = &mut self.links[link.index()];
        if !self.simulated && connection.port.trim().is_empty() {
            self.edit = Some(Edit::Port(String::new()));
            return;
        }
        if let Some(tx) = connection.tx.take() {
            let _ = tx.send(Request::Stop);
        }
        let (tx, requests) = mpsc::channel();
        let (notices, rx) = mpsc::channel();
        let port = connection.port.clone();
        let baud = connection.baud;
        if self.simulated {
            let state = Arc::clone(&self.sim_state);
            thread::spawn(move || simulator::worker(link, state, requests, notices));
        } else {
            thread::spawn(move || transport::worker(link, port, baud, requests, notices));
        }
        connection.tx = Some(tx);
        connection.rx = Some(rx);
        connection.connected = false;
        connection.status = "Opening".into();
    }
    fn close(&mut self) {
        let link = self.active;
        let connection = &mut self.links[link.index()];
        if let Some(tx) = connection.tx.take() {
            let _ = tx.send(Request::Stop);
        }
        connection.rx = None;
        connection.connected = false;
        connection.status = "Closed".into();
        self.log(format!("{} closed", link.short()));
    }
    fn send(&mut self, command: String) {
        let link = self.active;
        if command.trim().is_empty() {
            return;
        }
        let connection = &self.links[link.index()];
        if !connection.connected {
            self.log(format!(
                "{}: open a serial device first (p, then o)",
                link.short()
            ));
            return;
        }
        if let Some(tx) = &connection.tx {
            if tx.send(Request::Command(command.clone())).is_ok() {
                self.log(format!("QUEUE {} > {command}", link.short()));
            } else {
                self.log(format!("{}: serial worker stopped", link.short()));
            }
        }
    }
    fn table(&mut self, table: u8) {
        if table > 10 {
            self.log("RPARAM table must be 0-10");
            return;
        }
        if matches!(self.active, Link::Console | Link::Xcli) {
            self.send(format!("param_show {table}"));
        } else if let Some(tx) = &self.links[self.active.index()].tx {
            if self.links[self.active.index()].connected {
                let _ = tx.send(Request::Table(table));
                self.log(format!(
                    "QUEUE {} RPARAM table {table}",
                    self.active.short()
                ));
            } else {
                self.log("Open the link first");
            }
        } else {
            self.log("Open the link first");
        }
    }
    fn handle_notice(&mut self, link: Link, notice: Notice) {
        match notice {
            Notice::Line(s) => self.log(format!("{}: {s}", link.short())),
            Notice::Text(s) => {
                for line in self.links[link.index()].lines.feed(&s) {
                    if let Some(meta) = camera_data::Metadata::parse(&line) {
                        self.latest_meta = Some(meta);
                    }
                    self.log(format!("{} < {line}", link.short()));
                }
            }
            Notice::Connected(connected) => {
                self.links[link.index()].connected = connected;
                if !connected {
                    self.links[link.index()].tx = None;
                }
            }
            Notice::Status(s) => {
                self.links[link.index()].status = s.clone();
                self.log(format!("{}: {s}", link.short()));
            }
            Notice::Downloaded(meta, bytes, path) => {
                self.log(format!(
                    "{}: saved object {} ({} bytes) to {}",
                    link.short(),
                    meta.object,
                    bytes.len(),
                    path.display()
                ));
            }
            Notice::DownloadFailed(s) => {
                self.log(format!("{}: download failed: {s}", link.short()))
            }
        }
    }
    fn tick(&mut self) {
        for link in Link::ALL {
            let notices = self.links[link.index()]
                .rx
                .as_ref()
                .map(|rx| rx.try_iter().collect::<Vec<_>>())
                .unwrap_or_default();
            for notice in notices {
                self.handle_notice(link, notice);
            }
            if let Some(line) = self.links[link.index()].lines.idle() {
                if let Some(meta) = camera_data::Metadata::parse(&line) {
                    self.latest_meta = Some(meta);
                }
                self.log(format!("{} < {line}", link.short()));
            }
        }
    }
    fn download(&mut self) {
        if self.simulated {
            self.log("SIMULATED mode does not download or create camera files");
            return;
        }
        if self.active != Link::Console {
            self.log("XMODEM image download requires Console / UART2");
            return;
        }
        let Some(meta) = self.latest_meta.clone() else {
            self.log("Read camera object metadata first; no DATA record has been received");
            return;
        };
        let folder = env::var_os("EXP_TEST_GUI_DOWNLOAD_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("camera_downloads"));
        if let Some(tx) = &self.links[0].tx {
            if self.links[0].connected {
                let _ = tx.send(Request::Download(meta, folder));
                self.log("Starting verified UART2 XMODEM download");
            } else {
                self.log("Open Console / UART2 first");
            }
        } else {
            self.log("Open Console / UART2 first");
        }
    }
    fn activate(&mut self) {
        if let Some(command) = self.selected_command() {
            if command.text.ends_with(' ') {
                self.edit = Some(Edit::Command(command.text.into()));
            } else if command.text == "burn_fire" {
                self.edit = Some(Edit::Confirm(command.text.into()));
            } else {
                self.send(command.text.into());
            }
        }
    }
    fn finish_edit(&mut self, edit: Edit) {
        match edit {
            Edit::Command(command) => {
                let command = command.trim().to_owned();
                if command.is_empty() {
                    return;
                }
                if command == "burn_fire" {
                    self.edit = Some(Edit::Confirm(command));
                } else {
                    self.send(command);
                }
            }
            Edit::Port(port) => {
                if self.links[self.active.index()].connected {
                    self.close();
                }
                self.links[self.active.index()].port = port.trim().into();
            }
            Edit::Baud(value) => match value.parse::<u32>() {
                Ok(baud) if baud > 0 => {
                    if self.links[self.active.index()].connected {
                        self.close();
                    }
                    self.links[self.active.index()].baud = baud;
                }
                _ => self.log("Invalid baud rate"),
            },
            Edit::Table(value) => match value.trim().parse::<u8>() {
                Ok(table) => self.table(table),
                Err(_) => self.log("RPARAM table must be 0-10"),
            },
            Edit::Confirm(command) => self.send(command),
        }
    }
    fn key(&mut self, key: KeyEvent) {
        if !matches!(key.kind, KeyEventKind::Press | KeyEventKind::Repeat) {
            return;
        }
        if let Some(mut edit) = self.edit.take() {
            match (&edit, key.code) {
                (_, KeyCode::Esc) => return,
                (Edit::Confirm(_), KeyCode::Char('y' | 'Y')) => self.finish_edit(edit),
                (Edit::Confirm(_), _) => return,
                (_, KeyCode::Enter) => self.finish_edit(edit),
                (_, KeyCode::Backspace) => {
                    if let Some(text) = edit.text() {
                        text.pop();
                    }
                    self.edit = Some(edit);
                }
                (_, KeyCode::Char(c)) => {
                    if let Some(text) = edit.text() {
                        text.push(c);
                    }
                    self.edit = Some(edit);
                }
                _ => self.edit = Some(edit),
            }
            return;
        }
        match key.code {
            KeyCode::Char('q') | KeyCode::Esc => self.quit = true,
            KeyCode::Char('1'..='4') => {
                let index = match key.code {
                    KeyCode::Char(c) => c as usize - '1' as usize,
                    _ => 0,
                };
                self.active = Link::ALL[index];
            }
            KeyCode::Tab | KeyCode::Right => self.page = (self.page + 1) % PAGES.len(),
            KeyCode::BackTab | KeyCode::Left => self.page = (self.page + PAGES.len() - 1) % PAGES.len(),
            KeyCode::Char(']') => self.change_section(1),
            KeyCode::Char('[') => self.change_section(-1),
            KeyCode::Up => {
                self.selected[self.page] = self.selected[self.page].saturating_sub(1)
            }
            KeyCode::Down => {
                self.selected[self.page] =
                    (self.selected[self.page] + 1).min(self.commands().len().saturating_sub(1));
            }
            KeyCode::Enter => self.activate(),
            KeyCode::Char('/') => self.edit = Some(Edit::Command(String::new())),
            KeyCode::Char('e') => {
                if let Some(command) = self.selected_command() {
                    self.edit = Some(Edit::Command(command.text.into()));
                }
            }
            KeyCode::Char('p') => {
                if self.simulated {
                    self.log("SIMULATED mode does not open serial devices");
                } else {
                    self.edit = Some(Edit::Port(self.links[self.active.index()].port.clone()));
                }
            }
            KeyCode::Char('b') => {
                self.edit = Some(Edit::Baud(self.links[self.active.index()].baud.to_string()))
            }
            KeyCode::Char('o') => {
                if self.links[self.active.index()].connected {
                    self.close();
                } else {
                    self.open();
                }
            }
            KeyCode::Char('t') => self.edit = Some(Edit::Table(String::new())),
            KeyCode::Char('d') => self.download(),
            KeyCode::Char('x') => self.logs.clear(),
            _ => {}
        }
    }
}

fn draw(frame: &mut Frame, app: &App) {
    if app.page == 0 {
        draw_home(frame, app);
        return;
    }
    let area = frame.area();
    let rows = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1),
            Constraint::Length(2),
            Constraint::Length(6),
            Constraint::Min(8),
            Constraint::Length(3),
            Constraint::Length(1),
        ])
        .split(area);
    let title = Line::from(vec![
        Span::styled(
            " EXP CONTROL ",
            Style::default().fg(YELLOW).add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            if app.simulated {
                " SIMULATED / NO HARDWARE  |  "
            } else {
                " direct UART / RS485 / SLCAN  |  "
            },
            Style::default().fg(if app.simulated {
                Color::LightRed
            } else {
                Color::White
            }),
        ),
        Span::styled(PAGES[app.page].title, Style::default().fg(BLUE)),
    ]);
    frame.render_widget(Paragraph::new(title), rows[0]);
    let current_section = PAGES[app.page].section;
    let tabs = Line::from(SECTIONS.iter().map(|section| {
        Span::styled(
            format!(" [{section}] "),
            Style::default()
                .fg(if *section == current_section { Color::Black } else { BLUE })
                .bg(if *section == current_section { YELLOW } else { Color::Reset }),
        )
    }).collect::<Vec<_>>());
    frame.render_widget(Paragraph::new(tabs), rows[1]);
    let links: Vec<Line> = Link::ALL
        .iter()
        .map(|link| {
            let state = &app.links[link.index()];
            let marker = if state.connected { "OPEN" } else { "OFF" };
            let color = if state.connected { GREEN } else { Color::Gray };
            Line::from(vec![
                Span::styled(
                    format!("{} {} ", link.index() + 1, link.label()),
                    Style::default().fg(if *link == app.active { YELLOW } else { BLUE }),
                ),
                Span::styled(format!("[{marker}] "), Style::default().fg(color)),
                Span::raw(format!(
                    "{} @ {}  {}",
                    if state.port.is_empty() {
                        "<set port>"
                    } else {
                        &state.port
                    },
                    state.baud,
                    state.status
                )),
            ])
        })
        .collect();
    frame.render_widget(
        Paragraph::new(links).block(
            Block::default()
                .title(" Serial links ")
                .borders(Borders::ALL),
        ),
        rows[2],
    );
    let columns = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Percentage(22), Constraint::Percentage(38), Constraint::Percentage(40)])
        .split(rows[3]);
    let visible_pages: Vec<_> = PAGES.iter().enumerate().filter(|(_, page)| page.section == current_section).collect();
    let page_items: Vec<ListItem> = visible_pages.iter().map(|(_, page)| ListItem::new(page.title)).collect();
    let mut page_state = ListState::default();
    page_state.select(visible_pages.iter().position(|(index, _)| *index == app.page));
    frame.render_stateful_widget(
        List::new(page_items)
            .block(Block::default().title(" Panels ").borders(Borders::ALL))
            .highlight_style(Style::default().fg(Color::Black).bg(YELLOW)),
        columns[0],
        &mut page_state,
    );
    let center = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Min(5), Constraint::Length(3)])
        .split(columns[1]);
    let commands = app.commands();
    let items: Vec<ListItem> = commands
        .iter()
        .map(|command| ListItem::new(format!("[ {} ]", command.label)))
        .collect();
    let mut state = ListState::default();
    if !items.is_empty() {
        state.select(Some(app.selected[app.page].min(items.len() - 1)));
    }
    frame.render_stateful_widget(
        List::new(items)
            .block(
                Block::default()
                    .title(" Buttons  ↑/↓ select  Enter press ")
                    .borders(Borders::ALL),
            )
            .highlight_style(Style::default().fg(Color::Black).bg(YELLOW)),
        center[0],
        &mut state,
    );
    frame.render_widget(
        Paragraph::new(app.selected_command().map(|c| c.text).unwrap_or("No command"))
            .block(Block::default().title(" Selected command ").borders(Borders::ALL)),
        center[1],
    );
    let lines: Vec<Line> = app
        .logs
        .iter()
        .rev()
        .take(columns[2].height.saturating_sub(2) as usize)
        .rev()
        .map(|line| Line::raw(line.clone()))
        .collect();
    frame.render_widget(
        Paragraph::new(lines)
            .wrap(Wrap { trim: false })
            .block(Block::default().title(" Responses ").borders(Borders::ALL)),
        columns[2],
    );
    let input = if let Some(edit) = &app.edit {
        match edit {
            Edit::Confirm(command) => {
                format!("Send {command}? Press y to confirm, any other key to cancel")
            }
            _ => format!("{}: {}_", edit.label(), edit.value()),
        }
    } else {
        "Tab/←/→ panel  [ or ] section  ↑/↓ button  Enter press  / command  p port  o open  q quit".into()
    };
    frame.render_widget(
        Paragraph::new(input).block(Block::default().title(" Control ").borders(Borders::ALL)),
        rows[4],
    );
    frame.render_widget(
        Paragraph::new(if app.simulated {
            "SIMULATED responses only. No serial device is opened and no camera file is written."
        } else {
            "Direct EXP transport. UART2=115200, XCLI=230400, RS485=115200; select link 1-4."
        }),
        rows[5],
    );
}

fn draw_home(frame: &mut Frame, app: &App) {
    let rows = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(2),
            Constraint::Length(2),
            Constraint::Length(2),
            Constraint::Min(8),
        ])
        .split(frame.area());
    let header = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Percentage(60), Constraint::Percentage(40)])
        .split(rows[0]);
    let header_style = Style::default().fg(Color::White).bg(BLUE);
    frame.render_widget(
        Paragraph::new("EXP  /  TEST GUI").style(header_style),
        header[0],
    );
    frame.render_widget(
        Paragraph::new(format!(
            "{} / 4 LINKS OPEN  Q: QUIT",
            app.links.iter().filter(|link| link.connected).count()
        ))
        .alignment(ratatui::layout::Alignment::Right)
        .style(header_style),
        header[1],
    );

    let tabs = Line::from(
        SECTIONS
            .iter()
            .map(|section| {
                Span::styled(
                    format!(" {section} "),
                    if *section == "HOME" {
                        Style::default().fg(Color::White).bg(BLUE)
                    } else {
                        Style::default().fg(Color::Gray)
                    },
                )
            })
            .collect::<Vec<_>>(),
    );
    frame.render_widget(Paragraph::new(tabs), rows[1]);

    let mut status = vec![Span::styled(
        " PORT SETTINGS ",
        Style::default().fg(Color::White).bg(BLUE),
    )];
    for link in Link::ALL {
        let connection = &app.links[link.index()];
        let state = if connection.connected { "OPEN" } else { "CLOSED" };
        let label = if connection.connected && !connection.port.is_empty() {
            format!("  {} {} {state}  │", link.short(), connection.port)
        } else {
            format!("  {} {state}  │", link.short())
        };
        status.push(Span::styled(
            label,
            Style::default().fg(if connection.connected { GREEN } else { Color::Gray }),
        ));
    }
    frame.render_widget(Paragraph::new(Line::from(status)), rows[2]);

    let columns = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage(14),
            Constraint::Percentage(51),
            Constraint::Percentage(35),
        ])
        .split(rows[3]);
    let yellow_border = Style::default().fg(YELLOW);
    let panel = |title| Block::default().title(title).borders(Borders::ALL).border_style(yellow_border);

    frame.render_widget(panel(" PERIPHERALS "), columns[0]);
    let left_inner = panel(" ").inner(columns[0]);
    if left_inner.height >= 5 {
        frame.render_widget(
            Paragraph::new(" Overview ").style(Style::default().fg(Color::Black).bg(YELLOW)),
            Rect::new(left_inner.x, left_inner.y + 2, left_inner.width, 1),
        );
        frame.render_widget(
            Paragraph::new(" RPARAM tables ").style(Style::default().fg(Color::White)),
            Rect::new(left_inner.x, left_inner.y + 4, left_inner.width, 1),
        );
    }

    frame.render_widget(panel(" Overview "), columns[1]);
    let center = panel(" ").inner(columns[1]);
    if center.height >= 9 {
        let route = format!(
            "Peripheral commands via  [{} ▾]  {}",
            app.active.label(),
            if app.links[app.active.index()].connected { "OPEN" } else { "CLOSED" }
        );
        frame.render_widget(
            Paragraph::new(route).style(Style::default().fg(Color::White)),
            Rect::new(center.x, center.y + 1, center.width, 1),
        );
        frame.render_widget(
            Paragraph::new("Select a peripheral on the left. Responses appear in the log on the right."),
            Rect::new(center.x, center.y + 4, center.width, 2),
        );
        let checks = Rect::new(center.x, center.y + 7, center.width, 5);
        if checks.bottom() <= center.bottom() {
            frame.render_widget(
                Block::default().borders(Borders::ALL).border_style(yellow_border),
                checks,
            );
            frame.render_widget(
                Paragraph::new("System checks").style(Style::default().fg(YELLOW)),
                Rect::new(checks.x + 2, checks.y + 1, checks.width.saturating_sub(4), 1),
            );
            let buttons = Line::from(
                app.commands()
                    .iter()
                    .enumerate()
                    .flat_map(|(index, command)| {
                        let selected = index == app.selected[0];
                        [
                            Span::styled(
                                format!("[{}]", command.label),
                                if selected {
                                    Style::default().fg(Color::Black).bg(YELLOW)
                                } else {
                                    Style::default().fg(Color::White)
                                },
                            ),
                            Span::raw(" "),
                        ]
                    })
                    .collect::<Vec<_>>(),
            );
            frame.render_widget(
                Paragraph::new(buttons),
                Rect::new(checks.x + 2, checks.y + 3, checks.width.saturating_sub(4), 1),
            );
        }
    }

    frame.render_widget(panel(" CONSOLE / TX + RX "), columns[2]);
    let console = panel(" ").inner(columns[2]);
    if console.height >= 7 {
        frame.render_widget(
            Paragraph::new("QUEUE = requested | TX = written to link | RX = received"),
            Rect::new(console.x + 1, console.y + 1, console.width.saturating_sub(2), 1),
        );
        let input = if let Some(edit) = &app.edit {
            match edit {
                Edit::Confirm(command) => format!("Confirm {command}? y / Esc"),
                _ => format!("{}: {}_", edit.label(), edit.value()),
            }
        } else {
            "[/ Enter command] [Send: Enter]".into()
        };
        frame.render_widget(
            Paragraph::new(format!("{}  {input}", app.active.short())).style(Style::default().fg(Color::Gray)),
            Rect::new(console.x + 1, console.y + 3, console.width.saturating_sub(2), 1),
        );
        frame.render_widget(
            Paragraph::new("[Clear log: x]"),
            Rect::new(console.x + 1, console.y + 5, console.width.saturating_sub(2), 1),
        );
        let log_height = console.height.saturating_sub(8);
        let lines: Vec<Line> = app.logs.iter().rev().take(log_height as usize).rev()
            .map(|line| Line::raw(line.clone())).collect();
        frame.render_widget(
            Paragraph::new(lines).wrap(Wrap { trim: false }),
            Rect::new(console.x + 1, console.y + 8, console.width.saturating_sub(2), log_height),
        );
    }
}

struct TerminalGuard;
impl TerminalGuard {
    fn enter() -> io::Result<Self> {
        enable_raw_mode()?;
        if let Err(error) = execute!(io::stdout(), EnterAlternateScreen) {
            let _ = disable_raw_mode();
            return Err(error);
        }
        Ok(Self)
    }
}
impl Drop for TerminalGuard {
    fn drop(&mut self) {
        let _ = execute!(io::stdout(), LeaveAlternateScreen);
        let _ = disable_raw_mode();
    }
}

fn usage() {
    println!(
        "Usage: exp_test_gui [--demo] [--port DEVICE] [--link console|xcli|rs485|can] [--baud RATE]\n\
        exp_test_gui --list-commands\n\
        Keys: 1-4 link, Tab/Left/Right panel, [ or ] section, Up/Down button, Enter send, / type,\n\
        e edit, p port, b baud, o open/close, t RPARAM table, d camera download, q quit.\n\
        Environment: EXP_TEST_GUI_CONSOLE_PORT, EXP_TEST_GUI_XCLI_PORT, EXP_TEST_GUI_RS485_PORT,\n\
        EXP_TEST_GUI_CAN_PORT, EXP_TEST_GUI_DOWNLOAD_DIR."
    );
}
fn configure(app: &mut App, args: &[String]) -> Result<bool, String> {
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--help" | "-h" => {
                usage();
                return Ok(false);
            }
            "--list-commands" => {
                for command in COMMANDS {
                    println!("{}\t{}\t{}", command.page, command.label, command.text);
                }
                return Ok(false);
            }
            "--demo" => {
                app.simulated = true;
                i += 1;
            }
            "--link" | "--port" | "--baud" => {
                let value = args
                    .get(i + 1)
                    .ok_or_else(|| format!("{} needs a value", args[i]))?;
                match args[i].as_str() {
                    "--link" => {
                        app.active = match value.as_str() {
                            "console" | "uart2" => Link::Console,
                            "xcli" | "uart0" => Link::Xcli,
                            "rs485" | "uart4" => Link::Rs485,
                            "can" | "slcan" => Link::Can,
                            _ => return Err(format!("Unknown link: {value}")),
                        }
                    }
                    "--port" => app.links[app.active.index()].port = value.clone(),
                    "--baud" => {
                        app.links[app.active.index()].baud = value
                            .parse::<u32>()
                            .ok()
                            .filter(|v| *v > 0)
                            .ok_or("Invalid baud rate")?
                    }
                    _ => unreachable!(),
                }
                i += 2;
            }
            other => return Err(format!("Unknown argument: {other}")),
        }
    }
    Ok(true)
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut app = App::new();
    let args: Vec<String> = env::args().skip(1).collect();
    if !configure(&mut app, &args)? {
        return Ok(());
    }
    if !io::stdin().is_terminal() || !io::stdout().is_terminal() {
        return Err(
            "exp_test_gui needs an interactive terminal; use --list-commands for text output".into(),
        );
    }
    if app.simulated {
        for link in Link::ALL {
            app.active = link;
            app.links[link.index()].port = "SIMULATED".into();
            app.open();
        }
        app.active = Link::Console;
        app.log("SIMULATED EXP. Use the command pages to try the UI; no hardware is accessed.");
    }
    let _guard = TerminalGuard::enter()?;
    let mut terminal = Terminal::new(CrosstermBackend::new(io::stdout()))?;
    while !app.quit {
        app.tick();
        terminal.draw(|frame| draw(frame, &app))?;
        if event::poll(Duration::from_millis(100))? {
            if let Event::Key(key) = event::read()? {
                app.key(key);
            }
        }
    }
    for link in Link::ALL {
        if let Some(tx) = app.links[link.index()].tx.take() {
            let _ = tx.send(Request::Stop);
        }
    }
    Ok(())
}

#[cfg(test)]
mod panel_tests {
    use super::*;
    use ratatui::backend::TestBackend;

    #[test]
    fn every_desktop_panel_has_buttons_and_tab_visits_all_twenty() {
        let mut app = App::new();
        assert_eq!(PAGES.len(), 20);
        for expected in 0..PAGES.len() {
            assert_eq!(app.page, expected);
            assert!(!app.commands().is_empty(), "{} has no buttons", PAGES[expected].title);
            app.key(KeyEvent::new(KeyCode::Tab, crossterm::event::KeyModifiers::NONE));
        }
        assert_eq!(app.page, 0);
        app.key(KeyEvent::new(KeyCode::Char(']'), crossterm::event::KeyModifiers::NONE));
        assert_eq!(PAGES[app.page].section, "POWER");
    }

    #[test]
    fn home_matches_reference_sections_and_commands() {
        let mut terminal = Terminal::new(TestBackend::new(160, 45)).unwrap();
        let app = App::new();
        terminal.draw(|frame| draw(frame, &app)).unwrap();
        let buffer = terminal.backend().buffer();
        let screen = (0..45)
            .map(|y| (0..160).map(|x| buffer[(x, y)].symbol()).collect::<String>())
            .collect::<Vec<_>>()
            .join("\n");
        for label in [
            "EXP  /  TEST GUI",
            "PORT SETTINGS",
            "PERIPHERALS",
            "Overview",
            "RPARAM tables",
            "System checks",
            "[Help]",
            "[Power]",
            "[Sensors]",
            "[RS485]",
            "[PC]",
            "CONSOLE / TX + RX",
            "QUEUE = requested",
            "Clear log",
        ] {
            assert!(screen.contains(label), "missing {label}");
        }
        assert_eq!(
            app.commands().iter().map(|command| command.text).collect::<Vec<_>>(),
            ["help", "power_all_get", "sensor_status", "rs485_status", "pc_status"]
        );
    }
}
