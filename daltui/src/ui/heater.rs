//! Manual heater and automatic temperature profile controls.
use super::*;

pub fn draw(f: &mut Frame, st: &State, area: Rect) {
    let cols = Layout::horizontal([
        Constraint::Percentage(31),
        Constraint::Percentage(32),
        Constraint::Percentage(37),
    ])
    .split(area);
    let e = &st.sim.exp;
    let mut lines = vec![
        kv1(
            "Source",
            8,
            match e.ht_status_at {
                Some(_) if e.ht_status_seen[0] => "RPARAM 3 fresh".to_string(),
                Some(at) => format!("RPARAM 3 stale {:.0}s", (st.sim.t - at).max(0.0)),
                None if st.sim.live.mode == crate::link::Mode::Sim => "simulation".to_string(),
                None => "RPARAM 3 waiting".to_string(),
            },
            if e.ht_status_seen[0] {
                Color::Cyan
            } else {
                Color::Yellow
            },
        ),
        kv1(
            "EXP 12V",
            8,
            if e.exp12v { "ON" } else { "OFF" },
            if e.exp12v {
                Color::Green
            } else {
                Color::Yellow
            },
        ),
        Line::raw(""),
    ];
    for i in 0..4 {
        let fresh = e.ht_status_seen[i];
        let known = fresh || e.ht_status_at.is_some();
        let phase = ["disabled", "soak", "error"]
            .get(e.ht_phase[i] as usize)
            .copied()
            .unwrap_or("unknown");
        let color = if !known || !fresh {
            Color::DarkGray
        } else if e.ht_phase[i] == 2 {
            Color::Red
        } else if e.ht_running[i] {
            Color::Green
        } else {
            Color::Yellow
        };
        let button = if !known {
            "[READ]"
        } else if e.ht_running[i] {
            "[STOP]"
        } else {
            "[START]"
        };
        lines.push(Line::from(txt(
            format!(
                "{} P{i}/H{i} {:<8} {button}",
                if i == e.ht_sel { '▶' } else { ' ' },
                if known { phase } else { "n/a" },
            ),
            color,
        )));
        if known {
            let temp = |v: f32| {
                if v.is_finite() {
                    format!("{v:.1}")
                } else {
                    "n/a".to_string()
                }
            };
            lines.push(Line::from(txt(
                format!(
                    "  NTC {}/{} -> {} °C",
                    temp(e.ht_main_temp[i]),
                    temp(e.ht_sec_temp[i]),
                    temp(e.ht_target_actual[i])
                ),
                if fresh { Color::White } else { Color::DarkGray },
            )));
            lines.push(Line::from(txt(
                format!(
                    "  Duty {} / {}%  limit {}%",
                    e.ht_applied[i], e.ht_requested[i], e.ht_limit[i]
                ),
                if fresh { Color::White } else { Color::DarkGray },
            )));
            let reason = [
                "none",
                "config",
                "sensor",
                "overtemp",
                "NTC delta",
                "power",
                "restored",
                "manual timeout",
            ]
            .get(e.ht_reason[i] as usize)
            .copied()
            .unwrap_or("unknown");
            lines.push(Line::from(txt(
                format!(
                    "  Config {} / fault {reason}",
                    if e.ht_configured[i] { "yes" } else { "no" }
                ),
                if e.ht_reason[i] == 0 {
                    Color::DarkGray
                } else {
                    Color::Red
                },
            )));
        } else {
            lines.push(Line::from(txt("  NTC / target: n/a", Color::DarkGray)));
            lines.push(Line::from(txt("  Duty / limit: n/a", Color::DarkGray)));
            lines.push(Line::from(txt("  Config / fault: n/a", Color::DarkGray)));
        }
        lines.push(Line::raw(""));
    }
    lines.push(Line::from(txt(
        "Use Profile 0-3 actions →",
        Color::DarkGray,
    )));
    panel(f, cols[0], "ALL HEATER PROFILES", Color::LightGreen, lines);
    controls::draw_heater_manual(f, st, cols[1]);
    controls::draw_heater_auto(f, st, cols[2]);
}
