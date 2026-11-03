use crate::app::{App, Edit, Kind, Zone};
use crate::catalog::{self, Group, Page, SECTIONS, W};
use crate::link::LINKS;
use crate::sim;
use crate::theme::{self, *};
use ratatui::backend::TestBackend;
use ratatui::layout::{Constraint, Layout, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph};
use ratatui::{Frame, Terminal};

fn card_block(title: &str) -> Block<'static> {
    Block::default()
        .borders(Borders::ALL)
        .border_style(theme::border())
        .style(theme::base())
        .title(Span::styled(format!(" {title} "), theme::title()))
}

pub fn draw(f: &mut Frame, app: &App) {
    let area = f.area();
    f.render_widget(Block::default().style(theme::base()), area);
    let rows = Layout::vertical([
        Constraint::Length(1),
        Constraint::Length(1),
        Constraint::Length(1),
        Constraint::Min(5),
        Constraint::Length(1),
    ])
    .split(area);

    // header bar
    let mode = if app.sim.is_some() { "SIM / NO HARDWARE" } else { "LIVE" };
    let right = format!("AUTO {}  {} / 4 LINKS OPEN  Q: QUIT ", if app.auto { "ON" } else { "OFF" }, app.open_count());
    let left = format!(" EXP  /  TEST TUI  /  {mode}");
    let pad = (rows[0].width as usize).saturating_sub(left.chars().count() + right.chars().count());
    f.render_widget(
        Paragraph::new(Line::from(format!("{left}{}{right}", " ".repeat(pad)))).style(theme::header()),
        rows[0],
    );

    // section tabs
    let mut tabs = vec![Span::raw(" ")];
    for (i, s) in SECTIONS.iter().enumerate() {
        let st = if i == app.top {
            if app.zone == Zone::Top { theme::selected() } else { theme::tab_active() }
        } else {
            theme::button()
        };
        tabs.push(Span::styled(format!(" {s} "), st));
        tabs.push(Span::raw(" "));
    }
    f.render_widget(Paragraph::new(Line::from(tabs)).style(theme::base()), rows[1]);

    // link bar
    let mut lk = vec![
        Span::raw(" "),
        Span::styled(
            " PORT SETTINGS ",
            if app.top == crate::app::TOP_PORT {
                if app.zone == Zone::Top { theme::selected() } else { theme::tab_active() }
            } else {
                theme::button()
            },
        ),
    ];
    for (i, (_, short, _)) in LINKS.iter().enumerate() {
        let l = &app.links[i];
        let state = if l.open { "OPEN" } else { "CLOSED" };
        let mut st = Style::default().fg(if l.open { GREEN } else { MUTED }).bg(BG);
        if i == app.active {
            st = st.add_modifier(Modifier::BOLD | Modifier::UNDERLINED).fg(YELLOW);
        }
        lk.push(Span::styled(format!("  {}:{short} {state}", i + 1), st));
    }
    f.render_widget(Paragraph::new(Line::from(lk)).style(theme::base()), rows[2]);

    let cols = Layout::horizontal([Constraint::Length(26), Constraint::Min(34), Constraint::Percentage(38)])
        .split(rows[3]);
    draw_periph(f, app, cols[0]);
    draw_center(f, app, cols[1]);
    draw_console(f, app, cols[2]);

    let hint = match app.zone {
        Zone::Top => " TOP MENU   Tab/←/→ choose menu   Enter open it   p ports   a auto-refresh   q quit",
        Zone::Periph => " LEFT MENU   ↑/↓ choose   → or Enter go to page   ← back to top menu   Tab next menu",
        Zone::Page if app.port_view => " PORT SETTINGS   ↑/↓ link   e port list   b baud   o/Enter open-close   → console   ← back",
        Zone::Page => " PAGE   ↑/↓ select   Enter press/edit   Space change pick   → console   ← left menu   Tab next menu",
        Zone::Console => " CONSOLE (debug)   type a command, Enter sends   Esc/← back   PgUp/PgDn scroll   Ctrl-L clear",
    };
    f.render_widget(Paragraph::new(hint).style(theme::muted()), rows[4]);
}

fn draw_periph(f: &mut Frame, app: &App, area: Rect) {
    let mut lines = Vec::new();
    if app.port_view {
        let name = format!(" {:<w$}", "Port settings", w = (area.width as usize).saturating_sub(4));
        lines.push(Line::from(Span::styled(name, theme::selected().add_modifier(Modifier::DIM))));
        f.render_widget(Paragraph::new(lines).block(card_block("PERIPHERALS")), area);
        return;
    }
    for p in catalog::pages_in(app.section) {
        let name = format!(" {:<w$}", catalog::PAGES[p].name, w = (area.width as usize).saturating_sub(4));
        let st = if p == app.page && app.zone != Zone::Top || (p == app.page && !app.port_view) {
            if app.zone == Zone::Periph { theme::selected() } else { theme::selected().add_modifier(Modifier::DIM) }
        } else {
            theme::button()
        };
        lines.push(Line::from(Span::styled(name, st)));
    }
    f.render_widget(Paragraph::new(lines).block(card_block("PERIPHERALS")), area);
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
fn page_lines<'a>(app: &'a App, page: &'a Page, width: usize) -> (Vec<Line<'a>>, usize) {
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

fn draw_center(f: &mut Frame, app: &App, area: Rect) {
    let title = if app.port_view { "Port settings" } else { app.page_def().name };
    let block = card_block(title);
    let inner = block.inner(area);
    f.render_widget(block, area);
    if inner.height < 4 {
        return;
    }
    let link = &app.links[app.active];
    let head = Line::from(vec![
        Span::styled("Peripheral commands via ", theme::base()),
        Span::styled(format!("[ {} ▾ ]", LINKS[app.active].0), theme::button()),
        Span::styled(format!("  {}", link.status), Style::default().fg(if link.open { GREEN } else { MUTED }).bg(BG)),
    ]);
    f.render_widget(Paragraph::new(head), Rect { height: 1, ..inner });
    let body = Rect { y: inner.y + 2, height: inner.height - 2, ..inner };

    let (lines, focus) = if app.port_view {
        (port_lines(app), 0)
    } else {
        page_lines(app, app.page_def(), body.width as usize)
    };
    let h = body.height as usize;
    let scroll = if focus + 2 > h { focus + 2 - h } else { 0 };
    f.render_widget(Paragraph::new(lines).scroll((scroll as u16, 0)), body);
}

fn port_lines(app: &App) -> Vec<Line<'static>> {
    let mut v = vec![
        Line::from(Span::styled("e choose port from list   b edit baud   o open. Only the Console link talks to hardware in v1.", theme::muted())),
        Line::from(""),
    ];
    for (i, (name, _, _)) in LINKS.iter().enumerate() {
        let l = &app.links[i];
        let sel = i == app.port_cursor;
        let mut port = l.port.clone();
        let mut baud = l.baud.clone();
        if sel && app.edit == Edit::Port {
            port.push('▏');
        }
        if sel && app.edit == Edit::Baud {
            baud.push('▏');
        }
        let st = if sel && app.zone == Zone::Page {
            theme::selected()
        } else if sel {
            theme::title()
        } else {
            theme::button()
        };
        v.push(Line::from(vec![
            Span::styled(format!(" {name:<17}"), st),
            Span::styled(format!(" port [{port:<16}] baud [{baud:<7}]"), theme::base()),
        ]));
        if sel && app.edit == Edit::Pick {
            let n = app.port_list.len();
            let start = app.port_sel.saturating_sub(4).min(n.saturating_sub(5));
            for (k, p) in app.port_list.iter().enumerate().skip(start).take(5) {
                let st = if k == app.port_sel { theme::selected() } else { theme::button() };
                v.push(Line::from(Span::styled(format!("      {p:<22}"), st)));
            }
            v.push(Line::from(Span::styled("      ↑/↓ choose   Enter select   r rescan   Esc cancel", theme::muted())));
        }
        v.push(Line::from(Span::styled(
            format!("   {}", l.status),
            Style::default().fg(if l.open { GREEN } else { MUTED }).bg(BG),
        )));
    }
    v.push(Line::from(""));
    v.push(Line::from(Span::styled("↑/↓ select   e pick port   t type path   b edit baud   o/Enter open/close   Esc back", theme::muted())));
    v
}

fn draw_console(f: &mut Frame, app: &App, area: Rect) {
    let block = card_block("CONSOLE / TX + RX");
    let inner = block.inner(area);
    f.render_widget(block, area);
    if inner.height < 5 {
        return;
    }
    let w = inner.width as usize;
    let legend = Paragraph::new("QUEUE = requested | TX = written to link | RX = received")
        .style(theme::base())
        .wrap(ratatui::widgets::Wrap { trim: true });
    f.render_widget(legend, Rect { height: 2, ..inner });
    let on = app.zone == Zone::Console;
    let label = LINKS[app.active].1;
    let mut text = app.input.clone();
    if on {
        text.push('▏');
    }
    let input = Line::from(vec![
        Span::styled(format!("{label} "), theme::muted()),
        Span::styled(
            format!("[ {:<width$} ]", if text.is_empty() && !on { "Enter command (press /)" } else { &text }, width = w.saturating_sub(label.len() + 6)),
            if on { theme::selected() } else { theme::button() },
        ),
    ]);
    f.render_widget(Paragraph::new(input), Rect { y: inner.y + 2, height: 1, ..inner });
    f.render_widget(
        Paragraph::new("Ctrl-L clear log").style(theme::muted()),
        Rect { y: inner.y + 3, height: 1, ..inner },
    );
    let log_area = Rect { y: inner.y + 4, height: inner.height - 4, ..inner };
    let mut rows: Vec<Line> = Vec::new();
    for l in &app.log {
        let style = match l.kind {
            Kind::Info => theme::muted(),
            Kind::Queue => Style::default().fg(YELLOW).bg(BG),
            Kind::Tx => Style::default().fg(GREEN).bg(BG),
            Kind::Rx => Style::default().fg(GREEN).bg(BG).add_modifier(Modifier::BOLD),
            Kind::Err => Style::default().fg(RED).bg(BG),
        };
        for part in chunk(&l.text, w) {
            rows.push(Line::from(Span::styled(part, style)));
        }
    }
    let h = log_area.height as usize;
    let end = rows.len().saturating_sub(app.log_scroll.min(rows.len()));
    let start = end.saturating_sub(h);
    f.render_widget(Paragraph::new(rows[start..end].to_vec()), log_area);
}

/// Render one frame into a text buffer (no tty needed): used by `--dump` and tests.
pub fn dump(app: &App, w: u16, h: u16) -> String {
    let mut term = Terminal::new(TestBackend::new(w, h)).unwrap();
    term.draw(|f| draw(f, app)).unwrap();
    let buf = term.backend().buffer().clone();
    let mut out = String::new();
    for y in 0..h {
        let mut line = String::new();
        for x in 0..w {
            line.push_str(buf[(x, y)].symbol());
        }
        out.push_str(line.trim_end());
        out.push('\n');
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_page_renders_at_80x24_and_120x40() {
        for (w, h) in [(80, 24), (120, 40)] {
            let mut app = App::new(true);
            for p in 0..catalog::PAGES.len() {
                app.page = p;
                app.section = catalog::PAGES[p].section;
                app.cursor = 0;
                let s = dump(&app, w, h);
                assert!(s.contains("PERIPHERALS"));
                assert!(s.contains("CONSOLE / TX + RX"));
            }
        }
    }

    #[test]
    fn camera_image_shows_after_capture() {
        let mut app = App::new(true);
        let p = catalog::PAGES.iter().position(|p| p.name == "IR camera 1").unwrap();
        app.page = p;
        app.section = 4;
        app.send("camera_capture 1");
        assert!(dump(&app, 120, 60).contains("simulated frame"));
    }
}
