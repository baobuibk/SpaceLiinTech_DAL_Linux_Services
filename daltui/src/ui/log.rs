//! Event / command log tab.

use super::*;

pub fn draw(f: &mut Frame, st: &State, area: Rect) {
    let block = Block::bordered().title(Span::styled(
        format!(" Event log ({}) ", st.log.len()),
        Style::default()
            .fg(Color::Magenta)
            .add_modifier(Modifier::BOLD),
    ));
    let inner = block.inner(area);
    f.render_widget(block, area);

    let h = inner.height as usize;
    let end = st.log.len().saturating_sub(st.log_scroll);
    let start = end.saturating_sub(h);
    let lines: Vec<Line> = st.log[start..end]
        .iter()
        .map(|l| {
            Line::from(vec![
                Span::styled(format!(" {} ", l.clock), dim()),
                Span::styled(
                    format!("{} ", l.sub.name()),
                    Style::default()
                        .fg(sub_color(l.sub))
                        .add_modifier(Modifier::BOLD),
                ),
                Span::styled(l.text.clone(), Style::default().fg(level_color(l.level))),
            ])
        })
        .collect();
    f.render_widget(Paragraph::new(lines), inner);
}
