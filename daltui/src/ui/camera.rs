//! OBC camera controls and capture results, split by camera interface.

use super::*;
use crate::controls::Sub;
use crate::sim::{Shot, ShotState};

pub fn draw(f: &mut Frame, st: &State, area: Rect) {
    let cols =
        Layout::horizontal([Constraint::Percentage(50), Constraint::Percentage(50)]).split(area);
    draw_side(f, st, cols[0], 0..7, "USB cameras", usb_status(st));
    draw_side(f, st, cols[1], 7..12, "MIPI cameras", mipi_status(st));
}

fn draw_side(
    f: &mut Frame,
    st: &State,
    area: Rect,
    range: std::ops::Range<usize>,
    title: &str,
    status: Vec<Line<'static>>,
) {
    let selected = st.sel[Sub::Obc.idx()][1];
    let panel_color = if range.contains(&selected) {
        Color::Cyan
    } else {
        Color::DarkGray
    };
    let block = Block::bordered()
        .border_style(Style::default().fg(panel_color))
        .title(Span::styled(
            format!(" {title} "),
            Style::default()
                .fg(panel_color)
                .add_modifier(Modifier::BOLD),
        ));
    let inner = block.inner(area);
    f.render_widget(block, area);
    if inner.height < 6 {
        return;
    }
    let parts = Layout::vertical([
        Constraint::Length(range.len() as u16 + 1),
        Constraint::Min(3),
        Constraint::Length(4),
    ])
    .split(inner);
    let ctrls = Sub::Obc.controls(1);
    let focus = Style::default()
        .fg(Color::Black)
        .bg(Color::Cyan)
        .add_modifier(Modifier::BOLD);
    let mut controls = Vec::new();
    for i in range.clone() {
        controls.push(super::controls::row(
            st,
            &ctrls[i],
            i == selected,
            parts[0].width as usize,
            focus,
        ));
    }
    f.render_widget(Paragraph::new(controls), parts[0]);
    f.render_widget(
        Paragraph::new(status)
            .wrap(Wrap { trim: false })
            .block(Block::default().borders(Borders::TOP).border_style(dim())),
        parts[1],
    );
    if range.contains(&selected) {
        let control = &ctrls[selected];
        let hint = if st.sim.live.mode == crate::link::Mode::Sim {
            None
        } else {
            crate::live::unwired_in(st.sim.live.mode, control.id)
        };
        f.render_widget(
            Paragraph::new(hint.unwrap_or(control.hint))
                .wrap(Wrap { trim: true })
                .style(dim())
                .block(Block::default().borders(Borders::TOP).border_style(dim())),
            parts[2],
        );
    }
}

fn shot_status(shot: &Shot) -> (&'static str, Color) {
    match shot.state {
        ShotState::Idle => ("idle", Color::DarkGray),
        ShotState::Capturing => ("capturing", Color::Yellow),
        ShotState::Saved => ("saved", Color::Green),
        ShotState::Failed => ("failed", Color::Red),
    }
}

fn usb_status(st: &State) -> Vec<Line<'static>> {
    let o = &st.sim.obc;
    let mut rows = vec![Line::from(txt(" Capture status", Color::Cyan))];
    for i in 0..4 {
        let (state, color) = shot_status(&o.usb_shot[i]);
        rows.push(Line::from(vec![
            txt(
                format!(
                    " {} USB {}: ",
                    if i == o.usb_sel { '▶' } else { ' ' },
                    i + 1
                ),
                Color::White,
            ),
            txt(
                if o.usb[i] { "ON  " } else { "OFF " },
                if o.usb[i] { Color::Green } else { Color::Red },
            ),
            txt(state, color),
        ]));
    }
    rows.push(Line::raw(""));
    rows.push(Line::from(txt(
        " JPEG storage: /home/bee/data/images/usb",
        Color::DarkGray,
    )));
    let shot = &o.usb_shot[o.usb_sel];
    if !shot.path.is_empty() {
        rows.push(Line::from(txt(
            format!(" Last file: {}", shot.path),
            Color::White,
        )));
    }
    if shot.bytes > 0 {
        rows.push(Line::from(txt(
            format!(" Size: {} bytes", shot.bytes),
            Color::White,
        )));
    }
    if !shot.note.is_empty() {
        rows.push(Line::from(txt(
            format!(" Result: {}", shot.note),
            Color::Gray,
        )));
    }
    rows
}

fn mipi_status(st: &State) -> Vec<Line<'static>> {
    let o = &st.sim.obc;
    let mut rows = vec![Line::from(txt(" Camera / sensor status", Color::Cyan))];
    for i in 0..2 {
        let sensor = o.mipi_sensor[i];
        let routed = o.mipi_route == Some((i, sensor));
        rows.push(Line::from(vec![
            txt(
                format!(
                    " {} MIPI {}: ",
                    if i == o.mipi_sel { '▶' } else { ' ' },
                    i + 1
                ),
                Color::White,
            ),
            txt(
                if o.mipi[i] { "ON  " } else { "OFF " },
                if o.mipi[i] { Color::Green } else { Color::Red },
            ),
            txt(
                format!("sensor {sensor}"),
                if routed { Color::Green } else { Color::Cyan },
            ),
        ]));
        let mut sensors = vec![txt("     sensors: ", Color::DarkGray)];
        for sn in 0..4 {
            let style = if o.mipi_route == Some((i, sn)) {
                Style::default().fg(Color::Black).bg(Color::Green)
            } else if sensor == sn {
                Style::default().fg(Color::Black).bg(Color::Cyan)
            } else {
                dim()
            };
            sensors.push(Span::styled(format!(" {sn} "), style));
            sensors.push(Span::raw(" "));
        }
        rows.push(Line::from(sensors));
    }
    rows.push(Line::raw(""));
    rows.push(Line::from(txt(
        " RAW + PNG: /home/bee/data/images/mipi",
        Color::DarkGray,
    )));
    let (state, color) = shot_status(&o.mipi_shot);
    rows.push(Line::from(vec![
        txt(" Last capture: ", Color::White),
        txt(state, color),
    ]));
    if !o.mipi_shot.path.is_empty() {
        rows.push(Line::from(txt(
            format!(" File: {}", o.mipi_shot.path),
            Color::White,
        )));
    }
    if o.mipi_shot.bytes > 0 {
        rows.push(Line::from(txt(
            format!(" Size: {} bytes", o.mipi_shot.bytes),
            Color::White,
        )));
    }
    if !o.mipi_shot.note.is_empty() {
        rows.push(Line::from(txt(
            format!(" Result: {}", o.mipi_shot.note),
            Color::Gray,
        )));
    }
    rows
}
