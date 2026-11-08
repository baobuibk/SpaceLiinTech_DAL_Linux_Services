//! Telemetry panels of the OBC / EXP / PDU tabs.

use super::*;
use crate::sim::{civil, Sim};

type RailFn = fn(&Rail, usize) -> Line<'static>;

struct Panel {
    title: &'static str,
    lines: Vec<Line<'static>>,
}

fn p(title: &'static str, lines: Vec<Line<'static>>) -> Panel {
    Panel { title, lines }
}

pub fn draw(f: &mut Frame, st: &State, sub: Sub, area: Rect) {
    let two = area.width >= 70;
    // Two columns leave ~36 inner cells: drop the power column of rail rows.
    let rl: RailFn = if two { rail_line_c } else { rail_line };
    let page = st.page[sub.idx()];
    let (a, b) = match sub {
        Sub::Obc if page == 2 => (
            vec![p(
                "DATA TRANSFER STATUS",
                st.sim.transfer.lines().into_iter().map(Line::raw).collect(),
            )],
            vec![],
        ),
        Sub::Obc => obc(&st.sim, rl),
        Sub::Exp => exp(&st.sim, rl, page),
        Sub::Pdu => pdu(&st.sim, rl),
    };
    let color = sub_color(sub);
    if two && !b.is_empty() {
        let cols = Layout::horizontal([Constraint::Percentage(50), Constraint::Percentage(50)])
            .split(area);
        column(f, cols[0], a, color);
        column(f, cols[1], b, color);
    } else {
        let mut all = a;
        all.extend(b);
        column(f, area, all, color);
    }
}

fn column(f: &mut Frame, area: Rect, panels: Vec<Panel>, color: Color) {
    let mut cons: Vec<Constraint> = panels
        .iter()
        .map(|p| Constraint::Length(p.lines.len() as u16 + 2))
        .collect();
    cons.push(Constraint::Min(0));
    let rects = Layout::vertical(cons).split(area);
    for (i, pn) in panels.into_iter().enumerate() {
        panel(f, rects[i], pn.title, color, pn.lines);
    }
}

fn pa(pa: i32) -> String {
    format!("{:.1} hPa", pa as f32 / 100.0)
}

fn tline(label: &str, t: f32, warn: f32, crit: f32) -> Line<'static> {
    kv1(
        label,
        10,
        if t.is_nan() {
            "  n/a".to_string()
        } else {
            format!("{t:5.1} °C")
        },
        temp_color(t, warn, crit),
    )
}

fn yn(b: bool) -> Span<'static> {
    if b {
        txt("YES", Color::Green)
    } else {
        txt("no", Color::DarkGray)
    }
}

fn obc(s: &Sim, rail_line: RailFn) -> (Vec<Panel>, Vec<Panel>) {
    let o = &s.obc;
    let up = |b: bool| {
        if b {
            txt("UP", Color::Green)
        } else {
            txt("DOWN", Color::Red)
        }
    };
    let system = vec![
        kv(
            "RS-422",
            10,
            vec![up(o.rs422_up), txt("  ttyS1 921600 8N1", Color::DarkGray)],
        ),
        kv1(
            "Frames",
            10,
            format!("rx {}  tx {}", cnt(o.rx_frames), cnt(o.tx_frames)),
            Color::White,
        ),
        kv(
            "dal_obc_fsw",
            12,
            vec![if o.fsw_up {
                txt("RUNNING", Color::Green)
            } else {
                txt("STOPPED", Color::Red)
            }],
        ),
        kv1("Uptime", 10, fmt_uptime(o.uptime), Color::White),
        kv1("Boot count", 10, o.boot_count.to_string(), Color::White),
        kv1("RTC", 10, civil(s.rtc_now()), Color::White),
        kv1(
            "HK age",
            10,
            format!("{:.0} s  (obc_data.json)", o.snapshot_age),
            Color::White,
        ),
        bar_line("CPU", o.cpu_pct, 14, ""),
        bar_line("Memory", o.mem_pct, 14, ""),
        bar_line("Disk /", o.disk_pct, 14, ""),
    ];
    let devices = vec![
        kv("I2C0", 10, vec![txt("BME688 0x76  OK", Color::Green)]),
        kv("", 10, vec![txt("PAC1934 U6 0x1f  OK", Color::Green)]),
        kv("", 10, vec![txt("PAC1934 U2 0x10  OK", Color::Green)]),
        kv("RTC", 10, vec![txt("/dev/rtc2  OK", Color::Green)]),
        kv("CAN", 10, vec![txt("PC104 bus  OK", Color::Green)]),
        kv(
            "AI board",
            10,
            vec![if o.ai_efuse {
                txt("powered (5V_AI)", Color::Green)
            } else {
                txt("unpowered", Color::Red)
            }],
        ),
        kv(
            "FPGA",
            10,
            vec![if o.fpga_efuse {
                txt("powered (XC7A35T)", Color::Green)
            } else {
                txt("unpowered", Color::Red)
            }],
        ),
    ];
    let temps = vec![
        tline("Onboard", o.temp_onboard, 50.0, 70.0),
        tline("NTC1", o.ntc[0], 50.0, 70.0),
        tline("NTC2", o.ntc[1], 50.0, 70.0),
        tline("NTC3", o.ntc[2], 50.0, 70.0),
        tline("NTC4", o.ntc[3], 50.0, 70.0),
        kv1("BME temp", 10, nv(o.bme_t, 1, "°C"), Color::White),
        kv1("Pressure", 10, pa(o.bme_p_pa), Color::White),
        kv1("Humidity", 10, format!("{:.1} %RH", o.bme_rh), Color::White),
        kv1(
            "Gas",
            10,
            format!("{:.1} kΩ", o.bme_gas as f32 / 1000.0),
            Color::White,
        ),
    ];
    let mut rails: Vec<Line> = vec![Line::from(Span::styled(" PAC1934 U6", dim()))];
    rails.extend(o.u6.iter().map(|r| rail_line(r, 14)));
    rails.push(Line::from(Span::styled(" PAC1934 U2", dim())));
    rails.extend(o.u2.iter().map(|r| rail_line(r, 14)));
    let sum: f32 = rails_total(&o.u6) + rails_total(&o.u2);
    rails.push(kv1("Total", 14, format!("{sum:5.2} W"), Color::White));
    (
        vec![p("System", system), p("Devices", devices)],
        vec![
            p("Temperature / environment", temps),
            p("Power rails", rails),
        ],
    )
}

fn exp(s: &Sim, rail_line: RailFn, page: usize) -> (Vec<Panel>, Vec<Panel>) {
    let e = &s.exp;
    match page {
        1 => return exp_u11(s),
        2 => return exp_u15(s),
        3 => return exp_motors(s),
        4 => return exp_ir(s),
        5 => return exp_burn(s),
        8 => return exp_camera(s, 36),
        9 => return exp_sensors(s),
        _ => {}
    }
    let link = if e.link {
        txt("ONLINE", Color::Green)
    } else {
        txt("LOST", Color::Red)
    };
    let faults = if e.faults.is_empty() {
        txt("none", Color::Green)
    } else {
        txt(e.faults.join(" "), Color::Red)
    };
    let status = vec![
        kv(
            "Link",
            10,
            vec![link, txt(format!("  CAN node {}", e.node), Color::DarkGray)],
        ),
        kv1(
            "Sample age",
            10,
            format!("{:.1} s", e.age),
            if e.age > 5.0 {
                Color::Red
            } else {
                Color::White
            },
        ),
        kv1("Uptime", 10, fmt_uptime(e.uptime), Color::White),
        kv("Faults", 10, vec![faults]),
    ];
    let mut power: Vec<Line> = e.pac.iter().map(|r| rail_line(r, 10)).collect();
    power.push(kv1(
        "Total",
        10,
        format!("{:5.2} W", rails_total(&e.pac)),
        Color::White,
    ));
    let temps = vec![
        tline("NTC0", e.ntc[0], 45.0, 60.0),
        tline("NTC1", e.ntc[1], 45.0, 60.0),
        tline("NTC2", e.ntc[2], 45.0, 60.0),
        tline("NTC3", e.ntc[3], 45.0, 60.0),
        kv1("Board", 10, nv(e.board_t, 1, "°C"), Color::White),
        kv1(
            "BME688",
            10,
            if e.bme_t.is_nan() {
                "n/a".to_string()
            } else {
                format!(
                    "{:.1} °C {:.0} hPa {:.0}%",
                    e.bme_t,
                    e.bme_p_pa as f32 / 100.0,
                    e.bme_rh
                )
            },
            Color::White,
        ),
    ];

    let heaters: Vec<Line> = (0..4)
        .map(|i| {
            let on = e.heater[i] > 0 && e.exp12v;
            kv(
                &format!("Heater {i}"),
                9,
                vec![
                    txt(
                        gauge_bar(e.heater[i] as f32, 10),
                        if on { Color::LightRed } else { Color::DarkGray },
                    ),
                    txt(format!(" {:3}%", e.heater[i]), Color::White),
                ],
            )
        })
        .collect();
    let run = |b: bool| {
        if b {
            txt("RUNNING", Color::Green)
        } else {
            txt("stopped", Color::DarkGray)
        }
    };
    let pumps = vec![
        kv(
            "U11 pump",
            9,
            vec![
                run(e.u11.en && e.sln),
                txt(format!("  {}", nv(e.flow[0], 2, "mL/min")), Color::White),
            ],
        ),
        kv(
            "U15 pump",
            9,
            vec![
                run(e.u15.en && e.sln),
                txt(format!("  {}", nv(e.flow[1], 2, "mL/min")), Color::White),
            ],
        ),
        kv(
            "Bubble",
            9,
            (0..2)
                .map(|i| {
                    if e.bubble[i] {
                        txt(format!("in{i} BUBBLE  "), Color::Yellow)
                    } else {
                        txt(format!("in{i} clear   "), Color::DarkGray)
                    }
                })
                .collect(),
        ),
    ];
    let motor = |i: usize| {
        let m = &e.motors[i];
        kv(
            &format!("Motor {i}"),
            9,
            vec![
                txt(format!("{:8.0} st  ", m.pos), Color::White),
                if m.homing {
                    txt("HOMING", Color::Yellow)
                } else if m.running {
                    txt("RUNNING", Color::Yellow)
                } else {
                    txt("stopped", Color::DarkGray)
                },
            ],
        )
    };
    let burn = if e.burn_left > 0.0 {
        txt(format!("FIRING {:.1}s", e.burn_left), Color::Red)
    } else {
        txt(format!("idle (fired {}x)", e.burn_count), Color::DarkGray)
    };
    let act = vec![
        motor(0),
        motor(1),
        kv(
            "IR LED",
            9,
            vec![
                txt("0 ", Color::Gray),
                onoff(e.ir[0]),
                txt("   1 ", Color::Gray),
                onoff(e.ir[1]),
            ],
        ),
        kv("Burn wire", 9, vec![burn]),
    ];
    let state = if e.pc_running {
        txt("RUNNING", Color::Green)
    } else if e.pc_progress >= 100.0 {
        txt("DONE", Color::Cyan)
    } else {
        txt("INACTIVE", Color::DarkGray)
    };
    let step = (e.pc_progress / 100.0 * 7.0).ceil().clamp(0.0, 7.0) as u32;
    let pc = vec![
        kv(
            "State",
            9,
            vec![
                state,
                txt(format!("   runs {}", e.pc_runs), Color::DarkGray),
            ],
        ),
        bar_line("Progress", e.pc_progress, 8, &format!(" V{step}/7")),
    ];
    (
        vec![
            p("Link / status", status),
            p("Power (PAC1934)", power),
            p("Temperature", temps),
        ],
        vec![
            p("Heaters", heaters),
            p("Pumps / flow", pumps),
            p("Actuators", act),
            p("PC experiment", pc),
        ],
    )
}

pub fn draw_exp_camera(f: &mut Frame, st: &State, area: Rect) {
    let (mut cards, transfer) = exp_camera(&st.sim, area.width.saturating_sub(2) as usize);
    cards.extend(transfer);
    column(f, area, cards, STATUS_COLOR);
}

fn exp_camera(s: &Sim, content_width: usize) -> (Vec<Panel>, Vec<Panel>) {
    let e = &s.exp;
    let mut cards = Vec::new();
    for i in 0..2 {
        let c = &e.camera[i];
        let temperature =
            |v: Option<i32>| v.map_or("n/a".into(), |v| format!("{:.2} °C", v as f32 / 100.0));
        let saved = if c.saved_file.is_some() {
            "saved"
        } else if c.object_id > 0 && s.live.cam_download_wait[i].is_some() {
            "waiting for FSW"
        } else if c.object_id > 0 {
            "not confirmed"
        } else {
            "n/a"
        };
        let (monitor_status, monitor_color) = if c.monitor_enabled == Some(false) {
            if c.monitor_running {
                ("stopping...".into(), Color::Magenta)
            } else {
                ("stopped".into(), Color::White)
            }
        } else if c.address_restart_required {
            ("sensor restart needed".into(), Color::Yellow)
        } else if !c.initialized && (c.confirmed || c.monitor.is_some()) {
            ("don't init".into(), Color::Yellow)
        } else if c.buffer_enabled == Some(false) {
            ("I2C buffer disabled".into(), Color::Yellow)
        } else if let Some(m) = &c.monitor {
            if m.error != 0 {
                let reason = match m.error {
                    -1 => "I2C read failed",
                    -30 => "sensor timeout",
                    -34 => "frame changed",
                    -35 => "invalid temperature",
                    -40 => "incomplete subpages",
                    _ => "read failed",
                };
                (format!("{reason} ({})", m.error), Color::Red)
            } else if !m.valid || m.sequence == 0 {
                ("waiting for sample".into(), Color::Magenta)
            } else {
                ("OK / 10s".into(), Color::Green)
            }
        } else {
            ("not read".into(), Color::Yellow)
        };
        // Keep the last successfully read temperatures regardless of sample age
        // or a later failed attempt. The status above describes the interruption.
        let last_sample = c.monitor.as_ref().filter(|m| m.sequence > 0);
        let peak = c
            .peak
            .or_else(|| last_sample.map(|m| (m.max_centi, m.x, m.y)));
        let mean = c
            .frame_mean_centi
            .or_else(|| last_sample.map(|m| m.mean_centi));
        let lines = vec![
            kv1("Auto monitor", 16, monitor_status, monitor_color),
            kv1(
                "Max temperature",
                16,
                temperature(peak.map(|p| p.0)),
                Color::White,
            ),
            kv1("Average temp", 16, temperature(mean), Color::White),
            kv1(
                "Max position",
                16,
                peak.map_or("n/a".into(), |(_, x, y)| format!("({x}, {y})")),
                Color::White,
            ),
            kv1(
                "Sensor / address",
                16,
                if c.confirmed {
                    format!(
                        "{} / 0x{:02X}",
                        ["MLX90640", "MLX90642"][c.actual_model],
                        c.actual_address
                    )
                } else {
                    "n/a".into()
                },
                Color::White,
            ),
            kv1(
                "Format",
                16,
                if c.confirmed {
                    ["int16", "float32"][c.actual_format]
                } else {
                    "n/a"
                },
                Color::White,
            ),
            kv1(
                "Refresh rate",
                16,
                if c.confirmed {
                    format!("{} Hz", c.actual_hz)
                } else {
                    "n/a".into()
                },
                Color::White,
            ),
            kv1(
                "Ready / I2C",
                16,
                format!(
                    "{} / {}",
                    if c.initialized { "ready" } else { "not ready" },
                    match c.buffer_enabled {
                        Some(true) => "enabled",
                        Some(false) => "disabled",
                        None => "n/a",
                    }
                ),
                if c.initialized && c.buffer_enabled == Some(true) {
                    Color::Green
                } else {
                    Color::Yellow
                },
            ),
            kv1(
                "Operation",
                16,
                c.result.clone(),
                if c.result == "OK" {
                    Color::Green
                } else {
                    Color::Yellow
                },
            ),
            kv1(
                "Saved image",
                16,
                if c.object_id > 0 {
                    format!("#{} {saved}", c.object_id)
                } else {
                    saved.into()
                },
                if c.saved_file.is_some() {
                    Color::Green
                } else {
                    Color::White
                },
            ),
            kv1(
                "Point (x,y,d)",
                16,
                c.point_coords
                    .map_or("n/a".into(), |(x, y, d)| format!("({x}, {y}, {d})")),
                Color::White,
            ),
            kv1("Point center", 16, temperature(c.temp_centi), Color::White),
            kv1(
                "Point max / avg",
                16,
                format!(
                    "{} / {}",
                    temperature(c.point_max_centi),
                    temperature(c.mean_centi)
                ),
                Color::White,
            ),
        ];
        cards.push(p(
            if i == 0 {
                "EXP IR CAMERA 0"
            } else {
                "EXP IR CAMERA 1"
            },
            lines,
        ));
    }
    let mut transfer = vec![Line::from(txt("Folder: images/exp", Color::White))];
    for i in 0..2 {
        if let Some(path) = &s.exp.camera[i].saved_file {
            let name = path.rsplit('/').next().unwrap_or(path);
            let label = format!("C{i}: {name}");
            let chars: Vec<char> = label.chars().collect();
            for chunk in chars.chunks(content_width.max(1)) {
                transfer.push(Line::from(txt(
                    chunk.iter().collect::<String>(),
                    Color::Green,
                )));
            }
        } else {
            transfer.push(Line::from(txt(
                format!("Camera {i} saved file: n/a"),
                Color::White,
            )));
        }
    }
    (cards, vec![p("IMAGE TRANSFER", transfer)])
}

fn exp_ir(s: &Sim) -> (Vec<Panel>, Vec<Panel>) {
    let e = &s.exp;
    let selected = e.ir_sel;
    let c = e.ir_setup[selected];
    let outputs = vec![
        rail_state(e.exp12v, "EXP 12 V rail"),
        kv1("Selected", 12, format!("IR {selected}"), Color::Cyan),
        kv(
            "IR 0",
            12,
            vec![
                onoff(e.ir[0]),
                txt(format!("  {}%", e.ir_actual_duty[0]), Color::White),
            ],
        ),
        kv(
            "IR 1",
            12,
            vec![
                onoff(e.ir[1]),
                txt(format!("  {}%", e.ir_actual_duty[1]), Color::White),
            ],
        ),
        kv1("Source", 12, "thm_stat / IR status", Color::DarkGray),
    ];
    let limits = vec![
        kv1(
            "Status",
            12,
            if e.ir_config_pending[selected].is_some() {
                "applying…"
            } else if !e.ir_status_seen[selected] {
                "not read"
            } else if e.ir_configured[selected] {
                "configured"
            } else {
                "unconfigured"
            },
            if e.ir_configured[selected] {
                Color::Green
            } else {
                Color::Yellow
            },
        ),
        kv1("Requested", 12, format!("{} %", c.duty), Color::White),
        kv1("Max duty", 12, format!("{} %", c.max_duty), Color::White),
        kv1(
            "On time",
            12,
            if c.on_ms == 0 {
                "continuous".into()
            } else {
                format!("{} ms", c.on_ms)
            },
            Color::White,
        ),
        kv1("PWM", 12, format!("{} Hz", c.pwm_hz), Color::White),
        kv1("Limit store", 12, "RAM; lost on MCU reset", Color::DarkGray),
    ];
    (
        vec![p("IR outputs", outputs)],
        vec![p("Selected channel limits", limits)],
    )
}

pub fn draw_burn(f: &mut Frame, st: &State, area: Rect) {
    let (mut runtime, wires) = exp_burn(&st.sim);
    runtime.extend(wires);
    column(f, area, runtime, STATUS_COLOR);
}

fn exp_burn(s: &Sim) -> (Vec<Panel>, Vec<Panel>) {
    let e = &s.exp;
    let remaining = e
        .burn_armed_until
        .filter(|(_, until)| *until > s.t)
        .map(|(_, until)| format!("{:.1} s", until - s.t))
        .unwrap_or_else(|| "—".into());
    let runtime = vec![
        rail_state(e.motor, "MOTOR rail"),
        kv1(
            "State",
            12,
            e.burn_state.clone(),
            if e.burn_state == "FIRING" {
                Color::Red
            } else if e.burn_state == "ARMED" {
                Color::Yellow
            } else {
                Color::White
            },
        ),
        kv1(
            "Active wire",
            12,
            e.burn_active_wire.map_or("none".into(), |i| i.to_string()),
            Color::White,
        ),
        kv1("Arm left", 12, remaining, Color::Yellow),
        kv1(
            "Firing left",
            12,
            if e.burn_left > 0.0 {
                format!("{:.2} s", e.burn_left)
            } else {
                "—".into()
            },
            Color::White,
        ),
    ];
    let mut wires = Vec::new();
    for selected in 0..2 {
        let c = e.burn_setup[selected];
        let config = vec![
            kv1(
                "Status",
                12,
                if !e.burn_status_seen[selected] {
                    "not read"
                } else if e.burn_configured[selected] {
                    "configured"
                } else {
                    "incomplete"
                },
                if e.burn_configured[selected] {
                    Color::Green
                } else {
                    Color::Yellow
                },
            ),
            kv1("PWM duty", 12, format!("{}%", c.duty), Color::White),
            kv1(
                "Burn time",
                12,
                format!("{} ms", c.burn_time_ms),
                Color::White,
            ),
            kv1(
                "Max total I",
                12,
                format!("{} mA", c.max_total_current_ma),
                Color::White,
            ),
            kv1(
                "Last result",
                12,
                e.burn_last[selected].clone(),
                Color::White,
            ),
            kv1(
                "Attempts",
                12,
                e.burn_attempt[selected].to_string(),
                Color::White,
            ),
            kv1(
                "Peak delta",
                12,
                format!("{} mA", e.burn_peak_ma[selected]),
                Color::White,
            ),
        ];
        wires.push(p(
            if selected == 0 {
                "WIRE 0 STATUS"
            } else {
                "WIRE 1 STATUS"
            },
            config,
        ));
    }
    (vec![p("BURN RUNTIME", runtime)], wires)
}

/// `applied` value with the staged one in yellow while they differ.
fn staged(applied: i32, staged: i32, unit: &str) -> Vec<Span<'static>> {
    let mut v = vec![txt(format!("{applied} {unit}"), Color::White)];
    if applied != staged {
        v.push(txt(format!("  -> {staged}"), Color::Yellow));
    }
    v
}

fn rail_state(on: bool, name: &str) -> Line<'static> {
    kv(name, 12, vec![onoff(on)])
}

fn exp_u11(s: &Sim) -> (Vec<Panel>, Vec<Panel>) {
    let e = &s.exp;
    let u = &e.u11;
    let names = ["lowdriver", "highdriver"];
    let mut ty = vec![txt(
        if u.ptype_configured {
            names[u.ptype]
        } else {
            "unconfigured"
        },
        if u.ptype_configured {
            Color::White
        } else {
            Color::Yellow
        },
    )];
    if !u.ptype_configured || u.ptype != u.ptype_staged {
        ty.push(txt(
            format!("  -> {}", names[u.ptype_staged]),
            Color::Yellow,
        ));
    }
    let driver = vec![
        rail_state(e.sln, "SLN / HD rail"),
        kv("Fitted type", 12, ty),
        kv("Detected", 12, vec![yn(u.detected)]),
        kv("Initialized", 12, vec![yn(u.inited)]),
    ];
    let output = vec![
        kv("Frequency", 12, staged(u.freq, u.freq_staged, "Hz")),
        kv("Voltage", 12, staged(u.volt, u.volt_staged, "Vpp")),
        kv(
            "Output",
            12,
            vec![if u.en && e.sln {
                txt("ENABLED", Color::Green)
            } else {
                txt("disabled", Color::DarkGray)
            }],
        ),
        kv1("Flow 0", 12, nv(e.flow[0], 2, "mL/min"), Color::White),
        kv1("Note", 12, "values set, not measured", Color::DarkGray),
    ];
    (
        vec![p("U11 single pump: driver", driver)],
        vec![p("U11 single pump: output", output)],
    )
}

fn exp_u15(s: &Sim) -> (Vec<Panel>, Vec<Panel>) {
    let e = &s.exp;
    let u = &e.u15;
    let driver = vec![
        rail_state(e.sln, "SLN / HD rail"),
        kv(
            "Driver",
            12,
            vec![txt("U15 mp-Highdriver4 (fixed)", Color::White)],
        ),
        kv("Detected", 12, vec![yn(u.detected)]),
        kv("Initialized", 12, vec![yn(u.inited)]),
    ];
    let chans: Vec<Line> = (0..4)
        .map(|i| {
            let sel = i == u.ch;
            kv(
                &format!("{} Ch {i}", if sel { "▶" } else { " " }),
                7,
                vec![
                    txt(
                        gauge_bar(u.volt[i] as f32, 10),
                        if u.en && e.sln && u.volt[i] > 0 {
                            Color::Green
                        } else {
                            Color::DarkGray
                        },
                    ),
                    txt(format!(" {:3} Vpp", u.volt[i]), Color::White),
                ],
            )
        })
        .collect();
    let output = vec![
        kv("Frequency", 12, staged(u.freq, u.freq_staged, "Hz")),
        kv1(
            "Voltage Ch",
            12,
            format!("{}  staged {}", u.ch, u.volt_staged),
            if u.volt[u.ch] != u.volt_staged {
                Color::Yellow
            } else {
                Color::White
            },
        ),
        kv(
            "Output",
            12,
            vec![if u.en && e.sln {
                txt("ENABLED (all channels)", Color::Green)
            } else {
                txt("disabled", Color::DarkGray)
            }],
        ),
        kv1("Flow 1", 12, nv(e.flow[1], 2, "mL/min"), Color::White),
    ];
    (
        vec![
            p("U15 four-channel: driver", driver),
            p("Channel voltages", chans),
        ],
        vec![p("U15 four-channel: output", output)],
    )
}

fn exp_motors(s: &Sim) -> (Vec<Panel>, Vec<Panel>) {
    let e = &s.exp;
    let d = &e.drv;
    let driver = vec![
        rail_state(e.motor, "MOTOR rail"),
        kv("Detected", 12, vec![yn(d.detected)]),
        kv("Initialized", 12, vec![yn(d.inited)]),
        kv(
            "Fault",
            12,
            vec![if d.fault {
                txt("YES", Color::Red)
            } else {
                txt("none", Color::Green)
            }],
        ),
    ];
    let mut motors: Vec<Line> = Vec::new();
    for (i, m) in e.motors.iter().enumerate() {
        let sel = i == e.mc;
        let state = if m.homing {
            txt("HOMING", Color::Yellow)
        } else if m.holding {
            txt("HOLDING", Color::Yellow)
        } else if m.running {
            txt(
                format!(
                    "RUN {} {} st/s",
                    if m.dir > 0 { "cw" } else { "ccw" },
                    m.sps
                ),
                Color::Yellow,
            )
        } else {
            txt("stopped", Color::DarkGray)
        };
        motors.push(kv(
            &format!("{} Motor {i}", if sel { "▶" } else { " " }),
            9,
            vec![txt(format!("{:8.0} st  ", m.pos), Color::White), state],
        ));
    }
    motors.push(kv1(
        "Next jog",
        9,
        format!("{} {} st/s", ["cw", "ccw"][e.mdir], e.msps),
        Color::Gray,
    ));
    let i = e.mc;
    motors.push(kv1(
        "MCU mode",
        12,
        if e.motor_mode_verified[i] {
            ["MANUAL", "OPERATION"][e.motor_mode[i]]
        } else {
            "unknown — read mode"
        },
        if e.motor_mode_verified[i] {
            Color::Green
        } else {
            Color::Yellow
        },
    ));
    motors.push(kv1(
        "Mode draft",
        12,
        ["MANUAL", "OPERATION"][e.motor_mode_draft[i]],
        Color::White,
    ));
    motors.push(kv1(
        "Next move",
        12,
        format!(
            "{} {} steps @ {} st/s",
            ["cw", "ccw"][e.mdir],
            e.move_steps[i],
            e.msps
        ),
        Color::Gray,
    ));
    motors.push(kv1(
        "Limit map",
        12,
        if e.motor_limits_verified[i] {
            "confirmed"
        } else {
            "not confirmed"
        },
        if e.motor_limits_verified[i] {
            Color::Green
        } else {
            Color::Yellow
        },
    ));
    motors.push(kv1(
        "Max speed",
        12,
        format!("{} st/s", e.motor_max_sps[i]),
        Color::White,
    ));
    motors.push(kv1(
        "Duty cycle",
        12,
        format!("{} %", e.motor_drive_applied_pct[i]),
        Color::White,
    ));
    motors.push(kv1(
        "PWM",
        12,
        format!("{} Hz", [80, 100, 200, 2000][e.motor_pwm[i]]),
        Color::White,
    ));
    motors.push(kv1(
        "MIN input",
        12,
        format!(
            "{}{} active {}",
            ["PMU", "PML"][e.motor_bank[i]],
            e.motor_min[i],
            ["LOW", "HIGH"][e.motor_min_level[i]]
        ),
        Color::White,
    ));
    motors.push(kv1(
        "MAX input",
        12,
        format!(
            "{}{} active {}",
            ["PMU", "PML"][e.motor_bank[i]],
            e.motor_max[i],
            ["LOW", "HIGH"][e.motor_max_level[i]]
        ),
        Color::White,
    ));
    motors.push(kv1(
        "HOME end",
        12,
        ["MIN", "MAX"][e.motor_home_end[i]],
        Color::Yellow,
    ));
    (
        vec![p("DRV8910 driver", driver)],
        vec![p("Motors / setting draft", motors)],
    )
}

fn pdu(s: &Sim, rail_line: RailFn) -> (Vec<Panel>, Vec<Panel>) {
    let d = &s.pdu;
    let live = s.live.mode != crate::link::Mode::Sim;
    let valid = if live {
        d.status.map_or(0, |x| x.valid)
    } else {
        127
    };
    let tps = if live {
        d.status.map_or(0, |x| x.tps)
    } else if d.trans_en {
        3
    } else {
        1
    };
    let flags = if live {
        d.status.map_or(0, |x| x.flags)
    } else {
        (d.led_state as u16)
            | (d.trans_en as u16) << 1
            | (d.trans_fault as u16) << 2
            | (d.mcu_fault as u16) << 3
            | (d.latched as u16) << 4
    };
    let tps_label = [
        "UNKNOWN",
        "NOT POWERED — TRANS OFF",
        "STARTING",
        "READY",
        "COMM ERROR",
    ][tps.min(4) as usize];
    let mode = ["NORMAL", "MANUAL", "WARNING", "ERROR"][d.mode.min(3)];
    let mode_c = [Color::Green, Color::Cyan, Color::Yellow, Color::Red][d.mode.min(3)];
    let status = vec![
        kv(
            "Link",
            11,
            vec![
                if if live { valid != 0 } else { d.online } {
                    txt("ONLINE", Color::Green)
                } else {
                    txt("OFFLINE", Color::Red)
                },
                txt("  Modbus via OBC", Color::DarkGray),
            ],
        ),
        kv(
            "Mode",
            11,
            vec![Span::styled(
                if valid & 2 != 0 { mode } else { "N/A" },
                Style::default().fg(mode_c).add_modifier(Modifier::BOLD),
            )],
        ),
        kv1(
            "Status flags",
            12,
            if valid & 1 != 0 {
                format!("0x{flags:04X}")
            } else {
                "N/A".into()
            },
            Color::White,
        ),
        kv1(
            "TPS55287",
            11,
            tps_label,
            if tps == 4 {
                Color::Red
            } else if tps == 2 {
                Color::Magenta
            } else {
                Color::White
            },
        ),
        kv1(
            "Board temp",
            11,
            if d.temp.is_nan() {
                "n/a".to_string()
            } else {
                format!("{:.1} °C", d.temp)
            },
            temp_color(d.temp, d.temp_limit as f32 - 15.0, d.temp_limit as f32),
        ),
        kv1(
            "Setpoint",
            11,
            if valid & 32 != 0 {
                format!("{} mV", d.vout_set)
            } else {
                "N/A".into()
            },
            Color::White,
        ),
        kv1(
            "Boot",
            11,
            if live {
                "N/A".into()
            } else {
                format!("#{}  {}", d.boot_count, d.boot_cause)
            },
            Color::White,
        ),
        kv1("OBC clock", 11, civil(s.rtc_now()), Color::White),
        kv1(
            "Poll age",
            11,
            if d.poll_age.is_finite() {
                format!("{:.1} s", d.poll_age)
            } else {
                "N/A".into()
            },
            Color::White,
        ),
    ];
    let mut rails: Vec<Line> = d
        .rails
        .iter()
        .enumerate()
        .map(|(i, r)| {
            if !live {
                return rail_line(r, 11);
            }
            let v = d.power_valid[i];
            kv1(
                r.name,
                11,
                format!(
                    "{}   {}",
                    if v[0] {
                        format!("{:.2} V", r.mv as f32 / 1000.0)
                    } else {
                        "N/A V".into()
                    },
                    if v[1] {
                        format!("{} mA", r.ma)
                    } else {
                        "N/A mA".into()
                    }
                ),
                Color::White,
            )
        })
        .collect();
    rails.push(kv1(
        "Total out",
        11,
        if live && !(d.power_valid[0].iter().all(|x| *x) && d.power_valid[2].iter().all(|x| *x)) {
            "N/A".into()
        } else {
            format!(
                "{:5.2} W",
                (d.rails[0].mw() + d.rails[2].mw() + d.rails[3].mw()) as f32 / 1000.0
            )
        },
        Color::White,
    ));
    let flag = |label: &str, on: bool, bad: bool, known: bool| {
        kv(
            label,
            18,
            vec![if !known {
                txt("N/A", Color::White)
            } else if on {
                txt("YES", if bad { Color::Red } else { Color::Green })
            } else {
                txt("no", Color::DarkGray)
            }],
        )
    };
    let faults = vec![
        flag("TRANS eFuse fault", d.trans_fault, true, valid & 1 != 0),
        flag("MCU eFuse fault", d.mcu_fault, true, valid & 1 != 0),
        flag("Latched fault", d.latched, true, valid & 1 != 0),
        flag(
            "Heartbeat output",
            d.led_state && d.heartbeat,
            false,
            valid & 1 != 0,
        ),
        flag("TRANS output", d.trans_en, false, valid & 8 != 0),
        flag("VOUT output", d.vout_en, false, valid & 16 != 0),
    ];
    let event = vec![
        kv1("Sequence", 11, cnt(d.event_seq), Color::White),
        kv1(
            "Last event",
            11,
            d.last_event.clone(),
            if d.last_event.contains("CURRENT") || d.last_event == "TEMP" {
                Color::Yellow
            } else {
                Color::White
            },
        ),
    ];
    (
        vec![p("Status", status), p("Rails", rails)],
        vec![p("Faults / outputs", faults), p("Events", event)],
    )
}

#[cfg(test)]
mod monitor_tests {
    use super::*;
    use crate::sim::CameraMonitor;

    #[test]
    fn last_temperature_remains_visible_with_direct_interruption_status() {
        let mut sim = Sim::new();
        sim.exp.camera[0].initialized = true;
        sim.exp.camera[0].monitor = Some(CameraMonitor {
            valid: true,
            error: 0,
            sequence: 1,
            max_centi: 3210,
            mean_centi: 2500,
            x: 31,
            y: 23,
        });
        let rows = |sim: &Sim| {
            exp_camera(sim, 40).0[0]
                .lines
                .iter()
                .take(3)
                .map(|line| {
                    line.spans
                        .iter()
                        .map(|span| span.content.as_ref())
                        .collect::<String>()
                })
                .collect::<Vec<_>>()
        };
        let fresh = rows(&sim);
        assert!(fresh[0].contains("OK"));
        assert!(fresh[1].contains("32.10"));
        assert!(fresh[2].contains("25.00"));
        sim.t = 7.0;
        let stale = rows(&sim);
        assert!(stale[0].contains("OK"));
        assert!(stale[1].contains("32.10") && stale[2].contains("25.00"));
        let m = sim.exp.camera[0].monitor.as_mut().unwrap();
        m.valid = false;
        m.error = -12;
        let failed = rows(&sim);
        assert!(failed[0].contains("read failed (-12)"));
        assert!(failed[1].contains("32.10"));
        sim.exp.camera[0].buffer_enabled = Some(false);
        assert!(rows(&sim)[0].contains("I2C buffer disabled"));
        sim.exp.camera[0].initialized = false;
        assert!(rows(&sim)[0].contains("don't init"));
        assert!(rows(&sim)[1].contains("32.10"));
        sim.exp.camera[0].monitor_enabled = Some(false);
        sim.exp.camera[0].monitor_running = true;
        assert!(rows(&sim)[0].contains("stopping..."));
        sim.exp.camera[0].monitor_running = false;
        assert!(rows(&sim)[0].contains("stopped"));
        assert!(rows(&sim)[1].contains("32.10"));
        sim.exp.camera[0].monitor.as_mut().unwrap().sequence = 0;
        assert!(rows(&sim)[1].contains("n/a"));
    }
}

fn exp_sensors(s: &Sim) -> (Vec<Panel>, Vec<Panel>) {
    let e = &s.exp;
    let mut bme = vec![];
    let mut imu = vec![];
    if let Some((d, valid)) = e.sensor_actual {
        let os = ["skip", "1x", "2x", "4x", "8x", "16x"];
        bme.push(kv1(
            "Config",
            12,
            if valid[0] {
                "OK / applied"
            } else {
                "not available"
            },
            Color::White,
        ));
        imu.push(kv1(
            "Config",
            12,
            if valid[1] {
                "OK / applied"
            } else {
                "not available"
            },
            Color::White,
        ));
        if valid[0] {
            bme.push(kv1(
                "T / P / H",
                12,
                format!(
                    "{} / {} / {}",
                    os[d[0] as usize], os[d[1] as usize], os[d[2] as usize]
                ),
                Color::White,
            ));
            bme.push(kv1(
                "Filter",
                12,
                ["off", "1", "3", "7", "15", "31", "63", "127"][d[3] as usize],
                Color::White,
            ));
        }
        if valid[1] {
            let rates = ["off", "12.5", "26", "52", "104", "208", "416", "833"];
            imu.push(kv1(
                "Accel",
                12,
                format!(
                    "{} Hz / {}",
                    rates[d[4] as usize],
                    ["2g", "16g", "4g", "8g"][d[5] as usize]
                ),
                Color::White,
            ));
            imu.push(kv1(
                "Gyro",
                12,
                format!(
                    "{} Hz / {} dps",
                    rates[d[6] as usize],
                    [125, 250, 500, 1000, 2000][d[7] as usize]
                ),
                Color::White,
            ));
            imu.push(kv1(
                "Free-fall",
                12,
                format!("threshold {} / duration {}", d[8], d[9]),
                Color::White,
            ));
            imu.push(kv1(
                "Wake-up",
                12,
                format!("threshold {} / duration {}", d[10], d[11]),
                Color::White,
            ));
        }
    } else {
        bme.push(kv1("Config", 12, "Read both configs", Color::Magenta));
        imu.push(kv1("Config", 12, "Read both configs", Color::Magenta));
    }
    for (i, lines) in [(0, &mut bme), (1, &mut imu)] {
        if let Some(values) = &e.sensor_readings[i] {
            let labels: &[&str] = if i == 0 {
                &["Temperature", "Pressure", "Humidity"]
            } else {
                &[
                    "Temperature",
                    "Accel X",
                    "Accel Y",
                    "Accel Z",
                    "Gyro X",
                    "Gyro Y",
                    "Gyro Z",
                ]
            };
            for (j, value) in values.iter().enumerate() {
                let text = match value {
                    None => "unavailable".into(),
                    Some(v) if j == 0 => format!("{:.2} C", *v as f32 / 100.0),
                    Some(v) if i == 0 && j == 1 => format!("{} Pa", *v as i32 * 10),
                    Some(v) if i == 0 => format!("{:.2} %", *v as f32 / 100.0),
                    Some(v) => format!("{v} {}", if j < 4 { "mg" } else { "dps" }),
                };
                lines.push(kv1(labels[j], 12, text, Color::White));
            }
        } else {
            lines.push(kv1(
                "Measurements",
                12,
                "Read sensor status",
                Color::Magenta,
            ));
        }
    }
    (
        vec![
            p("EXP BME CONFIG / STATUS", bme),
            p("EXP IMU CONFIG / STATUS", imu),
        ],
        vec![],
    )
}

#[cfg(test)]
mod pdu_validity_tests {
    use super::*;
    #[test]
    fn unpowered_tps_and_missing_measurements_are_not_reported_as_zero() {
        let mut s = Sim::new();
        s.live.mode = crate::link::Mode::Rs422;
        s.pdu.status = Some(crate::proto::PduStatus {
            valid: 15,
            flags: 0,
            mode: 0,
            tps: 1,
            temp: 251,
            age_ms: 0,
            setpoint: 65535,
        });
        s.pdu.temp = 25.1;
        s.pdu.power_valid = [[false; 2]; 4];
        let (a, b) = pdu(&s, rail_line);
        let lines: Vec<String> = a
            .into_iter()
            .chain(b)
            .flat_map(|p| p.lines)
            .map(|l| {
                l.spans
                    .iter()
                    .map(|s| s.content.as_ref())
                    .collect::<String>()
            })
            .collect();
        let text = lines.join("\n");
        assert!(text.contains("NOT POWERED — TRANS OFF"));
        assert!(text.contains("25.1 °C"));
        assert!(text.contains("N/A V"));
        assert!(!text.contains("0.00 V"));
        let vout = lines.iter().find(|l| l.contains("VOUT output")).unwrap();
        assert!(vout.contains("N/A"));
    }
}
