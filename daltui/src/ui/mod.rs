//! Root render coordinator and shared drawing helpers.

mod camera;
pub(crate) mod cfg;
mod controls;
mod heater;
mod home;
mod log;
mod panels;
mod pc;

use crate::app::{State, Tab};
use crate::controls::Sub;
use crate::sim::{Level, Rail};
use ratatui::{prelude::*, widgets::*};

// ── Colors ────────────────────────────────────────────────────────────────────

pub fn tab_color(tab: Tab) -> Color {
    match tab {
        Tab::Home => Color::Green,
        Tab::Obc => Color::Cyan,
        Tab::Exp => Color::LightGreen,
        Tab::Pdu => Color::LightYellow,
        Tab::Log => Color::Magenta,
        Tab::Cfg => Color::LightBlue,
    }
}

pub fn sub_color(sub: Sub) -> Color {
    match sub {
        Sub::Obc => Color::Cyan,
        Sub::Exp => Color::LightGreen,
        Sub::Pdu => Color::LightYellow,
    }
}

pub fn level_color(l: Level) -> Color {
    match l {
        Level::Info => Color::Gray,
        Level::Ok => Color::Green,
        Level::Warn => Color::Yellow,
        Level::Err => Color::Red,
    }
}

pub fn pct_color(p: f32) -> Color {
    if p >= 90.0 {
        Color::Red
    } else if p >= 70.0 {
        Color::Yellow
    } else {
        Color::Green
    }
}

pub fn temp_color(t: f32, warn: f32, crit: f32) -> Color {
    if t.is_nan() {
        Color::DarkGray
    } else if t >= crit {
        Color::Red
    } else if t >= warn {
        Color::Yellow
    } else {
        Color::Green
    }
}

// ── Small helpers ─────────────────────────────────────────────────────────────

pub fn hms(ts: i64) -> String {
    let s = ts.rem_euclid(86_400);
    format!("{:02}:{:02}:{:02}", s / 3600, (s / 60) % 60, s % 60)
}

pub fn fmt_uptime(secs: f64) -> String {
    let s = secs as u64;
    let (d, h, m, sec) = (s / 86_400, (s % 86_400) / 3600, (s % 3600) / 60, s % 60);
    if d > 0 {
        format!("{d}d {h:02}:{m:02}:{sec:02}")
    } else {
        format!("{h:02}:{m:02}:{sec:02}")
    }
}

/// Value with unit, or `n/a` when the active data source has no reading (NaN).
pub fn nv(v: f32, prec: usize, unit: &str) -> String {
    if v.is_nan() {
        "n/a".into()
    } else {
        format!("{v:.prec$} {unit}")
    }
}

/// Frame counter; `n/a` when the source does not report it.
pub fn cnt(n: u32) -> String {
    if n == crate::live::NA_U32 {
        "n/a".into()
    } else {
        n.to_string()
    }
}

pub fn gauge_bar(pct: f32, w: usize) -> String {
    let filled = ((pct.clamp(0.0, 100.0) / 100.0) * w as f32).round() as usize;
    format!("{}{}", "█".repeat(filled), "░".repeat(w - filled))
}

pub fn lbl() -> Style {
    Style::default().fg(Color::Cyan)
}

pub fn dim() -> Style {
    Style::default().fg(Color::DarkGray)
}

pub fn txt(s: impl Into<String>, c: Color) -> Span<'static> {
    Span::styled(s.into(), Style::default().fg(c))
}

pub fn onoff(b: bool) -> Span<'static> {
    if b {
        Span::styled(
            "ON ",
            Style::default()
                .fg(Color::Green)
                .add_modifier(Modifier::BOLD),
        )
    } else {
        Span::styled("OFF", dim())
    }
}

/// `label` padded to `w`, then the value spans.
pub fn kv(label: &str, w: usize, mut value: Vec<Span<'static>>) -> Line<'static> {
    let mut v = vec![Span::styled(format!(" {label:<w$} "), lbl())];
    v.append(&mut value);
    Line::from(v)
}

pub fn kv1(label: &str, w: usize, value: impl Into<String>, c: Color) -> Line<'static> {
    kv(label, w, vec![txt(value, c)])
}

pub fn bar_line(label: &str, pct: f32, w: usize, extra: &str) -> Line<'static> {
    if pct.is_nan() {
        return kv(
            label,
            8,
            vec![
                txt(gauge_bar(0.0, w), Color::DarkGray),
                txt("  n/a", Color::DarkGray),
            ],
        );
    }
    kv(
        label,
        8,
        vec![
            txt(gauge_bar(pct, w), pct_color(pct)),
            txt(format!(" {pct:4.1}%{extra}"), Color::White),
        ],
    )
}

/// A rail is ON when at least 1 V is measured.
pub fn rail_line(r: &Rail, name_w: usize) -> Line<'static> {
    let on = r.mv >= 1000;
    let c = if on { Color::White } else { Color::DarkGray };
    kv(
        r.name,
        name_w,
        vec![
            txt(format!("{:5.2} V ", r.mv as f32 / 1000.0), c),
            txt(format!("{:5} mA ", r.ma), c),
            txt(format!("{:5.2} W ", r.mw() as f32 / 1000.0), c),
            onoff(on),
        ],
    )
}

/// Narrow variant for the HOME cards: no power column.
pub fn rail_line_c(r: &Rail, name_w: usize) -> Line<'static> {
    let on = r.mv >= 1000;
    let c = if on { Color::White } else { Color::DarkGray };
    kv(
        r.name,
        name_w,
        vec![
            txt(format!("{:5.2} V {:5} mA ", r.mv as f32 / 1000.0, r.ma), c),
            onoff(on),
        ],
    )
}

pub fn rails_total(rails: &[Rail]) -> f32 {
    rails.iter().map(|r| r.mw()).sum::<i32>() as f32 / 1000.0
}

pub fn panel(f: &mut Frame, area: Rect, title: &str, color: Color, lines: Vec<Line<'static>>) {
    let block = Block::bordered()
        .border_style(Style::default().fg(color))
        .title(Span::styled(
            format!(" {title} "),
            Style::default().fg(color).add_modifier(Modifier::BOLD),
        ));
    f.render_widget(Paragraph::new(lines).block(block), area);
}

// ── Draw ──────────────────────────────────────────────────────────────────────

pub fn draw(f: &mut Frame, st: &mut State) {
    let area = f.area();
    st.last_area = area;

    let chunks = Layout::vertical([
        Constraint::Length(1), // header
        Constraint::Length(3), // tab bar
        Constraint::Min(5),    // content
        Constraint::Length(3), // status line
        Constraint::Length(1), // footer
    ])
    .split(area);

    draw_header(f, st, chunks[0]);
    draw_tabs(f, st, chunks[1]);

    match st.tab {
        Tab::Home => home::draw(f, st, chunks[2]),
        Tab::Log => log::draw(f, st, chunks[2]),
        Tab::Cfg => cfg::draw(f, st, chunks[2]),
        t => {
            if let Some(sub) = t.sub() {
                draw_subsystem(f, st, sub, chunks[2]);
            }
        }
    }

    draw_status_line(f, st, chunks[3]);
    draw_footer(f, st, chunks[4]);

    if let Some((sub, i)) = st.confirm {
        draw_confirm(f, area, sub, sub.controls(st.page[sub.idx()])[i].label);
    }
    if let Some(value) = &st.rtc_edit {
        draw_rtc_editor(f, area, value);
    }
    if let Some(value) = &st.pc_name_edit {
        draw_pc_name_editor(f, area, value);
    }
    if let Some(value) = &st.pwm_edit {
        draw_pwm_editor(f, area, value);
    }
    if let Some((id, value)) = &st.num_edit {
        draw_number_editor(f, area, id, value, st.num_edit_replace);
    }
    if let Some((id, draft)) = &st.choice_edit {
        draw_choice_editor(f, area, id, *draft);
    }
}

fn draw_number_editor(f: &mut Frame, area: Rect, id: &str, value: &str, replace: bool) {
    let Some(control) = crate::app::control_by_id(id) else {
        return;
    };
    let crate::controls::Kind::Step { min, max, unit, .. } = control.kind else {
        return;
    };
    let w = 52.min(area.width);
    let h = 7.min(area.height);
    let r = Rect {
        x: area.x + (area.width - w) / 2,
        y: area.y + (area.height - h) / 2,
        width: w,
        height: h,
    };
    f.render_widget(Clear, r);
    let lines = vec![
        Line::raw(format!(" {} ({min}–{max})", control.label)),
        Line::raw(""),
        Line::from(txt(
            if replace {
                format!(" [{value}] {unit}")
            } else {
                format!(" {value}_ {unit}")
            },
            Color::Yellow,
        )),
        Line::raw(""),
        Line::raw(" Type: replace   Enter: stage   Esc: cancel"),
    ];
    f.render_widget(
        Paragraph::new(lines).block(
            Block::bordered()
                .title(" Edit value ")
                .border_style(Style::default().fg(Color::Cyan)),
        ),
        r,
    );
}

fn draw_choice_editor(f: &mut Frame, area: Rect, id: &str, draft: usize) {
    let Some(control) = crate::app::control_by_id(id) else {
        return;
    };
    let crate::controls::Kind::Choice(options) = &control.kind else {
        return;
    };
    let w = 52.min(area.width);
    let h = (options.len() as u16 + 5).min(area.height);
    let r = Rect {
        x: area.x + (area.width - w) / 2,
        y: area.y + (area.height - h) / 2,
        width: w,
        height: h,
    };
    f.render_widget(Clear, r);
    let mut lines = vec![Line::raw(format!(" {}", control.label))];
    for (i, option) in options.iter().enumerate() {
        lines.push(Line::from(txt(
            format!(" {} {}", if i == draft { '▶' } else { ' ' }, option),
            if i == draft {
                Color::Yellow
            } else {
                Color::White
            },
        )));
    }
    lines.push(Line::raw(""));
    lines.push(Line::raw(" Up/Down: choose   Enter: stage   Esc: cancel"));
    f.render_widget(
        Paragraph::new(lines).block(
            Block::bordered()
                .title(" Select value ")
                .border_style(Style::default().fg(Color::Cyan)),
        ),
        r,
    );
}

fn draw_pwm_editor(f: &mut Frame, area: Rect, value: &str) {
    let w = 52.min(area.width);
    let h = 7.min(area.height);
    let r = Rect {
        x: area.x + (area.width - w) / 2,
        y: area.y + (area.height - h) / 2,
        width: w,
        height: h,
    };
    f.render_widget(Clear, r);
    let lines = vec![
        Line::raw(" PWM frequency (100–100000 Hz)"),
        Line::raw(""),
        Line::from(txt(format!(" {value}_ Hz"), Color::Yellow)),
        Line::raw(""),
        Line::raw(" Enter: stage value   Ctrl+U: clear   Esc: cancel"),
    ];
    f.render_widget(
        Paragraph::new(lines).block(
            Block::bordered()
                .title(" IR PWM frequency ")
                .border_style(Style::default().fg(Color::Cyan)),
        ),
        r,
    );
}

fn draw_pc_name_editor(f: &mut Frame, area: Rect, value: &str) {
    let w = 56.min(area.width);
    let h = 7.min(area.height);
    let r = Rect {
        x: area.x + (area.width - w) / 2,
        y: area.y + (area.height - h) / 2,
        width: w,
        height: h,
    };
    f.render_widget(Clear, r);
    let lines = vec![
        Line::raw(" Profile name (1–23 ASCII characters, no spaces)"),
        Line::raw(""),
        Line::from(txt(format!(" {value}_"), Color::Yellow)),
        Line::raw(""),
        Line::raw(" Enter: stage name   Ctrl+U: clear   Esc: cancel"),
    ];
    f.render_widget(
        Paragraph::new(lines).block(
            Block::bordered()
                .title(" PC profile name ")
                .border_style(Style::default().fg(Color::Cyan)),
        ),
        r,
    );
}

fn draw_rtc_editor(f: &mut Frame, area: Rect, value: &str) {
    let w = 52.min(area.width);
    let h = 7.min(area.height);
    let r = Rect {
        x: area.x + (area.width - w) / 2,
        y: area.y + (area.height - h) / 2,
        width: w,
        height: h,
    };
    f.render_widget(Clear, r);
    let lines = vec![
        Line::raw(" UTC time (YYYY-MM-DD HH:MM:SS)"),
        Line::raw(""),
        Line::from(txt(format!(" {value}_"), Color::Yellow)),
        Line::raw(""),
        Line::raw(" Enter: set RTC   Ctrl+U: clear   Esc: cancel"),
    ];
    f.render_widget(
        Paragraph::new(lines).block(
            Block::bordered()
                .title(" Set Time ")
                .border_style(Style::default().fg(Color::Cyan)),
        ),
        r,
    );
}

fn draw_subsystem(f: &mut Frame, st: &State, sub: Sub, area: Rect) {
    const CTRL_W: u16 = 44;
    let rows = if sub.pages().len() > 1 {
        Layout::vertical([Constraint::Length(1), Constraint::Min(3)]).split(area)
    } else {
        Layout::vertical([Constraint::Length(0), Constraint::Min(3)]).split(area)
    };
    if sub.pages().len() > 1 {
        draw_pages(f, st, sub, rows[0]);
    }
    if sub == Sub::Obc && st.page[sub.idx()] == 1 {
        camera::draw(f, st, rows[1]);
        return;
    }
    if sub == Sub::Exp && st.page[sub.idx()] == 7 {
        heater::draw(f, st, rows[1]);
        return;
    }
    if sub == Sub::Exp && st.page[sub.idx()] == 6 {
        pc::draw(f, st, rows[1]);
        return;
    }
    if sub == Sub::Exp && st.page[sub.idx()] == 3 {
        let cols = Layout::horizontal([
            Constraint::Percentage(29),
            Constraint::Percentage(39),
            Constraint::Percentage(32),
        ])
        .split(rows[1]);
        panels::draw(f, st, sub, cols[0]);
        controls::draw_motor_main(f, st, cols[1]);
        controls::draw_motor_mapping(f, st, cols[2]);
        return;
    }
    let cols = Layout::horizontal([
        Constraint::Min(30),
        Constraint::Length(CTRL_W.min(area.width / 2)),
    ])
    .split(rows[1]);
    panels::draw(f, st, sub, cols[0]);
    controls::draw(f, st, sub, cols[1]);
}

/// Page strip above a subsystem tab; comma and period move between pages.
fn draw_pages(f: &mut Frame, st: &State, sub: Sub, area: Rect) {
    let color = sub_color(sub);
    let cur = st.page[sub.idx()];
    let mut spans = vec![Span::styled(" , ", dim())];
    for (i, name) in sub.pages().iter().enumerate() {
        let style = if i == cur {
            Style::default()
                .bg(color)
                .fg(Color::Black)
                .add_modifier(Modifier::BOLD)
        } else {
            Style::default().fg(Color::White)
        };
        spans.push(Span::styled(format!(" {name} "), style));
        spans.push(Span::raw(" "));
    }
    spans.push(Span::styled(".  (< > control panel)", dim()));
    f.render_widget(Paragraph::new(Line::from(spans)), area);
}

fn draw_header(f: &mut Frame, st: &State, area: Rect) {
    let w = area.width as usize;
    let dot_color = if st.started.elapsed().as_millis() % 1000 < 150 {
        Color::Green
    } else {
        Color::Blue
    };
    let left = " daltui v0.1.0";
    let title = "DAL Control Dashboard";
    let live = &st.sim.live;
    // MANUAL before the first UPDATE: nothing was sent yet, so "no link" would be wrong.
    let idle_manual = !live.auto && live.last_update.is_none() && live.last_err.is_empty();
    let sim = match live.mode {
        crate::link::Mode::Sim if st.sim.paused => " SIM PAUSED ".to_string(),
        crate::link::Mode::Sim => " SIM ".to_string(),
        m if !live.connected && idle_manual => format!(" {} MANUAL ", m.label()),
        m => format!(
            " {} {} ",
            m.label(),
            if live.connected { "LINK" } else { "NO LINK" }
        ),
    };
    let sim_bg = match live.mode {
        crate::link::Mode::Sim => Color::Yellow,
        _ if !live.connected && !idle_manual => Color::Red,
        m => cfg::mode_color(m),
    };
    let right = format!(" {} UTC  ", hms(st.sim.rtc_now()));

    let used = 1
        + left.chars().count()
        + title.chars().count()
        + sim.chars().count()
        + right.chars().count();
    let pad = w.saturating_sub(used);
    let (pl, pr) = (pad / 2, pad - pad / 2);

    let base = Style::default()
        .bg(Color::Blue)
        .fg(Color::White)
        .add_modifier(Modifier::BOLD);
    let line = Line::from(vec![
        Span::styled("●", base.fg(dot_color)),
        Span::styled(left, base),
        Span::styled(" ".repeat(pl), base),
        Span::styled(title, base),
        Span::styled(" ".repeat(pr), base),
        Span::styled(
            sim,
            Style::default()
                .bg(sim_bg)
                .fg(Color::Black)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(right, base),
    ]);
    f.render_widget(Paragraph::new(line), area);
}

fn draw_tabs(f: &mut Frame, st: &State, area: Rect) {
    let block = Block::bordered().border_style(Style::default().fg(Color::Gray));
    let inner = block.inner(area);
    f.render_widget(block, area);

    let n = Tab::ALL.len();
    let cols = Layout::horizontal(vec![Constraint::Ratio(1, n as u32); n]).split(inner);

    for (i, &tab) in Tab::ALL.iter().enumerate() {
        let has_div = i < n - 1;
        let cell_w = cols[i].width as usize;
        let label = tab.label();
        let pad = cell_w.saturating_sub(label.len());
        let lpad = pad / 2;
        let rpad = if has_div {
            pad.saturating_sub(lpad + 1)
        } else {
            pad - lpad
        };
        let text = format!("{}{}{}", " ".repeat(lpad), label, " ".repeat(rpad));
        let text_w = text.chars().count() as u16;

        let style = if tab == st.tab {
            Style::default()
                .bg(tab_color(tab))
                .fg(Color::Black)
                .add_modifier(Modifier::BOLD)
        } else {
            // Tabs of a subsystem with a problem get a colored label.
            let c = match tab.sub().map(|s| health(st, s)) {
                Some(Health::Err) => Color::Red,
                Some(Health::Warn) => Color::Yellow,
                _ => Color::White,
            };
            Style::default().fg(c)
        };
        f.render_widget(
            Paragraph::new(Span::styled(text, style)),
            Rect {
                x: cols[i].x,
                y: cols[i].y,
                width: text_w.min(cols[i].width),
                height: cols[i].height,
            },
        );
        if has_div {
            f.render_widget(
                Paragraph::new(Span::styled("│", dim())),
                Rect {
                    x: cols[i].x + cols[i].width - 1,
                    y: cols[i].y,
                    width: 1,
                    height: cols[i].height,
                },
            );
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Health {
    Ok,
    Warn,
    Err,
}

/// Overall health badge of a subsystem, shared by HOME cards and the tab bar.
pub fn health(st: &State, sub: Sub) -> Health {
    let s = &st.sim;
    match sub {
        Sub::Obc => {
            let hot = s
                .obc
                .ntc
                .iter()
                .cloned()
                .fold(0.0, f32::max)
                .max(s.obc.temp_onboard);
            if !s.obc.fsw_up || !s.obc.rs422_up {
                Health::Err
            } else if hot >= 60.0 {
                Health::Warn
            } else {
                Health::Ok
            }
        }
        Sub::Exp => {
            if !s.exp.link {
                Health::Err
            } else if !s.exp.faults.is_empty() {
                Health::Warn
            } else {
                Health::Ok
            }
        }
        Sub::Pdu => {
            if !s.pdu.online || s.pdu.mode == 3 {
                Health::Err
            } else if s.pdu.mode == 2 {
                Health::Warn
            } else {
                Health::Ok
            }
        }
    }
}

pub fn health_span(h: Health) -> Span<'static> {
    let (t, c) = match h {
        Health::Ok => (" ONLINE ", Color::Green),
        Health::Warn => (" WARNING ", Color::Yellow),
        Health::Err => (" FAULT ", Color::Red),
    };
    Span::styled(
        t,
        Style::default()
            .bg(c)
            .fg(Color::Black)
            .add_modifier(Modifier::BOLD),
    )
}

fn draw_status_line(f: &mut Frame, st: &State, area: Rect) {
    let (level, msg) = st.status_line();
    let max_w = (area.width as usize).saturating_sub(4);
    let text = if msg.chars().count() > max_w {
        msg.chars()
            .take(max_w.saturating_sub(1))
            .collect::<String>()
            + "…"
    } else {
        msg
    };
    f.render_widget(
        Paragraph::new(text)
            .style(Style::default().fg(level_color(level)))
            .block(Block::bordered()),
        area,
    );
}

fn draw_footer(f: &mut Frame, st: &State, area: Rect) {
    const BADGE_W: u16 = 12;
    let cols = Layout::horizontal([Constraint::Length(BADGE_W), Constraint::Min(0)]).split(area);
    let label = st.tab.label();
    let pad = (BADGE_W as usize).saturating_sub(label.len());
    let badge = format!(
        "{}{}{}",
        " ".repeat(pad / 2),
        label,
        " ".repeat(pad - pad / 2)
    );
    f.render_widget(
        Paragraph::new(Span::styled(
            badge,
            Style::default()
                .bg(tab_color(st.tab))
                .fg(Color::Black)
                .add_modifier(Modifier::BOLD),
        )),
        cols[0],
    );
    let hint = match st.tab {
        Tab::Cfg => " []/Tab/1-6  ·  < > section  ·  k/j select  ·  Enter edit/run  ·  PgUp/PgDn history  ·  q quit",
        Tab::Home => " []/Tab/1-6 switch tab  ·  k/j select  ·  h/l AUTO/MANUAL  ·  Enter or u UPDATE  ·  p pause sim  ·  q quit",
        Tab::Log => " []/Tab/1-6  ·  k/j scroll  ·  g/G oldest/newest  ·  c clear  ·  q quit",
        _ => " []/Tab  ·  , . page  ·  < > panel  ·  k/j select  ·  Enter edit/run  ·  f fault  ·  p pause  ·  q quit",
    };
    f.render_widget(Paragraph::new(hint).style(dim()), cols[1]);
}

fn draw_confirm(f: &mut Frame, area: Rect, sub: Sub, label: &str) {
    let w = 52.min(area.width);
    let h = 5.min(area.height);
    let r = Rect {
        x: area.x + (area.width - w) / 2,
        y: area.y + (area.height - h) / 2,
        width: w,
        height: h,
    };
    f.render_widget(Clear, r);
    let lines = vec![
        Line::from(Span::styled(
            format!("{}: {}", sub.name(), label),
            Style::default()
                .fg(Color::White)
                .add_modifier(Modifier::BOLD),
        )),
        Line::raw(""),
        Line::from(vec![
            txt("Press ", Color::Gray),
            txt("Y", Color::Red),
            txt(" to confirm, any other key cancels", Color::Gray),
        ]),
    ];
    let block = Block::bordered()
        .border_style(Style::default().fg(Color::Red))
        .title(Span::styled(
            " CONFIRM ",
            Style::default().fg(Color::Red).add_modifier(Modifier::BOLD),
        ));
    f.render_widget(
        Paragraph::new(lines)
            .alignment(Alignment::Center)
            .block(block),
        r,
    );
}
