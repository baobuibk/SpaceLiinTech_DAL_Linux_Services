//! Reassemble UART text without assuming that a serial read is a response.
use std::time::{Duration, Instant};

#[derive(Default)]
pub struct Decoder {
    pending: Vec<u8>,
}
impl Decoder {
    pub fn feed(&mut self, bytes: &[u8]) -> String {
        self.pending.extend_from_slice(bytes);
        let mut result = String::new();
        loop {
            match std::str::from_utf8(&self.pending) {
                Ok(text) => {
                    result.push_str(text);
                    self.pending.clear();
                    break;
                }
                Err(error) => {
                    let valid = error.valid_up_to();
                    result.push_str(std::str::from_utf8(&self.pending[..valid]).unwrap());
                    self.pending.drain(..valid);
                    if let Some(size) = error.error_len() {
                        self.pending.drain(..size);
                        result.push('\u{fffd}');
                    } else {
                        break;
                    }
                }
            }
        }
        result
    }
}

#[derive(Default)]
pub struct Lines {
    pending: String,
    updated: Option<Instant>,
}
impl Lines {
    pub fn feed(&mut self, text: &str) -> Vec<String> {
        if text.is_empty() {
            return Vec::new();
        }
        self.pending.push_str(text);
        self.updated = Some(Instant::now());
        let mut lines = Vec::new();
        while let Some(end) = self.pending.find('\n') {
            let line: String = self.pending.drain(..=end).collect();
            let line = line.trim_end_matches(['\r', '\n']);
            if !line.is_empty() {
                lines.push(line.to_owned());
            }
        }
        // Bound display buffering for malformed output without newlines.
        if self.pending.len() >= 65536 {
            lines.push(std::mem::take(&mut self.pending));
        }
        lines
    }
    pub fn idle(&mut self) -> Option<String> {
        if !self.pending.is_empty()
            && self
                .updated
                .is_some_and(|at| at.elapsed() >= Duration::from_millis(250))
        {
            self.updated = None;
            return Some(std::mem::take(&mut self.pending));
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fragmented_echo_reply_and_prompt() {
        let mut lines = Lines::default();
        assert!(lines.feed("p").is_empty());
        assert!(lines.feed("ump_type_get\r").is_empty());
        assert_eq!(
            lines.feed("\nPump type: highdriver\r\nDEBUG@MCU:~ $ "),
            ["pump_type_get", "Pump type: highdriver"]
        );
        lines.updated = Some(Instant::now() - Duration::from_secs(1));
        assert_eq!(lines.idle().as_deref(), Some("DEBUG@MCU:~ $ "));
        assert!(lines.idle().is_none());
    }
    #[test]
    fn utf8_across_serial_reads_is_preserved() {
        let mut decoder = Decoder::default();
        assert_eq!(decoder.feed(&[b'2', b'5', 0xc2]), "25");
        assert_eq!(decoder.feed(&[0xb0, b'C', b'\n']), "°C\n");
        assert_eq!(decoder.feed(&[0xff, b'!']), "\u{fffd}!");
    }
    #[test]
    fn links_are_independent_and_long_replies_are_bounded() {
        let mut a = Lines::default();
        let mut b = Lines::default();
        a.feed("Pump ");
        assert_eq!(b.feed("RTC OK\n"), ["RTC OK"]);
        assert_eq!(a.feed("type: highdriver\n"), ["Pump type: highdriver"]);
        assert_eq!(a.feed(&"x".repeat(65536)).len(), 1);
        assert!(a.pending.is_empty());
    }
}
