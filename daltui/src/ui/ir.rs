use super::*;
use crate::sim::IrSetup;

fn settings(c: IrSetup) -> Vec<Line<'static>> {
    vec![
        kv1("Max duty", 11, format!("{} %", c.max_duty), Color::White),
        kv1("Duty", 11, format!("{} %", c.duty), Color::White),
        kv1("PWM", 11, format!("{} Hz", c.pwm_hz), Color::White),
        kv1(
            "On time",
            11,
            if c.on_ms == 0 {
                "continuous".into()
            } else {
                format!("{} ms", c.on_ms)
            },
            Color::White,
        ),
    ]
}

pub fn draw(f: &mut Frame, st: &State, area: Rect) {
    let cols = Layout::horizontal([
        Constraint::Percentage(30),
        Constraint::Percentage(30),
        Constraint::Percentage(40),
    ])
    .split(area);
    let e = &st.sim.exp;
    for i in 0..2 {
        let output = Layout::vertical([Constraint::Percentage(50), Constraint::Percentage(50)])
            .split(cols[0])[i];
        let draft = Layout::vertical([Constraint::Percentage(50), Constraint::Percentage(50)])
            .split(cols[1])[i];
        let control = Layout::vertical([Constraint::Percentage(50), Constraint::Percentage(50)])
            .split(cols[2])[i];
        let mut lines = vec![
            kv("12V_EXP", 11, vec![onoff(e.exp12v)]),
            kv(
                "Output",
                11,
                vec![if e.ir_status_seen[i] {
                    onoff(e.ir[i])
                } else {
                    txt("not read", Color::DarkGray)
                }],
            ),
            kv1(
                "Live duty",
                11,
                if e.ir_status_seen[i] {
                    format!("{} %", e.ir_actual_duty[i])
                } else {
                    "n/a".into()
                },
                Color::Green,
            ),
        ];
        if let Some(c) = e.ir_applied[i] {
            lines.extend(settings(c));
        } else {
            lines.push(Line::from(txt(
                " Read status to load EXP config",
                Color::Yellow,
            )));
        }
        panel(
            f,
            output,
            &format!("IR {i} OUTPUT / EXP"),
            Color::Green,
            lines,
        );
        panel(
            f,
            draft,
            &format!("IR {i} CONFIG DRAFT"),
            Color::Cyan,
            settings(e.ir_setup[i]),
        );
        controls::draw_ir(f, st, control, i);
    }
}
