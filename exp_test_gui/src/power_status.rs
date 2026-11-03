//! Reported GPIO request state, not measured rail voltage.
#[derive(Default)]
pub struct State {
    pending: String,
    pub rails: [Option<bool>; 3], // SLN/HD, MOTOR, EXP
}
impl State {
    pub fn feed(&mut self, text: &str) {
        self.pending.push_str(text);
        while let Some(end) = self.pending.find('\n') {
            let line: String = self.pending.drain(..=end).collect();
            for (name, index) in [("SLN HD", 0), ("SLN_HD", 0), ("MOTOR", 1), ("12V_EXP", 2)] {
                for (action, state) in [("ON", true), ("OFF", false)] {
                    if line
                        .trim_end()
                        .ends_with(&format!("Power {name} {action}: OK"))
                    {
                        self.rails[index] = Some(state);
                    }
                }
            }
            for (i, name) in ["SLN HD", "MOTOR", "12V_EXP"].iter().enumerate() {
                if let Some((_, rest)) = line.split_once(&format!("Power {name} requested=")) {
                    self.rails[i] = match rest.split_whitespace().next() {
                        Some("ON") => Some(true),
                        Some("OFF") => Some(false),
                        _ => None,
                    };
                }
            }
        }
        if self.pending.len() > 8192 {
            self.pending.clear();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fragmented_reports_and_failed_commands() {
        let mut state = State::default();
        state.feed("Power MOTOR requested=O");
        assert_eq!(state.rails, [None; 3]);
        state.feed("N observed=UNKNOWN\nPower SLN HD requested=OFF observed=UNKNOWN\nPower 12V_EXP requested=ON observed=UNKNOWN\n");
        assert_eq!(state.rails, [Some(false), Some(true), Some(true)]);
        state.feed("Power MOTOR OFF: REFUSED\n");
        assert_eq!(state.rails[1], Some(true));
        state.feed("Power MOTOR requested=OFF observed=UNKNOWN\n");
        assert_eq!(state.rails[1], Some(false));
        state.feed("Power SLN_HD ON: OK\nPower MOTOR ON: OK\n");
        assert_eq!(state.rails, [Some(true), Some(true), Some(true)]);
    }
}
