//! Crystallization experiment: settings and live state on the left, commands on the right.
use super::*;

pub fn draw(f: &mut Frame, st: &State, area: Rect) {
    let cols =
        Layout::horizontal([Constraint::Percentage(56), Constraint::Percentage(44)]).split(area);
    let left = Layout::vertical([Constraint::Length(14), Constraint::Min(5)]).split(cols[0]);
    let e = &st.sim.exp;
    let state_color = if e.pc_error != "NONE" {
        Color::Red
    } else if e.pc_running {
        Color::Yellow
    } else {
        Color::Green
    };
    let lines = vec![
        kv1(
            "State",
            10,
            format!(
                "{}  |  activated {}",
                e.pc_state,
                if e.pc_activated { "YES" } else { "NO" }
            ),
            state_color,
        ),
        kv1(
            "Error",
            10,
            format!("{}  |  case {}", e.pc_error, ["both", "A", "B"][e.pc_case]),
            if e.pc_error == "NONE" {
                Color::White
            } else {
                Color::Red
            },
        ),
        kv1(
            "Mode",
            10,
            format!("{}  |  remain {}", e.pc_run_mode, e.pc_remaining),
            Color::White,
        ),
        kv1("Volume", 10, &e.pc_volume, Color::White),
        kv1(
            "Flow",
            10,
            format!(
                "{} (sensor {})",
                nv(e.flow[e.pc_flow_ch], 2, "uL/min"),
                e.pc_flow_ch
            ),
            Color::White,
        ),
        kv1(
            "Profile",
            10,
            format!(
                "{}  |  log {}",
                e.pc_profile_name,
                if e.pc_datalog { "ON" } else { "OFF" }
            ),
            Color::White,
        ),
        kv1(
            "U11 type",
            10,
            if e.u11.ptype_configured {
                ["lowdriver", "highdriver"][e.u11.ptype]
            } else {
                "unconfigured — set in Pump U11"
            },
            if e.u11.ptype_configured {
                Color::White
            } else {
                Color::Yellow
            },
        ),
        kv1(
            "Valves",
            10,
            (0..4)
                .map(|i| {
                    let dir = if e.pc_manual_valid & (1 << i) == 0 {
                        "?"
                    } else if e.pc_manual_actual & (1 << i) != 0 {
                        "B"
                    } else {
                        "A"
                    };
                    format!("V{}={dir}", i + 1)
                })
                .collect::<Vec<_>>()
                .join(" "),
            Color::Cyan,
        ),
        kv1(
            "U11",
            10,
            format!(
                "{}  {} Hz  {} Vpp",
                if e.u11.en { "ON" } else { "OFF" },
                e.u11.freq,
                e.u11.volt
            ),
            if e.u11.en {
                Color::Yellow
            } else {
                Color::White
            },
        ),
        kv1("Origin", 10, "MCU PSRAM · PCLG v3", Color::Gray),
        kv1("OBC folder", 10, "/home/bee/data/logs/pc/", Color::Cyan),
        kv1("Downloaded", 10, "not confirmed by FSW", Color::Yellow),
    ];
    panel(f, left[0], "PC SYSTEM STATUS", Color::LightGreen, lines);
    controls::draw_pc_settings(f, st, left[1]);
    controls::draw_pc_operations(f, st, cols[1]);
}
