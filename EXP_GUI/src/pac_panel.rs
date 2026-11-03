use std::time::{Duration, Instant};

pub const CHANNELS: [&str; 4] = ["12V_MOTOR", "12V_EXP", "5V_EXP", "5V_SLN_HD"];

#[derive(Default)]
pub struct State {
    pending: String,
    pub detection: &'static str,
    pub message: String,
    pub samples: [Option<[f64; 3]>; 4],
    pub snapshot: String,
    deadline: Option<Instant>,
    ticket: Option<u32>,
    next_poll: Option<Instant>,
}
impl State {
    pub fn detect(&mut self) {
        self.detection = "Detecting...";
        self.message.clear();
        self.ticket = None;
        self.deadline = Some(Instant::now() + Duration::from_secs(15));
        self.next_poll = None;
    }
    pub fn refresh(&mut self) {
        self.samples = [None; 4];
        self.snapshot = "Waiting for readings...".into();
    }
    pub fn poll(&mut self) -> Option<String> {
        let now = Instant::now();
        if self.deadline.is_some_and(|end| now >= end) {
            self.detection = "Unavailable";
            self.message = "Detection timed out".into();
            self.deadline = None;
            return None;
        }
        if self.deadline.is_some() && self.next_poll.is_some_and(|at| now >= at) {
            self.next_poll = Some(now + Duration::from_millis(500));
            return self.ticket.map(|id| format!("pac1934_result {id}"));
        }
        None
    }
    pub fn feed(&mut self, text: &str) {
        self.pending.push_str(text);
        while let Some(end) = self.pending.find('\n') {
            let line: String = self.pending.drain(..=end).collect();
            self.line(line.trim());
        }
        if self.pending.len() > 8192 {
            self.pending.clear();
        }
    }
    fn line(&mut self, line: &str) {
        if let Some(start) = line.find("PAC1934 ") {
            let text = &line[start + 8..];
            let mut words = text.split_whitespace();
            let result = words.next().unwrap_or("");
            if result == "snapshot" {
                self.snapshot = text.into();
                self.samples = [None; 4];
                return;
            }
            let id = words.find_map(|w| w.strip_prefix("id=")?.parse::<u32>().ok());
            if self.deadline.is_some() {
                if result == "QUEUED" && self.ticket.is_none() && id.is_some_and(|id| id > 0) {
                    self.ticket = id;
                    self.next_poll = Some(Instant::now() + Duration::from_millis(500));
                } else if (self.ticket.is_some() && id == self.ticket)
                    || (self.ticket.is_none() && id == Some(0))
                {
                    if result != "QUEUED" {
                        self.detection = if result == "DONE" {
                            "Detected"
                        } else {
                            "Unavailable"
                        };
                        self.message = text.into();
                        self.deadline = None;
                    }
                }
            }
            if result == "NOT_READY" && id == Some(0) {
                self.samples = [None; 4];
                self.snapshot = "Unavailable: no valid fresh sensor cache".into();
            }
        }
        for (i, name) in CHANNELS.iter().enumerate() {
            let Some(start) = line.find(&format!("{name} ")) else {
                continue;
            };
            let fields: Vec<_> = line[start..].split_whitespace().collect();
            if fields.len() != 4 {
                continue;
            }
            let mut values = [0.0; 3];
            let valid = ['V', 'A', 'W'].iter().enumerate().all(|(n, unit)| {
                if let Some(value) = fields[n + 1]
                    .strip_suffix(*unit)
                    .and_then(|v| v.parse::<f64>().ok())
                    .filter(|v| v.is_finite())
                {
                    values[n] = value;
                    true
                } else {
                    false
                }
            });
            self.samples[i] = valid.then_some(values);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn samples_are_fragmented_and_have_correct_units() {
        let mut state = State::default();
        state.feed("PAC1934 snapshot seq=3 age_ms=20\n12V_MOTOR 12.000V 0.1");
        assert!(state.samples[0].is_none());
        state.feed("00A 1.200W\n12V_EXP 11.000V 0.200A 2.200W\n5V_EXP 5.000V 0.100A 0.500W\n5V_SLN_HD 5.000V 0.200A 1.000W\n");
        assert_eq!(state.samples[0], Some([12.0, 0.1, 1.2]));
        assert!(state.samples.iter().all(Option::is_some));
        state.feed("ERROR PAC1934 NOT_READY id=0\n");
        assert!(state.samples.iter().all(Option::is_none));
    }
    #[test]
    fn detection_requires_matching_completion_and_times_out() {
        let mut state = State::default();
        state.detect();
        state.feed("PAC1934 QUEUED id=42\nPAC1934 DONE id=41\n");
        assert_eq!(state.detection, "Detecting...");
        state.next_poll = Some(Instant::now());
        assert_eq!(state.poll().as_deref(), Some("pac1934_result 42"));
        state.feed("PAC1934 DONE id=42\n");
        assert_eq!(state.detection, "Detected");
        state.detect();
        state.feed("PAC1934 QUEUED id=43\nERROR PAC1934 FAILED id=43\n");
        assert_eq!(state.detection, "Unavailable");
        state.detect();
        state.deadline = Some(Instant::now());
        state.poll();
        assert_eq!(state.detection, "Unavailable");
    }
}
