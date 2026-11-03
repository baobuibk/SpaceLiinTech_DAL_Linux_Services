//! EXP monitoring and control panel: the EXP test GUI pages (catalog), a status
//! auto-refresh, and a transport that sends firmware commands through
//! `dal_flight_console exp_run` (or the built-in simulator in host-sim mode).
use super::catalog::{self, Group, Page, PAGES, SECTIONS, W};
use super::sim::{self, Sim};
use super::theme::{self, *};
use crate::{command_spec, run_command, Action, Finished};
use crossterm::event::{KeyCode, KeyEvent};
use ratatui::layout::{Constraint, Layout, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::Frame;
use std::collections::{HashMap, VecDeque};
use std::sync::mpsc::{self, Receiver};
use std::thread;
use std::time::{Duration, Instant};

type App = ExpPanel;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Zone {
    Section,
    Periph,
    Page,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Edit {
    None,
    Value,
}

const POLL_SIM: Duration = Duration::from_secs(2);
const POLL_LIVE: Duration = Duration::from_secs(4);

struct Inflight {
    rx: Receiver<Finished>,
    quiet: bool,
    cmd: String,
}

pub struct ExpPanel {
    pub section: usize,
    pub page: usize,
    pub zone: Zone,
    pub cursor: usize,
    pub edit: Edit,
    pub values: Vec<Vec<String>>,
    pub reads: HashMap<&'static str, String>,
    needles: Vec<&'static str>,
    pub image: Option<Vec<f32>>,
    pub auto: bool,
    pub confirm: Option<String>,
    pub sim: Option<Sim>,
    /// Lines for the application's command-result log.
    pub out: Vec<String>,
    last_poll: Option<Instant>,
    queue: VecDeque<(String, bool)>,
    inflight: Option<Inflight>,
    updated: Option<Instant>,
}

impl ExpPanel {
    pub fn new(simulated: bool) -> Self {
        Self {
            section: 0,
            page: 0,
            zone: Zone::Section,
            cursor: 0,
            edit: Edit::None,
            values: PAGES.iter().map(catalog::defaults).collect(),
            reads: HashMap::new(),
            needles: catalog::all_needles(),
            image: None,
            auto: true,
            confirm: None,
            sim: simulated.then(Sim::new),
            out: Vec::new(),
            last_poll: None,
            queue: VecDeque::new(),
            inflight: None,
            updated: None,
        }
    }

    pub fn page_def(&self) -> &'static Page {
        &PAGES[self.page]
    }

    pub fn is_rparam(&self) -> bool {
        self.page_def().name == "RPARAM tables"
    }

    fn item_count(&self) -> usize {
        self.values[self.page].len()
    }

    fn select_section(&mut self, s: usize) {
        self.section = s % SECTIONS.len();
        self.page = catalog::pages_in(self.section)[0];
        self.cursor = 0;
        self.last_poll = None;
    }

    fn select_page(&mut self, p: usize) {
        self.page = p;
        self.cursor = 0;
        self.last_poll = None;
    }

    /// True while a text field is being typed into (global keys must not fire).
    pub fn capturing(&self) -> bool {
        self.edit != Edit::None || self.confirm.is_some()
    }

    pub fn refresh_now(&mut self) {
        self.last_poll = None;
    }

    pub fn footer(&self) -> String {
        if let Some(c) = &self.confirm {
            return format!("CONFIRM `{c}`: press Enter to send, any other key cancels");
        }
        let auto = if self.auto { "ON" } else { "OFF" };
        match (self.edit, self.zone) {
            (Edit::Value, _) => "EDIT FIELD   type value | Enter/Esc done".into(),
            (_, Zone::Section) => format!("EXP   ←/→ section | ↓/Enter pages | a auto-refresh {auto} | / command | Tab/1-8 other pages | q quit"),
            (_, Zone::Periph) => format!("EXP PERIPHERALS   ↑/↓ choose | →/Enter controls | ← sections | a auto {auto} | / command | q quit"),
            (_, Zone::Page) => format!("EXP PAGE   ↑/↓ select | Enter press/edit | Space change | ← peripherals | a auto {auto} | / command"),
        }
    }

    pub fn status_line(&self) -> String {
        let age = match self.updated {
            Some(t) => format!("updated {}s ago", t.elapsed().as_secs()),
            None => "no data yet".into(),
        };
        let busy = if self.inflight.is_some() || !self.queue.is_empty() { "  ..." } else { "" };
        format!("AUTO {}  {age}{busy}", if self.auto { "ON" } else { "OFF" })
    }

    /// Run background work: collect a reply, queue periodic reads, start the next command.
    pub fn tick(&mut self, active: bool) {
        if let Some(inf) = &self.inflight {
            if let Ok(done) = inf.rx.try_recv() {
                let (quiet, cmd) = (inf.quiet, inf.cmd.clone());
                self.inflight = None;
                self.finish(&cmd, quiet, &done.output);
            }
        }
        if active && self.auto && self.edit == Edit::None {
            let every = if self.sim.is_some() { POLL_SIM } else { POLL_LIVE };
            let due = self.last_poll.map_or(true, |t| t.elapsed() >= every);
            if due && self.queue.is_empty() && self.inflight.is_none() {
                self.last_poll = Some(Instant::now());
                for c in self.page_def().poll {
                    self.queue.push_back((c.to_string(), true));
                }
            }
        }
        if self.inflight.is_none() {
            if let Some((cmd, quiet)) = self.queue.pop_front() {
                self.start(cmd, quiet);
            }
        }
    }

    fn start(&mut self, cmd: String, quiet: bool) {
        if let Some(sim) = &mut self.sim {
            let replies = sim.reply(&cmd);
            if let Some(f) = sim.frame.clone() {
                self.image = Some(f);
            }
            if !quiet {
                self.out.push(format!("EXP > {cmd}  (simulated)"));
            }
            for r in replies {
                self.ingest(&r);
                if !quiet {
                    self.out.push(format!("EXP < {r}"));
                }
            }
            self.updated = Some(Instant::now());
            return;
        }
        match command_spec(Action::ExpRun, Some(cmd.clone())) {
            Ok(spec) => {
                if !quiet {
                    self.out.push(format!("EXP > {cmd}"));
                }
                let (tx, rx) = mpsc::channel();
                thread::spawn(move || {
                    let _ = tx.send(run_command(spec));
                });
                self.inflight = Some(Inflight { rx, quiet, cmd });
            }
            Err(e) => self.out.push(e),
        }
    }

    fn finish(&mut self, cmd: &str, quiet: bool, output: &str) {
        match output.strip_prefix("OK: ") {
            Some(text) => {
                for line in text.lines() {
                    self.ingest(line);
                }
                if !quiet {
                    self.out.extend(text.lines().map(|l| format!("EXP < {l}")));
                }
                self.updated = Some(Instant::now());
            }
            None => {
                self.out.push(format!("EXP {cmd}: {output}"));
                if quiet {
                    self.auto = false;
                    self.queue.clear();
                    self.out.push("EXP auto-refresh stopped after an error (press a to resume)".into());
                }
            }
        }
    }

    fn ingest(&mut self, line: &str) {
        for n in &self.needles {
            if let Some(pos) = line.find(n) {
                self.reads.insert(n, line[pos + n.len()..].trim().to_string());
            }
        }
    }

    /// Send a user command (front of the queue). `burn_fire` needs a second Enter.
    pub fn send_user(&mut self, cmd: &str) {
        let cmd = cmd.trim();
        if cmd.is_empty() {
            return;
        }
        if cmd == "burn_fire" && self.confirm.as_deref() != Some("burn_fire") {
            self.confirm = Some(cmd.into());
            return;
        }
        self.queue.push_front((cmd.to_string(), false));
    }

    fn press_item(&mut self) {
        let page = self.page_def();
        let mut i = 0;
        let mut cmd: Option<String> = None;
        'find: for g in page.groups {
            for w in g.widgets {
                match w {
                    W::Btn(_, c) => {
                        if i == self.cursor {
                            cmd = Some(catalog::expand(page, &self.values[self.page], c));
                            break 'find;
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
                        for (_, c) in btns.iter() {
                            if i == self.cursor {
                                cmd = Some(catalog::expand(page, &self.values[self.page], c));
                                break 'find;
                            }
                            i += 1;
                        }
                    }
                }
            }
        }
        if let Some(c) = cmd {
            self.send_user(&c);
        }
    }

    /// Handle a key while the EXP page is shown; true when consumed.
    pub fn key(&mut self, key: KeyEvent) -> bool {
        if let Some(c) = self.confirm.take() {
            if key.code == KeyCode::Enter {
                self.queue.push_front((c, false));
            }
            return true;
        }
        if self.edit == Edit::Value {
            let buf = &mut self.values[self.page][self.cursor];
            match key.code {
                KeyCode::Enter | KeyCode::Esc => self.edit = Edit::None,
                KeyCode::Backspace => {
                    buf.pop();
                }
                KeyCode::Char(c) => buf.push(c),
                _ => {}
            }
            return true;
        }
        match (self.zone, key.code) {
            (_, KeyCode::Char('a')) => {
                self.auto = !self.auto;
                self.last_poll = None;
            }
            (Zone::Section, KeyCode::Left) => self.select_section(self.section + SECTIONS.len() - 1),
            (Zone::Section, KeyCode::Right) => self.select_section(self.section + 1),
            (Zone::Section, KeyCode::Down | KeyCode::Enter) => self.zone = Zone::Periph,
            (Zone::Periph, KeyCode::Up | KeyCode::Down) => {
                let pages = catalog::pages_in(self.section);
                let pos = pages.iter().position(|&p| p == self.page).unwrap_or(0);
                let n = pages.len();
                let np = if key.code == KeyCode::Down { (pos + 1) % n } else { (pos + n - 1) % n };
                self.select_page(pages[np]);
            }
            (Zone::Periph, KeyCode::Right | KeyCode::Enter) if self.item_count() > 0 => self.zone = Zone::Page,
            (Zone::Periph, KeyCode::Left | KeyCode::Esc) => self.zone = Zone::Section,
            (Zone::Page, KeyCode::Up) if self.item_count() > 0 => {
                self.cursor = (self.cursor + self.item_count() - 1) % self.item_count()
            }
            (Zone::Page, KeyCode::Down) if self.item_count() > 0 => self.cursor = (self.cursor + 1) % self.item_count(),
            (Zone::Page, KeyCode::Enter | KeyCode::Char(' ')) if self.item_count() > 0 => self.press_item(),
            (Zone::Page, KeyCode::Left | KeyCode::Esc) => self.zone = Zone::Periph,
            _ => return false,
        }
        true
    }

    /// Draw the section bar, peripheral list and page into `area`.
    pub fn draw(&self, f: &mut Frame, area: Rect, extra: &[String]) {
        f.render_widget(Block::default().style(theme::base()), area);
        let rows = Layout::vertical([Constraint::Length(1), Constraint::Min(4)]).split(area);
        let mut tabs = vec![Span::raw(" ")];
        for (i, s) in SECTIONS.iter().enumerate() {
            let st = if i == self.section {
                if self.zone == Zone::Section { theme::selected() } else { theme::tab_active() }
            } else {
                theme::button()
            };
            tabs.push(Span::styled(format!(" {s} "), st));
            tabs.push(Span::raw(" "));
        }
        tabs.push(Span::styled(format!("  {}", self.status_line()), theme::muted()));
        f.render_widget(Paragraph::new(Line::from(tabs)).style(theme::base()), rows[0]);

        let cols = Layout::horizontal([Constraint::Length(22), Constraint::Min(30)]).split(rows[1]);
        let mut list = Vec::new();
        for p in catalog::pages_in(self.section) {
            let name = format!(" {:<w$}", PAGES[p].name, w = (cols[0].width as usize).saturating_sub(4));
            let st = if p == self.page {
                if self.zone == Zone::Periph { theme::selected() } else { theme::selected().add_modifier(Modifier::DIM) }
            } else {
                theme::button()
            };
            list.push(Line::from(Span::styled(name, st)));
        }
        f.render_widget(Paragraph::new(list).block(card_block("PERIPHERALS")), cols[0]);

        let block = card_block(self.page_def().name);
        let inner = block.inner(cols[1]);
        f.render_widget(block, cols[1]);
        if inner.height < 3 {
            return;
        }
        let (lines, focus) = page_lines(self, self.page_def(), inner.width as usize, extra);
        let h = inner.height as usize;
        let scroll = if focus + 2 > h { focus + 2 - h } else { 0 };
        f.render_widget(Paragraph::new(lines).scroll((scroll as u16, 0)), inner);
    }
}

fn card_block(title: &str) -> Block<'static> {
    Block::default()
        .borders(Borders::ALL)
        .border_style(theme::border())
        .style(theme::base())
        .title(Span::styled(format!(" {title} "), theme::title()))
}

fn wrap(text: &str, width: usize) -> Vec<String> {
    let width = width.max(8);
    let mut out = Vec::new();
    let mut cur = String::new();
    for word in text.split_whitespace() {
        if !cur.is_empty() && cur.chars().count() + 1 + word.chars().count() > width {
            out.push(std::mem::take(&mut cur));
        }
        if !cur.is_empty() {
            cur.push(' ');
        }
        cur.push_str(word);
    }
    if !cur.is_empty() {
        out.push(cur);
    }
    out
}

fn chunk(text: &str, width: usize) -> Vec<String> {
    let width = width.max(8);
    let chars: Vec<char> = text.chars().collect();
    if chars.is_empty() {
        return vec![String::new()];
    }
    chars.chunks(width).map(|c| c.iter().collect()).collect()
}

struct Boxed<'a> {
    width: usize,
    lines: Vec<Line<'a>>,
}

impl<'a> Boxed<'a> {
    fn open(&mut self, title: &str) {
        let t = format!("┌ {title} ");
        let fill = self.width.saturating_sub(t.chars().count() + 1);
        self.lines.push(Line::from(vec![
            Span::styled("┌ ", theme::border()),
            Span::styled(format!("{title} "), theme::title()),
            Span::styled(format!("{}┐", "─".repeat(fill)), theme::border()),
        ]));
    }
    fn row(&mut self, mut spans: Vec<Span<'a>>) {
        let used: usize = spans.iter().map(|s| s.content.chars().count()).sum();
        let pad = self.width.saturating_sub(used + 4);
        let mut line = vec![Span::styled("│ ", theme::border())];
        line.append(&mut spans);
        line.push(Span::raw(" ".repeat(pad)));
        line.push(Span::styled(" │", theme::border()));
        self.lines.push(Line::from(line));
    }
    fn close(&mut self) {
        self.lines
            .push(Line::from(Span::styled(format!("└{}┘", "─".repeat(self.width.saturating_sub(2))), theme::border())));
    }
}

fn read_text(app: &App, needle: &str, first_word: bool) -> Option<String> {
    app.reads.get(needle).map(|v| {
        if first_word {
            v.split_whitespace().next().unwrap_or("").to_string()
        } else {
            v.clone()
        }
    })
}

fn state_style(v: &str) -> Style {
    match v {
        "ON" => Style::default().fg(GREEN).bg(BG).add_modifier(Modifier::BOLD),
        "OFF" => theme::muted(),
        _ => Style::default().fg(YELLOW).bg(BG),
    }
}

/// Returns (lines, line index holding the focused item).
fn page_lines<'a>(app: &'a App, page: &'a Page, width: usize, extra: &[String]) -> (Vec<Line<'a>>, usize) {
    let mut b = Boxed { width, lines: Vec::new() };
    let mut idx = 0usize;
    let mut focus_line = 0usize;
    let editing = app.edit == Edit::Value;
    let focused = |i: usize| app.zone == Zone::Page && app.cursor == i;
    for g in page.groups {
        draw_group(app, page, g, &mut b, &mut idx, &mut focus_line, editing, &focused);
    }
    if page.camera {
        thumb_card(app, &mut b);
    }
    if !extra.is_empty() {
        b.open("CAN RPARAM SNAPSHOT (flight software)");
        for l in extra {
            for part in chunk(l, width.saturating_sub(4)) {
                b.row(vec![Span::styled(part, theme::base())]);
            }
        }
        b.close();
    }
    (b.lines, focus_line)
}

#[allow(clippy::too_many_arguments)]
fn draw_group<'a>(
    app: &'a App,
    page: &'a Page,
    g: &'a Group,
    b: &mut Boxed<'a>,
    idx: &mut usize,
    focus_line: &mut usize,
    editing: bool,
    focused: &dyn Fn(usize) -> bool,
) {
    let inner = b.width.saturating_sub(4);
    b.open(g.title);
    let mut flow: Vec<Span<'a>> = Vec::new();
    let mut flow_w = 0usize;
    macro_rules! flush {
        () => {
            if !flow.is_empty() {
                b.row(std::mem::take(&mut flow));
            }
        };
    }
    for w in g.widgets {
        match w {
            W::Row(label, needle, btns) => {
                flush!();
                flow_w = 0;
                let v = read_text(app, needle, true).unwrap_or_else(|| "UNKNOWN".into());
                let mut spans = vec![
                    Span::styled(format!("{label:<10}"), theme::title()),
                    Span::styled(format!("{v:<8}"), state_style(&v)),
                ];
                for (bl, _) in btns.iter() {
                    let on = focused(*idx);
                    if on {
                        *focus_line = b.lines.len();
                    }
                    spans.push(Span::styled(format!("[ {bl} ]"), if on { theme::selected() } else { theme::button() }));
                    spans.push(Span::raw(" "));
                    *idx += 1;
                }
                b.row(spans);
            }
            other => {
                let on = focused(*idx);
                let (spans, wdt): (Vec<Span<'a>>, usize) = match other {
                    W::Btn(l, _) => (
                        vec![Span::styled(format!("[ {l} ]"), if on { theme::selected() } else { theme::button() })],
                        l.chars().count() + 4,
                    ),
                    W::Field(l, _, _) => {
                        let mut v = app.values[app.page][*idx].clone();
                        if on && editing {
                            v.push('▏');
                        }
                        let vs = format!("[{v:<6}]");
                        let wd = l.chars().count() + 1 + vs.chars().count();
                        (
                            vec![
                                Span::styled(format!("{l} "), theme::base()),
                                Span::styled(vs, if on { theme::selected() } else { theme::button() }),
                            ],
                            wd,
                        )
                    }
                    W::Pick(l, _, _) => {
                        let vs = format!("‹ {} ›", app.values[app.page][*idx]);
                        let wd = l.chars().count() + 1 + vs.chars().count();
                        (
                            vec![
                                Span::styled(format!("{l} "), theme::base()),
                                Span::styled(vs, if on { theme::selected() } else { theme::button() }),
                            ],
                            wd,
                        )
                    }
                    W::Row(..) => unreachable!(),
                };
                if flow_w > 0 && flow_w + 2 + wdt > inner {
                    flush!();
                    flow_w = 0;
                }
                if on {
                    *focus_line = b.lines.len();
                }
                if flow_w > 0 {
                    flow.push(Span::raw("  "));
                    flow_w += 2;
                }
                flow.extend(spans);
                flow_w += wdt;
                *idx += 1;
            }
        }
    }
    flush!();
    let _ = page;
    let _ = flow_w;
    if !g.reads.is_empty() {
        for r in g.reads {
            let v = read_text(app, r.needle, r.first_word);
            let (text, st) = match v {
                Some(t) => (t, theme::base()),
                None => ("--".to_string(), theme::muted()),
            };
            let label = format!("{:<16}", r.label);
            let room = inner.saturating_sub(16);
            let parts = chunk(&text, room);
            for (n, p) in parts.into_iter().enumerate() {
                let l = if n == 0 { label.clone() } else { " ".repeat(16) };
                b.row(vec![Span::styled(l, theme::title()), Span::styled(p, st)]);
            }
        }
    }
    for n in wrap(g.note, inner) {
        b.row(vec![Span::styled(n, theme::muted())]);
    }
    b.close();
}

fn heat(t: f32) -> Color {
    let t = t.clamp(0.0, 1.0);
    let stops: [(f32, [f32; 3]); 5] = [
        (0.0, [10.0, 5.0, 60.0]),
        (0.3, [120.0, 20.0, 140.0]),
        (0.6, [230.0, 70.0, 50.0]),
        (0.85, [255.0, 200.0, 40.0]),
        (1.0, [255.0, 255.0, 200.0]),
    ];
    for w in stops.windows(2) {
        if t <= w[1].0 {
            let k = (t - w[0].0) / (w[1].0 - w[0].0);
            let c = |i: usize| (w[0].1[i] + (w[1].1[i] - w[0].1[i]) * k) as u8;
            return Color::Rgb(c(0), c(1), c(2));
        }
    }
    Color::Rgb(255, 255, 200)
}

fn thumb_card<'a>(app: &'a App, b: &mut Boxed<'a>) {
    b.open("THERMAL IMAGE");
    match &app.image {
        None => b.row(vec![Span::styled("No image yet. Capture image, then it is shown here.", theme::muted())]),
        Some(f) => {
            let min = f.iter().cloned().fold(f32::MAX, f32::min);
            let max = f.iter().cloned().fold(f32::MIN, f32::max);
            let mean = f.iter().sum::<f32>() / f.len() as f32;
            let stats = format!("{}x{} min={min:.1} C mean={mean:.1} C max={max:.1} C (simulated frame)", sim::W, sim::H);
            for part in wrap(&stats, b.width.saturating_sub(4)) {
                b.row(vec![Span::styled(part, theme::base())]);
            }
            let span = (max - min).max(0.001);
            for y in (0..sim::H).step_by(2) {
                let mut row = Vec::new();
                for x in 0..sim::W {
                    let top = heat((f[y * sim::W + x] - min) / span);
                    let bot = heat((f[(y + 1) * sim::W + x] - min) / span);
                    row.push(Span::styled("▀", Style::default().fg(top).bg(bot)));
                }
                b.row(row);
            }
        }
    }
    b.close();
}

