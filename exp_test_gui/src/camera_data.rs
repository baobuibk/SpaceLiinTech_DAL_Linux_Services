use std::io::{Read, Write};
use std::time::{Duration, Instant};

pub fn field<'a>(line: &'a str, key: &str) -> Option<&'a str> {
    line.split_whitespace()
        .find_map(|v| v.strip_prefix(key))
        .map(|v| v.trim_end_matches(';'))
}
fn number(line: &str, key: &str) -> Option<u32> {
    let value = field(line, key)?;
    if let Some(hex) = value.strip_prefix("0x") {
        u32::from_str_radix(hex, 16).ok()
    } else {
        value.parse().ok()
    }
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Metadata {
    pub boot: u32,
    pub object: u32,
    pub kind: u32,
    pub source: u32,
    pub encoding: u32,
    pub width: usize,
    pub height: usize,
    pub length: usize,
    pub crc: u32,
    pub run: u32,
    pub sequence: u32,
    pub timestamp_ms: u32,
}
impl Metadata {
    pub fn parse(line: &str) -> Option<Self> {
        let start = line.find("DATA boot=")?;
        let line = &line[start..];
        let m = Self {
            boot: number(line, "boot=")?,
            object: number(line, "object=")?,
            kind: number(line, "type=")?,
            source: number(line, "source=")?,
            encoding: number(line, "encoding=")?,
            width: number(line, "width=")? as usize,
            height: number(line, "height=")? as usize,
            length: number(line, "length=")? as usize,
            crc: number(line, "crc32=")?,
            run: number(line, "run=").unwrap_or(0),
            sequence: number(line, "seq=").unwrap_or(0),
            timestamp_ms: number(line, "ms=").unwrap_or(0),
        };
        if m.length == 0 || m.length > 4 * 1024 * 1024 {
            None
        } else {
            Some(m)
        }
    }
    pub fn ack(&self) -> String {
        format!("data_collect_done {}", self.object)
    }
    fn matches_header(&self, line: &str) -> bool {
        number(line, "boot=") == Some(self.boot)
            && number(line, "object=") == Some(self.object)
            && number(line, "length=") == Some(self.length as u32)
            && number(line, "crc32=") == Some(self.crc)
    }
    pub fn verify(&self, bytes: &[u8]) -> Result<(), String> {
        if bytes.len() != self.length {
            return Err("Payload length does not match metadata".into());
        }
        if crc32(bytes) != self.crc {
            return Err("Payload CRC32 does not match metadata; object retained".into());
        }
        Ok(())
    }
    pub fn temperatures(&self, bytes: &[u8]) -> Result<Vec<f32>, String> {
        self.verify(bytes)?;
        if self.kind != 3 || self.source > 1 || self.width != 32 || self.height != 24 {
            return Err("Not a supported 32x24 camera object".into());
        }
        let values: Vec<f32> = match self.encoding {
            1 if bytes.len() == 1536 => bytes
                .chunks_exact(2)
                .map(|b| i16::from_le_bytes([b[0], b[1]]) as f32 / 50.0)
                .collect(),
            2 if bytes.len() == 3072 => bytes
                .chunks_exact(4)
                .map(|b| f32::from_le_bytes(b.try_into().unwrap()))
                .collect(),
            _ => return Err("Camera encoding/length mismatch".into()),
        };
        if values.iter().any(|v| !v.is_finite()) {
            return Err("Image contains non-finite temperatures".into());
        }
        Ok(values)
    }
}
pub fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for byte in bytes {
        crc ^= *byte as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ if crc & 1 != 0 { 0xEDB88320 } else { 0 };
        }
    }
    !crc
}
fn crc16(bytes: &[u8]) -> u16 {
    let mut crc = 0u16;
    for byte in bytes {
        crc ^= (*byte as u16) << 8;
        for _ in 0..8 {
            crc = (crc << 1) ^ if crc & 0x8000 != 0 { 0x1021 } else { 0 };
        }
    }
    crc
}
fn read_exact_until<T: Read + ?Sized>(
    port: &mut T,
    bytes: &mut [u8],
    cancel: &mut impl FnMut() -> bool,
) -> Result<(), String> {
    let end = Instant::now() + Duration::from_secs(6);
    let mut offset = 0;
    while offset < bytes.len() {
        if cancel() {
            return Err("Transfer cancelled; object retained".into());
        }
        if Instant::now() > end {
            return Err("XMODEM timeout; object retained".into());
        }
        match port.read(&mut bytes[offset..]) {
            Ok(0) => return Err("Serial link closed".into()),
            Ok(n) => offset += n,
            Err(e)
                if matches!(
                    e.kind(),
                    std::io::ErrorKind::TimedOut
                        | std::io::ErrorKind::WouldBlock
                        | std::io::ErrorKind::Interrupted
                ) => {}
            Err(e) => return Err(e.to_string()),
        }
    }
    Ok(())
}
fn write<T: Write + ?Sized>(port: &mut T, bytes: &[u8]) -> Result<(), String> {
    port.write_all(bytes).map_err(|e| e.to_string())
}
// Exact UART2 firmware protocol: CRC-mode, 128-byte blocks, duplicate retransmits and single EOT ACK.
pub fn receive<T: Read + Write + ?Sized>(
    port: &mut T,
    meta: &Metadata,
    cancel: &mut impl FnMut() -> bool,
    log: &mut impl FnMut(String),
) -> Result<Vec<u8>, String> {
    let result = receive_inner(port, meta, cancel, log);
    if result.is_err() {
        let _ = port.write_all(&[0x18, 0x18]);
    }
    result
}
fn receive_inner<T: Read + Write + ?Sized>(
    port: &mut T,
    meta: &Metadata,
    cancel: &mut impl FnMut() -> bool,
    log: &mut impl FnMut(String),
) -> Result<Vec<u8>, String> {
    if meta.length == 0 || meta.length > 4 * 1024 * 1024 {
        return Err("Invalid transfer length".into());
    }
    let command = format!("xmodem_collect_data {}", meta.object);
    write(port, format!("{command}\r").as_bytes())?;
    log(format!("TX > {command}"));
    let start = Instant::now();
    let mut line = Vec::new();
    loop {
        if start.elapsed() > Duration::from_secs(12) {
            return Err("No XMODEM header received".into());
        }
        let mut byte = [0];
        read_exact_until(port, &mut byte, cancel)?;
        if byte[0] == b'\n' {
            let text = String::from_utf8_lossy(&line).trim().to_owned();
            line.clear();
            log(format!("RX < {text}"));
            if text.contains("XMODEM waiting:") {
                if !meta.matches_header(&text) {
                    return Err(
                        "Selected object changed: refresh metadata before downloading".into(),
                    );
                }
                break;
            }
            if text.contains("ERROR") || text == "EMPTY" {
                return Err(text);
            }
        } else {
            line.push(byte[0]);
            if line.len() > 2048 {
                return Err("Invalid XMODEM preamble".into());
            }
        }
    }
    write(port, b"C")?;
    let mut bytes = Vec::new();
    let mut expected = 1u8;
    let mut failures = 0;
    loop {
        let mut byte = [0];
        read_exact_until(port, &mut byte, cancel)?;
        match byte[0] {
            1 => {
                let mut block = [0; 132];
                read_exact_until(port, &mut block, cancel)?;
                let valid = block[0] == !block[1]
                    && crc16(&block[2..130]) == u16::from_be_bytes([block[130], block[131]]);
                if !valid {
                    failures += 1;
                    write(port, &[0x15])?;
                    if failures >= 10 {
                        return Err("Repeated XMODEM checksum failures".into());
                    }
                    continue;
                }
                if block[0] == expected {
                    if bytes.len() >= meta.length {
                        return Err("Unexpected extra XMODEM block".into());
                    }
                    bytes.extend_from_slice(&block[2..130]);
                    expected = expected.wrapping_add(1);
                    failures = 0;
                } else if block[0] != expected.wrapping_sub(1) || bytes.is_empty() {
                    return Err("Unexpected XMODEM sequence".into());
                }
                write(port, &[6])?;
            }
            4 => {
                if bytes.len() != meta.length.div_ceil(128) * 128 {
                    return Err("Incomplete XMODEM payload".into());
                }
                bytes.truncate(meta.length);
                meta.verify(&bytes)?;
                write(port, &[6])?;
                log(format!("RX < XMODEM {} bytes; CRC32 verified", bytes.len()));
                return Ok(bytes);
            }
            0x18 => return Err("Sender cancelled XMODEM; object retained".into()),
            _ => return Err("Unexpected byte during XMODEM".into()),
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn meta(bytes: &[u8]) -> Metadata {
        Metadata {
            boot: 7,
            object: 2,
            kind: 3,
            source: 1,
            encoding: 2,
            width: 32,
            height: 24,
            length: bytes.len(),
            crc: crc32(bytes),
            run: 0,
            sequence: 0,
            timestamp_ms: 0,
        }
    }
    struct Wire {
        input: std::io::Cursor<Vec<u8>>,
        output: Vec<u8>,
    }
    impl Read for Wire {
        fn read(&mut self, b: &mut [u8]) -> std::io::Result<usize> {
            self.input.read(b)
        }
    }
    impl Write for Wire {
        fn write(&mut self, b: &[u8]) -> std::io::Result<usize> {
            self.output.extend_from_slice(b);
            Ok(b.len())
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    fn packet(id: u8, b: &[u8]) -> Vec<u8> {
        let mut data = [0x1a; 128];
        data[..b.len()].copy_from_slice(b);
        let mut p = vec![1, id, !id];
        p.extend(data);
        p.extend(crc16(&data).to_be_bytes());
        p
    }
    fn wire(m: &Metadata, packets: Vec<u8>) -> Wire {
        let mut data = format!(
            "XMODEM waiting: boot={} object={} length={} crc32=0x{:08X}; start receiver\r\n",
            m.boot, m.object, m.length, m.crc
        )
        .into_bytes();
        data.extend(packets);
        Wire {
            input: std::io::Cursor::new(data),
            output: Vec::new(),
        }
    }
    #[test]
    fn checksum_and_metadata() {
        assert_eq!(crc32(b"123456789"), 0xcbf43926);
        assert_eq!(crc16(b"123456789"), 0x31c3);
        assert!(Metadata::parse("DATA boot=7 pending=1").is_none());
        assert!(Metadata::parse("DATA boot=7 object=2 type=3 source=1 encoding=2 width=32 height=24 length=3072 crc32=0xABCD seq=1").is_some());
    }
    #[test]
    fn decode_both_formats_and_reject_bad_data() {
        let b = 25.5f32.to_le_bytes().repeat(768);
        let m = meta(&b);
        assert_eq!(m.temperatures(&b).unwrap()[0], 25.5);
        let b = 1250i16.to_le_bytes().repeat(768);
        let mut m = meta(&b);
        m.encoding = 1;
        assert_eq!(m.temperatures(&b).unwrap()[767], 25.0);
        assert!(m.temperatures(&b[..100]).is_err());
        let b = f32::NAN.to_le_bytes().repeat(768);
        assert!(meta(&b).temperatures(&b).is_err());
    }
    #[test]
    fn transfer_retries_and_duplicate_blocks() {
        let b = b"camera bytes";
        let m = meta(b);
        let good = packet(1, b);
        let mut bad = good.clone();
        bad[5] ^= 1;
        bad.extend(&good);
        bad.extend(&good);
        bad.push(4);
        let mut w = wire(&m, bad);
        assert_eq!(receive(&mut w, &m, &mut || false, &mut |_| {}).unwrap(), b);
        assert!(w.output.starts_with(b"xmodem_collect_data 2\r"));
        assert!(w.output.contains(&0x15));
    }
    #[test]
    fn changed_identity_and_early_end_are_refused() {
        let b = b"hello";
        let m = meta(b);
        let mut w = wire(&m, vec![4]);
        assert!(receive(&mut w, &m, &mut || false, &mut |_| {}).is_err());
        let mut w = wire(&m, vec![4]);
        let mut wrong = m.clone();
        wrong.object = 3;
        assert!(receive(&mut w, &wrong, &mut || false, &mut |_| {}).is_err());
        assert!(w.output.ends_with(&[0x18, 0x18]));
    }
    #[test]
    fn multiple_blocks_trim_padding_and_wrap_sequence() {
        let bytes: Vec<u8> = (0..(128 * 258 + 17)).map(|i| (i % 251) as u8).collect();
        let m = meta(&bytes);
        let mut packets = Vec::new();
        for (i, chunk) in bytes.chunks(128).enumerate() {
            packets.extend(packet((i + 1) as u8, chunk));
        }
        packets.push(4);
        let mut w = wire(&m, packets);
        assert_eq!(
            receive(&mut w, &m, &mut || false, &mut |_| {}).unwrap(),
            bytes
        );
        assert_eq!(m.ack(), "data_collect_done 2");
    }
    #[test]
    fn whole_object_corruption_and_cancel_are_refused() {
        let m = meta(b"hello");
        let mut p = packet(1, b"other");
        p.push(4);
        let mut w = wire(&m, p);
        assert!(receive(&mut w, &m, &mut || false, &mut |_| {}).is_err());
        let mut w = wire(&m, vec![]);
        assert!(receive(&mut w, &m, &mut || true, &mut |_| {}).is_err());
    }
}
