use std::io::{self, Write};
use std::time::Duration;

pub const READ_TIMEOUT: Duration = Duration::from_millis(30);
const WRITE_TIMEOUT: Duration = Duration::from_secs(1);

// Continue only the unwritten suffix. Never replay an entire actuator command.
fn complete(writer: &mut impl Write, bytes: &[u8]) -> Result<(), String> {
    let mut sent = 0;
    while sent < bytes.len() {
        match writer.write(&bytes[sent..]) {
            Ok(0) => return Err(format!("write returned zero after {sent}/{} bytes; serial adapter may be disconnected or stalled", bytes.len())),
            Ok(n) => sent += n,
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            Err(error) => return Err(format!("{error} after {sent}/{} bytes", bytes.len())),
        }
    }
    Ok(())
}

pub fn write_packet(mut port: &mut dyn serialport::SerialPort, bytes: &[u8]) -> Result<(), String> {
    port.set_timeout(WRITE_TIMEOUT)
        .map_err(|e| format!("set write timeout: {e}"))?;
    let result = complete(&mut port, bytes);
    let restored = port
        .set_timeout(READ_TIMEOUT)
        .map_err(|e| format!("restore read timeout after transmission: {e}"));
    result.and(restored)
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Fake {
        sizes: std::collections::VecDeque<io::Result<usize>>,
        received: Vec<u8>,
    }
    impl Write for Fake {
        fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
            let n = self.sizes.pop_front().unwrap_or(Ok(bytes.len()))?;
            self.received.extend_from_slice(&bytes[..n]);
            Ok(n)
        }
        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }
    #[test]
    fn short_writes_continue_without_replaying_prefix() {
        let mut f = Fake {
            sizes: [Ok(2), Err(io::ErrorKind::Interrupted.into()), Ok(1)].into(),
            received: vec![],
        };
        complete(&mut f, b"pc_status\r").unwrap();
        assert_eq!(f.received, b"pc_status\r");
    }
    #[test]
    fn zero_and_timeout_stop_without_resending() {
        for error in [Ok(0), Err(io::ErrorKind::TimedOut.into())] {
            let mut f = Fake {
                sizes: [Ok(2), error].into(),
                received: vec![],
            };
            let error = complete(&mut f, b"pc_status\r").unwrap_err();
            assert!(error.contains("2/10 bytes"));
            assert_eq!(f.received, b"pc");
        }
    }
}
