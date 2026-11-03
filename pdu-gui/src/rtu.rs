//! Modbus RTU framing. One outstanding request; writes are never retried.
use std::io::{self, Read, Write};
use std::time::{Duration, Instant};

pub fn crc16(bytes: &[u8]) -> u16 {
    let mut crc = 0xffffu16;
    for byte in bytes {
        crc ^= *byte as u16;
        for _ in 0..8 {
            crc = (crc >> 1) ^ if crc & 1 != 0 { 0xa001 } else { 0 };
        }
    }
    crc
}
pub fn frame(unit: u8, function: u8, payload: &[u8]) -> Vec<u8> {
    let mut result = vec![unit, function];
    result.extend_from_slice(payload);
    result.extend_from_slice(&crc16(&result).to_le_bytes());
    result
}

/// Serial reads have a short timeout; this loop enforces a whole-response deadline.
pub fn read_response(
    reader: &mut impl Read,
    unit: u8,
    function: u8,
    timeout: Duration,
) -> Result<Vec<u8>, String> {
    let deadline = Instant::now() + timeout;
    let mut data = Vec::new();
    let mut expected = 3;
    while data.len() < expected {
        if Instant::now() >= deadline {
            return Err("Response timeout".into());
        }
        let mut buf = [0u8; 256];
        let need = expected - data.len();
        match reader.read(&mut buf[..need]) {
            Ok(0) => return Err("Serial link closed".into()),
            Ok(n) => data.extend_from_slice(&buf[..n]),
            Err(e)
                if matches!(
                    e.kind(),
                    io::ErrorKind::TimedOut
                        | io::ErrorKind::WouldBlock
                        | io::ErrorKind::Interrupted
                ) =>
            {
                continue
            }
            Err(e) => return Err(e.to_string()),
        }
        if data.len() >= 2
            && (data[0] != unit || (data[1] != function && data[1] != function | 0x80))
        {
            return Err("Unexpected slave address or function".into());
        }
        if data.len() >= 3 {
            expected = if data[1] & 0x80 != 0 {
                5
            } else if (1..=4).contains(&function) {
                data[2] as usize + 5
            } else {
                8
            };
            if expected > 256 {
                return Err("Oversized RTU response".into());
            }
        }
    }
    let end = data.len() - 2;
    if crc16(&data[..end]) != u16::from_le_bytes([data[end], data[end + 1]]) {
        return Err("Response CRC mismatch".into());
    }
    if data[1] & 0x80 != 0 {
        let name = match data[2] {
            1 => "illegal function",
            2 => "illegal address",
            3 => "illegal value",
            4 => "device failure",
            _ => "unknown exception",
        };
        return Err(format!("Modbus exception {}: {name}", data[2]));
    }
    Ok(data[2..end].to_vec())
}

pub struct Client {
    port: Box<dyn serialport::SerialPort>,
    unit: u8,
    gap: Duration,
    last_end: Instant,
    timeout: Duration,
}
impl Client {
    pub fn open(path: &str, baud: u32, unit: u8, timeout: Duration) -> Result<Self, String> {
        if !(1..=247).contains(&unit) || baud == 0 || timeout.is_zero() {
            return Err("Use slave 1–247, a positive baud and timeout".into());
        }
        let port = serialport::new(path, baud)
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::One)
            .flow_control(serialport::FlowControl::None)
            .timeout(Duration::from_millis(50))
            .open()
            .map_err(|e| e.to_string())?;
        Ok(Self {
            port,
            unit,
            gap: Duration::from_secs_f64((3.5 * 11.0 / baud as f64).max(0.00175)),
            last_end: Instant::now(),
            timeout,
        })
    }
    fn exchange(&mut self, function: u8, payload: &[u8]) -> Result<Vec<u8>, String> {
        std::thread::sleep(self.gap.saturating_sub(self.last_end.elapsed()));
        let result = (|| {
            self.port
                .clear(serialport::ClearBuffer::Input)
                .map_err(|e| e.to_string())?;
            self.port
                .write_all(&frame(self.unit, function, payload))
                .map_err(|e| e.to_string())?;
            self.port.flush().map_err(|e| e.to_string())?;
            read_response(&mut self.port, self.unit, function, self.timeout)
        })();
        self.last_end = Instant::now();
        result
    }
    pub fn read(&mut self, function: u8, offset: u16, count: u16) -> Result<Vec<u16>, String> {
        if !(1..=4).contains(&function)
            || count == 0
            || count > if function <= 2 { 2000 } else { 125 }
        {
            return Err("Invalid read function or count".into());
        }
        let payload = [offset.to_be_bytes(), count.to_be_bytes()].concat();
        let response = self.exchange(function, &payload)?;
        decode_read(function, count, &response)
    }
    pub fn write(&mut self, function: u8, payload: &[u8]) -> Result<(), String> {
        let response = self.exchange(function, payload)?;
        if response != payload[..4] {
            return Err(
                "Write acknowledgment mismatch; outcome unknown, refresh before retrying".into(),
            );
        }
        Ok(())
    }
    /// Send one firmware text CLI line between Modbus transactions on UART0.
    pub fn text_command(&mut self, command: &str) -> Result<Vec<u8>, String> {
        if command.len() > 63
            || !command
                .bytes()
                .all(|byte| byte.is_ascii_graphic() || byte == b' ')
        {
            return Err("Use at most 63 printable ASCII characters".into());
        }
        std::thread::sleep(self.gap.saturating_sub(self.last_end.elapsed()));
        let result = (|| {
            self.port
                .clear(serialport::ClearBuffer::Input)
                .map_err(|e| e.to_string())?;
            self.port
                .write_all(command.as_bytes())
                .and_then(|_| self.port.write_all(b"\r"))
                .map_err(|e| e.to_string())?;
            self.port.flush().map_err(|e| e.to_string())?;
            let deadline = Instant::now() + Duration::from_secs(5);
            let mut response = Vec::new();
            let mut buffer = [0; 512];
            while Instant::now() < deadline && response.len() < 65536 {
                match self.port.read(&mut buffer) {
                    Ok(0) => return Err("Serial link closed".into()),
                    Ok(size) => {
                        response.extend_from_slice(&buffer[..size]);
                        if response.ends_with(b"> ") {
                            return Ok(response);
                        }
                    }
                    Err(error)
                        if matches!(
                            error.kind(),
                            io::ErrorKind::TimedOut
                                | io::ErrorKind::WouldBlock
                                | io::ErrorKind::Interrupted
                        ) => {}
                    Err(error) => return Err(error.to_string()),
                }
            }
            Err("Text CLI response timeout or too long".into())
        })();
        self.last_end = Instant::now();
        result
    }
}
pub fn decode_read(function: u8, count: u16, response: &[u8]) -> Result<Vec<u16>, String> {
    let expected = if function <= 2 {
        (count as usize).div_ceil(8)
    } else {
        count as usize * 2
    };
    if response.len() != expected + 1 || response[0] as usize != expected {
        return Err("Unexpected response byte count".into());
    }
    Ok(if function <= 2 {
        (0..count as usize)
            .map(|i| ((response[1 + i / 8] >> (i % 8)) & 1) as u16)
            .collect()
    } else {
        response[1..]
            .as_chunks::<2>()
            .0
            .iter()
            .map(|p| u16::from_be_bytes([p[0], p[1]]))
            .collect()
    })
}
