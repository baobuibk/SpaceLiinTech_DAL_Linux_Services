//! BEE-DAL wire protocol: RS-422 frames, command builders and payload decoders.
//!
//! Every `Cmd` carries both forms of one request: the flight-console text line
//! and the binary `(type, addr, payload)` triple. The console translates the
//! text into the same triple (`parse_command` in flight_console_service.c), and
//! its `json` reply returns the TM payload as `payload_hex`. Both transports
//! therefore feed the same decoders below.

use std::time::Duration;

pub const START: u8 = 0xA5;
pub const ERR_MARK: u8 = 0xFF;
/// Inter-byte gap after which the flight decoder resynchronises.
pub const INTER_BYTE_MS: u64 = 500;
const MAX_PAYLOAD: usize = 32768;

// ── CRC-16/CCSDS ──────────────────────────────────────────────────────────────

pub fn crc16(data: &[u8]) -> u16 {
    let mut crc = 0xFFFFu16;
    for &b in data {
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

// ── Framing ───────────────────────────────────────────────────────────────────

/// `A5 TYPE ADDR LEN_LO LEN_HI PAYLOAD CRC_LO CRC_HI`.
pub fn encode_tc(ty: u8, addr: u8, payload: &[u8]) -> Vec<u8> {
    let mut f = Vec::with_capacity(payload.len() + 7);
    f.push(START);
    f.push(ty);
    f.push(addr);
    f.extend_from_slice(&(payload.len() as u16).to_le_bytes());
    f.extend_from_slice(payload);
    f.extend_from_slice(&crc16(payload).to_le_bytes());
    f
}

#[derive(Debug, Clone, PartialEq)]
pub struct Tm {
    /// Echoed TYPE, or 0xFF for an error frame.
    pub ack: u8,
    /// Echoed ADDR, or the error code when `ack == 0xFF`.
    pub code: u8,
    pub payload: Vec<u8>,
}

impl Tm {
    pub fn is_err(&self) -> bool {
        self.ack == ERR_MARK
    }
}

#[derive(Debug, PartialEq)]
pub enum Fed {
    More,
    Frame(Tm),
    CrcError,
}

/// Byte-at-a-time TM decoder, same state machine as the flight and Python ones.
#[derive(Default)]
pub struct Decoder {
    state: u8,
    ack: u8,
    code: u8,
    len: usize,
    payload: Vec<u8>,
    crc_rx: u16,
}

impl Decoder {
    pub fn new() -> Decoder {
        Decoder::default()
    }

    pub fn reset(&mut self) {
        *self = Decoder::default();
    }

    /// True while a frame is partly received (the idle timeout applies).
    pub fn busy(&self) -> bool {
        self.state != 0
    }

    pub fn feed(&mut self, b: u8) -> Fed {
        match self.state {
            0 => {
                if b == START {
                    self.state = 1;
                }
            }
            1 => {
                self.ack = b;
                self.state = 2;
            }
            2 => {
                self.code = b;
                self.state = 3;
            }
            3 => {
                self.len = b as usize;
                self.state = 4;
            }
            4 => {
                self.len |= (b as usize) << 8;
                self.payload = Vec::with_capacity(self.len.min(MAX_PAYLOAD));
                if self.len > MAX_PAYLOAD {
                    self.reset();
                } else {
                    self.state = if self.len == 0 { 6 } else { 5 };
                }
            }
            5 => {
                self.payload.push(b);
                if self.payload.len() >= self.len {
                    self.state = 6;
                }
            }
            6 => {
                self.crc_rx = b as u16;
                self.state = 7;
            }
            _ => {
                self.crc_rx |= (b as u16) << 8;
                let ok = self.crc_rx == crc16(&self.payload);
                let tm = Tm {
                    ack: self.ack,
                    code: self.code,
                    payload: std::mem::take(&mut self.payload),
                };
                self.reset();
                return if ok { Fed::Frame(tm) } else { Fed::CrcError };
            }
        }
        Fed::More
    }
}

// ── Commands ──────────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Board {
    Obc = 3,
    Exp = 4,
    Pdu = 5,
}

impl Board {
    pub fn name(self) -> &'static str {
        match self {
            Board::Obc => "OBC",
            Board::Exp => "EXP",
            Board::Pdu => "PDU",
        }
    }
    /// Switchable outputs, bit order of `pwr_en`.
    pub fn outputs(self) -> &'static [&'static str] {
        match self {
            Board::Obc => &[
                "AI_power",
                "FPGA_power",
                "USB1_power",
                "USB2_power",
                "USB3_power",
                "USB4_power",
                "MIPI1_power",
                "MIPI2_power",
            ],
            Board::Exp => &["EXP12V", "SLN_HD", "MOTOR"],
            Board::Pdu => &["VOUT", "TRANS"],
        }
    }
}

#[derive(Debug, Clone)]
pub struct Cmd {
    /// Console form, without the `json ` prefix.
    pub text: String,
    pub ty: u8,
    pub addr: u8,
    pub payload: Vec<u8>,
    pub timeout: Duration,
}

fn cmd(text: impl Into<String>, ty: u8, addr: u8, payload: Vec<u8>, secs: u64) -> Cmd {
    Cmd {
        text: text.into(),
        ty,
        addr,
        payload,
        timeout: Duration::from_secs(secs),
    }
}

pub fn ping() -> Cmd {
    cmd("ping", 0, 0, vec![], 3)
}
pub fn sys_info() -> Cmd {
    cmd("sys_info", 1, 0, vec![], 3)
}
pub fn sys_rtc_get() -> Cmd {
    cmd("sys_rtc", 1, 1, vec![], 3)
}
pub fn sys_rtc_set(epoch: u32) -> Cmd {
    cmd(
        format!("sys_rtc {epoch}"),
        1,
        1,
        epoch.to_le_bytes().to_vec(),
        3,
    )
}
pub fn pdu_vout_get() -> Cmd {
    cmd("pdu_vout get", 1, 2, vec![], 3)
}
pub fn pdu_vout_set(mv: u16) -> Cmd {
    cmd(
        format!("pdu_vout set {mv}"),
        1,
        2,
        mv.to_le_bytes().to_vec(),
        3,
    )
}
pub fn pdu_fault_clear() -> Cmd {
    cmd("pdu_fault_clear", 1, 0x1c, vec![], 3)
}
pub fn pdu_heartbeat_get() -> Cmd {
    cmd("pdu_heartbeat get", 1, 0x1d, vec![], 3)
}
pub fn pdu_heartbeat_set(action: u8, value: u32) -> Cmd {
    let text = match action {
        1 => format!(
            "pdu_heartbeat enable {}",
            if value != 0 { "on" } else { "off" }
        ),
        2 => format!("pdu_heartbeat on {value}"),
        3 => format!("pdu_heartbeat off {value}"),
        _ => unreachable!("invalid PDU heartbeat action"),
    };
    let mut payload = vec![action];
    payload.extend_from_slice(&value.to_le_bytes());
    cmd(text, 1, 0x1d, payload, 3)
}
pub fn pdu_bootloader_enter() -> Cmd {
    cmd("pdu_bootloader enter", 1, 0x1e, vec![], 3)
}
pub fn tm_obc_get() -> Cmd {
    cmd("tm_obc_get", 4, 2, vec![], 3)
}
pub fn thm_cfg_get() -> Cmd {
    cmd("thm_cfg get", 1, 0x13, vec![], 20)
}
pub fn thm_cfg_set(i: u8, p: &crate::sim::HeaterProfile) -> Cmd {
    let mut b = vec![0u8; 32];
    b[0] = 1;
    b[1] = 1 << i;
    let o = 2 + 4 * i as usize;
    b[o] = p.max_duty as u8;
    b[o + 1] = p.manual_slew as u8;
    b[o + 2..o + 4].copy_from_slice(&((p.max_on / 10) as u16).to_le_bytes());
    cmd(
        format!(
            "thm_cfg heater {i} set max_duty {} slew {} max_on_ms {}",
            p.max_duty, p.manual_slew, p.max_on
        ),
        1,
        0x13,
        b,
        20,
    )
}
pub fn thm_ctl(i: u8, duty: u8, duration: u32) -> Cmd {
    let mut b = vec![0u8; 8];
    b[1] = i;
    if duty != 0 {
        b[2] = 1;
        b[3] = duty;
        b[4..8].copy_from_slice(&duration.to_le_bytes());
    }
    let text = if duty == 0 {
        format!("thm_ctl heater {i} off")
    } else {
        format!("thm_ctl heater {i} set {duty} {duration}")
    };
    cmd(text, 1, 0x14, b, 20)
}
pub fn exp_csp_snapshot(table: u8) -> Cmd {
    cmd(format!("exp_csp_snapshot {table}"), 2, 0x0f, vec![table], 3)
}

pub fn thp_cfg_get(i: u8) -> Cmd {
    let mut b = vec![0u8; 32];
    b[2] = i;
    cmd(format!("thp_cfg heater get {i}"), 2, 0x03, b, 20)
}
pub fn thp_cfg_set(i: u8, p: &crate::sim::HeaterProfile) -> Cmd {
    let mut b = vec![0u8; 32];
    b[0] = 1;
    b[2] = i;
    b[3] = p.main_ntc as u8;
    b[4] = p.sec_ntc as u8;
    b[5..7].copy_from_slice(&(p.target as i16).to_le_bytes());
    b[7..9].copy_from_slice(&(p.delta as i16).to_le_bytes());
    b[9..11].copy_from_slice(&(p.cutoff as i16).to_le_bytes());
    b[11..13].copy_from_slice(&(p.timeout as u16).to_le_bytes());
    b[13] = u8::from(p.resume_boot);
    b[14] = p.slew_up as u8;
    b[15] = p.slew_down as u8;
    for (o, v) in [(16, p.kp), (20, p.ki), (24, p.kd)] {
        b[o..o + 4].copy_from_slice(&((v as f32) / 1000.0).to_le_bytes());
    }
    cmd(
        format!(
            "thp_cfg heater set {i} {} {} {} {} {} {} {} {} {} {:.3} {:.3} {:.3}",
            p.main_ntc,
            p.sec_ntc,
            p.target,
            p.delta,
            p.cutoff,
            p.timeout,
            u8::from(p.resume_boot),
            p.slew_up,
            p.slew_down,
            p.kp as f32 / 1000.0,
            p.ki as f32 / 1000.0,
            p.kd as f32 / 1000.0
        ),
        2,
        0x03,
        b,
        20,
    )
}
pub fn thp_ctl(i: u8, action: u8) -> Cmd {
    let name = ["status", "start", "stop", "resume"][action as usize];
    cmd(
        format!("thp_ctl heater {i} {name}"),
        2,
        0x04,
        vec![i | (action << 3)],
        20,
    )
}

pub fn thm_stat() -> Cmd {
    cmd("thm_stat", 1, 0x15, vec![], 20)
}
pub fn fld_stat() -> Cmd {
    cmd("fld_stat", 1, 0x18, vec![], 20)
}
pub fn rba_stat() -> Cmd {
    cmd("rba_stat", 1, 0x1B, vec![], 20)
}
pub fn pc_stat() -> Cmd {
    cmd("pc_stat", 2, 0x00, vec![], 20)
}
pub fn pwr_stat(board: Board) -> Cmd {
    let mut p = vec![board as u8];
    p.extend_from_slice(&0u16.to_le_bytes());
    cmd(
        format!("pwr_stat {} get", board.name()),
        1,
        4,
        p,
        if board == Board::Exp { 20 } else { 3 },
    )
}

/// Switch one output. `force` is only valid for EXP off.
pub fn pwr_en(board: Board, output: usize, on: bool, force: bool) -> Cmd {
    let select = 1u16 << output;
    pwr_en_mask(
        board,
        select,
        if on { select } else { 0 },
        force,
        Some(output),
    )
}

/// Several outputs at once: `pwr_en <board> set <select> <on>`.
pub fn pwr_en_mask(board: Board, select: u16, on: u16, force: bool, single: Option<usize>) -> Cmd {
    let mut p = vec![board as u8];
    p.extend_from_slice(&select.to_le_bytes());
    p.extend_from_slice(&on.to_le_bytes());
    p.push(force as u8);
    let tail = if force { " force" } else { "" };
    let text = match single {
        Some(o) => format!(
            "pwr_en {} {} {}{}",
            board.name(),
            board.outputs()[o],
            if on != 0 { "on" } else { "off" },
            tail
        ),
        None => format!("pwr_en {} set 0x{select:x} 0x{on:x}{tail}", board.name()),
    };
    let n = select.count_ones() as u64;
    cmd(
        text,
        1,
        3,
        p,
        if board == Board::Exp { 15 * (n + 1) } else { 3 },
    )
}

pub fn uvc_capture(camera: u8, id: u32) -> Cmd {
    let mut p = vec![camera, 1, 0, 0];
    p.extend_from_slice(&id.to_le_bytes());
    cmd(format!("uvc_ctl {camera} capture {id}"), 1, 0x11, p, 5)
}

pub fn uvc_led_pwm(camera: u8, duty: u8) -> Cmd {
    let mut p = vec![0; 32];
    p[0] = camera;
    p[1] = 1;
    p[3] = 0x10; // field mask bit 12
    p[31] = duty;
    cmd(
        format!("uvc_cfg {camera} set led_pwm {duty}"),
        1,
        0x10,
        p,
        5,
    )
}

pub fn uvc_cfg_get(camera: u8) -> Cmd {
    cmd(format!("uvc_cfg {camera} get"), 1, 0x10, vec![camera], 5)
}

/// Local flight console helper; never sent as a binary RS-422 frame.
pub fn gui_hw(args: &str, seconds: u64) -> Cmd {
    cmd(
        format!("gui_shell {seconds} /proc/$PPID/exe --gui-hw {args}"),
        0xFE,
        0,
        vec![],
        seconds + 5,
    )
}
/// Local administrator command on the flight console socket.
pub fn port_cfg(args: &str) -> Cmd {
    cmd(format!("port_cfg {args}"), 0xFE, 0, vec![], 5)
}
pub fn uvc_stat(camera: u8, id: u32) -> Cmd {
    let mut p = vec![camera];
    p.extend_from_slice(&id.to_le_bytes());
    cmd(format!("uvc_stat {camera} get {id}"), 1, 0x12, p, 5)
}

/// `fld_ctl valve mask <0..15>` (01/17); all four logical V1–V4 directions.
pub fn fld_valve_mask(mask: u8) -> Cmd {
    cmd(
        format!("fld_ctl valve mask {}", mask & 0x0f),
        1,
        0x17,
        vec![0, 0, 0, mask & 0x0f, 0, 0, 0, 0],
        20,
    )
}

/// `fld_ctl pump <u11|u15> ...` (01/17). `dev` 1 = U11, 2 = U15.
/// `op`: 0 off, 1 on, 2 frequency (Hz), 3 voltage (Vpp; `ch` selects the U15 channel).
pub fn fld_pump(dev: u8, op: u8, ch: u8, value: u32) -> Cmd {
    let mut p = vec![dev, op, ch, 0];
    p.extend_from_slice(&value.to_le_bytes());
    let unit = if dev == 1 { "u11" } else { "u15" };
    let tail = match (op, dev) {
        (0, _) => "off".to_string(),
        (1, _) => "on".to_string(),
        (2, _) => format!("frequency {value}"),
        (_, 1) => format!("voltage {value}"),
        _ => format!("voltage {ch} {value}"),
    };
    cmd(format!("fld_ctl pump {unit} {tail}"), 1, 0x17, p, 20)
}

/// `fld_cfg pump detect <u11|u15>` (01/16, asynchronous: replies with a request id).
pub fn fld_pump_detect(unit: u8) -> Cmd {
    let mut p = vec![0u8; 32];
    p[0] = 1;
    p[1] = 9;
    p[3] = unit;
    cmd(
        format!(
            "fld_cfg pump detect {}",
            if unit == 0 { "u11" } else { "u15" }
        ),
        1,
        0x16,
        p,
        20,
    )
}

/// `fld_cfg pump result <id>`.
pub fn fld_pump_result(id: u32) -> Cmd {
    let mut p = vec![0u8; 32];
    p[0] = 1;
    p[1] = 9;
    p[2] = 1;
    p[3] = 0xFF;
    p[4..8].copy_from_slice(&id.to_le_bytes());
    cmd(format!("fld_cfg pump result {id}"), 1, 0x16, p, 20)
}

/// `fld_cfg pump_type <lowdriver|highdriver>`.
pub fn fld_pump_type(high: bool) -> Cmd {
    let mut p = vec![0u8; 32];
    p[0] = 1;
    p[1] = 8;
    p[4] = high as u8;
    cmd(
        format!(
            "fld_cfg pump_type {}",
            if high { "highdriver" } else { "lowdriver" }
        ),
        1,
        0x16,
        p,
        20,
    )
}

/// `rba_ctl` (01/1A), fixed 8-byte request: action, motor, speed, 0, value.
fn rba(action: u8, motor: u8, speed: u8, value: u32, text: String) -> Cmd {
    let mut p = vec![action, motor, speed, 0];
    p.extend_from_slice(&value.to_le_bytes());
    cmd(text, 1, 0x1A, p, 20)
}
pub fn rba_init() -> Cmd {
    rba(1, 2, 0, 0, "rba_ctl init".into())
}
pub fn rba_stop(motor: Option<u8>) -> Cmd {
    match motor {
        Some(m) => rba(6, m, 0, 0, format!("rba_ctl stop {m}")),
        None => rba(6, 2, 0, 0, "rba_ctl stop all".into()),
    }
}
pub fn rba_stop_hold(motor: u8) -> Cmd {
    rba(9, motor, 0, 0, format!("rba_ctl stop_hold {motor}"))
}
pub fn rba_home(motor: u8) -> Cmd {
    rba(5, motor, 0, 0, format!("rba_ctl home {motor}"))
}

/// Request id returned by `fld_ctl`/`rba_ctl` when the EXP queued the work.
pub fn dec_req_id(p: &[u8]) -> Option<u32> {
    (p.len() == 4).then(|| u32le(p, 0))
}

pub const PUMP_STATES: [&str; 12] = [
    "queued",
    "accepted",
    "done",
    "refused",
    "io_error",
    "power_lost",
    "cancelled",
    "busy",
    "interlocked",
    "not_ready",
    "invalid",
    "unknown",
];

/// `fld_cfg pump detect|result` reply: (target 0 u11 / 1 u15 / 0xFF unspecified, state index, request id).
pub fn dec_pump_req(p: &[u8]) -> Option<(u8, u8, u32)> {
    (p.len() == 32 && p[0] == 1 && p[1] == 9).then(|| (p[3], p[8], u32le(p, 4)))
}

/// EXP UART0 CLI command (`exp_run`, TYPE 02 / ADDR 0C). Limit 95 bytes.
pub fn exp_run(command: &str) -> Cmd {
    cmd(
        format!("exp_run {command}"),
        2,
        0x0C,
        command.as_bytes().to_vec(),
        30,
    )
}

/// Validated burn configuration console alias; RS-422 uses the EXP gateway frame.
pub fn burn_cfg_set(wire: usize, key: &str, value: i32) -> Cmd {
    let line = format!("burn_cfg_set {wire} {key} {value}");
    cmd(line.clone(), 2, 0x0C, line.into_bytes(), 30)
}

// ── Decoders ──────────────────────────────────────────────────────────────────

fn u16le(p: &[u8], o: usize) -> u16 {
    u16::from_le_bytes([p[o], p[o + 1]])
}
fn u32le(p: &[u8], o: usize) -> u32 {
    u32::from_le_bytes([p[o], p[o + 1], p[o + 2], p[o + 3]])
}
fn i16le(p: &[u8], o: usize) -> i16 {
    u16le(p, o) as i16
}
fn i32le(p: &[u8], o: usize) -> i32 {
    u32le(p, o) as i32
}

pub fn from_hex(s: &str) -> Option<Vec<u8>> {
    if s.len() % 2 != 0 {
        return None;
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(s.get(i..i + 2)?, 16).ok())
        .collect()
}

pub fn to_hex(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

/// `tm_obc_get`, 64 bytes (type 04 / addr 02).
#[derive(Debug, Clone, PartialEq)]
pub struct ObcHk {
    pub valid: u16,
    pub epoch: u32,
    pub boot: u32,
    pub onboard_c: f32,
    pub ntc_c: [f32; 4],
    pub bme_c: f32,
    pub press_pa: u32,
    pub rh: f32,
    pub gas_ohm: u32,
    /// PAC1934 U6 ch1-4 then U2 ch1-4.
    pub mv: [u16; 8],
    pub ma: [i16; 8],
}

pub fn dec_obc_hk(p: &[u8]) -> Option<ObcHk> {
    if p.len() != 64 {
        return None;
    }
    let mut ntc_c = [0.0; 4];
    for (i, v) in ntc_c.iter_mut().enumerate() {
        *v = i16le(p, 12 + 2 * i) as f32 / 100.0;
    }
    let mut mv = [0u16; 8];
    let mut ma = [0i16; 8];
    for i in 0..8 {
        mv[i] = u16le(p, 32 + 2 * i);
        ma[i] = i16le(p, 48 + 2 * i);
    }
    Some(ObcHk {
        valid: u16le(p, 0),
        epoch: u32le(p, 2),
        boot: u32le(p, 6),
        onboard_c: i16le(p, 10) as f32 / 100.0,
        ntc_c,
        bme_c: i16le(p, 20) as f32 / 100.0,
        press_pa: u32le(p, 22),
        rh: u16le(p, 26) as f32 / 100.0,
        gas_ohm: u32le(p, 28),
        mv,
        ma,
    })
}

/// One `pwr_stat` record.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PwrRec {
    pub bit: u8,
    pub state_known: bool,
    pub on: bool,
    pub mv: Option<u32>,
    pub ma: Option<i32>,
    /// u16::MAX = unknown.
    pub age_ms: u16,
}

pub fn dec_pwr_stat(p: &[u8]) -> Option<(Board, u32, Vec<PwrRec>)> {
    if p.len() < 6 || p.len() != 6 + 12 * p[1] as usize {
        return None;
    }
    let board = match p[0] {
        3 => Board::Obc,
        4 => Board::Exp,
        5 => Board::Pdu,
        _ => return None,
    };
    let recs = (0..p[1] as usize)
        .map(|i| {
            let o = 6 + 12 * i;
            let f = p[o + 1];
            PwrRec {
                bit: p[o],
                state_known: f & 1 != 0,
                on: f & 2 != 0,
                mv: (f & 4 != 0).then(|| u32le(p, o + 4)),
                ma: (f & 8 != 0).then(|| i32le(p, o + 8)),
                age_ms: u16le(p, o + 2),
            }
        })
        .collect();
    Some((board, u32le(p, 2), recs))
}

/// `pwr_en` reply: board, on mask, known mask, failed mask.
pub fn dec_pwr_en(p: &[u8]) -> Option<(u8, u16, u16, u16)> {
    (p.len() == 7).then(|| (p[0], u16le(p, 1), u16le(p, 3), u16le(p, 5)))
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct PduVout {
    pub set_mv: Option<u16>,
    pub mv: Option<u16>,
    pub ma: Option<u16>,
    pub trans_on: bool,
    pub vout_on: bool,
}

pub fn dec_pdu_vout(p: &[u8]) -> Option<PduVout> {
    if p.len() != 8 {
        return None;
    }
    let f = u16le(p, 6);
    Some(PduVout {
        set_mv: (f & 4 != 0).then(|| u16le(p, 0)),
        mv: (f & 8 != 0).then(|| u16le(p, 2)),
        ma: (f & 16 != 0).then(|| u16le(p, 4)),
        trans_on: f & 1 != 0,
        vout_on: f & 2 != 0,
    })
}

#[derive(Debug, Clone, PartialEq)]
pub struct BoardInfo {
    pub online: bool,
    pub hw: [u8; 3],
    pub fw: [u8; 3],
    pub build: String,
}

/// `sys_info`: 12-byte header then PDU, EXP, OBC records of 28 bytes.
pub fn dec_sys_info(p: &[u8]) -> Option<[BoardInfo; 3]> {
    if p.len() != 96 || p[0] != 1 || p[1] != 3 {
        return None;
    }
    let rec = |i: usize| {
        let r = &p[12 + 28 * i..12 + 28 * (i + 1)];
        BoardInfo {
            online: r[1] != 0,
            hw: [r[2], r[3], r[4]],
            fw: [r[5], r[6], r[7]],
            build: String::from_utf8_lossy(&r[8..28])
                .trim_end_matches('\0')
                .to_string(),
        }
    };
    Some([rec(0), rec(1), rec(2)])
}

/// `thm_stat`: heater 0-3 then IR 0-1, bit 7 running, bits 0-6 duty %.
pub fn dec_thm_stat(p: &[u8]) -> Option<[(bool, u8); 6]> {
    if p.len() != 6 {
        return None;
    }
    let mut o = [(false, 0); 6];
    for (i, v) in o.iter_mut().enumerate() {
        *v = (p[i] & 0x80 != 0, p[i] & 0x7F);
    }
    Some(o)
}

#[derive(Debug, Clone, PartialEq)]
pub struct FldStat {
    pub u11_rail: bool,
    pub u11_init: bool,
    pub u11_en: bool,
    pub u11_type: u8,
    pub u11_hz: u16,
    pub u11_vpp: u8,
    pub u15_init: bool,
    pub u15_en: bool,
    pub u15_hz: u16,
    pub u15_vpp: [u8; 4],
    pub valves: u8,
    pub valves_valid: u8,
}

pub fn dec_fld_stat(p: &[u8]) -> Option<FldStat> {
    if p.len() != 16 {
        return None;
    }
    let f = p[2];
    Some(FldStat {
        u11_rail: f & 1 != 0,
        u11_init: f & 2 != 0,
        u11_en: f & 4 != 0,
        u11_type: p[3],
        u11_hz: u16le(p, 4),
        u11_vpp: p[6],
        u15_init: f & 8 != 0,
        u15_en: f & 16 != 0,
        u15_hz: u16le(p, 7),
        u15_vpp: [p[9], p[10], p[11], p[12]],
        valves: p[0],
        valves_valid: p[1],
    })
}

#[derive(Debug, Clone, PartialEq)]
pub struct RbaStat {
    pub drv_init: bool,
    pub drv_fault: bool,
    /// 0 stopped 1 moving 2 jogging 3 homing 4 holding 5 error.
    pub state: [u8; 2],
    pub pos: [i32; 2],
    pub reverse: [bool; 2],
    pub sps: [u8; 2],
}

pub fn dec_rba_stat(p: &[u8]) -> Option<RbaStat> {
    if p.len() != 16 {
        return None;
    }
    Some(RbaStat {
        drv_init: p[0] & 1 != 0,
        drv_fault: p[0] & 4 != 0,
        state: [p[4], p[5]],
        pos: [i32le(p, 6), i32le(p, 10)],
        reverse: [p[3] & 4 != 0, p[3] & 8 != 0],
        sps: [p[14], p[15]],
    })
}

/// `pc_stat`: (state 0-6, activated, error).
pub fn dec_pc_stat(p: &[u8]) -> Option<(u8, bool, bool)> {
    (p.len() == 1).then(|| (p[0] & 7, p[0] & 8 != 0, p[0] & 16 != 0))
}

/// `uvc_stat` / `uvc_ctl` reply: (camera, state, error, request id).
/// State: 0 unknown 1 queued 2 capturing 3 saved 4 failed.
pub fn dec_uvc(p: &[u8]) -> Option<(u8, u8, u8, u32)> {
    (p.len() == 8).then(|| (p[0], p[1], p[2], u32le(p, 4)))
}

/// `exp_run` / `exp_tlm` gateway reply: (status, text).
pub fn dec_exp_text(p: &[u8]) -> Option<(u8, String)> {
    if p.len() < 3 {
        return None;
    }
    let n = (u16le(p, 1) as usize).min(p.len() - 3);
    Some((p[0], String::from_utf8_lossy(&p[3..3 + n]).to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;

    // Captured from the host dal_obc_fsw build (sandbox rig), `json <cmd>` -> payload_hex.
    const OBC_HK: &str = "ff01f70ee66a03000000dd09d204ea0bb80ba00f6009cd8b0100f21240e2010092138813e40c8813e40c46057e047e0483ffc8002c0190016400c8002c019001";
    const PDU_STAT: &str = "0504010fe66a001d00000000000000000000011d00000000000000000000021400007c2e000000000000031400009213000000000000";
    const SYS_INFO: &str = "010307005d0fe66a00000000010101000000010030314132423343340000000000000000000000000201010000010203346463376262622d64697274790000000000000003010100000001000000000000000000000000000000000000000000";

    #[test]
    fn crc_matches_ccsds_check_value() {
        assert_eq!(crc16(b"123456789"), 0x29B1);
        assert_eq!(crc16(&[]), 0xFFFF);
    }

    #[test]
    fn frame_round_trip_and_ping() {
        let f = encode_tc(0, 0, &[]);
        assert_eq!(f, vec![0xA5, 0, 0, 0, 0, 0xFF, 0xFF]);
        let mut d = Decoder::new();
        let mut got = None;
        for b in encode_tc(1, 3, &[1, 2, 3]) {
            if let Fed::Frame(t) = d.feed(b) {
                got = Some(t);
            }
        }
        assert_eq!(got.unwrap().payload, vec![1, 2, 3]);
    }

    #[test]
    fn bad_crc_is_reported_and_decoder_recovers() {
        let mut f = encode_tc(0, 0, &[9]);
        let n = f.len();
        f[n - 1] ^= 1;
        let mut d = Decoder::new();
        let mut last = Fed::More;
        for b in f {
            last = d.feed(b);
        }
        assert_eq!(last, Fed::CrcError);
        assert!(!d.busy());
    }

    #[test]
    fn decodes_obc_hk() {
        let h = dec_obc_hk(&from_hex(OBC_HK).unwrap()).unwrap();
        assert_eq!(h.valid, 0x01FF);
        assert_eq!(h.boot, 3);
        assert_eq!(h.onboard_c, 25.25);
        assert_eq!(h.ntc_c, [12.34, 30.5, 30.0, 40.0]);
        assert_eq!((h.press_pa, h.gas_ohm), (101325, 123456));
        assert_eq!(h.mv[0], 5010);
        assert_eq!(h.ma[0], -125);
    }

    #[test]
    fn decodes_pdu_power_status() {
        let (b, _, r) = dec_pwr_stat(&from_hex(PDU_STAT).unwrap()).unwrap();
        assert_eq!(b, Board::Pdu);
        assert_eq!(r.len(), 4);
        assert!(r[0].state_known && !r[0].on);
        assert_eq!(r[2].mv, Some(11900));
        assert_eq!(r[2].ma, None);
    }

    #[test]
    fn decodes_sys_info() {
        let i = dec_sys_info(&from_hex(SYS_INFO).unwrap()).unwrap();
        assert!(i[0].online && i[1].online && i[2].online);
        assert_eq!(i[0].build, "01A2B3C4");
        assert_eq!(i[1].fw, [1, 2, 3]);
    }

    #[test]
    fn heater_commands_match_icd_frames() {
        let p = crate::sim::HeaterProfile {
            target: 4500,
            cutoff: 9000,
            kp: 1750,
            ..Default::default()
        };
        let cfg = thp_cfg_set(2, &p);
        assert_eq!((cfg.ty, cfg.addr, cfg.payload.len()), (2, 3, 32));
        assert_eq!(&cfg.payload[..7], &[1, 0, 2, 0, 1, 0x94, 0x11]);
        assert_eq!(
            f32::from_le_bytes(cfg.payload[16..20].try_into().unwrap()),
            1.75
        );
        assert_eq!(
            cfg.text,
            "thp_cfg heater set 2 0 1 4500 500 9000 5000 0 10 10 1.750 0.250 0.000"
        );
        assert_eq!(thp_ctl(2, 1).payload, vec![0x0A]);
        assert_eq!(thp_ctl(2, 2).text, "thp_ctl heater 2 stop");
        let limits = thm_cfg_set(2, &p);
        assert_eq!((limits.ty, limits.addr, limits.payload[1]), (1, 0x13, 4));
        assert_eq!(&limits.payload[10..14], &[100, 10, 0x70, 0x17]);
        assert_eq!(
            thm_ctl(2, 50, 1500).payload,
            vec![0, 2, 1, 50, 0xDC, 5, 0, 0]
        );
    }

    #[test]
    fn builders_match_console_text() {
        let c = pwr_en(Board::Obc, 2, true, false);
        assert_eq!(c.text, "pwr_en OBC USB1_power on");
        assert_eq!(c.payload, vec![3, 4, 0, 4, 0, 0]);
        let c = pwr_en(Board::Exp, 1, false, true);
        assert_eq!(c.text, "pwr_en EXP SLN_HD off force");
        assert_eq!(c.payload, vec![4, 2, 0, 0, 0, 1]);
        assert_eq!(uvc_capture(2, 7).payload, vec![2, 1, 0, 0, 7, 0, 0, 0]);
        assert_eq!(pdu_vout_set(12000).payload, vec![0xE0, 0x2E]);
        let hb = pdu_heartbeat_set(2, 1500);
        assert_eq!(hb.text, "pdu_heartbeat on 1500");
        assert_eq!(
            (hb.ty, hb.addr, hb.payload),
            (1, 0x1d, vec![2, 220, 5, 0, 0])
        );
        assert_eq!(pdu_heartbeat_get().payload, Vec::<u8>::new());
        assert_eq!(pdu_bootloader_enter().text, "pdu_bootloader enter");
        let clear = pdu_fault_clear();
        assert_eq!(
            (
                clear.text.as_str(),
                clear.ty,
                clear.addr,
                clear.payload.as_slice()
            ),
            ("pdu_fault_clear", 1, 0x1c, &[][..])
        );
        let c = fld_pump(2, 3, 1, 40);
        assert_eq!(c.text, "fld_ctl pump u15 voltage 1 40");
        assert_eq!(c.payload, vec![2, 3, 1, 0, 40, 0, 0, 0]);
        assert_eq!(rba_stop(None).text, "rba_ctl stop all");
        assert_eq!(rba_stop_hold(1).payload, vec![9, 1, 0, 0, 0, 0, 0, 0]);
        assert_eq!(fld_pump_detect(1).payload[..4], [1, 9, 0, 1]);
    }
}

/// PC experiment configuration, ICD 02/01. Set replies echo all 32 bytes.
pub fn pc_cfg(text: String, mut payload: [u8; 32]) -> Cmd {
    payload[0] = 1;
    cmd(text, 2, 1, payload.to_vec(), 30)
}

/// PC experiment operation, ICD 02/02.
pub fn pc_ctl(op: u8, choice: u8, text: String) -> Cmd {
    cmd(text, 2, 2, vec![op, choice, 0, 0, 0, 0, 0, 0], 30)
}
