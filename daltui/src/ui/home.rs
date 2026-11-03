//! HOME tab: three side-by-side subsystem cards.

use super::*;
use crate::sim::Sim;

pub fn draw(f: &mut Frame, st: &State, area: Rect) {
    let split = Layout::vertical([Constraint::Length(4), Constraint::Min(0)]).split(area);
    update_strip(f, st, split[0]);
    let area = split[1];
    let rects = if area.width >= 96 {
        Layout::horizontal([Constraint::Ratio(1, 3); 3]).split(area)
    } else {
        Layout::vertical([Constraint::Ratio(1, 3); 3]).split(area)
    };
    for (i, sub) in Sub::ALL.into_iter().enumerate() {
        card(f, st, sub, rects[i]);
    }
}

/// Update Housekeeping selector and the UPDATE button.
fn update_strip(f: &mut Frame, st: &State, area: Rect) {
    let l = &st.sim.live;
    let live = l.mode != crate::link::Mode::Sim;
    let mark = |row: usize| {
        if st.home_sel == row {
            Span::styled("▶", Style::default().fg(Color::Yellow))
        } else {
            Span::raw(" ")
        }
    };
    let name = |row: usize, t: &str| {
        Span::styled(
            format!(" {t:<20}"),
            if st.home_sel == row {
                Style::default()
                    .fg(Color::Yellow)
                    .add_modifier(Modifier::BOLD)
            } else {
                lbl()
            },
        )
    };
    let pick = |on: bool, t: &str| {
        let style = if on {
            Style::default()
                .bg(Color::Green)
                .fg(Color::Black)
                .add_modifier(Modifier::BOLD)
        } else {
            dim()
        };
        Span::styled(format!(" {t} "), style)
    };
    let mut row0 = vec![
        mark(0),
        name(0, "Update Housekeeping"),
        pick(l.auto && !l.control_only, "AUTO"),
        Span::raw(" "),
        pick(!l.auto && !l.control_only, "MANUAL"),
    ];
    row0.push(txt(
        if l.control_only {
            "   CONTROL ONLY: no automatic reads".into()
        } else if l.auto {
            format!("   every {:.1} s", l.poll_every)
        } else {
            "   nothing is sent until UPDATE".into()
        },
        Color::DarkGray,
    ));

    let button = if !live || l.auto || l.control_only {
        Span::styled(" UPDATE ", dim())
    } else if l.round.is_some() || l.update_req {
        Span::styled(
            " UPDATE ",
            Style::default().bg(Color::Yellow).fg(Color::Black),
        )
    } else {
        Span::styled(
            " UPDATE ",
            Style::default()
                .bg(Color::Cyan)
                .fg(Color::Black)
                .add_modifier(Modifier::BOLD),
        )
    };
    let status = if l.control_only && live {
        txt("operator commands only".to_string(), Color::DarkGray)
    } else if !live {
        txt("simulator: no flight link".to_string(), Color::DarkGray)
    } else if let Some(r) = &l.round {
        txt(
            format!("waiting for replies {}/{}", r.done, r.sent),
            Color::Yellow,
        )
    } else if l.update_req {
        txt("queued, waiting for the link".to_string(), Color::Yellow)
    } else if let Some(u) = &l.last_update {
        let c = if u.ok == u.sent {
            Color::Green
        } else {
            Color::Yellow
        };
        txt(
            format!(
                "last {}  {}/{} replies OK  {} ms",
                hms(u.clock),
                u.ok,
                u.sent,
                u.ms
            ),
            c,
        )
    } else {
        txt("no update yet".to_string(), Color::DarkGray)
    };
    let row1 = vec![
        mark(1),
        name(1, "Request now"),
        button,
        txt("   ".to_string(), Color::Gray),
        status,
    ];
    panel(
        f,
        area,
        "Housekeeping",
        Color::Green,
        vec![Line::from(row0), Line::from(row1)],
    );
}

fn card(f: &mut Frame, st: &State, sub: Sub, area: Rect) {
    let color = sub_color(sub);
    let s = &st.sim;
    let block = Block::bordered()
        .border_style(Style::default().fg(color))
        .title(Span::styled(
            format!(" {} ", sub.name()),
            Style::default().fg(color).add_modifier(Modifier::BOLD),
        ))
        .title(Line::from(health_span(health(st, sub))).right_aligned());
    let lines = match sub {
        Sub::Obc => obc(s),
        Sub::Exp => exp(s),
        Sub::Pdu => pdu(s),
    };
    f.render_widget(Paragraph::new(lines).block(block), area);
}

fn sw(label: &str, on: bool) -> Vec<Span<'static>> {
    vec![
        txt(format!("{label} "), Color::Gray),
        onoff(on),
        txt("  ", Color::Gray),
    ]
}

fn join(parts: Vec<Vec<Span<'static>>>) -> Vec<Span<'static>> {
    parts.into_iter().flatten().collect()
}

fn obc(s: &Sim) -> Vec<Line<'static>> {
    let o = &s.obc;
    let hot = o.ntc.iter().cloned().fold(0.0, f32::max);
    let usb = o.usb.iter().filter(|&&b| b).count();
    let mipi = o.mipi.iter().filter(|&&b| b).count();
    vec![
        kv(
            "RS-422",
            9,
            vec![
                if o.rs422_up {
                    txt("UP", Color::Green)
                } else {
                    txt("DOWN", Color::Red)
                },
                txt(format!("  rx {}", cnt(o.rx_frames)), Color::DarkGray),
            ],
        ),
        kv1("Uptime", 9, fmt_uptime(o.uptime), Color::White),
        kv1("Boots", 9, o.boot_count.to_string(), Color::White),
        bar_line("CPU", o.cpu_pct, 12, ""),
        bar_line("Memory", o.mem_pct, 12, ""),
        Line::raw(""),
        kv1(
            "Onboard",
            9,
            format!("{:.1} °C", o.temp_onboard),
            temp_color(o.temp_onboard, 50.0, 70.0),
        ),
        kv1(
            "NTC max",
            9,
            format!("{hot:.1} °C"),
            temp_color(hot, 50.0, 70.0),
        ),
        kv1(
            "BME",
            9,
            format!(
                "{:.1} °C {:.0} hPa {:.0}%",
                o.bme_t,
                o.bme_p_pa as f32 / 100.0,
                o.bme_rh
            ),
            Color::White,
        ),
        Line::raw(""),
        kv1(
            "Power",
            9,
            format!("{:.2} W", rails_total(&o.u6) + rails_total(&o.u2)),
            Color::White,
        ),
        Line::from(join(vec![
            vec![Span::raw(" ")],
            sw("5V_AI", o.ai_efuse),
            sw("FPGA", o.fpga_efuse),
        ])),
        kv1(
            "Cameras",
            9,
            format!("USB {usb}/4  MIPI {mipi}/2  LED {}%", o.led_pwm),
            Color::White,
        ),
    ]
}

fn exp(s: &Sim) -> Vec<Line<'static>> {
    let e = &s.exp;
    let hot = e.ntc.iter().cloned().fold(0.0, f32::max);
    let pc = if e.pc_running {
        format!("RUNNING {:.0}%", e.pc_progress)
    } else {
        "INACTIVE".into()
    };
    let mut l = vec![
        kv(
            "Link",
            9,
            vec![
                if e.link {
                    txt("ONLINE", Color::Green)
                } else {
                    txt("LOST", Color::Red)
                },
                txt(format!("  CAN node {}", e.node), Color::DarkGray),
            ],
        ),
        kv1(
            "Age",
            9,
            format!("{:.1} s", e.age),
            if e.age > 5.0 {
                Color::Red
            } else {
                Color::White
            },
        ),
        kv1("Uptime", 9, fmt_uptime(e.uptime), Color::White),
        kv1(
            "Faults",
            9,
            if e.faults.is_empty() {
                "none".into()
            } else {
                e.faults.join(" ")
            },
            if e.faults.is_empty() {
                Color::Green
            } else {
                Color::Red
            },
        ),
        Line::raw(""),
        Line::from(join(vec![
            vec![Span::raw(" ")],
            sw("SLN/HD", e.sln),
            sw("MOTOR", e.motor),
            sw("12V", e.exp12v),
        ])),
        kv1(
            "Power",
            9,
            format!("{:.2} W", rails_total(&e.pac)),
            Color::White,
        ),
        Line::raw(""),
        kv1(
            "NTC max",
            9,
            format!("{hot:.1} °C"),
            temp_color(hot, 45.0, 60.0),
        ),
        kv1(
            "Heaters",
            9,
            format!(
                "{} {} {} {} %",
                e.heater[0], e.heater[1], e.heater[2], e.heater[3]
            ),
            Color::White,
        ),
        kv1(
            "Pumps",
            9,
            format!(
                "U11 {}  U15 {}",
                if e.u11.en { "ON" } else { "off" },
                if e.u15.en { "ON" } else { "off" }
            ),
            Color::White,
        ),
        kv1(
            "Flow",
            9,
            format!(
                "{} / {}",
                nv(e.flow[0], 2, "mL/min"),
                nv(e.flow[1], 2, "mL/min")
            ),
            Color::White,
        ),
        kv1(
            "Motors",
            9,
            format!(
                "{:.0} / {:.0} st{}",
                e.motors[0].pos,
                e.motors[1].pos,
                if e.motors.iter().any(|m| m.running) {
                    "  RUN"
                } else {
                    ""
                }
            ),
            Color::White,
        ),
        kv1(
            "PC exp.",
            9,
            pc,
            if e.pc_running {
                Color::Green
            } else {
                Color::DarkGray
            },
        ),
    ];
    if e.burn_left > 0.0 {
        l.push(kv1("Burn", 9, "FIRING", Color::Red));
    }
    l
}

fn pdu(s: &Sim) -> Vec<Line<'static>> {
    let d = &s.pdu;
    let mode = ["NORMAL", "MANUAL", "WARNING", "ERROR"][d.mode.min(3)];
    let mode_c = [Color::Green, Color::Cyan, Color::Yellow, Color::Red][d.mode.min(3)];
    let mut l = vec![
        kv(
            "Link",
            9,
            vec![
                if d.online {
                    txt("ONLINE", Color::Green)
                } else {
                    txt("OFFLINE", Color::Red)
                },
                txt("  Modbus unit 1", Color::DarkGray),
            ],
        ),
        kv(
            "Mode",
            9,
            vec![Span::styled(
                mode,
                Style::default().fg(mode_c).add_modifier(Modifier::BOLD),
            )],
        ),
        kv1(
            "Temp",
            9,
            nv(d.temp, 1, "°C"),
            temp_color(d.temp, d.temp_limit as f32 - 15.0, d.temp_limit as f32),
        ),
        Line::raw(""),
        Line::from(join(vec![
            vec![Span::raw(" ")],
            sw("VOUT", d.vout_en),
            sw("TRANS", d.trans_en),
        ])),
        kv1("Setpoint", 9, format!("{} mV", d.vout_set), Color::White),
        Line::raw(""),
    ];
    for r in &d.rails {
        l.push(rail_line_c(r, 10));
    }
    l.push(Line::raw(""));
    l.push(kv1(
        "Faults",
        9,
        if d.latched { "LATCHED" } else { "none" },
        if d.latched { Color::Red } else { Color::Green },
    ));
    l.push(kv1("Last evt", 9, d.last_event.clone(), Color::White));
    l
}
