//! EXP xTP/xCLI and CSP/RPARAM framing.
pub fn crc16(bytes: &[u8]) -> u16 {
    let mut crc = 0u16;
    for &b in bytes {
        crc ^= (b as u16) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            };
        }
    }
    crc
}
fn ecc(data: &[u8]) -> [u8; 2] {
    let (mut a, mut b) = (0, 0);
    for &v in data {
        a ^= v;
        b ^= v;
        b = (b >> 1) ^ if b & 1 != 0 { 0x8e } else { 0 };
    }
    [a, b]
}
pub fn xtp(id: u16, seq: u8, data: &[u8]) -> Result<Vec<u8>, String> {
    if data.len() > 255 {
        return Err("xTP payload too long".into());
    }
    let mut out = vec![0xc0, 0xde, 0xa2];
    out.extend_from_slice(&id.to_le_bytes());
    out.push(seq);
    out.push(data.len() as u8);
    out.extend_from_slice(data);
    out.extend_from_slice(&ecc(data));
    let checksum = crc16(&out[2..]);
    out.extend_from_slice(&checksum.to_le_bytes());
    out.extend_from_slice(&[0xda, 0xed]);
    Ok(out)
}
pub fn xcli(command: &str, seq: u8) -> Result<Vec<u8>, String> {
    let (id, body) = if command.starts_with("obc_result ") {
        (0x33, command.to_owned())
    } else {
        (0x30, format!("cli {command}"))
    };
    if body.len() >= 96 {
        return Err("xCLI command exceeds the firmware's 95-byte limit".into());
    }
    let mut data = vec![0, id, seq];
    data.extend_from_slice(body.as_bytes());
    xtp(0x100, seq & 0x7f, &data)
}
pub fn parse_xtp(input: &mut Vec<u8>) -> Vec<(u16, u8, Vec<u8>)> {
    let mut out = Vec::new();
    loop {
        let Some(start) = input.windows(2).position(|w| w == [0xc0, 0xde]) else {
            input.clear();
            break;
        };
        input.drain(..start);
        if input.len() < 7 {
            break;
        }
        if input[2] != 0xa2 {
            input.drain(..1);
            continue;
        }
        let n = input[6] as usize;
        let total = n + 13;
        if input.len() < total {
            break;
        }
        let valid = input[total - 2..total] == [0xda, 0xed]
            && input[n + 7..n + 9] == ecc(&input[7..n + 7])
            && crc16(&input[2..total - 4])
                == u16::from_le_bytes([input[total - 4], input[total - 3]]);
        if valid {
            out.push((
                u16::from_le_bytes([input[3], input[4]]),
                input[5],
                input[7..n + 7].to_vec(),
            ));
            input.drain(..total);
        } else {
            input.drain(..1);
        }
    }
    out
}
pub fn xcli_text(data: &[u8]) -> String {
    if data.len() < 6 {
        return format!("short xCLI response: {data:02X?}");
    }
    let mut text = format!(
        "xCLI seq={} status={} part={}/{}",
        data[2],
        data[3],
        data[5] + 1,
        data[4]
    );
    let mut i = 6;
    while i + 4 <= data.len() {
        let namesize = data[i] as usize;
        if i + namesize + 4 > data.len() {
            break;
        }
        let name = String::from_utf8_lossy(&data[i + 1..i + 1 + namesize]);
        let kind = data[i + 1 + namesize];
        let n = u16::from_le_bytes([data[i + 2 + namesize], data[i + 3 + namesize]]) as usize;
        i += namesize + 4;
        if i + n > data.len() {
            break;
        }
        let value = &data[i..i + n];
        if name == "text" {
            // Command text is assembled separately by the shared UART display.
            i += n;
            continue;
        }
        text.push_str(&format!(
            "\n{name}={}",
            if kind == 9 || name == "text" {
                String::from_utf8_lossy(value)
                    .trim_end_matches('\0')
                    .to_string()
            } else {
                format!("{value:02X?}")
            }
        ));
        i += n;
    }
    text
}
pub fn xcli_field<'a>(data: &'a [u8], wanted: &str) -> Option<&'a [u8]> {
    if data.len() < 6 {
        return None;
    }
    let mut i = 6;
    while i + 4 <= data.len() {
        let namesize = data[i] as usize;
        if i + namesize + 4 > data.len() {
            break;
        }
        let name = &data[i + 1..i + 1 + namesize];
        let n = u16::from_le_bytes([data[i + 2 + namesize], data[i + 3 + namesize]]) as usize;
        i += namesize + 4;
        if i + n > data.len() {
            break;
        }
        if name == wanted.as_bytes() {
            return Some(&data[i..i + n]);
        }
        i += n;
    }
    None
}
pub fn crc32c(bytes: &[u8]) -> u32 {
    let mut crc = !0u32;
    for &b in bytes {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ if crc & 1 != 0 { 0x82f63b78 } else { 0 };
        }
    }
    !crc
}
pub fn csp_header(source: u8, sport: u8) -> [u8; 4] {
    (((source as u32) << 25) | (11 << 20) | (7 << 14) | ((sport as u32) << 8)).to_be_bytes()
}
pub fn rparam_get(table: u8) -> Vec<u8> {
    vec![0, table, 0, 0, 0x0b, 0xb0, 0, 0, 0, 0]
}
pub fn rparam_shell(command: &str) -> Result<Vec<u8>, String> {
    if command.is_empty() || command.len() > 63 {
        return Err("remote shell command must be 1-63 bytes".into());
    }
    let mut out = vec![0xff, 1, 0, 0, 0x0b, 0xb0, 0, 0, 0, 0, 0, 0];
    out.extend_from_slice(command.as_bytes());
    out.resize(10 + 2 + 64, 0);
    let length = (out.len() - 10) as u16;
    out[2..4].copy_from_slice(&length.to_be_bytes());
    Ok(out)
}
pub fn kiss(header: &[u8; 4], payload: &[u8]) -> Vec<u8> {
    let mut raw = header.to_vec();
    raw.extend_from_slice(payload);
    raw.extend_from_slice(&crc32c(payload).to_be_bytes());
    let mut out = vec![0xc0, 0];
    for b in raw {
        match b {
            0xc0 => out.extend_from_slice(&[0xdb, 0xdc]),
            0xdb => out.extend_from_slice(&[0xdb, 0xdd]),
            _ => out.push(b),
        }
    }
    out.push(0xc0);
    out
}
pub fn parse_kiss(input: &mut Vec<u8>) -> Vec<Vec<u8>> {
    let mut out = Vec::new();
    loop {
        let Some(start) = input.iter().position(|b| *b == 0xc0) else {
            input.clear();
            break;
        };
        input.drain(..start);
        let Some(end) = input.iter().skip(1).position(|b| *b == 0xc0).map(|n| n + 1) else {
            break;
        };
        let frame: Vec<_> = input.drain(..=end).collect();
        if frame.len() < 11 || frame[1] != 0 {
            continue;
        }
        let mut raw = Vec::new();
        let mut i = 2;
        let mut valid = true;
        while i < frame.len() - 1 {
            if frame[i] == 0xdb {
                i += 1;
                if i >= frame.len() - 1 {
                    valid = false;
                    break;
                }
                match frame[i] {
                    0xdc => raw.push(0xc0),
                    0xdd => raw.push(0xdb),
                    _ => {
                        valid = false;
                        break;
                    }
                }
            } else {
                raw.push(frame[i]);
            }
            i += 1;
        }
        if valid
            && raw.len() >= 8
            && crc32c(&raw[4..raw.len() - 4])
                == u32::from_be_bytes(raw[raw.len() - 4..].try_into().unwrap())
        {
            raw.truncate(raw.len() - 4);
            out.push(raw);
        }
    }
    out
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn checksum_vectors() {
        assert_eq!(crc16(b"123456789"), 0x31c3);
        assert_eq!(crc32c(b"123456789"), 0xe3069283);
    }
    #[test]
    fn xtp_roundtrip() {
        let mut bytes = xtp(0x101, 0, &[1, 0x30, 5, 0, 1, 0]).unwrap();
        assert_eq!(parse_xtp(&mut bytes)[0].0, 0x101);
    }
    #[test]
    fn kiss_roundtrip() {
        let mut bytes = kiss(&csp_header(2, 42), &rparam_get(0));
        assert_eq!(&parse_kiss(&mut bytes)[0][4..], rparam_get(0).as_slice());
    }
    #[test]
    fn shell_record_is_fixed_size() {
        let bytes = rparam_shell("pump_state").unwrap();
        assert_eq!(bytes.len(), 76);
        assert_eq!(&bytes[2..4], &[0, 66]);
        assert_eq!(&bytes[10..12], &[0, 0]);
    }
}
