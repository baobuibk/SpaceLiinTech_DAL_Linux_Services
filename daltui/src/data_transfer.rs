//! ICD v1.2 TYPE 03. One outstanding request; files are published only after CRC32.
use crate::proto::Cmd;
use std::{
    fs::{self, File, OpenOptions},
    io::Write,
    path::PathBuf,
    time::{Duration, SystemTime, UNIX_EPOCH},
};

#[derive(Clone, Debug)]
pub struct Metadata {
    pub kind: u8,
    pub index: u32,
    pub chunk: u32,
    pub name: String,
    pub size: u32,
    pub crc: u32,
    pub chunks: u32,
}
fn u32_at(p: &[u8], n: usize) -> u32 {
    u32::from_le_bytes(p[n..n + 4].try_into().unwrap())
}
pub fn crc_update(mut crc: u32, data: &[u8]) -> u32 {
    for b in data {
        crc ^= *b as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xEDB88320u32 & 0u32.wrapping_sub(crc & 1));
        }
    }
    crc
}
fn command(addr: u8, text: String, payload: Vec<u8>) -> Cmd {
    Cmd {
        ty: 3,
        addr,
        text,
        payload,
        timeout: Duration::from_secs(30),
    }
}
pub struct Transfer {
    pub filter: i32,
    pub kind: usize,
    pub index: u32,
    pub chunk: i32,
    pub mark: i32,
    pub counts: Option<[u32; 3]>,
    pub listed_filter: Option<u8>,
    pub selected: Option<Metadata>,
    pub next: u32,
    pub received: u64,
    pub busy: bool,
    pub automatic: bool,
    pub status: String,
    pub path: String,
    pub verified: bool,
    crc: u32,
    output: Option<File>,
    part: PathBuf,
    final_path: PathBuf,
    broken: bool,
}
impl Default for Transfer {
    fn default() -> Self {
        Self {
            filter: 255,
            kind: 0,
            index: 0,
            chunk: 4096,
            mark: 1,
            counts: None,
            listed_filter: None,
            selected: None,
            next: 0,
            received: 0,
            busy: false,
            automatic: false,
            status: "List files to begin".into(),
            path: String::new(),
            verified: false,
            crc: !0,
            output: None,
            part: PathBuf::new(),
            final_path: PathBuf::new(),
            broken: false,
        }
    }
}
impl Transfer {
    pub fn fail(&mut self, e: &str) {
        self.busy = false;
        self.automatic = false;
        self.broken = true;
        self.selected = None;
        self.counts = None;
        self.output = None;
        self.status = format!("Error: {e}; select file again");
    }
    fn reset_download(&mut self) {
        self.output = None;
        self.next = 0;
        self.received = 0;
        self.crc = !0;
        self.verified = false;
        self.broken = false;
        self.path.clear();
        self.automatic = false;
    }
    pub fn plan(&mut self, id: &str) -> Result<Vec<Cmd>, String> {
        if id == "obc.dat_cancel" {
            self.automatic = false;
            self.status = "Paused; pending chunk may finish".into();
            return Ok(vec![]);
        }
        if self.busy {
            return Err("Wait for the pending data-transfer reply".into());
        }
        if self.automatic {
            return Err("Pause download first".into());
        }
        let cmd = match id {
            "obc.dat_list" => command(
                0,
                format!("dat_lst {}", self.filter),
                vec![self.filter as u8],
            ),
            "obc.dat_select" => {
                let counts = self.counts.ok_or("List files first")?;
                if self.index >= counts[self.kind] {
                    return Err("File index is outside the current list".into());
                }
                let mut p = vec![self.kind as u8];
                p.extend(self.index.to_le_bytes());
                p.extend((self.chunk as u16).to_le_bytes());
                command(
                    1,
                    format!("dat_sel {} {} {}", self.kind, self.index, self.chunk),
                    p,
                )
            }
            "obc.dat_read" | "obc.dat_download" => {
                let m = self.selected.as_ref().ok_or("Select a file first")?;
                if self.broken {
                    return Err("Select file again after a transfer error".into());
                }
                if self.verified {
                    return Err(
                        "File already verified; select again to download another copy".into(),
                    );
                }
                let chunks = m.chunks;
                if self.output.is_none() {
                    self.open_output().map_err(|e| {
                        self.fail(&e);
                        e
                    })?;
                }
                if chunks == 0 {
                    self.finish().map_err(|e| {
                        self.fail(&e);
                        e
                    })?;
                    return Ok(vec![]);
                }
                self.automatic = id == "obc.dat_download";
                self.get_command()
            }
            "obc.dat_mark" => {
                self.selected.as_ref().ok_or("Select a file first")?;
                command(5, format!("dat_mrk {}", self.mark), vec![self.mark as u8])
            }
            "obc.dat_deselect" => command(3, "dat_des".into(), vec![]),
            "obc.dat_delete" => {
                let m = self
                    .selected
                    .as_ref()
                    .ok_or("Select and check the file before deleting")?;
                let mut p = vec![m.kind];
                p.extend(m.index.to_le_bytes());
                command(4, format!("dat_del {} {}", m.kind, m.index), p)
            }
            _ => return Err("Unknown data-transfer action".into()),
        };
        self.busy = true;
        self.status = format!("Waiting: {}", cmd.text);
        Ok(vec![cmd])
    }
    fn get_command(&self) -> Cmd {
        command(
            2,
            format!("dat_get {}", self.next),
            self.next.to_le_bytes().to_vec(),
        )
    }
    fn open_output(&mut self) -> Result<(), String> {
        let m = self.selected.as_ref().ok_or("No selection")?;
        let root = std::env::var_os("DAL_TUI_DOWNLOAD_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("downloads"));
        fs::create_dir_all(&root).map_err(|e| e.to_string())?;
        let name: String = m
            .name
            .chars()
            .map(|c| {
                if c.is_ascii_alphanumeric() || c == '.' || c == '_' || c == '-' {
                    c
                } else {
                    '_'
                }
            })
            .collect();
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|e| e.to_string())?
            .as_nanos();
        self.final_path = root.join(format!(
            "T{}_I{}_{}_{}_{}",
            m.kind,
            m.index,
            std::process::id(),
            stamp,
            name
        ));
        self.part = self.final_path.with_extension(format!(
            "{}part",
            self.final_path
                .extension()
                .and_then(|s| s.to_str())
                .map(|s| format!("{s}."))
                .unwrap_or_default()
        ));
        self.output = Some(
            OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&self.part)
                .map_err(|e| e.to_string())?,
        );
        self.path = self.part.display().to_string();
        Ok(())
    }
    fn finish(&mut self) -> Result<(), String> {
        let m = self.selected.as_ref().ok_or("No selection")?;
        if self.received != m.size as u64 || !self.crc != m.crc {
            return Err("File size/CRC32 mismatch; .part retained".into());
        }
        let f = self.output.take().ok_or("No output file")?;
        f.sync_all().map_err(|e| e.to_string())?;
        drop(f);
        // Atomic publication without replacing an existing destination.
        fs::hard_link(&self.part, &self.final_path).map_err(|e| e.to_string())?;
        fs::remove_file(&self.part).map_err(|e| e.to_string())?;
        self.path = self.final_path.display().to_string();
        self.verified = true;
        self.automatic = false;
        self.status = "DONE: saved, CRC32 verified".into();
        Ok(())
    }
    pub fn reply(&mut self, cmd: &Cmd, p: &[u8]) -> Result<Option<Cmd>, String> {
        self.busy = false;
        match cmd.addr {
            0 | 4 => {
                if p.len() != 12 {
                    return Err("Invalid count payload (expected 12 bytes)".into());
                }
                self.counts = Some([u32_at(p, 0), u32_at(p, 4), u32_at(p, 8)]);
                if cmd.addr == 0 {
                    self.listed_filter = cmd.payload.first().copied();
                }
                self.selected = None;
                self.reset_download();
                self.status = "OK: counts updated; select file by index".into();
            }
            1 => {
                if p.len() != 41 || cmd.payload.len() != 7 {
                    return Err("Invalid selection metadata".into());
                }
                let m = Metadata {
                    kind: p[0],
                    index: u32_at(p, 1),
                    chunk: u32_at(p, 5),
                    name: String::from_utf8_lossy(&p[9..29])
                        .trim_end_matches('\0')
                        .chars()
                        .filter(|c| !c.is_control())
                        .collect(),
                    size: u32_at(p, 29),
                    crc: u32_at(p, 33),
                    chunks: u32_at(p, 37),
                };
                if m.kind != cmd.payload[0]
                    || m.index != u32_at(&cmd.payload, 1)
                    || !(1..=32768).contains(&m.chunk)
                    || m.chunk != u16::from_le_bytes(cmd.payload[5..7].try_into().unwrap()) as u32
                    || m.chunks != m.size / m.chunk + u32::from(m.size % m.chunk != 0)
                {
                    return Err("Selection metadata does not match request/size".into());
                }
                self.reset_download();
                self.selected = Some(m);
                self.status = "OK: selected; ready to download".into();
            }
            2 => {
                let m = self.selected.as_ref().ok_or("No selected file")?;
                if self.broken
                    || cmd.payload.len() != 4
                    || u32_at(&cmd.payload, 0) != self.next
                    || self.next >= m.chunks
                {
                    return Err("Unexpected chunk index".into());
                }
                let expected = (m.size as u64 - self.received).min(m.chunk as u64) as usize;
                if p.len() != expected {
                    return Err("Invalid chunk length".into());
                }
                self.output
                    .as_mut()
                    .ok_or("No output file")?
                    .write_all(p)
                    .map_err(|e| e.to_string())?;
                self.crc = crc_update(self.crc, p);
                self.received += p.len() as u64;
                self.next += 1;
                if self.next == m.chunks {
                    self.finish()?;
                } else if self.automatic {
                    self.busy = true;
                    self.status = "Downloading".into();
                    return Ok(Some(self.get_command()));
                } else {
                    self.status = "OK: chunk saved; ready for next chunk".into();
                }
            }
            3 | 5 => {
                if p.len() != 4 {
                    return Err("Invalid ACK (expected uptime U32)".into());
                }
                if cmd.addr == 3 {
                    self.selected = None;
                    self.output = None;
                    self.automatic = false;
                    self.status = "OK: deselected".into();
                } else {
                    self.status = format!("OK: mark {} applied", cmd.payload[0]);
                }
            }
            _ => return Err("Unexpected data-transfer address".into()),
        }
        Ok(None)
    }
    pub fn lines(&self) -> Vec<String> {
        let mut v = vec![
            format!("State: {}", self.status),
            format!(
                "List filter: {}",
                self.listed_filter
                    .map(|x| x.to_string())
                    .unwrap_or_else(|| "not listed".into())
            ),
        ];
        if let Some(c) = self.counts {
            v.push(format!("HK: {}   SCI1: {}   SCI2: {}", c[0], c[1], c[2]));
        }
        if let Some(m) = &self.selected {
            v.extend([
                format!("Selected: type {} / index {}", m.kind, m.index),
                format!("File: {}", m.name),
                format!("Size: {} bytes", m.size),
                format!("Chunk: {} bytes / {} chunks", m.chunk, m.chunks),
                format!("Expected CRC32: {:08X}", m.crc),
            ]);
        }
        v.extend([
            format!("Received: {} bytes / {} chunks", self.received, self.next),
            format!(
                "Local CRC32: {:08X} ({})",
                !self.crc,
                if self.verified {
                    "verified"
                } else {
                    "not verified"
                }
            ),
            format!(
                "Local file: {}",
                if self.path.is_empty() {
                    "not started"
                } else {
                    &self.path
                }
            ),
            "Destination: DAL_TUI_DOWNLOAD_DIR or ./downloads".into(),
            "Files are saved on the machine running DALTUI.".into(),
            "Use one file-transfer client at a time.".into(),
        ]);
        v
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn crc_iso_hdlc() {
        assert_eq!(!crc_update(!0, b"123456789"), 0xcbf43926);
        assert_eq!(!crc_update(!0, b""), 0);
    }
    #[test]
    fn selection_uses_u32_index_and_seven_bytes() {
        let mut t = Transfer::default();
        t.counts = Some([u32::MAX, 0, 0]);
        t.index = 65536;
        let c = t.plan("obc.dat_select").unwrap().remove(0);
        assert_eq!(c.payload, [0, 0, 0, 1, 0, 0, 16]);
        assert!(t.plan("obc.dat_list").is_err());
    }
    #[test]
    fn strict_metadata_and_counts() {
        let mut t = Transfer::default();
        let c = t.plan("obc.dat_list").unwrap().remove(0);
        assert!(t.reply(&c, &[0; 11]).is_err());
        assert!(t.reply(&c, &[0; 12]).is_ok());
        let c = command(1, "".into(), vec![0, 0, 0, 0, 0, 64, 0]);
        let mut p = vec![0; 41];
        p[5] = 64;
        p[37] = 1;
        assert!(t.reply(&c, &p).is_err());
        p[37] = 0;
        assert!(t.reply(&c, &p).is_ok());
    }
}

#[cfg(test)]
mod download_tests {
    use super::*;
    fn transfer(data: &[u8], chunk: u32) -> (Transfer, PathBuf) {
        let root = std::env::temp_dir().join(format!(
            "daltui-transfer-{}-{}",
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir(&root).unwrap();
        let mut t = Transfer::default();
        t.selected = Some(Metadata {
            kind: 0,
            index: 0,
            chunk,
            name: "test.bin".into(),
            size: data.len() as u32,
            crc: !crc_update(!0, data),
            chunks: (data.len() as u32).div_ceil(chunk),
        });
        t.part = root.join("test.bin.part");
        t.final_path = root.join("test.bin");
        t.path = t.part.display().to_string();
        t.output = Some(
            OpenOptions::new()
                .write(true)
                .create_new(true)
                .open(&t.part)
                .unwrap(),
        );
        (t, root)
    }
    #[test]
    fn automatic_download_verifies_and_publishes() {
        let (mut t, root) = transfer(b"123456789", 4);
        let c = t.plan("obc.dat_download").unwrap().remove(0);
        let next = t.reply(&c, b"1234").unwrap().unwrap();
        assert_eq!(next.payload, 1u32.to_le_bytes());
        let next = t.reply(&next, b"5678").unwrap().unwrap();
        assert!(t.reply(&next, b"9").unwrap().is_none());
        assert!(t.verified);
        assert_eq!(fs::read(&t.final_path).unwrap(), b"123456789");
        assert!(!t.part.exists());
        assert!(t.plan("obc.dat_download").is_err());
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn pause_waits_for_inflight_chunk_then_manual_resume() {
        let (mut t, root) = transfer(b"abcdef", 3);
        let c = t.plan("obc.dat_download").unwrap().remove(0);
        assert!(t.plan("obc.dat_delete").is_err());
        assert!(t.plan("obc.dat_cancel").unwrap().is_empty());
        assert!(t.reply(&c, b"abc").unwrap().is_none());
        assert!(!t.verified);
        let c = t.plan("obc.dat_read").unwrap().remove(0);
        assert!(t.reply(&c, b"def").unwrap().is_none());
        assert!(t.verified);
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn crc_mismatch_retains_partial_and_never_publishes() {
        let (mut t, root) = transfer(b"abc", 3);
        let c = t.plan("obc.dat_download").unwrap().remove(0);
        let error = t.reply(&c, b"xyz").unwrap_err();
        t.fail(&error);
        assert!(!t.verified);
        assert!(t.part.exists());
        assert!(!t.final_path.exists());
        assert!(t.plan("obc.dat_delete").is_err());
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn empty_file_needs_no_get_and_existing_destination_is_safe() {
        let (mut t, root) = transfer(b"", 64);
        assert!(t.plan("obc.dat_download").unwrap().is_empty());
        assert!(t.verified);
        fs::remove_dir_all(root).unwrap();
        let (mut t, root) = transfer(b"a", 64);
        fs::write(&t.final_path, b"existing").unwrap();
        let c = t.plan("obc.dat_download").unwrap().remove(0);
        assert!(t.reply(&c, b"a").is_err());
        assert_eq!(fs::read(&t.final_path).unwrap(), b"existing");
        assert!(t.part.exists());
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn wrong_chunk_length_does_not_advance_progress() {
        let (mut t, root) = transfer(b"abc", 3);
        let c = t.plan("obc.dat_read").unwrap().remove(0);
        assert!(t.reply(&c, b"ab").is_err());
        assert_eq!(t.received, 0);
        assert_eq!(t.next, 0);
        drop(t);
        fs::remove_dir_all(root).unwrap();
    }
}
