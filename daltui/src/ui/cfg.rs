//! CFG tab: data-source selection, link status and the command/response history.

use super::*;
use crate::link::Mode;

pub const ROWS: [&str; 6] = [
    "Data source",
    "Console socket",
    "RS-422 target",
    "Poll interval",
    "Reconnect",
    "Clear history",
];

pub const PORT_KEYS: [&str; 9] = [
    "DAL_OBC_RS422_PORT",
    "DAL_EXP_UART_PORT",
    "DAL_EXP_UART_BAUD",
    "DAL_GUI_PDU_PORT",
    "DAL_GUI_PDU_UNIT",
    "DAL_EXP_CSP_RS485_PORT",
    "DAL_EXP_CSP_RS485_BAUD",
    "DAL_EXP_CSP_RS485_ADDRESS",
    "DAL_EXP_CSP_RS485_ENABLE",
];
const PORT_LABELS: [&str; 9] = [
    "Ground RS-422",
    "EXP UART CLI",
    "EXP UART baud",
    "PDU Modbus",
    "PDU unit ID",
    "EXP CSP RS-485",
    "EXP CSP baud",
    "EXP CSP address",
    "CSP RS-485 enabled",
];

pub fn draw(f: &mut Frame, st: &State, area: Rect) {
    if st.cfg_ports {
        return draw_ports(f, st, area);
    }
    let cols = Layout::horizontal([
        Constraint::Length(58.min(area.width / 2)),
        Constraint::Min(30),
    ])
    .split(area);
    let left = Layout::vertical([
        Constraint::Length(11),
        Constraint::Length(8),
        Constraint::Min(3),
    ])
    .split(cols[0]);
    draw_config(f, st, left[0]);
    draw_status(f, st, left[1]);
    draw_sources(f, st, left[2]);
    draw_history(f, st, cols[1]);
}

fn draw_ports(f: &mut Frame, st: &State, area: Rect) {
    let cols = Layout::horizontal([
        Constraint::Length(72.min(area.width.saturating_sub(30))),
        Constraint::Min(30),
    ])
    .split(area);
    let left = Layout::vertical([Constraint::Length(16), Constraint::Min(4)]).split(cols[0]);
    let mut lines = Vec::new();
    for i in 0..11 {
        let selected = st.cfg_port_sel == i;
        let (label, value) = match i {
            0..=8 => (
                PORT_LABELS[i],
                if selected {
                    st.cfg_edit
                        .as_deref()
                        .unwrap_or(&st.sim.live.port_values[i])
                } else {
                    &st.sim.live.port_values[i]
                },
            ),
            9 => ("Read settings", "press Enter"),
            _ => ("Restart FSW", "press Enter to apply saved settings"),
        };
        lines.push(Line::from(vec![
            txt(if selected { "▶ " } else { "  " }, Color::Green),
            txt(
                format!("{label:<20}"),
                if selected { Color::Green } else { Color::Gray },
            ),
            txt(
                value.to_string(),
                if selected && st.cfg_edit.is_some() {
                    Color::Green
                } else {
                    Color::Green
                },
            ),
        ]));
    }
    lines.push(Line::raw(""));
    lines.push(Line::from(txt(
        " Enter: edit/save  Ctrl-U: clear  </>/p/Esc: link settings",
        Color::DarkGray,
    )));
    control_panel(f, left[0], "Flight software serial ports", lines);
    let live = &st.sim.live;
    let state = if live.mode != Mode::Console {
        "Select CONSOLE on link settings"
    } else if !live.port_loaded {
        "Read settings to load saved configuration"
    } else if live.port_restart_pending {
        "Saved changes pending service restart"
    } else {
        "Saved settings loaded"
    };
    let info = vec![
        kv1(
            "Status",
            9,
            state,
            if live.mode == Mode::Console {
                Color::Green
            } else {
                Color::Red
            },
        ),
        Line::from(txt(
            " /etc/default/dal-obc-fsw; values apply at FSW startup.",
            Color::Gray,
        )),
        Line::from(txt(
            " PDU Modbus and EXP CSP RS-485 need separate ports.",
            Color::Gray,
        )),
        Line::from(txt(
            " Local console only; changing RS-422 may break remote access.",
            Color::Gray,
        )),
    ];
    panel(f, left[1], "Configuration notes", Color::Blue, info);
    draw_history(f, st, cols[1]);
}

fn draw_config(f: &mut Frame, st: &State, area: Rect) {
    let l = &st.sim.live;
    let mut lines = Vec::new();
    for (i, name) in ROWS.iter().enumerate() {
        let sel = i == st.cfg_sel;
        let editing = sel && st.cfg_edit.is_some();
        let value: Vec<Span> = match i {
            0 => Mode::ALL
                .iter()
                .flat_map(|&m| {
                    let on = m == l.mode;
                    let style = if on {
                        Style::default()
                            .bg(mode_color(m))
                            .fg(Color::Black)
                            .add_modifier(Modifier::BOLD)
                    } else {
                        dim()
                    };
                    [
                        Span::styled(format!(" {} ", m.label()), style),
                        Span::raw(" "),
                    ]
                })
                .collect(),
            1 | 2 => {
                let v = if editing {
                    format!("{}_", st.cfg_edit.as_deref().unwrap_or(""))
                } else if i == 1 {
                    l.console_path.clone()
                } else {
                    l.rs422_path.clone()
                };
                let c = if editing { Color::Green } else { Color::Green };
                vec![txt(v, c)]
            }
            3 => vec![txt(
                if editing {
                    format!("{}_ s", st.cfg_edit.as_deref().unwrap_or(""))
                } else {
                    format!("{:.1} s [ EDIT ]", l.poll_every)
                },
                Color::Green,
            )],
            _ => vec![txt("press Enter", Color::DarkGray)],
        };
        let mark = if sel {
            Span::styled("▶", Style::default().fg(Color::Green))
        } else {
            Span::raw(" ")
        };
        let mut spans = vec![
            mark,
            Span::styled(
                format!(" {name:<14} "),
                if sel {
                    Style::default()
                        .fg(Color::Green)
                        .add_modifier(Modifier::BOLD)
                } else {
                    lbl()
                },
            ),
        ];
        spans.extend(value);
        lines.push(Line::from(spans));
    }
    lines.push(Line::raw(""));
    let hint = match st.cfg_sel {
        0 => "Enter: cycle Sim / Console (text commands) / RS-422 (binary frames).",
        1 => "Enter to edit. Unix socket of the flight console (root only).",
        2 => "Enter to edit. rs422.sock (DAL_OBC_RS422_SOCKET) or a serial device, e.g. /dev/ttyUSB0.",
        3 => "Enter: edit 0.5–10 s. A new round starts only when the previous one is complete.",
        4 => "Closes the socket and opens it again (the flight RS-422 socket serves one client).",
        _ => "Empties the history list below.",
    };
    lines.push(Line::from(txt(format!(" {hint}"), Color::DarkGray)));
    lines.push(Line::from(txt(
        " < > or p: flight software port settings",
        Color::Cyan,
    )));
    control_panel(f, area, "Configuration", lines);
}

fn draw_status(f: &mut Frame, st: &State, area: Rect) {
    let l = &st.sim.live;
    let (state, c) = match (l.mode, l.connected) {
        (Mode::Sim, _) => ("simulation, no link", Color::Yellow),
        (_, true) => ("connected", Color::Green),
        (_, false) => ("NOT CONNECTED", Color::Red),
    };
    let proto = match l.mode {
        Mode::Sim => "-",
        Mode::Console => "text line `json <cmd>` -> JSON {type,addr,ack,code,text,payload_hex}",
        Mode::Rs422 => "A5 TYPE ADDR LEN CRC16 frames, one TM frame per TC",
    };
    let lines = vec![
        kv("State", 9, vec![txt(state, c)]),
        kv1(
            "Target",
            9,
            if l.mode == Mode::Sim {
                "-".to_string()
            } else {
                l.target().to_string()
            },
            Color::White,
        ),
        kv1("Protocol", 9, proto, Color::Gray),
        kv1(
            "Traffic",
            9,
            format!(
                "{} ok  {} failed  {} in flight",
                l.ok_count, l.err_count, l.pending
            ),
            Color::White,
        ),
        kv1(
            "Last error",
            9,
            if l.last_err.is_empty() {
                "-".to_string()
            } else {
                l.last_err.clone()
            },
            if l.last_err.is_empty() {
                Color::DarkGray
            } else {
                Color::Red
            },
        ),
        kv1(
            "gui_state",
            9,
            if l.mode != Mode::Console {
                "console only".to_string()
            } else if l.gui_state_age < 0.0 {
                "never".to_string()
            } else {
                format!("{:.0} s ago", l.gui_state_age)
            },
            Color::Gray,
        ),
    ];
    panel(f, area, "Link status", mode_color(l.mode), lines);
}

fn draw_sources(f: &mut Frame, st: &State, area: Rect) {
    let _ = st;
    let rows: [(&str, &str); 9] = [
        ("OBC temps, BME, PAC", "tm_obc_get 04/02 (64 B)"),
        ("OBC/EXP/PDU switches", "pwr_stat 01/04, pwr_en 01/03"),
        ("PDU VOUT, TRANS, inputs", "pwr_stat PDU, pdu_vout 01/02"),
        ("PDU temp, mode, TPS", "pwr_stat 01/04 (binary)"),
        ("Board online, versions", "sys_info 01/00 (96 B)"),
        ("Heaters, IR", "thm_stat 01/15, exp_run 02/0C"),
        ("Pumps U11/U15", "fld_stat 01/18, exp_run pump*"),
        ("DRV8910, motors", "rba_stat 01/1B, exp_run motor5_*"),
        ("USB camera capture", "uvc_ctl 01/11, uvc_stat 01/12"),
    ];
    let lines: Vec<Line> = rows
        .iter()
        .map(|(a, b)| {
            Line::from(vec![
                txt(format!(" {a:<24}"), Color::Cyan),
                txt(*b, Color::Gray),
            ])
        })
        .collect();
    panel(f, area, "Data sources", Color::Blue, lines);
}

fn draw_history(f: &mut Frame, st: &State, area: Rect) {
    let l = &st.sim.live;
    let rows = Layout::vertical([Constraint::Min(4), Constraint::Length(8)]).split(area);

    let h = rows[0].height.saturating_sub(2) as usize;
    let n = l.hist.len();
    let end = n.saturating_sub(st.cfg_scroll);
    let start = end.saturating_sub(h);
    let w = rows[0].width.saturating_sub(2) as usize;
    let lines: Vec<Line> = l
        .hist
        .iter()
        .skip(start)
        .take(end - start)
        .map(|t| {
            let c = if t.ok { Color::Green } else { Color::Red };
            let fixed = 9 + 3 + 6 + 7;
            let room = w.saturating_sub(fixed + 22);
            let text: String = format!("{} {}", t.cmd, t.summary)
                .chars()
                .take(room.max(10))
                .collect();
            Line::from(vec![
                txt(format!(" {} ", hms(t.clock)), Color::DarkGray),
                txt(if t.ok { "✓ " } else { "✗ " }, c),
                txt(format!("{:02X}/{:02X} ", t.ty, t.addr), Color::DarkGray),
                txt(format!("{:>5}ms ", t.ms), Color::DarkGray),
                txt(
                    format!("{:<8}", t.why.chars().take(8).collect::<String>()),
                    Color::Magenta,
                ),
                txt(text, if t.ok { Color::White } else { Color::Red }),
            ])
        })
        .collect();
    panel(
        f,
        rows[0],
        &format!("Command history ({n})  k/j scroll"),
        Color::Magenta,
        lines,
    );

    let detail = match l.hist.iter().rev().nth(st.cfg_scroll) {
        Some(t) => {
            let cut = |s: &str| -> String { s.chars().take(w.saturating_sub(7)).collect() };
            vec![
                kv1("Command", 8, t.cmd.clone(), Color::White),
                kv1("TC", 8, cut(&t.tx), Color::Yellow),
                kv1("TM", 8, cut(&t.rx), Color::Cyan),
                kv1(
                    "Decoded",
                    8,
                    cut(&t.summary),
                    if t.ok { Color::Green } else { Color::Red },
                ),
            ]
        }
        None => vec![txt(" no traffic yet", Color::DarkGray).into()],
    };
    panel(f, rows[1], "Selected transaction", Color::Blue, detail);
}

pub fn mode_color(m: Mode) -> Color {
    match m {
        Mode::Sim => Color::Yellow,
        Mode::Console => Color::Green,
        Mode::Rs422 => Color::Cyan,
    }
}
