//! Right-hand control list of a subsystem tab.

use super::*;
use crate::controls::{Control, Kind};
use crate::sim::Val;

pub fn draw(f: &mut Frame, st: &State, sub: Sub, area: Rect) {
    draw_filtered(
        f,
        st,
        sub,
        area,
        &format!("{} CONTROL", sub.name()),
        |_| true,
        false,
    );
}

pub fn draw_ir(f: &mut Frame, st: &State, area: Rect, channel: usize) {
    draw_filtered(
        f,
        st,
        Sub::Exp,
        area,
        &format!("IR {channel} CONTROL"),
        |c| crate::controls::ir_field(c.id).is_some_and(|(i, _)| i == channel),
        false,
    );
}

fn motor_mapping(c: &Control) -> bool {
    matches!(c.group, "Limit mapping" | "Limit polarity")
}

pub fn draw_motor_main(f: &mut Frame, st: &State, area: Rect) {
    draw_filtered(
        f,
        st,
        Sub::Exp,
        area,
        "MOTOR CONTROL",
        |c| !motor_mapping(c),
        false,
    );
}

pub fn draw_motor_mapping(f: &mut Frame, st: &State, area: Rect) {
    draw_filtered(f, st, Sub::Exp, area, "MOTOR MAPPING", motor_mapping, true);
}

pub fn draw_heater_manual(f: &mut Frame, st: &State, area: Rect) {
    draw_filtered(
        f,
        st,
        Sub::Exp,
        area,
        "HEATER MANUAL",
        |c| c.group == "Heater manual",
        false,
    );
}

pub fn draw_heater_auto(f: &mut Frame, st: &State, area: Rect) {
    draw_filtered(
        f,
        st,
        Sub::Exp,
        area,
        "HEATER PROFILE / AUTO",
        |c| c.group != "Heater manual",
        false,
    );
}

pub fn draw_pc_settings(f: &mut Frame, st: &State, area: Rect) {
    draw_filtered(
        f,
        st,
        Sub::Exp,
        area,
        "PC SETTINGS",
        |c| {
            !matches!(
                c.group,
                "PC operation" | "Manual valves" | "Manual pump U11"
            )
        },
        false,
    );
}

pub fn draw_pc_operations(f: &mut Frame, st: &State, area: Rect) {
    draw_filtered(
        f,
        st,
        Sub::Exp,
        area,
        "PC OPERATION",
        |c| {
            matches!(
                c.group,
                "PC operation" | "Manual valves" | "Manual pump U11"
            )
        },
        false,
    );
}

fn draw_filtered(
    f: &mut Frame,
    st: &State,
    sub: Sub,
    area: Rect,
    title: &str,
    include: impl Fn(&Control) -> bool,
    summary: bool,
) {
    draw_filtered_channel(f, st, sub, area, title, include, summary, None);
}

pub fn draw_exp_camera(f: &mut Frame, st: &State, area: Rect, channel: usize) {
    draw_filtered_channel(
        f,
        st,
        Sub::Exp,
        area,
        &format!("EXP IR {channel} CONTROL"),
        |c| c.id != "exp.cam_sel",
        false,
        Some(channel),
    );
}

pub fn draw_burn(f: &mut Frame, st: &State, area: Rect, wire: usize) {
    draw_filtered_channel(
        f,
        st,
        Sub::Exp,
        area,
        &format!("BURN {wire} CONTROL"),
        |c| c.id != "exp.burn_sel",
        false,
        Some(wire),
    );
}

fn draw_filtered_channel(
    f: &mut Frame,
    st: &State,
    sub: Sub,
    area: Rect,
    title: &str,
    include: impl Fn(&Control) -> bool,
    summary: bool,
    channel: Option<usize>,
) {
    let page = st.page[sub.idx()];
    let ctrls = sub.controls(page);
    let sel = if channel.is_some() {
        st.sel[sub.idx()][page].max(1)
    } else {
        st.sel[sub.idx()][page]
    };
    let active = include(&ctrls[sel])
        && channel.is_none_or(|i| {
            i == if page == 5 {
                st.sim.exp.burn_sel
            } else {
                st.sim.exp.cam_sel
            }
        });
    let active_color = control_color(active);
    let focus = control_focus_style();
    let block = Block::bordered()
        .border_style(Style::default().fg(active_color))
        .title(Span::styled(
            format!(" {title} "),
            Style::default()
                .fg(active_color)
                .add_modifier(Modifier::BOLD),
        ));
    let inner = block.inner(area);
    f.render_widget(block, area);
    if inner.height < 4 {
        return;
    }
    let hint_h = if title.starts_with("IR ") {
        1
    } else if title == "PC SETTINGS" {
        2
    } else {
        4
    };
    let parts = Layout::vertical([Constraint::Min(1), Constraint::Length(hint_h)]).split(inner);
    let w = parts[0].width as usize;

    let mut lines: Vec<Line> = Vec::new();
    if summary {
        let e = &st.sim.exp;
        lines.push(Line::from(Span::styled("─ Both motor maps ─", dim())));
        for i in 0..2 {
            let label = format!(
                "{}M{i} {}{}{} /{}{} H:{} {}",
                if i == e.map_motor { "▶" } else { " " },
                ["PMU", "PML"][e.motor_bank[i]],
                e.motor_min[i],
                ["L", "H"][e.motor_min_level[i]],
                e.motor_max[i],
                ["L", "H"][e.motor_max_level[i]],
                ["MIN", "MAX"][e.motor_home_end[i]],
                if e.motor_limits_verified[i] {
                    "mapped"
                } else {
                    "draft"
                }
            );
            lines.push(Line::from(txt(
                label,
                if e.motor_limits_verified[i] {
                    Color::Green
                } else {
                    Color::Yellow
                },
            )));
        }
        lines.push(Line::raw(""));
    }
    let mut sel_line = None;
    let mut group = "";
    for (i, c) in ctrls.iter().enumerate() {
        if !include(c) {
            continue;
        }
        if c.group != group && !title.starts_with("IR ") {
            group = c.group;
            let mode_panel = matches!(group, "MANUAL commands" | "OPERATION commands");
            let active = crate::live::motor_group_reason(&st.sim, group).is_none();
            let head = if mode_panel {
                format!(
                    "─ {group} [{}] ",
                    if active { "ACTIVE" } else { "INACTIVE" }
                )
            } else {
                format!("─ {group} ")
            };
            lines.push(Line::from(Span::styled(
                format!(
                    "{head}{}",
                    "─".repeat(w.saturating_sub(head.chars().count()))
                ),
                if mode_panel && active {
                    Style::default().fg(Color::Green)
                } else {
                    dim()
                },
            )));
            if group == "Heater profile" {
                lines.push(Line::from(Span::styled(
                    format!("  Heater channel: {} (fixed)", st.sim.exp.ht_sel),
                    dim(),
                )));
            }
        }
        if i == sel && active {
            sel_line = Some(lines.len());
        }
        let mut line = row_value(
            st,
            c,
            i == sel && active,
            w,
            focus,
            channel_value(st, c.id, channel),
        );
        let camera = channel.unwrap_or(st.sim.exp.cam_sel);
        if camera_buffer_redundant(c.id, st.sim.exp.camera[camera].buffer_enabled) {
            for span in &mut line.spans {
                span.style = span.style.fg(Color::DarkGray);
            }
        }
        lines.push(line);
    }

    let h = parts[0].height as usize;
    let off = if sel_line.is_some_and(|line| line + 2 > h) {
        (sel_line.unwrap() + 2 - h).min(lines.len().saturating_sub(h))
    } else {
        0
    };
    let shown: Vec<Line> = lines.into_iter().skip(off).take(h).collect();
    f.render_widget(Paragraph::new(shown), parts[0]);

    let hint = if active {
        let reason = if st.sim.live.mode != crate::link::Mode::Sim {
            crate::live::unwired_in(st.sim.live.mode, ctrls[sel].id)
        } else {
            None
        }
        .or_else(|| crate::live::motor_group_reason(&st.sim, ctrls[sel].group));
        reason.unwrap_or(ctrls[sel].hint)
    } else if summary {
        "Use < or > to focus motor mapping; then select inputs and active levels."
    } else {
        "Use < or > to focus this control panel."
    };
    f.render_widget(
        Paragraph::new(hint)
            .wrap(Wrap { trim: true })
            .style(dim())
            .block(Block::default().borders(Borders::TOP).border_style(dim())),
        parts[1],
    );
}

pub(super) fn row(
    st: &State,
    c: &Control,
    selected: bool,
    w: usize,
    focus: Style,
) -> Line<'static> {
    row_value(st, c, selected, w, focus, st.sim.value(c.id))
}

fn camera_buffer_redundant(id: &str, enabled: Option<bool>) -> bool {
    matches!(
        (id, enabled),
        ("exp.cam_buffer_on", Some(true)) | ("exp.cam_buffer_off", Some(false))
    )
}

fn channel_value(st: &State, id: &str, channel: Option<usize>) -> Val {
    let Some(i) = channel else {
        return st.sim.value(id);
    };
    let burn = st.sim.exp.burn_setup[i];
    match id {
        "exp.burn_duty" => return Val::Num(burn.duty),
        "exp.burn_time" => return Val::Num(burn.burn_time_ms),
        "exp.burn_max_total_current" => return Val::Num(burn.max_total_current_ma),
        _ => {}
    }
    let c = &st.sim.exp.camera[i];
    match id {
        "exp.cam_model" => Val::Idx(c.model),
        "exp.cam_addr" => Val::Num(c.address),
        "exp.cam_format" => Val::Idx(c.format),
        "exp.cam_hz" => Val::Idx(c.hz),
        "exp.cam_emiss" => Val::Num(c.emissivity),
        "exp.cam_x" => Val::Num(c.x),
        "exp.cam_y" => Val::Num(c.y),
        "exp.cam_delta" => Val::Num(c.delta),
        _ => st.sim.value(id),
    }
}

fn row_value(
    st: &State,
    c: &Control,
    selected: bool,
    w: usize,
    focus: Style,
    value: Val,
) -> Line<'static> {
    let na = st.sim.live.mode != crate::link::Mode::Sim
        && crate::live::unwired_in(st.sim.live.mode, c.id).is_some();
    let inactive = crate::live::motor_group_reason(&st.sim, c.group).is_some();
    let val: Vec<Span<'static>> = match (&c.kind, value) {
        _ if na => vec![txt("[ n/a ]", Color::DarkGray)],
        _ if inactive => vec![txt("[ mode ]", Color::DarkGray)],
        _ if c.id == "exp.pc_name" && st.sim.exp.pc_target != 0 => {
            vec![txt("[ n/a ]", Color::DarkGray)]
        }
        (Kind::Toggle, Val::Bool(b)) => {
            vec![if b {
                txt("[ ON  ]", Color::Green)
            } else {
                txt("[ OFF ]", Color::Green)
            }]
        }
        (Kind::Step { .. }, Val::Num(v)) if matches!(c.id, "exp.pc_minflow" | "exp.pc_volume") => {
            let unit = if c.id == "exp.pc_minflow" {
                "uL/min"
            } else {
                "uL"
            };
            vec![txt(
                format!("{}.{:03} {unit} [ EDIT ]", v / 1000, v % 1000),
                Color::Green,
            )]
        }
        (Kind::Step { .. }, Val::Num(v))
            if matches!(c.id, "exp.ht_target" | "exp.ht_delta" | "exp.ht_cutoff") =>
        {
            vec![txt(
                format!("{v}cC={:.2}°C [edit]", v as f32 / 100.0),
                Color::Green,
            )]
        }
        (Kind::Action, _) if c.id.starts_with("exp.ht_toggle") => {
            let i = (c.id.as_bytes()[c.id.len() - 1] - b'0') as usize;
            let (label, color) =
                if st.sim.exp.ht_status_at.is_none() && !st.sim.exp.ht_status_seen[i] {
                    ("[ READ ]", Color::DarkGray)
                } else if st.sim.exp.ht_running[i] {
                    (
                        "[ STOP ]",
                        if st.sim.exp.ht_status_seen[i] {
                            Color::Yellow
                        } else {
                            Color::DarkGray
                        },
                    )
                } else {
                    (
                        "[ START ]",
                        if st.sim.exp.ht_status_seen[i] {
                            Color::Green
                        } else {
                            Color::DarkGray
                        },
                    )
                };
            vec![txt(label, color)]
        }
        (Kind::Unsigned, Val::Unsigned(v)) => vec![txt(format!("{v} [ EDIT ]"), Color::Green)],
        (Kind::Step { unit, .. }, Val::Num(v)) => {
            vec![txt(format!("{v} {unit} [ EDIT ]"), Color::Green)]
        }
        (Kind::Choice(opts), Val::Idx(i)) => vec![txt(
            format!("{} [ SELECT ]", opts[i.min(opts.len() - 1)]),
            Color::Green,
        )],
        (Kind::Action, _) if c.id == "exp.pc_name" => vec![txt(
            format!("{} [ EDIT ]", st.sim.exp.pc_name_draft),
            Color::Green,
        )],
        (Kind::Action, _) if c.id == "obc.rtc_set" => vec![txt("[ EDIT ]", Color::Green)],
        (Kind::Action, _) => vec![txt("[ RUN ]", Color::Green)],
        (Kind::Danger, _) => vec![txt("[ ! ]", Color::Red)],
        (_, Val::None) if c.id.starts_with("exp.pc_") => vec![txt("[ n/a ]", Color::DarkGray)],
        _ => vec![],
    };
    let val_w: usize = val.iter().map(|s| s.content.chars().count()).sum();
    let label_w = w.saturating_sub(val_w + 3);
    let profile_index = crate::sim::pc_field_index(c.id)
        .filter(|i| crate::sim::pc_field_allowed(st.sim.exp.pc_target, *i));
    let dirty = profile_index.is_some_and(|i| st.sim.exp.pc_dirty[st.sim.exp.pc_target][i])
        || (c.id == "exp.pc_name" && st.sim.exp.pc_target == 0 && st.sim.exp.pc_name_dirty);
    let unknown = profile_index
        .is_some_and(|i| st.sim.exp.pc_applied[st.sim.exp.pc_target][i].is_none())
        || (c.id == "exp.pc_name"
            && st.sim.exp.pc_target == 0
            && st.sim.exp.pc_name_applied.is_none());
    let mark = if dirty {
        "*"
    } else if unknown {
        "?"
    } else {
        ""
    };
    let mut label: String = c
        .label
        .chars()
        .take(label_w.saturating_sub(mark.len()))
        .collect();
    label.push_str(mark);
    label = format!("{label:<label_w$}");

    let marker = if selected { "▶ " } else { "  " };
    let mut spans = vec![Span::styled(
        format!("{marker}{label} "),
        Style::default().fg(if na || inactive {
            Color::DarkGray
        } else {
            Color::White
        }),
    )];
    spans.extend(val);
    if selected {
        for s in spans.iter_mut() {
            s.style = s.style.patch(focus).fg(s.style.fg.unwrap_or(Color::Green));
        }
    }
    Line::from(spans)
}

#[cfg(test)]
mod camera_buffer_tests {
    use super::*;
    #[test]
    fn dims_only_the_action_matching_confirmed_buffer_state() {
        assert!(camera_buffer_redundant("exp.cam_buffer_on", Some(true)));
        assert!(!camera_buffer_redundant("exp.cam_buffer_off", Some(true)));
        assert!(camera_buffer_redundant("exp.cam_buffer_off", Some(false)));
        assert!(!camera_buffer_redundant("exp.cam_buffer_on", Some(false)));
        assert!(!camera_buffer_redundant("exp.cam_buffer_on", None));
        assert!(!camera_buffer_redundant("exp.cam_buffer_off", None));
    }
}
