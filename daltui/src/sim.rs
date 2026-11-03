//! Simulated DAL subsystems. Field names follow the real telemetry so a real
//! backend (flight-software `gui_state` socket) can fill the same structs later.

use crate::controls::Sub;
use crate::link::Mode;
use crate::live::{default_target, Act, Live};
use std::time::{SystemTime, UNIX_EPOCH};

pub const PC_FIELDS: [&str; 16] = [
    "duration",
    "minflow",
    "flow_action",
    "pump_freq",
    "pump_volt",
    "volume",
    "pump_on",
    "pump_off",
    "cycles",
    "pump_off_settle",
    "run_mode",
    "max_pump_on",
    "flow_check",
    "flow_delay",
    "fail_samples",
    "pump_enable",
];

pub fn pc_field_index(id: &str) -> Option<usize> {
    id.strip_prefix("exp.pc_")
        .and_then(|name| PC_FIELDS.iter().position(|field| *field == name))
}

pub fn pc_field_allowed(target: usize, index: usize) -> bool {
    let field = index + 1;
    match target {
        0 => matches!(field, 10..=12),
        1 | 2 => matches!(field, 1..=6 | 13..=16),
        3 => matches!(field, 2..=9 | 13..=14 | 16),
        _ => false,
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Level {
    Info,
    Ok,
    Warn,
    Err,
}

pub struct LogMsg {
    pub sub: Sub,
    pub level: Level,
    pub text: String,
}

pub enum Val {
    Bool(bool),
    Num(i32),
    Idx(usize),
    None,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ShotState {
    Idle,
    Capturing,
    Saved,
    Failed,
}

/// Result of the last camera capture.
#[derive(Clone)]
pub struct Shot {
    pub state: ShotState,
    pub path: String,
    pub bytes: u64,
    pub left: f64,
    pub note: String,
}

impl Shot {
    pub fn idle() -> Shot {
        Shot::new()
    }
    fn new() -> Shot {
        Shot {
            state: ShotState::Idle,
            path: String::new(),
            bytes: 0,
            left: 0.0,
            note: String::new(),
        }
    }
}

pub struct Pump1 {
    pub ptype: usize, // applied: 0 lowdriver 1 highdriver
    pub ptype_configured: bool,
    pub ptype_staged: usize,
    pub detected: bool,
    pub inited: bool,
    pub freq: i32,
    pub freq_staged: i32,
    pub volt: i32,
    pub volt_staged: i32,
    pub en: bool,
}

pub struct Pump4 {
    pub detected: bool,
    pub inited: bool,
    pub ch: usize,
    pub freq: i32,
    pub freq_staged: i32,
    pub volt: [i32; 4],
    pub volt_staged: i32,
    pub en: bool,
}

pub struct Drv {
    pub detected: bool,
    pub inited: bool,
    pub fault: bool,
}

#[derive(Clone, Copy)]
pub struct Motor {
    pub pos: f32,
    pub running: bool,
    pub homing: bool,
    pub holding: bool,
    pub dir: i32,
    pub sps: i32,
    pub remaining_steps: f32,
}

pub struct Rail {
    pub name: &'static str,
    pub mv: i32,
    pub ma: i32,
}

#[derive(Clone, Copy)]
pub struct IrSetup {
    pub duty: i32,
    pub max_duty: i32,
    pub max_on_ms: i32,
    pub pwm_hz: i32,
}

#[derive(Clone, Copy, Default)]
pub struct BurnSetup {
    pub duty: i32,
    pub burn_time_ms: i32,
    pub max_total_current_ma: i32,
}

impl BurnSetup {
    pub fn valid(&self) -> bool {
        (1..=100).contains(&self.duty)
            && (50..=1000).contains(&self.burn_time_ms)
            && (1..=3000).contains(&self.max_total_current_ma)
    }
}

impl Rail {
    pub fn mw(&self) -> i32 {
        self.mv * self.ma / 1000
    }
}

pub fn unix_now() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

pub struct Obc {
    pub fsw_up: bool,
    pub rs422_up: bool,
    pub rx_frames: u32,
    pub tx_frames: u32,
    pub boot_count: u32,
    pub uptime: f64,
    pub rtc_offset: i64,
    pub cpu_pct: f32,
    pub mem_pct: f32,
    pub disk_pct: f32,
    pub temp_onboard: f32,
    pub ntc: [f32; 4],
    pub bme_t: f32,
    pub bme_p_pa: i32,
    pub bme_rh: f32,
    pub bme_gas: i32,
    pub u6: [Rail; 4],
    pub u2: [Rail; 4],
    pub ai_efuse: bool,
    pub fpga_efuse: bool,
    pub usb: [bool; 4],
    pub mipi: [bool; 2],
    pub led_pwm: i32,
    pub usb_led_pwm: [i32; 4],
    pub usb_sel: usize,
    pub usb_shot: [Shot; 4],
    pub mipi_sel: usize,
    pub mipi_sensor: [usize; 2],
    pub mipi_route: Option<(usize, usize)>,
    pub mipi_shot: Shot,
    pub snapshot_age: f64,
}

#[derive(Clone, Copy)]
pub struct HeaterProfile {
    pub duty: i32,
    pub duration: i32,
    pub max_duty: i32,
    pub manual_slew: i32,
    pub max_on: i32,
    pub main_ntc: usize,
    pub sec_ntc: usize,
    pub target: i32,
    pub delta: i32,
    pub cutoff: i32,
    pub timeout: i32,
    pub resume_boot: bool,
    pub slew_up: i32,
    pub slew_down: i32,
    pub kp: i32,
    pub ki: i32,
    pub kd: i32,
}
impl Default for HeaterProfile {
    fn default() -> Self {
        Self {
            duty: 10,
            duration: 1000,
            max_duty: 100,
            manual_slew: 10,
            max_on: 60000,
            main_ntc: 0,
            sec_ntc: 1,
            target: 3000,
            delta: 500,
            cutoff: 8000,
            timeout: 5000,
            resume_boot: false,
            slew_up: 10,
            slew_down: 10,
            kp: 1500,
            ki: 250,
            kd: 0,
        }
    }
}

pub struct Exp {
    pub link: bool,
    pub node: u8,
    pub uptime: f64,
    pub age: f64,
    pub sln: bool,
    pub motor: bool,
    pub exp12v: bool,
    pub pac: [Rail; 4],
    pub ntc: [f32; 4],
    pub board_t: f32,
    pub bme_t: f32,
    pub bme_p_pa: i32,
    pub bme_rh: f32,
    pub heater: [i32; 4],
    pub ht_sel: usize,
    pub ht_profile: [HeaterProfile; 4],
    pub ht_loaded: [bool; 4],
    pub ht_phase: [u8; 4],
    pub ht_reason: [u8; 4],
    pub ht_configured: [bool; 4],
    pub ht_running: [bool; 4],
    pub ht_status_seen: [bool; 4],
    pub ht_status_at: Option<f64>,
    pub ht_main_temp: [f32; 4],
    pub ht_sec_temp: [f32; 4],
    pub ht_requested: [u8; 4],
    pub ht_applied: [u8; 4],
    pub ht_limit: [u8; 4],
    pub ht_target_actual: [f32; 4],
    pub ht_manual_left: [f64; 4],
    pub u11: Pump1,
    pub u15: Pump4,
    pub drv: Drv,
    pub motors: [Motor; 2],
    pub mc: usize,
    pub mdir: usize,
    pub msps: i32,
    pub move_steps: [i32; 2],
    pub motor_mode: [usize; 2],
    pub motor_mode_draft: [usize; 2],
    pub motor_mode_verified: [bool; 2],
    pub map_motor: usize,
    pub motor_max_sps: [i32; 2],
    pub motor_drive_pct: [i32; 2],
    pub motor_drive_applied_pct: [i32; 2],
    pub motor_drive_dirty: [bool; 2],
    pub motor_pwm: [usize; 2],
    pub motor_bank: [usize; 2],
    pub motor_min: [usize; 2],
    pub motor_max: [usize; 2],
    pub motor_home_end: [usize; 2],
    pub motor_min_level: [usize; 2],
    pub motor_max_level: [usize; 2],
    pub motor_limits_verified: [bool; 2],
    pub flow: [f32; 2],
    pub bubble: [bool; 2],
    pub ir: [bool; 2],
    pub ir_actual_duty: [i32; 2],
    pub ir_status_seen: [bool; 2],
    pub ir_sel: usize,
    pub ir_setup: [IrSetup; 2],
    pub ir_configured: [bool; 2],
    /// Time at which IR limits were queued; cleared once status confirms them.
    pub ir_config_pending: [Option<f64>; 2],
    pub cam_sel: usize,
    pub camera: [IrCamera; 2],
    pub burn_sel: usize,
    pub burn_setup: [BurnSetup; 2],
    pub burn_configured: [bool; 2],
    pub burn_armed_until: Option<(usize, f64)>,
    pub burn_state: String,
    pub burn_active_wire: Option<usize>,
    pub burn_last: [String; 2],
    pub burn_attempt: [u32; 2],
    pub burn_peak_ma: [u32; 2],
    pub burn_status_seen: [bool; 2],
    pub burn_left: f32,
    pub burn_count: u32,
    pub pc_running: bool,
    pub pc_progress: f32,
    pub pc_runs: u32,
    pub pc_activated: bool,
    pub pc_state: String,
    pub pc_error: String,
    pub pc_case: usize,
    pub pc_ground: usize,
    pub pc_valves: [usize; 4],
    pub pc_pulse: i32,
    pub pc_route: usize,
    pub pc_route_case: usize,
    pub pc_route_valves: [bool; 4],
    pub pc_manual_valves: [usize; 4],
    pub pc_manual_edited: bool,
    pub pc_manual_actual: u8,
    pub pc_manual_valid: u8,
    pub pc_flow_ch: usize,
    pub pc_log_ms: i32,
    pub pc_target: usize,
    pub pc_fields: [[i32; 16]; 4],
    pub pc_applied: [[Option<i32>; 16]; 4],
    pub pc_dirty: [[bool; 16]; 4],
    pub pc_apply_pending: usize,
    pub pc_name_draft: String,
    pub pc_name_applied: Option<String>,
    pub pc_name_dirty: bool,
    pub pc_field: usize,
    pub pc_value: i32,
    pub pc_minflow_milli: [i32; 3],
    pub pc_flow_action: [usize; 3],
    pub pc_flow_check: [bool; 3],
    pub pc_flow_delay_ms: [i32; 3],
    pub pc_fail_samples: [i32; 2],
    pub pc_datalog: bool,
    pub pc_resume: bool,
    pub pc_detail: String,
    pub pc_run_mode: String,
    pub pc_profile_name: String,
    pub pc_config_seen: bool,
    pub pc_volume: String,
    pub pc_remaining: String,
    pub faults: Vec<&'static str>,
}

pub struct IrCamera {
    pub model: usize,
    pub address: i32,
    pub format: usize,
    pub hz: usize,
    pub emissivity: i32,
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub initialized: bool,
    pub buffer_enabled: Option<bool>,
    pub data_exist: bool,
    pub request_id: u32,
    pub object_id: u32,
    pub saved_file: Option<String>,
    pub result: String,
    pub temp_centi: Option<i32>,
    pub sample_ms: u32,
    pub confirmed: bool,
    pub actual_model: usize,
    pub actual_address: i32,
    pub actual_format: usize,
    pub actual_hz: i32,
    pub address_restart_required: bool,
}

impl Default for IrCamera {
    fn default() -> Self {
        Self {
            model: 1,
            address: 0x66,
            format: 1,
            hz: 2,
            emissivity: 1000,
            x: 0,
            y: 0,
            width: 32,
            height: 24,
            initialized: false,
            buffer_enabled: None,
            data_exist: false,
            request_id: 0,
            object_id: 0,
            saved_file: None,
            result: "not read".into(),
            temp_centi: None,
            sample_ms: 0,
            confirmed: false,
            actual_model: 0,
            actual_address: 0,
            actual_format: 0,
            actual_hz: 0,
            address_restart_required: false,
        }
    }
}

#[cfg(test)]
mod ir_camera_tests {
    use super::*;

    #[test]
    fn capture_requires_init_and_measurement_requires_capture() {
        let mut s = Sim::new();
        s.run("exp.cam_capture");
        assert!(!s.exp.camera[0].data_exist);
        s.run("exp.cam_init");
        s.run("exp.cam_capture");
        assert_eq!(s.exp.camera[0].object_id, 1);
        s.run("exp.cam_temp");
        assert!(s.exp.camera[0].temp_centi.is_some());
        s.exp.camera[0].x = 1;
        s.run("exp.cam_temp");
        assert_eq!(s.exp.camera[0].object_id, 1);
    }
}

pub struct Pdu {
    pub online: bool,
    pub vout_en: bool,
    pub trans_en: bool,
    pub vout_set: i32,
    pub mode: usize, // 0 NORMAL 1 MANUAL 2 WARNING 3 ERROR
    pub req_manual: bool,
    pub temp: f32,
    pub temp_limit: i32,
    pub heartbeat: bool,
    pub led_on: i32,
    pub led_off: i32,
    pub led_state: bool,
    pub rails: [Rail; 4], // VOUT, +12 in, +5_TRANS, +5
    pub trans_fault: bool,
    pub mcu_fault: bool,
    pub latched: bool,
    pub boot_count: u32,
    pub boot_cause: &'static str,
    pub event_seq: u32,
    pub last_event: String,
    pub poll_age: f64,
}

pub struct Sim {
    pub t: f64,
    pub paused: bool,
    pub inject: [bool; 3],
    pub obc: Obc,
    pub exp: Exp,
    pub pdu: Pdu,
    pub live: Live,
    logs: Vec<LogMsg>,
    led_phase: f64,
}

fn rail(name: &'static str, mv: i32, ma: i32) -> Rail {
    Rail { name, mv, ma }
}

/// Smooth bounded pseudo-noise in [-1, 1].
fn wob(t: f64, seed: f64) -> f32 {
    (((t * 0.9 + seed).sin() + (t * 2.3 + seed * 1.7).sin()) * 0.5) as f32
}

impl Sim {
    pub fn new() -> Sim {
        let obc = Obc {
            fsw_up: true,
            rs422_up: true,
            rx_frames: 0,
            tx_frames: 0,
            boot_count: 7,
            uptime: 4_000.0,
            rtc_offset: 0,
            cpu_pct: 12.0,
            mem_pct: 31.0,
            disk_pct: 41.0,
            temp_onboard: 34.0,
            ntc: [28.0, 29.0, 31.0, 30.0],
            bme_t: 27.0,
            bme_p_pa: 101_300,
            bme_rh: 38.0,
            bme_gas: 120_000,
            u6: [
                rail("5V_SAMA7", 5020, 420),
                rail("5V_FPGA", 5010, 180),
                rail("3V3", 3300, 310),
                rail("5V_AI", 5000, 90),
            ],
            u2: [
                rail("PMIC_3V3", 3300, 260),
                rail("PMIC_DDR_1V35", 1350, 180),
                rail("PMIC_CORE_1V15", 1150, 520),
                rail("PMIC_CPU_1V15", 1150, 610),
            ],
            ai_efuse: true,
            fpga_efuse: true,
            usb: [false; 4],
            mipi: [false; 2],
            led_pwm: 0,
            usb_led_pwm: [0; 4],
            usb_sel: 0,
            usb_shot: [Shot::new(), Shot::new(), Shot::new(), Shot::new()],
            mipi_sel: 0,
            mipi_sensor: [0; 2],
            mipi_route: None,
            mipi_shot: Shot::new(),
            snapshot_age: 0.0,
        };
        let exp = Exp {
            link: true,
            node: 11,
            uptime: 1_800.0,
            age: 0.0,
            sln: false,
            motor: false,
            exp12v: false,
            pac: [
                rail("12V_MOTOR", 0, 0),
                rail("12V_EXP", 0, 0),
                rail("5V_EXP", 5000, 100),
                rail("5V_SLN_HD", 0, 0),
            ],
            ntc: [24.0; 4],
            board_t: 26.0,
            bme_t: 25.0,
            bme_p_pa: 101_100,
            bme_rh: 41.0,
            heater: [0; 4],
            ht_sel: 0,
            ht_profile: [HeaterProfile::default(); 4],
            ht_loaded: [false; 4],
            ht_phase: [0; 4],
            ht_reason: [0; 4],
            ht_configured: [false; 4],
            ht_running: [false; 4],
            ht_status_seen: [true; 4],
            ht_status_at: None,
            ht_main_temp: [24.0; 4],
            ht_sec_temp: [24.0; 4],
            ht_requested: [0; 4],
            ht_applied: [0; 4],
            ht_limit: [100; 4],
            ht_target_actual: [30.0; 4],
            ht_manual_left: [0.0; 4],
            u11: Pump1 {
                ptype: 0,
                ptype_configured: false,
                ptype_staged: 0,
                detected: false,
                inited: false,
                freq: 200,
                freq_staged: 200,
                volt: 30,
                volt_staged: 30,
                en: false,
            },
            u15: Pump4 {
                detected: false,
                inited: false,
                ch: 0,
                freq: 200,
                freq_staged: 200,
                volt: [0; 4],
                volt_staged: 30,
                en: false,
            },
            drv: Drv {
                detected: false,
                inited: false,
                fault: false,
            },
            motors: [Motor {
                pos: 0.0,
                running: false,
                homing: false,
                holding: false,
                dir: 1,
                sps: 100,
                remaining_steps: 0.0,
            }; 2],
            mc: 0,
            mdir: 0,
            msps: 100,
            move_steps: [100; 2],
            motor_mode: [0; 2],
            motor_mode_draft: [0; 2],
            motor_mode_verified: [true; 2],
            map_motor: 0,
            motor_max_sps: [100; 2],
            motor_drive_pct: [20; 2],
            motor_drive_applied_pct: [20; 2],
            motor_drive_dirty: [false; 2],
            motor_pwm: [3; 2],
            motor_bank: [0, 1],
            motor_min: [0; 2],
            motor_max: [1; 2],
            motor_home_end: [0; 2],
            motor_min_level: [0; 2],
            motor_max_level: [0; 2],
            motor_limits_verified: [false; 2],
            flow: [0.0; 2],
            bubble: [false; 2],
            ir: [false; 2],
            ir_actual_duty: [0; 2],
            ir_status_seen: [true; 2],
            ir_sel: 0,
            ir_setup: [IrSetup {
                duty: 10,
                max_duty: 0,
                max_on_ms: 0,
                pwm_hz: 100000,
            }; 2],
            ir_configured: [false; 2],
            ir_config_pending: [None; 2],
            cam_sel: 0,
            camera: std::array::from_fn(|_| IrCamera::default()),
            burn_sel: 0,
            burn_setup: [BurnSetup::default(); 2],
            burn_configured: [false; 2],
            burn_armed_until: None,
            burn_state: "IDLE".into(),
            burn_active_wire: None,
            burn_last: ["UNKNOWN".into(), "UNKNOWN".into()],
            burn_attempt: [0; 2],
            burn_peak_ma: [0; 2],
            burn_status_seen: [true; 2],
            burn_left: 0.0,
            burn_count: 0,
            pc_running: false,
            pc_progress: 0.0,
            pc_runs: 0,
            pc_activated: false,
            pc_state: "STANDBY".into(),
            pc_error: "NONE".into(),
            pc_case: 0,
            pc_ground: 0,
            pc_valves: [0, 1, 2, 3],
            pc_pulse: 20,
            pc_route: 0,
            pc_route_case: 0,
            pc_route_valves: [false; 4],
            pc_manual_valves: [0; 4],
            pc_manual_edited: false,
            pc_manual_actual: 0,
            pc_manual_valid: 0,
            pc_flow_ch: 0,
            pc_log_ms: 1000,
            pc_target: 1,
            pc_fields: {
                let mut fields = [[0; 16]; 4];
                fields[0][9] = 500;
                fields[0][11] = 60_000;
                for profile in fields.iter_mut().skip(1) {
                    profile[1] = 10_000;
                    profile[2] = 2;
                    profile[3] = 200;
                    profile[4] = 100;
                    profile[5] = 1_000_000;
                    profile[12] = 1;
                    profile[13] = 500;
                    profile[15] = 1;
                }
                fields[1][14] = 1;
                fields[2][14] = 1;
                fields[1][0] = 5_000;
                fields[2][0] = 120_000;
                fields[3][6] = 15_000;
                fields[3][7] = 15_000;
                fields[3][8] = 2;
                fields
            },
            pc_applied: [[None; 16]; 4],
            pc_dirty: [[false; 16]; 4],
            pc_apply_pending: 0,
            pc_name_draft: "default".into(),
            pc_name_applied: None,
            pc_name_dirty: false,
            pc_field: 0,
            pc_value: 15000,
            pc_minflow_milli: [10000; 3],
            pc_flow_action: [2; 3],
            pc_flow_check: [true; 3],
            pc_flow_delay_ms: [500; 3],
            pc_fail_samples: [1; 2],
            pc_datalog: false,
            pc_resume: false,
            pc_detail: String::new(),
            pc_run_mode: "TIME".into(),
            pc_profile_name: "default".into(),
            pc_config_seen: true,
            pc_volume: "n/a".into(),
            pc_remaining: "n/a".into(),
            faults: Vec::new(),
        };
        let pdu = Pdu {
            online: true,
            vout_en: false,
            trans_en: false,
            vout_set: 12_000,
            mode: 0,
            req_manual: false,
            temp: 31.0,
            temp_limit: 85,
            heartbeat: true,
            led_on: 500,
            led_off: 500,
            led_state: false,
            rails: [
                rail("VOUT", 0, 0),
                rail("+12 input", 12_000, 40),
                rail("+5_TRANS", 0, 0),
                rail("+5", 5000, 85),
            ],
            trans_fault: false,
            mcu_fault: false,
            latched: false,
            boot_count: 3,
            boot_cause: "POWER_ON",
            event_seq: 1,
            last_event: "CLOCK_SET".into(),
            poll_age: 0.0,
        };
        let live = Live::new(
            Mode::Sim,
            &default_target(Mode::Console),
            &default_target(Mode::Rs422),
        );
        let mut s = Sim {
            t: 0.0,
            paused: false,
            inject: [false; 3],
            obc,
            exp,
            pdu,
            live,
            logs: Vec::new(),
            led_phase: 0.0,
        };
        s.log(
            Sub::Obc,
            Level::Info,
            "simulation started (dal_obc_fsw gui_state mocked)",
        );
        s
    }

    pub fn take_logs(&mut self) -> Vec<LogMsg> {
        std::mem::take(&mut self.logs)
    }

    pub(crate) fn log(&mut self, sub: Sub, level: Level, text: impl Into<String>) {
        self.logs.push(LogMsg {
            sub,
            level,
            text: text.into(),
        });
    }

    pub fn rtc_now(&self) -> i64 {
        unix_now() + self.obc.rtc_offset
    }

    pub fn set_rtc(&mut self, epoch: u32) {
        if self.live.mode != Mode::Sim {
            self.live_action("obc.rtc_set", Act::Epoch(epoch));
        } else {
            self.obc.rtc_offset = epoch as i64 - unix_now();
            self.log(
                Sub::Obc,
                Level::Ok,
                format!("RTC set to {} UTC", civil(epoch as i64)),
            );
        }
    }

    // ── time step ────────────────────────────────────────────────────────────

    pub fn tick(&mut self, dt: f64) {
        if self.live.mode != Mode::Sim {
            return self.live_tick(dt);
        }
        if self.paused {
            return;
        }
        self.t += dt;
        self.tick_obc(dt);
        self.tick_exp(dt);
        self.tick_pdu(dt);
    }

    fn tick_obc(&mut self, dt: f64) {
        let t = self.t;
        let hot = self.inject[Sub::Obc.idx()];
        let mut done: Vec<String> = Vec::new();
        let o = &mut self.obc;
        for (i, sh) in o.usb_shot.iter_mut().enumerate() {
            if sh.state == ShotState::Capturing {
                sh.left -= dt;
                if sh.left <= 0.0 {
                    sh.state = ShotState::Saved;
                    sh.bytes = 160_000 + (20_000.0 * wob(t, 5.0 + i as f64)) as u64;
                    done.push(format!(
                        "usb_cam: camera {} saved {} ({} KB)",
                        i + 1,
                        sh.path,
                        sh.bytes / 1000
                    ));
                }
            }
        }
        if o.mipi_shot.state == ShotState::Capturing {
            o.mipi_shot.left -= dt;
            if o.mipi_shot.left <= 0.0 {
                o.mipi_shot.state = ShotState::Saved;
                o.mipi_shot.bytes = 8_000_000;
                done.push(format!(
                    "capture_ar2020: saved {} ({:.1} MB)",
                    o.mipi_shot.path,
                    o.mipi_shot.bytes as f64 / 1e6
                ));
            }
        }
        o.uptime += dt;
        o.snapshot_age = (o.snapshot_age + dt) % 5.0;
        if o.rs422_up {
            o.rx_frames += (dt * 4.0).round().max(1.0) as u32;
            o.tx_frames = o.rx_frames;
        }
        o.cpu_pct = (12.0 + 6.0 * wob(t, 1.0) + o.usb.iter().filter(|&&b| b).count() as f32 * 3.0)
            .clamp(1.0, 99.0);
        o.mem_pct = 31.0 + 1.5 * wob(t, 2.0);
        o.temp_onboard = 34.0 + 1.2 * wob(t, 3.0) + if hot { 25.0 } else { 0.0 };
        let base = [28.0, 29.0, 31.0, 30.0];
        for i in 0..4 {
            let target =
                base[i] + 0.8 * wob(t, 4.0 + i as f64) + if hot && i == 0 { 48.0 } else { 0.0 };
            o.ntc[i] += (target - o.ntc[i]) * (dt as f32 * 0.4).min(1.0);
        }
        o.bme_t = 27.0 + 0.5 * wob(t, 8.0);
        o.bme_p_pa = 101_300 + (60.0 * wob(t, 9.0)) as i32;
        o.bme_rh = 38.0 + 2.0 * wob(t, 10.0);
        o.bme_gas = 120_000 + (4000.0 * wob(t, 11.0)) as i32;

        let usb = o.usb.iter().filter(|&&b| b).count() as i32;
        let mipi = o.mipi.iter().filter(|&&b| b).count() as i32;
        let n = |s: f64| (6.0 * wob(t, s)) as i32;
        let v = |s: f64| (8.0 * wob(t, s)) as i32;
        o.u6[0].mv = 5020 + v(20.0);
        o.u6[0].ma = 420 + usb * 110 + mipi * 90 + o.led_pwm * 2 + n(21.0);
        o.u6[1].mv = if o.fpga_efuse { 5010 + v(22.0) } else { 0 };
        o.u6[1].ma = if o.fpga_efuse { 180 + n(23.0) } else { 0 };
        o.u6[2].mv = 3300 + v(24.0) / 2;
        o.u6[2].ma = 310 + n(25.0);
        o.u6[3].mv = if o.ai_efuse { 5000 + v(26.0) } else { 0 };
        o.u6[3].ma = if o.ai_efuse { 90 + n(27.0) } else { 0 };
        o.u2[0].ma = 260 + n(28.0);
        o.u2[1].ma = 180 + n(29.0);
        o.u2[2].ma = 520 + n(30.0) + (o.cpu_pct as i32);
        o.u2[3].ma = 610 + n(31.0) + (o.cpu_pct as i32) * 4;
        for m in done {
            self.log(Sub::Obc, Level::Ok, m);
        }
    }

    fn tick_exp(&mut self, dt: f64) {
        let t = self.t;
        let link_lost = self.inject[Sub::Exp.idx()];
        let mut logs: Vec<(Level, String)> = Vec::new();
        let e = &mut self.exp;
        e.link = !link_lost;
        if !e.link {
            e.age += dt;
            if e.faults.first() != Some(&"LINK_LOST") {
                e.faults.insert(0, "LINK_LOST");
                logs.push((
                    Level::Err,
                    "EXP telemetry stale: CAN node 11 not answering".into(),
                ));
            }
        } else {
            e.faults.retain(|f| *f != "LINK_LOST");
            e.age = (e.age + dt) % 2.0;
            e.uptime += dt;
        }

        if e.link {
            for i in 0..4 {
                if e.ht_manual_left[i] > 0.0 {
                    e.ht_manual_left[i] = (e.ht_manual_left[i] - dt).max(0.0);
                    if e.ht_manual_left[i] == 0.0 {
                        e.heater[i] = 0;
                    }
                }
                if e.ht_running[i] {
                    let p = e.ht_profile[i];
                    let main = e.ntc[p.main_ntc];
                    let sec = e.ntc[p.sec_ntc];
                    e.ht_main_temp[i] = main;
                    e.ht_sec_temp[i] = sec;
                    let cutoff = p.cutoff as f32 / 100.0;
                    let delta = p.delta as f32 / 100.0;
                    if main >= cutoff || sec >= cutoff || (main - sec).abs() > delta {
                        e.ht_running[i] = false;
                        e.ht_phase[i] = 2;
                        e.ht_reason[i] = if (main - sec).abs() > delta { 4 } else { 3 };
                        e.heater[i] = 0;
                    } else {
                        let error = p.target as f32 / 100.0 - main;
                        e.heater[i] = (error * p.kp as f32 / 1000.0 * 10.0)
                            .round()
                            .clamp(0.0, p.max_duty as f32)
                            as i32;
                    }
                }
            }
            for i in 0..4 {
                e.ht_requested[i] = e.heater[i] as u8;
                e.ht_applied[i] = e.heater[i] as u8;
                e.ht_limit[i] = e.ht_profile[i].max_duty as u8;
                e.ht_target_actual[i] = e.ht_profile[i].target as f32 / 100.0;
                if !e.ht_running[i] {
                    e.ht_main_temp[i] = e.ntc[e.ht_profile[i].main_ntc];
                    e.ht_sec_temp[i] = e.ntc[e.ht_profile[i].sec_ntc];
                }
            }
            let k = (dt as f32 * 0.08).min(1.0);
            let heat_on = e.exp12v;
            for i in 0..4 {
                let target =
                    24.0 + if heat_on {
                        e.heater[i] as f32 * 0.38
                    } else {
                        0.0
                    } + 0.2 * wob(t, 40.0 + i as f64);
                e.ntc[i] += (target - e.ntc[i]) * k;
            }
            let hot = e.ntc.iter().cloned().fold(0.0, f32::max);
            if hot > 60.0 && e.heater.iter().any(|&d| d > 0) {
                e.heater = [0; 4];
                logs.push((
                    Level::Warn,
                    format!("heater thermal limit: NTC {hot:.1} C, all heaters stopped"),
                ));
            }
            e.board_t = 26.0 + 0.6 * wob(t, 50.0) + (hot - 24.0) * 0.08;
            e.bme_t = 25.0 + 0.4 * wob(t, 51.0);
            e.bme_p_pa = 101_100 + (50.0 * wob(t, 52.0)) as i32;
            e.bme_rh = 41.0 + 2.0 * wob(t, 53.0);

            // Pumps and flow.
            let a11 = e.u11.en && e.sln;
            let a15 = e.u15.en && e.sln;
            let k11 = if e.u11.ptype == 1 { 1.5 } else { 1.0 };
            let want = [
                if a11 {
                    (e.u11.volt * e.u11.freq) as f32 / 5000.0 * k11
                } else {
                    0.0
                },
                if a15 {
                    (e.u15.volt.iter().sum::<i32>() * e.u15.freq) as f32 / 10_000.0
                } else {
                    0.0
                },
            ];
            for i in 0..2 {
                e.flow[i] += (want[i] - e.flow[i]) * (dt as f32 * 0.8).min(1.0);
                e.bubble[i] =
                    want[i] > 0.0 && e.flow[i] > 1.0 && wob(t, 60.0 + i as f64 * 3.0) > 0.93;
            }
            let pump_load = if a11 { e.u11.volt * 2 } else { 0 }
                + if a15 {
                    e.u15.volt.iter().sum::<i32>()
                } else {
                    0
                };

            // Motors.
            for m in e.motors.iter_mut() {
                if !(e.motor && e.drv.inited) {
                    m.running = false;
                    m.homing = false;
                    m.holding = false;
                    m.remaining_steps = 0.0;
                    continue;
                }
                if m.homing {
                    let stepv = (m.sps as f32 * dt as f32).min(m.pos.abs());
                    m.pos -= stepv * m.pos.signum();
                    if m.pos.abs() < 0.5 {
                        m.pos = 0.0;
                        m.homing = false;
                        m.running = false;
                    }
                } else if m.running {
                    let step = m.sps as f32 * dt as f32;
                    let step = if m.remaining_steps > 0.0 {
                        step.min(m.remaining_steps)
                    } else {
                        step
                    };
                    m.pos += m.dir as f32 * step;
                    if m.remaining_steps > 0.0 {
                        m.remaining_steps -= step;
                        if m.remaining_steps <= 0.0 {
                            m.running = false;
                        }
                    }
                }
            }

            // Burn wire.
            if e.burn_armed_until.is_some_and(|(_, until)| t >= until) {
                e.burn_armed_until = None;
                e.burn_state = "IDLE".into();
                e.burn_active_wire = None;
                logs.push((Level::Warn, "burn arm expired after 5 s".into()));
            }
            if e.burn_left > 0.0 {
                e.burn_left = (e.burn_left - dt as f32).max(0.0);
                if e.burn_left == 0.0 {
                    if let Some(wire) = e.burn_active_wire {
                        e.burn_last[wire] = "SIMULATED".into();
                    }
                    e.burn_state = "IDLE".into();
                    e.burn_active_wire = None;
                    logs.push((Level::Ok, "burn wire released".into()));
                }
            }

            // PC experiment.
            if e.pc_running {
                if !(e.exp12v && e.sln) {
                    e.pc_running = false;
                    logs.push((Level::Err, "PC experiment aborted: rail lost".into()));
                } else {
                    e.pc_progress += dt as f32 / 60.0 * 100.0;
                    if e.pc_progress >= 100.0 {
                        e.pc_progress = 100.0;
                        e.pc_running = false;
                        e.pc_state = "STANDBY".into();
                        e.pc_runs += 1;
                        logs.push((
                            Level::Ok,
                            format!("PC experiment finished (run {})", e.pc_runs),
                        ));
                    }
                }
            }

            // Rails (PAC1934: 12V_MOTOR, 12V_EXP, 5V_EXP, 5V_SLN_HD).
            let moving = e.motors.iter().filter(|m| m.running).count() as i32;
            let n = |s: f64| (4.0 * wob(t, s)) as i32;
            e.pac[0].mv = if e.motor { 12_010 + n(70.0) } else { 0 };
            e.pac[0].ma = if e.motor {
                15 + moving * 300 + if e.burn_left > 0.0 { 1200 } else { 0 }
            } else {
                0
            };
            let heaters: i32 = e.heater.iter().sum();
            let irs = e.ir.iter().filter(|&&b| b).count() as i32;
            e.pac[1].mv = if e.exp12v { 12_000 + n(71.0) } else { 0 };
            e.pac[1].ma = if e.exp12v {
                20 + heaters * 8 + irs * 40
            } else {
                0
            };
            e.pac[2].mv = 5000 + n(72.0);
            e.pac[2].ma = 100 + n(73.0);
            e.pac[3].mv = if e.sln { 5010 + n(74.0) } else { 0 };
            e.pac[3].ma = if e.sln { 60 + pump_load * 3 } else { 0 };
        }
        for (l, m) in logs {
            self.log(Sub::Exp, l, m);
        }
    }

    fn tick_pdu(&mut self, dt: f64) {
        let t = self.t;
        let fault_inj = self.inject[Sub::Pdu.idx()];
        let mut logs: Vec<(Level, String)> = Vec::new();
        let p = &mut self.pdu;
        p.poll_age = (p.poll_age + dt) % 5.0;

        if fault_inj && !p.trans_fault {
            p.trans_fault = true;
            p.latched = true;
            p.trans_en = false;
            p.vout_en = false;
            p.event_seq += 1;
            p.last_event = "TRANS_CURRENT".into();
            logs.push((
                Level::Err,
                "TRANS eFuse fault: TRANS and VOUT shut off, fault latched".into(),
            ));
        }
        if !fault_inj {
            p.trans_fault = false;
        }

        p.mode = if p.trans_fault || p.latched {
            3
        } else if p.req_manual {
            1
        } else {
            0
        };

        self.led_phase += dt * 1000.0;
        let period = if p.led_state { p.led_on } else { p.led_off } as f64;
        if self.led_phase >= period {
            self.led_phase = 0.0;
            p.led_state = !p.led_state;
        }

        let n = |s: f64| (3.0 * wob(t, s)) as i32;
        let vout_mv = if p.vout_en { p.vout_set } else { 0 };
        let vout_ma = if p.vout_en { vout_mv / 40 + n(80.0) } else { 0 };
        p.rails[0].mv = vout_mv;
        p.rails[0].ma = vout_ma;
        p.rails[2].mv = if p.trans_en { 5000 + n(81.0) } else { 0 };
        p.rails[2].ma = if p.trans_en { 120 + n(82.0) } else { 0 };
        p.rails[3].mv = 5000 + n(83.0);
        p.rails[3].ma = 85 + n(84.0);
        let out_mw = p.rails[0].mw() + p.rails[2].mw() + p.rails[3].mw();
        p.rails[1].mv = 12_000 + n(85.0) * 3;
        p.rails[1].ma = 40 + out_mw * 1000 / (12_000 * 9 / 10).max(1);

        let load_heat = out_mw as f32 / 1000.0;
        let target = 31.0 + load_heat * 1.1 + 0.4 * wob(t, 86.0);
        p.temp += (target - p.temp) * (dt as f32 * 0.1).min(1.0);
        if p.temp > p.temp_limit as f32 && p.vout_en {
            p.vout_en = false;
            p.event_seq += 1;
            p.last_event = "TEMP".into();
            logs.push((
                Level::Err,
                format!("PDU over-temperature {:.1} C: VOUT disabled", p.temp),
            ));
        }
        for (l, m) in logs {
            self.log(Sub::Pdu, l, m);
        }
    }

    // ── control surface ──────────────────────────────────────────────────────

    fn stage_pc_field(&mut self, index: usize, value: i32) {
        let target = self.exp.pc_target;
        if !pc_field_allowed(target, index) {
            return;
        }
        if self.exp.pc_fields[target][index] != value {
            self.exp.pc_fields[target][index] = value;
            self.exp.pc_dirty[target][index] = self.exp.pc_applied[target][index] != Some(value);
        }
    }

    pub fn stage_pc_name(&mut self, value: String) {
        if self.exp.pc_target != 0 {
            return;
        }
        self.exp.pc_name_dirty = self.exp.pc_name_applied.as_ref() != Some(&value);
        self.exp.pc_name_draft = value;
    }

    pub fn value(&self, id: &str) -> Val {
        let (o, e, p) = (&self.obc, &self.exp, &self.pdu);
        if let Some(i) = pc_field_index(id) {
            if !pc_field_allowed(e.pc_target, i) {
                return Val::None;
            }
            let v = e.pc_fields[e.pc_target][i];
            return match i {
                2 | 10 => Val::Idx(v.max(0) as usize),
                12 | 15 => Val::Bool(v != 0),
                _ => Val::Num(v),
            };
        }
        match id {
            "obc.ai_efuse" => Val::Bool(o.ai_efuse),
            "obc.fpga_efuse" => Val::Bool(o.fpga_efuse),
            "obc.usb0" | "obc.usb1" | "obc.usb2" | "obc.usb3" => Val::Bool(o.usb[idx(id)]),
            "obc.usb_sel" => Val::Idx(o.usb_sel),
            "obc.mipi_sel" => Val::Idx(o.mipi_sel),
            "obc.mipi_sensor" => Val::Idx(o.mipi_sensor[o.mipi_sel]),
            "obc.mipi_power" => Val::Bool(o.mipi[o.mipi_sel]),
            "obc.led_pwm" => Val::Num(o.usb_led_pwm[o.usb_sel]),
            "exp.pc_v1" | "exp.pc_v2" | "exp.pc_v3" | "exp.pc_v4" => {
                Val::Idx(e.pc_valves[idx(id) - 1])
            }
            "exp.pc_pulse" => Val::Num(e.pc_pulse),
            "exp.pc_route" => Val::Idx(e.pc_route),
            "exp.pc_route_case" => Val::Idx(e.pc_route_case),
            "exp.pc_rv1" | "exp.pc_rv2" | "exp.pc_rv3" | "exp.pc_rv4" => {
                Val::Bool(e.pc_route_valves[idx(id) - 1])
            }
            "exp.pc_manual_v1" | "exp.pc_manual_v2" | "exp.pc_manual_v3" | "exp.pc_manual_v4" => {
                Val::Idx(e.pc_manual_valves[idx(id) - 1])
            }
            "exp.pc_flow_ch" => Val::Idx(e.pc_flow_ch),
            "exp.pc_log_ms" => Val::Num(e.pc_log_ms),
            "exp.pc_target" => Val::Idx(e.pc_target),
            "exp.pc_field" => Val::Idx(e.pc_field),
            "exp.pc_value" => Val::Num(e.pc_value),
            "exp.pc_minflow" if e.pc_target > 0 => Val::Num(e.pc_minflow_milli[e.pc_target - 1]),
            "exp.pc_flow_action" if e.pc_target > 0 => Val::Idx(e.pc_flow_action[e.pc_target - 1]),
            "exp.pc_flow_check" if e.pc_target > 0 => Val::Bool(e.pc_flow_check[e.pc_target - 1]),
            "exp.pc_flow_delay" if e.pc_target > 0 => Val::Num(e.pc_flow_delay_ms[e.pc_target - 1]),
            "exp.pc_fail_samples" if (1..=2).contains(&e.pc_target) => {
                Val::Num(e.pc_fail_samples[e.pc_target - 1])
            }
            "exp.pc_case" => Val::Idx(e.pc_case),
            "exp.pc_ground" => Val::Idx(e.pc_ground),
            "exp.pc_datalog" => Val::Bool(e.pc_datalog),
            "exp.pc_resume" => Val::Bool(e.pc_resume),
            "exp.sln" => Val::Bool(e.sln),
            "exp.motor" => Val::Bool(e.motor),
            "exp.exp12v" => Val::Bool(e.exp12v),
            "exp.ht_sel" | "exp.ht_profile_sel" => Val::Idx(e.ht_sel),
            "exp.ht_main_ntc" => Val::Idx(e.ht_profile[e.ht_sel].main_ntc),
            "exp.ht_sec_ntc" => Val::Idx(e.ht_profile[e.ht_sel].sec_ntc),
            "exp.ht_resume_boot" => Val::Bool(e.ht_profile[e.ht_sel].resume_boot),
            "exp.ht_duty" => Val::Num(e.ht_profile[e.ht_sel].duty),
            "exp.ht_duration" => Val::Num(e.ht_profile[e.ht_sel].duration),
            "exp.ht_max_duty" => Val::Num(e.ht_profile[e.ht_sel].max_duty),
            "exp.ht_manual_slew" => Val::Num(e.ht_profile[e.ht_sel].manual_slew),
            "exp.ht_max_on" => Val::Num(e.ht_profile[e.ht_sel].max_on),
            "exp.ht_target" => Val::Num(e.ht_profile[e.ht_sel].target),
            "exp.ht_delta" => Val::Num(e.ht_profile[e.ht_sel].delta),
            "exp.ht_cutoff" => Val::Num(e.ht_profile[e.ht_sel].cutoff),
            "exp.ht_timeout" => Val::Num(e.ht_profile[e.ht_sel].timeout),
            "exp.ht_slew_up" => Val::Num(e.ht_profile[e.ht_sel].slew_up),
            "exp.ht_slew_down" => Val::Num(e.ht_profile[e.ht_sel].slew_down),
            "exp.ht_kp" => Val::Num(e.ht_profile[e.ht_sel].kp),
            "exp.ht_ki" => Val::Num(e.ht_profile[e.ht_sel].ki),
            "exp.ht_kd" => Val::Num(e.ht_profile[e.ht_sel].kd),
            "exp.heater0" | "exp.heater1" | "exp.heater2" | "exp.heater3" => {
                Val::Num(e.heater[idx(id)])
            }
            "exp.u11_type" => Val::Idx(e.u11.ptype_staged),
            "exp.u11_freq" => Val::Num(e.u11.freq_staged),
            "exp.u11_volt" => Val::Num(e.u11.volt_staged),
            "exp.u11_en" => Val::Bool(e.u11.en),
            "exp.u15_ch" => Val::Idx(e.u15.ch),
            "exp.u15_freq" => Val::Num(e.u15.freq_staged),
            "exp.u15_volt" => Val::Num(e.u15.volt_staged),
            "exp.u15_en" => Val::Bool(e.u15.en),
            "exp.mc" => Val::Idx(e.mc),
            "exp.mdir" => Val::Idx(e.mdir),
            "exp.msps" => Val::Num(e.msps),
            "exp.msteps" => Val::Num(e.move_steps[e.mc]),
            "exp.mmode" => Val::Idx(e.motor_mode_draft[e.mc]),
            "exp.map_motor" => Val::Idx(e.map_motor),
            "exp.mmax_sps" => Val::Num(e.motor_max_sps[e.mc]),
            "exp.mdrive_pct" => Val::Num(e.motor_drive_pct[e.mc]),
            "exp.mpwm" => Val::Idx(e.motor_pwm[e.mc]),
            "exp.mbank" => Val::Idx(e.motor_bank[e.map_motor]),
            "exp.mmin" => Val::Idx(e.motor_min[e.map_motor]),
            "exp.mmax" => Val::Idx(e.motor_max[e.map_motor]),
            "exp.mhome_end" => Val::Idx(e.motor_home_end[e.map_motor]),
            "exp.mmin_level" => Val::Idx(e.motor_min_level[e.map_motor]),
            "exp.mmax_level" => Val::Idx(e.motor_max_level[e.map_motor]),
            "exp.ir0" | "exp.ir1" => Val::Bool(e.ir[idx(id)]),
            "exp.ir_sel" => Val::Idx(e.ir_sel),
            "exp.ir_duty" => Val::Num(e.ir_setup[e.ir_sel].duty),
            "exp.ir_max_duty" => Val::Num(e.ir_setup[e.ir_sel].max_duty),
            "exp.ir_max_on" => Val::Num(e.ir_setup[e.ir_sel].max_on_ms),
            "exp.ir_pwm" => Val::Num(e.ir_setup[e.ir_sel].pwm_hz),
            "exp.cam_sel" => Val::Idx(e.cam_sel),
            "exp.cam_model" => Val::Idx(e.camera[e.cam_sel].model),
            "exp.cam_addr" => Val::Num(e.camera[e.cam_sel].address),
            "exp.cam_format" => Val::Idx(e.camera[e.cam_sel].format),
            "exp.cam_hz" => Val::Idx(e.camera[e.cam_sel].hz),
            "exp.cam_emiss" => Val::Num(e.camera[e.cam_sel].emissivity),
            "exp.cam_x" => Val::Num(e.camera[e.cam_sel].x),
            "exp.cam_y" => Val::Num(e.camera[e.cam_sel].y),
            "exp.cam_w" => Val::Num(e.camera[e.cam_sel].width),
            "exp.cam_h" => Val::Num(e.camera[e.cam_sel].height),
            "exp.burn_sel" => Val::Idx(e.burn_sel),
            "exp.burn_duty" => Val::Num(e.burn_setup[e.burn_sel].duty),
            "exp.burn_time" => Val::Num(e.burn_setup[e.burn_sel].burn_time_ms),
            "exp.burn_max_total_current" => Val::Num(e.burn_setup[e.burn_sel].max_total_current_ma),
            "pdu.vout_en" => Val::Bool(p.vout_en),
            "pdu.trans_en" => Val::Bool(p.trans_en),
            "pdu.vout_set" => Val::Num(p.vout_set),
            "pdu.mode" => Val::Idx(p.req_manual as usize),
            "pdu.temp_limit" => Val::Num(p.temp_limit),
            "pdu.heartbeat" => Val::Bool(p.heartbeat),
            "pdu.led_on" => Val::Num(p.led_on),
            "pdu.led_off" => Val::Num(p.led_off),
            _ => Val::None,
        }
    }

    /// True when a command can reach the subsystem at all.
    pub fn reachable(&self, sub: Sub) -> bool {
        match sub {
            Sub::Obc => self.obc.fsw_up,
            Sub::Exp => self.exp.link,
            Sub::Pdu => self.pdu.online,
        }
    }

    pub fn set_bool(&mut self, id: &str, on: bool) {
        if self.live.mode != Mode::Sim && self.live_action(id, Act::Bool(on)) {
            return;
        }
        if let Some(i) = pc_field_index(id) {
            self.stage_pc_field(i, on as i32);
            return;
        }
        let sub = sub_of(id);
        if !self.reachable(sub) {
            self.log(sub, Level::Err, format!("{id}: no link to {}", sub.name()));
            return;
        }
        let state = if on { "ON" } else { "OFF" };
        match id {
            "obc.ai_efuse" => self.obc.ai_efuse = on,
            "obc.fpga_efuse" => self.obc.fpga_efuse = on,
            "obc.usb0" | "obc.usb1" | "obc.usb2" | "obc.usb3" => self.obc.usb[idx(id)] = on,
            "obc.mipi_power" => {
                let c = self.obc.mipi_sel;
                self.obc.mipi[c] = on;
                if !on && self.obc.mipi_route.map_or(false, |(rc, _)| rc == c) {
                    self.obc.mipi_route = None;
                }
                return self.log(
                    Sub::Obc,
                    Level::Ok,
                    format!("MIPI camera {} power {state}", c + 1),
                );
            }
            "exp.pc_rv1" | "exp.pc_rv2" | "exp.pc_rv3" | "exp.pc_rv4" => {
                self.exp.pc_route_valves[idx(id) - 1] = on
            }
            "exp.pc_flow_check" if self.exp.pc_target > 0 => {
                self.exp.pc_flow_check[self.exp.pc_target - 1] = on
            }
            "exp.ht_resume_boot" => return self.exp.ht_profile[self.exp.ht_sel].resume_boot = on,
            "exp.pc_datalog" => self.exp.pc_datalog = on,
            "exp.pc_resume" => self.exp.pc_resume = on,
            "exp.sln" => {
                if !on && (self.exp.u11.en || self.exp.u15.en || self.exp.pc_running) {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "SLN / HD OFF refused: pumps or PC experiment own the rail",
                    );
                }
                self.exp.sln = on;
                if !on {
                    self.exp.u11.detected = false;
                    self.exp.u11.inited = false;
                    self.exp.u15.detected = false;
                    self.exp.u15.inited = false;
                }
            }
            "exp.motor" => {
                if !on
                    && (self.exp.motors.iter().any(|m| m.running)
                        || self.exp.burn_left > 0.0
                        || self.exp.burn_armed_until.is_some())
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "MOTOR OFF refused: a motor is running, stop it first",
                    );
                }
                self.exp.motor = on;
                if !on {
                    self.exp.drv.detected = false;
                    self.exp.drv.inited = false;
                }
            }
            "exp.exp12v" => {
                if !on
                    && (self.exp.heater.iter().any(|&d| d > 0)
                        || self.exp.ir.iter().any(|&b| b)
                        || self.exp.pc_running)
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "EXP 12 V OFF refused: heaters, IR LEDs or PC experiment own the rail",
                    );
                }
                self.exp.exp12v = on;
                if !on {
                    self.exp.ir = [false; 2];
                    self.exp.ir_actual_duty = [0; 2];
                }
            }
            "exp.u11_en" => {
                if on && !(self.exp.sln && self.exp.u11.inited) {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump_enable refused: SLN / HD must be ON and U11 initialized",
                    );
                }
                self.exp.u11.en = on;
                return self.log(
                    Sub::Exp,
                    Level::Ok,
                    if on {
                        "pump_enable (U11)"
                    } else {
                        "pump_disable (U11)"
                    },
                );
            }
            "exp.u15_en" => {
                if on && !(self.exp.sln && self.exp.u15.inited) {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump4_enable refused: SLN / HD must be ON and U15 initialized",
                    );
                }
                self.exp.u15.en = on;
                return self.log(
                    Sub::Exp,
                    Level::Ok,
                    if on {
                        "pump4_enable (U15)"
                    } else {
                        "pump4_disable (U15)"
                    },
                );
            }
            "exp.ir0" | "exp.ir1" => {
                if on && !self.exp.exp12v {
                    return self.log(Sub::Exp, Level::Warn, "IR LED refused: EXP 12 V is OFF");
                }
                let i = idx(id);
                let cfg = self.exp.ir_setup[i];
                if on
                    && (!self.exp.ir_configured[i]
                        || cfg.max_duty == 0
                        || cfg.max_on_ms == 0
                        || cfg.duty > cfg.max_duty)
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "IR LED refused: configure duty, max duty, max on time and PWM first",
                    );
                }
                if on && self.exp.ir[1 - i] {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "IR LED refused: other channel active",
                    );
                }
                self.exp.ir[i] = on;
                self.exp.ir_actual_duty[i] = if on { cfg.duty } else { 0 };
            }
            "pdu.vout_en" => {
                if on && self.pdu.mode == 3 {
                    return self.log(
                        Sub::Pdu,
                        Level::Warn,
                        "VOUT enable refused: PDU in ERROR mode, clear faults first",
                    );
                }
                self.pdu.vout_en = on;
                if on {
                    self.pdu.trans_en = true;
                }
            }
            "pdu.trans_en" => {
                if on && self.pdu.mode == 3 {
                    return self.log(
                        Sub::Pdu,
                        Level::Warn,
                        "TRANS enable refused: PDU in ERROR mode, clear faults first",
                    );
                }
                self.pdu.trans_en = on;
                if !on {
                    self.pdu.vout_en = false;
                }
            }
            "pdu.heartbeat" => self.pdu.heartbeat = on,
            _ => return,
        }
        self.log(sub, Level::Ok, format!("{id} -> {state}"));
    }

    pub fn set_num(&mut self, id: &str, v: i32) {
        if self.live.mode != Mode::Sim && self.live_action(id, Act::Num(v)) {
            return;
        }
        if let Some(i) = pc_field_index(id) {
            self.stage_pc_field(i, v);
            return;
        }
        let sub = sub_of(id);
        if !self.reachable(sub) {
            self.log(sub, Level::Err, format!("{id}: no link to {}", sub.name()));
            return;
        }
        match id {
            "obc.led_pwm" => self.obc.usb_led_pwm[self.obc.usb_sel] = v,
            "exp.ht_duty" => return self.exp.ht_profile[self.exp.ht_sel].duty = v,
            "exp.cam_addr" => return self.exp.camera[self.exp.cam_sel].address = v,
            "exp.cam_emiss" => return self.exp.camera[self.exp.cam_sel].emissivity = v,
            "exp.cam_x" => return self.exp.camera[self.exp.cam_sel].x = v,
            "exp.cam_y" => return self.exp.camera[self.exp.cam_sel].y = v,
            "exp.cam_w" => return self.exp.camera[self.exp.cam_sel].width = v,
            "exp.cam_h" => return self.exp.camera[self.exp.cam_sel].height = v,
            "exp.ht_duration" => return self.exp.ht_profile[self.exp.ht_sel].duration = v,
            "exp.ht_max_duty" => return self.exp.ht_profile[self.exp.ht_sel].max_duty = v,
            "exp.ht_manual_slew" => return self.exp.ht_profile[self.exp.ht_sel].manual_slew = v,
            "exp.ht_max_on" => return self.exp.ht_profile[self.exp.ht_sel].max_on = v,
            "exp.ht_target" => return self.exp.ht_profile[self.exp.ht_sel].target = v,
            "exp.ht_delta" => return self.exp.ht_profile[self.exp.ht_sel].delta = v,
            "exp.ht_cutoff" => return self.exp.ht_profile[self.exp.ht_sel].cutoff = v,
            "exp.ht_timeout" => return self.exp.ht_profile[self.exp.ht_sel].timeout = v,
            "exp.ht_slew_up" => return self.exp.ht_profile[self.exp.ht_sel].slew_up = v,
            "exp.ht_slew_down" => return self.exp.ht_profile[self.exp.ht_sel].slew_down = v,
            "exp.ht_kp" => return self.exp.ht_profile[self.exp.ht_sel].kp = v,
            "exp.ht_ki" => return self.exp.ht_profile[self.exp.ht_sel].ki = v,
            "exp.ht_kd" => return self.exp.ht_profile[self.exp.ht_sel].kd = v,
            "exp.heater0" | "exp.heater1" | "exp.heater2" | "exp.heater3" => {
                if v > 0 && !self.exp.exp12v {
                    return self.log(Sub::Exp, Level::Warn, "heater refused: EXP 12 V is OFF");
                }
                self.exp.heater[idx(id)] = v;
            }
            // Staged values: only sent by the matching Set / Jog action, so no log line.
            "exp.u11_freq" => return self.exp.u11.freq_staged = v,
            "exp.u11_volt" => return self.exp.u11.volt_staged = v,
            "exp.u15_freq" => return self.exp.u15.freq_staged = v,
            "exp.u15_volt" => return self.exp.u15.volt_staged = v,
            "exp.pc_pulse" => return self.exp.pc_pulse = v,
            "exp.pc_log_ms" => return self.exp.pc_log_ms = v,
            "exp.pc_value" => return self.exp.pc_value = v,
            "exp.pc_minflow" if self.exp.pc_target > 0 => {
                return self.exp.pc_minflow_milli[self.exp.pc_target - 1] = v
            }
            "exp.pc_flow_delay" if self.exp.pc_target > 0 => {
                return self.exp.pc_flow_delay_ms[self.exp.pc_target - 1] = v
            }
            "exp.pc_fail_samples" if (1..=2).contains(&self.exp.pc_target) => {
                return self.exp.pc_fail_samples[self.exp.pc_target - 1] = v
            }
            "exp.msps" => return self.exp.msps = v,
            "exp.msteps" => return self.exp.move_steps[self.exp.mc] = v,
            "exp.mmax_sps" => return self.exp.motor_max_sps[self.exp.mc] = v,
            "exp.mdrive_pct" => {
                let motor = self.exp.mc;
                self.exp.motor_drive_pct[motor] = v;
                self.exp.motor_drive_dirty[motor] = v != self.exp.motor_drive_applied_pct[motor];
                return;
            }
            "exp.ir_duty" => return self.exp.ir_setup[self.exp.ir_sel].duty = v,
            "exp.ir_max_duty" => self.exp.ir_setup[self.exp.ir_sel].max_duty = v,
            "exp.ir_max_on" => self.exp.ir_setup[self.exp.ir_sel].max_on_ms = v,
            "exp.ir_pwm" => self.exp.ir_setup[self.exp.ir_sel].pwm_hz = v,
            "exp.burn_duty" => self.exp.burn_setup[self.exp.burn_sel].duty = v,
            "exp.burn_time" => self.exp.burn_setup[self.exp.burn_sel].burn_time_ms = v,
            "exp.burn_max_total_current" => {
                self.exp.burn_setup[self.exp.burn_sel].max_total_current_ma = v
            }
            "pdu.vout_set" => {
                self.pdu.vout_set = v;
                self.pdu.event_seq += 1;
                self.pdu.last_event = "CONFIG_CHANGED".into();
            }
            "pdu.temp_limit" => self.pdu.temp_limit = v,
            "pdu.led_on" => self.pdu.led_on = v,
            "pdu.led_off" => self.pdu.led_off = v,
            _ => return,
        }
        if matches!(id, "exp.ir_max_duty" | "exp.ir_max_on" | "exp.ir_pwm") {
            self.exp.ir_configured[self.exp.ir_sel] = false;
            self.exp.ir_config_pending[self.exp.ir_sel] = None;
        }
        if id.starts_with("exp.burn_") {
            self.exp.burn_configured[self.exp.burn_sel] = false;
        }
        self.log(sub, Level::Ok, format!("{id} = {v}"));
    }

    pub fn set_idx(&mut self, id: &str, v: usize) {
        if let Some(i) = pc_field_index(id) {
            self.stage_pc_field(i, v as i32);
            return;
        }
        if self.live.mode != Mode::Sim && id == "pdu.mode" {
            return self.log(
                Sub::Pdu,
                Level::Warn,
                "pdu.mode: PDU register not exposed by the flight software",
            );
        }
        match id {
            "pdu.mode" => {
                self.pdu.req_manual = v == 1;
                self.log(
                    Sub::Pdu,
                    Level::Ok,
                    format!("mode request {}", ["NORMAL", "MANUAL"][v.min(1)]),
                );
            }
            "obc.usb_sel" => {
                self.obc.usb_sel = v.min(3);
                if self.live.mode != Mode::Sim && self.live.uvc.iter().all(Option::is_none) {
                    self.live.send(
                        crate::proto::uvc_cfg_get(self.obc.usb_sel as u8 + 1),
                        crate::link::Why::Refresh,
                    );
                }
            }
            "obc.mipi_sel" => self.obc.mipi_sel = v,
            "exp.pc_v1" | "exp.pc_v2" | "exp.pc_v3" | "exp.pc_v4" => {
                self.exp.pc_valves[idx(id) - 1] = v.min(3)
            }
            "exp.pc_manual_v1" | "exp.pc_manual_v2" | "exp.pc_manual_v3" | "exp.pc_manual_v4" => {
                self.exp.pc_manual_valves[idx(id) - 1] = v.min(1);
                self.exp.pc_manual_edited = true;
            }
            "exp.pc_flow_ch" => self.exp.pc_flow_ch = v.min(1),
            "exp.pc_route" => self.exp.pc_route = v.min(4),
            "exp.pc_route_case" => self.exp.pc_route_case = v.min(3),
            "exp.pc_target" => self.exp.pc_target = v.min(3),
            "exp.pc_field" => self.exp.pc_field = v.min(15),
            "exp.pc_flow_action" if self.exp.pc_target > 0 => {
                self.exp.pc_flow_action[self.exp.pc_target - 1] = v.min(2)
            }
            "exp.pc_case" => self.exp.pc_case = v.min(2),
            "exp.pc_ground" => self.exp.pc_ground = v.min(3),
            "exp.ht_sel" | "exp.ht_profile_sel" => {
                self.exp.ht_sel = v.min(3);
            }
            "exp.ht_main_ntc" => self.exp.ht_profile[self.exp.ht_sel].main_ntc = v.min(3),
            "exp.ht_sec_ntc" => self.exp.ht_profile[self.exp.ht_sel].sec_ntc = v.min(3),
            "exp.ir_sel" => self.exp.ir_sel = v.min(1),
            "exp.cam_sel" => {
                self.exp.cam_sel = v.min(1);
                if self.live.mode != Mode::Sim {
                    self.live.send(
                        crate::proto::exp_run(&format!("camera_status {}", self.exp.cam_sel)),
                        crate::link::Why::Refresh,
                    );
                }
            }
            "exp.cam_model" => self.exp.camera[self.exp.cam_sel].model = v.min(1),
            "exp.cam_format" => self.exp.camera[self.exp.cam_sel].format = v.min(1),
            "exp.cam_hz" => self.exp.camera[self.exp.cam_sel].hz = v.min(3),
            "exp.burn_sel" => self.exp.burn_sel = v.min(1),
            "obc.mipi_sensor" => {
                let c = self.obc.mipi_sel;
                self.obc.mipi_sensor[c] = v;
            }
            "exp.u11_type" => self.exp.u11.ptype_staged = v,
            "exp.u15_ch" => self.exp.u15.ch = v,
            "exp.mc" => self.exp.mc = v,
            "exp.map_motor" => self.exp.map_motor = v.min(1),
            "exp.mdir" => self.exp.mdir = v,
            "exp.mmode" => self.exp.motor_mode_draft[self.exp.mc] = v,
            "exp.mpwm" => self.exp.motor_pwm[self.exp.mc] = v,
            "exp.mbank" => {
                self.exp.motor_bank[self.exp.map_motor] = v;
                self.exp.motor_limits_verified[self.exp.map_motor] = false;
            }
            "exp.mmin" => {
                self.exp.motor_min[self.exp.map_motor] = v;
                self.exp.motor_limits_verified[self.exp.map_motor] = false;
            }
            "exp.mmax" => {
                self.exp.motor_max[self.exp.map_motor] = v;
                self.exp.motor_limits_verified[self.exp.map_motor] = false;
            }
            "exp.mhome_end" => {
                self.exp.motor_home_end[self.exp.map_motor] = v;
                self.exp.motor_limits_verified[self.exp.map_motor] = false;
            }
            "exp.mmin_level" => self.exp.motor_min_level[self.exp.map_motor] = v,
            "exp.mmax_level" => self.exp.motor_max_level[self.exp.map_motor] = v,
            _ => {}
        }
    }

    pub fn run(&mut self, id: &str) {
        if self.live.mode != Mode::Sim && self.live_action(id, Act::Run) {
            return;
        }
        let sub = sub_of(id);
        let mode_group = match id {
            "exp.mmove" | "exp.mjog" | "exp.mhome" => Some("MANUAL commands"),
            "exp.mmove_op" | "exp.mhome_op" => Some("OPERATION commands"),
            _ => None,
        };
        if let Some(group) = mode_group {
            if let Some(reason) = crate::live::motor_group_reason(self, group) {
                return self.log(Sub::Exp, Level::Warn, reason);
            }
        }
        if !self.reachable(sub) {
            self.log(sub, Level::Err, format!("{id}: no link to {}", sub.name()));
            return;
        }
        match id {
            "exp.cam_apply" => {
                let i = self.exp.cam_sel;
                let c = &mut self.exp.camera[i];
                c.initialized = false;
                c.result = "configured; initialize sensor".into();
                c.actual_model = c.model;
                c.actual_address = c.address;
                c.actual_format = c.format;
                c.actual_hz = [2, 4, 8, 16][c.hz];
                c.confirmed = true;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("camera_config {i}: settings saved"),
                );
            }
            "exp.cam_buffer_on" | "exp.cam_buffer_off" => {
                let on = id.ends_with("_on");
                self.exp.camera[self.exp.cam_sel].buffer_enabled = Some(on);
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!(
                        "camera buffer {} {}",
                        self.exp.cam_sel,
                        if on { "enabled" } else { "disabled" }
                    ),
                );
            }
            "exp.cam_address_set" => {
                let i = self.exp.cam_sel;
                let c = &mut self.exp.camera[i];
                if c.buffer_enabled != Some(true) {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "camera_address_set: enable only the selected I2C branch first",
                    );
                }
                c.address_restart_required = true;
                c.result = "RESTART_REQUIRED".into();
                self.log(
                    Sub::Exp,
                    Level::Warn,
                    format!("camera_address_set {i}: restart EXP before further capture"),
                );
            }
            "exp.cam_detect" => self.log(
                Sub::Exp,
                Level::Ok,
                format!("camera_detect {}: sensor found", self.exp.cam_sel),
            ),
            "exp.cam_init" => {
                let i = self.exp.cam_sel;
                self.exp.camera[i].initialized = true;
                self.exp.camera[i].result = "OK".into();
                self.log(Sub::Exp, Level::Ok, format!("camera_init {i}: ready"));
            }
            "exp.cam_capture" => {
                let i = self.exp.cam_sel;
                if !self.exp.camera[i].initialized {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        format!("camera_capture {i}: initialize sensor first"),
                    );
                }
                let c = &mut self.exp.camera[i];
                c.request_id += 1;
                c.object_id += 1;
                c.data_exist = true;
                c.result = "OK".into();
                c.temp_centi = None;
                let object_id = c.object_id;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("camera_capture {i}: object {object_id} in MCU PSRAM"),
                );
            }
            "exp.cam_status" => {
                let i = self.exp.cam_sel;
                self.log(
                    Sub::Exp,
                    Level::Info,
                    format!("camera_status {i}: {}", self.exp.camera[i].result),
                );
            }
            "exp.cam_temp" => {
                let i = self.exp.cam_sel;
                let c = &mut self.exp.camera[i];
                if !c.data_exist || c.x + c.width > 32 || c.y + c.height > 24 {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "camera_temp: capture first and use an ROI within 32x24",
                    );
                }
                c.temp_centi = Some(2400 + (i as i32) * 100 + c.x * 4 + c.y * 2);
                c.sample_ms = (self.t * 1000.0) as u32;
                let temp = c.temp_centi.unwrap() as f32 / 100.0;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("camera_temp {i}: ROI max {temp:.2} °C"),
                );
            }
            "obc.capture" => {
                let cam = self.obc.usb_sel;
                if self.obc.usb_shot[cam].state == ShotState::Capturing {
                    return self.log(
                        Sub::Obc,
                        Level::Warn,
                        format!("usb_cam: camera {} is already capturing", cam + 1),
                    );
                }
                if !self.obc.usb[cam] {
                    let sh = &mut self.obc.usb_shot[cam];
                    sh.state = ShotState::Failed;
                    sh.note = "camera is not powered".into();
                    return self.log(
                        Sub::Obc,
                        Level::Warn,
                        format!(
                            "usb_cam: camera {} capture failed, camera is not powered",
                            cam + 1
                        ),
                    );
                }
                self.obc.led_pwm = self.obc.usb_led_pwm[cam];
                let path = format!(
                    "/home/bee/data/images/usb/USB{}_{:08}.jpg",
                    cam,
                    self.obc.rx_frames.max(1)
                );
                let sh = &mut self.obc.usb_shot[cam];
                *sh = Shot {
                    state: ShotState::Capturing,
                    path: path.clone(),
                    bytes: 0,
                    left: 2.0,
                    note: "1920x1080 MJPG".into(),
                };
                self.log(
                    Sub::Obc,
                    Level::Info,
                    format!("usb_cam: camera {} capturing to {path}", cam + 1),
                );
            }
            "obc.mipi_route" => {
                let (c, sn) = (self.obc.mipi_sel, self.obc.mipi_sensor[self.obc.mipi_sel]);
                self.obc.mipi[c] = true;
                self.obc.mipi_route = Some((c, sn));
                self.log(
                    Sub::Obc,
                    Level::Ok,
                    format!(
                        "MIPI route applied: camera {} sensor {sn}, switch readbacks verified",
                        c + 1
                    ),
                );
            }
            "obc.mipi_capture" => {
                if self.obc.mipi_shot.state == ShotState::Capturing {
                    return self.log(
                        Sub::Obc,
                        Level::Warn,
                        "capture_ar2020: a MIPI capture is already running",
                    );
                }
                let (c, sn) = (self.obc.mipi_sel, self.obc.mipi_sensor[self.obc.mipi_sel]);
                self.obc.mipi[c] = true;
                self.obc.mipi_route = Some((c, sn));
                let path = format!(
                    "/home/bee/data/images/mipi/MIPI{c}.{sn}_H_{:06}.png",
                    (self.obc.rx_frames % 999_999) + 1
                );
                self.obc.mipi_shot = Shot {
                    state: ShotState::Capturing,
                    path: path.clone(),
                    bytes: 0,
                    left: 4.0,
                    note: format!("camera {} sensor {sn}, 5120x3840 BG10", c + 1),
                };
                self.log(
                    Sub::Obc,
                    Level::Info,
                    format!(
                        "capture_ar2020: camera {} sensor {sn} capturing to {path}",
                        c + 1
                    ),
                );
            }
            "obc.hk_refresh" => {
                self.obc.snapshot_age = 0.0;
                self.log(Sub::Obc, Level::Ok, "hk_service snapshot refreshed");
            }
            "obc.reboot" => {
                let o = &mut self.obc;
                o.boot_count += 1;
                o.uptime = 0.0;
                o.usb = [false; 4];
                o.mipi = [false; 2];
                o.led_pwm = 0;
                o.ai_efuse = true;
                o.fpga_efuse = true;
                self.log(
                    Sub::Obc,
                    Level::Warn,
                    "OBC rebooted, boot count incremented",
                );
            }
            "exp.power_all_on" => {
                self.exp.sln = true;
                self.exp.motor = true;
                self.exp.exp12v = true;
                self.log(Sub::Exp, Level::Ok, "power_all_on");
            }
            "exp.power_all_off" => {
                let e = &self.exp;
                if e.u11.en
                    || e.u15.en
                    || e.motors.iter().any(|m| m.running)
                    || e.pc_running
                    || e.heater.iter().any(|&d| d > 0)
                    || e.ir.iter().any(|&b| b)
                    || e.burn_armed_until.is_some()
                    || e.burn_left > 0.0
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "power_all_off refused: consumers own a rail",
                    );
                }
                self.exp.sln = false;
                self.exp.motor = false;
                self.exp.exp12v = false;
                self.log(Sub::Exp, Level::Ok, "power_all_off");
            }
            "exp.u11_type_set" => {
                if self.exp.sln {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump_type_set refused: switch SLN / HD OFF first",
                    );
                }
                self.exp.u11.ptype = self.exp.u11.ptype_staged;
                self.exp.u11.ptype_configured = true;
                let t = PUMP_TYPES[self.exp.u11.ptype];
                self.exp.u11.detected = false;
                self.exp.u11.inited = false;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("pump_type_set {t}: detect and initialize again"),
                );
            }
            "exp.u11_type_get" => {
                let t = PUMP_TYPES[self.exp.u11.ptype];
                self.log(Sub::Exp, Level::Info, format!("pump_type_get: {t}"));
            }
            "exp.u11_detect" => {
                let ok = self.exp.sln;
                self.exp.u11.detected = ok;
                if ok {
                    self.log(Sub::Exp, Level::Ok, "pump_detect: U11 driver found");
                } else {
                    self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump_detect: no response, SLN / HD rail is OFF",
                    );
                }
            }
            "exp.u11_init" => {
                if !self.exp.u11.detected {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump_init refused: driver not detected",
                    );
                }
                self.exp.u11.inited = true;
                self.log(Sub::Exp, Level::Ok, "pump_init: U11 initialized");
            }
            "exp.u11_freq_set" => {
                if !self.exp.u11.inited {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump_set_freq refused: U11 not initialized",
                    );
                }
                self.exp.u11.freq = self.exp.u11.freq_staged;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("pump_set_freq {}", self.exp.u11.freq),
                );
            }
            "exp.u11_volt_set" => {
                if !self.exp.u11.inited {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump_set_volt refused: U11 not initialized",
                    );
                }
                self.exp.u11.volt = self.exp.u11.volt_staged;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("pump_set_volt {}", self.exp.u11.volt),
                );
            }
            "exp.u11_state" => {
                let u = &self.exp.u11;
                let m = format!(
                    "pump_state: type={} en={} freq={} Hz volt={} Vpp",
                    PUMP_TYPES[u.ptype], u.en as u8, u.freq, u.volt
                );
                self.log(Sub::Exp, Level::Info, m);
            }
            "exp.u15_detect" => {
                let ok = self.exp.sln;
                self.exp.u15.detected = ok;
                if ok {
                    self.log(Sub::Exp, Level::Ok, "pump4_detect: U15 driver found");
                } else {
                    self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump4_detect: no response, SLN / HD rail is OFF",
                    );
                }
            }
            "exp.u15_init" => {
                if !self.exp.u15.detected {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump4_init refused: driver not detected",
                    );
                }
                self.exp.u15.inited = true;
                self.log(Sub::Exp, Level::Ok, "pump4_init: U15 initialized");
            }
            "exp.u15_freq_set" => {
                if !self.exp.u15.inited {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump4_set_freq refused: U15 not initialized",
                    );
                }
                self.exp.u15.freq = self.exp.u15.freq_staged;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("pump4_set_freq {}", self.exp.u15.freq),
                );
            }
            "exp.u15_volt_set" => {
                if !self.exp.u15.inited {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "pump4_set_volt refused: U15 not initialized",
                    );
                }
                let (ch, v) = (self.exp.u15.ch, self.exp.u15.volt_staged);
                self.exp.u15.volt[ch] = v;
                self.log(Sub::Exp, Level::Ok, format!("pump4_set_volt {ch} {v}"));
            }
            "exp.u15_state" => {
                let u = &self.exp.u15;
                let m = format!(
                    "pump4_state: en={} freq={} Hz volt={:?} Vpp",
                    u.en as u8, u.freq, u.volt
                );
                self.log(Sub::Exp, Level::Info, m);
            }
            "exp.drv_detect" => {
                let ok = self.exp.motor;
                self.exp.drv.detected = ok;
                if ok {
                    self.log(Sub::Exp, Level::Ok, "DRV8910: driver found");
                } else {
                    self.log(
                        Sub::Exp,
                        Level::Warn,
                        "DRV8910: no response, MOTOR rail is OFF",
                    );
                }
            }
            "exp.drv_init" => {
                if !self.exp.drv.detected {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "drv8910_init refused: driver not detected",
                    );
                }
                self.exp.drv.inited = true;
                self.log(Sub::Exp, Level::Ok, "drv8910_init: driver initialized");
            }
            "exp.drv_status" => {
                let d = &self.exp.drv;
                let m = format!(
                    "DRV8910 detected={} init={} fault={}",
                    d.detected as u8,
                    d.inited as u8,
                    if d.fault { "YES" } else { "none" }
                );
                self.log(Sub::Exp, Level::Info, m);
            }
            "exp.drv_fault_clear" => {
                self.exp.drv.fault = false;
                self.log(Sub::Exp, Level::Ok, "drv8910_fault_clear");
            }
            "exp.motor_stop_all" => {
                for m in self.exp.motors.iter_mut() {
                    m.running = false;
                    m.homing = false;
                    m.holding = false;
                    m.remaining_steps = 0.0;
                }
                self.log(Sub::Exp, Level::Ok, "motor5_stop all");
            }
            "exp.mmax_apply" => self.log(
                Sub::Exp,
                Level::Ok,
                format!(
                    "motor5_cfg_set {} max_speed_sps {}",
                    self.exp.mc, self.exp.motor_max_sps[self.exp.mc]
                ),
            ),
            "exp.mdrive_apply" => {
                let motor = self.exp.mc;
                self.exp.motor_drive_applied_pct[motor] = self.exp.motor_drive_pct[motor];
                self.exp.motor_drive_dirty[motor] = false;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!(
                        "motor5_cfg_set {} drive_pct {}",
                        motor, self.exp.motor_drive_pct[motor]
                    ),
                );
            }
            "exp.mpwm_apply" => self.log(
                Sub::Exp,
                Level::Ok,
                format!(
                    "motor5_pwm_freq_set {} {}",
                    self.exp.mc,
                    [80, 100, 200, 2000][self.exp.motor_pwm[self.exp.mc]]
                ),
            ),
            "exp.mlimits_apply" => {
                let i = self.exp.map_motor;
                if self.exp.motor_min[i] == self.exp.motor_max[i] {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "MIN and MAX must use different inputs",
                    );
                }
                self.exp.motor_limits_verified[i] = true;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!(
                        "motor5_limits_set {i} {} {} {} {}",
                        ["pmu", "pml"][self.exp.motor_bank[i]],
                        self.exp.motor_min[i],
                        self.exp.motor_max[i],
                        ["min", "max"][self.exp.motor_home_end[i]]
                    ),
                );
            }
            "exp.mlevels_apply" => self.log(
                Sub::Exp,
                Level::Ok,
                format!(
                    "limit_cfg_set {} min={} max={}",
                    self.exp.map_motor,
                    ["low", "high"][self.exp.motor_min_level[self.exp.map_motor]],
                    ["low", "high"][self.exp.motor_max_level[self.exp.map_motor]]
                ),
            ),
            "exp.mmap_read" => self.log(
                Sub::Exp,
                Level::Info,
                format!("motor5_limit_get {}", self.exp.map_motor),
            ),
            "exp.msettings_read" => self.log(
                Sub::Exp,
                Level::Info,
                format!("motor {} settings read", self.exp.mc),
            ),
            "exp.mmode_apply" => {
                if self.exp.motors.iter().any(|m| m.running || m.homing) {
                    return self.log(Sub::Exp, Level::Warn, "motor5_mode_set refused: motor busy");
                }
                let i = self.exp.mc;
                self.exp.motor_mode[i] = self.exp.motor_mode_draft[i];
                self.exp.motor_mode_verified[i] = true;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!(
                        "motor5_mode_set {i} {}",
                        ["manual", "operation"][self.exp.motor_mode[i]]
                    ),
                );
            }
            "exp.mmode_read" => self.log(
                Sub::Exp,
                Level::Info,
                format!(
                    "motor {} mode={}",
                    self.exp.mc,
                    ["MANUAL", "OPERATION"][self.exp.motor_mode[self.exp.mc]]
                ),
            ),
            "exp.mjog" | "exp.mmove" | "exp.mmove_op" | "exp.mhome" | "exp.mhome_op" => {
                if !(self.exp.motor && self.exp.drv.inited) {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "motor command refused: MOTOR rail ON and driver initialized required",
                    );
                }
                let i = self.exp.mc;
                if self.exp.motors[i].running || self.exp.motors[i].homing {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "motor command refused: selected motor is busy",
                    );
                }
                if matches!(id, "exp.mjog" | "exp.mmove" | "exp.mmove_op")
                    && self.exp.msps > self.exp.motor_max_sps[i]
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "motor command refused: speed exceeds configured max speed",
                    );
                }
                if matches!(id, "exp.mhome" | "exp.mhome_op" | "exp.mmove_op")
                    && !self.exp.motor_limits_verified[i]
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "operation refused: map MIN/MAX and choose HOME endpoint first",
                    );
                }
                let (dir, sps, home) = (
                    if self.exp.mdir == 0 { 1 } else { -1 },
                    self.exp.msps,
                    matches!(id, "exp.mhome" | "exp.mhome_op"),
                );
                let m = &mut self.exp.motors[i];
                m.running = true;
                m.homing = home;
                m.holding = false;
                m.dir = dir;
                m.sps = sps;
                m.remaining_steps = if matches!(id, "exp.mmove" | "exp.mmove_op") {
                    self.exp.move_steps[i] as f32
                } else {
                    0.0
                };
                let msg = if home {
                    format!("motor5_home {i}")
                } else if matches!(id, "exp.mmove" | "exp.mmove_op") {
                    format!("motor5_move {i} {} {sps}", dir * self.exp.move_steps[i])
                } else {
                    format!("motor5_run {i} {} {sps}", ["cw", "ccw"][self.exp.mdir])
                };
                self.log(Sub::Exp, Level::Ok, msg);
            }
            "exp.mstop" | "exp.mstop_op" => {
                let i = self.exp.mc;
                let release = self.exp.motors[i].holding;
                self.exp.motors[i].running = false;
                self.exp.motors[i].homing = false;
                self.exp.motors[i].holding = !release && self.exp.drv.inited && self.exp.motor;
                self.exp.motors[i].remaining_steps = 0.0;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    if release {
                        format!("motor5_stop {i}")
                    } else {
                        format!("motor5_stop {i} hold")
                    },
                );
            }
            "exp.ht_limits_read" | "exp.ht_profile_read" | "exp.ht_status" => self.log(
                Sub::Exp,
                Level::Info,
                format!("heater {}: local profile refreshed", self.exp.ht_sel),
            ),
            "exp.ht_limits_apply" => {
                let i = self.exp.ht_sel;
                self.log(Sub::Exp, Level::Ok, format!("heater {i} limits configured"));
            }
            "exp.ht_profile_apply" => {
                let i = self.exp.ht_sel;
                if self.exp.ht_profile[i].cutoff <= self.exp.ht_profile[i].target {
                    return self.log(Sub::Exp, Level::Warn, "cutoff must exceed target");
                }
                self.exp.ht_loaded[i] = true;
                self.exp.ht_configured[i] = true;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("heater {i} profile configured"),
                );
            }
            "exp.ht_manual_set" => {
                let i = self.exp.ht_sel;
                if self.exp.ht_phase[i] != 0 {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        format!("profile {i} owns heater {i}; stop the profile first"),
                    );
                }
                if !self.exp.exp12v {
                    return self.log(Sub::Exp, Level::Warn, "EXP 12 V is OFF");
                }
                self.exp.heater[i] = self.exp.ht_profile[i]
                    .duty
                    .min(self.exp.ht_profile[i].max_duty);
                self.exp.ht_manual_left[i] = self.exp.ht_profile[i].duration as f64 / 1000.0;
                self.log(Sub::Exp, Level::Ok, format!("heater {i} manual ON"));
            }
            "exp.ht_manual_off" => {
                let i = self.exp.ht_sel;
                if self.exp.ht_phase[i] != 0 {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        format!("profile {i} owns heater {i}; stop the profile first"),
                    );
                }
                self.exp.heater[i] = 0;
                self.exp.ht_manual_left[i] = 0.0;
                self.log(Sub::Exp, Level::Ok, format!("heater {i} manual OFF"));
            }
            "exp.ht_start" | "exp.ht_resume" | "exp.ht_toggle0" | "exp.ht_toggle1"
            | "exp.ht_toggle2" | "exp.ht_toggle3" => {
                let i = id
                    .strip_prefix("exp.ht_toggle")
                    .and_then(|n| n.parse::<usize>().ok())
                    .unwrap_or(self.exp.ht_sel);
                if id.starts_with("exp.ht_toggle") && self.exp.ht_running[i] {
                    self.exp.ht_running[i] = false;
                    self.exp.ht_phase[i] = 0;
                    self.exp.heater[i] = 0;
                    return self.log(Sub::Exp, Level::Ok, format!("heater {i} auto stopped"));
                }
                if !self.exp.exp12v || !self.exp.ht_configured[i] {
                    return self.log(Sub::Exp, Level::Warn, "EXP 12 V or profile not ready");
                }
                self.exp.ht_running[i] = true;
                self.exp.ht_phase[i] = 1;
                self.exp.ht_reason[i] = 0;
                self.log(Sub::Exp, Level::Ok, format!("heater {i} auto running"));
            }
            "exp.ht_stop" => {
                let i = self.exp.ht_sel;
                self.exp.ht_running[i] = false;
                self.exp.ht_phase[i] = 0;
                self.exp.heater[i] = 0;
                self.log(Sub::Exp, Level::Ok, format!("heater {i} auto stopped"));
            }
            "exp.heater_stop" => {
                self.exp.heater = [0; 4];
                self.log(Sub::Exp, Level::Ok, "heater_stop all");
            }
            "exp.ir_apply" => {
                let i = self.exp.ir_sel;
                let c = self.exp.ir_setup[i];
                if c.max_duty < 1
                    || c.max_duty > 25
                    || c.max_on_ms < 1
                    || c.max_on_ms > 100
                    || !(100..=100000).contains(&c.pwm_hz)
                {
                    return self.log(Sub::Exp, Level::Warn,
                        "set max duty 1..25%, max-at-limit time 1..100 ms, and PWM 100..100000 Hz before Queue limits");
                }
                self.exp.ir_configured[i] = true;
                self.exp.ir_config_pending[i] = None;
                self.exp.ir_status_seen[i] = true;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!(
                        "IR {i} configured: duty {}%, max {}%, on {} ms, PWM {} Hz",
                        c.duty, c.max_duty, c.max_on_ms, c.pwm_hz
                    ),
                );
            }
            "exp.ir_start" => self.set_bool(
                if self.exp.ir_sel == 0 {
                    "exp.ir0"
                } else {
                    "exp.ir1"
                },
                true,
            ),
            "exp.ir_stop" => self.set_bool(
                if self.exp.ir_sel == 0 {
                    "exp.ir0"
                } else {
                    "exp.ir1"
                },
                false,
            ),
            "exp.ir_status" => {
                let i = self.exp.ir_sel;
                self.log(
                    Sub::Exp,
                    Level::Info,
                    format!(
                        "IR {i}: active={} duty={}%, configured={}",
                        self.exp.ir[i], self.exp.ir_setup[i].duty, self.exp.ir_configured[i]
                    ),
                );
            }
            "exp.burn_apply" => {
                let i = self.exp.burn_sel;
                if !self.exp.burn_setup[i].valid() {
                    return self.log(Sub::Exp, Level::Warn, "burn configuration invalid");
                }
                self.exp.burn_configured[i] = true;
                self.exp.burn_status_seen[i] = true;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("burn wire {i} configuration persisted (simulated)"),
                );
            }
            "exp.burn_status" => {
                let i = self.exp.burn_sel;
                self.log(
                    Sub::Exp,
                    Level::Info,
                    format!(
                        "burn wire {i}: configured={} armed={} firing={}",
                        self.exp.burn_configured[i],
                        self.exp.burn_armed_until.is_some_and(|(w, _)| w == i),
                        self.exp.burn_left > 0.0
                    ),
                );
            }
            "exp.burn_arm" => {
                let i = self.exp.burn_sel;
                if !self.exp.motor
                    || !self.exp.burn_configured[i]
                    || self.exp.burn_left > 0.0
                    || self.exp.burn_armed_until.is_some()
                    || self.exp.motors.iter().any(|m| m.running)
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "burn arm refused: MOTOR rail, valid config and idle motor required",
                    );
                }
                self.exp.burn_armed_until = Some((i, self.t + 5.0));
                self.exp.burn_state = "ARMED".into();
                self.exp.burn_active_wire = Some(i);
                self.log(
                    Sub::Exp,
                    Level::Warn,
                    format!("burn wire {i} ARMED for 5 s"),
                );
            }
            "exp.burn_abort" => {
                if let Some(wire) = self.exp.burn_active_wire {
                    if self.exp.burn_left > 0.0 {
                        self.exp.burn_last[wire] = "ABORTED".into();
                    }
                }
                self.exp.burn_left = 0.0;
                self.exp.burn_armed_until = None;
                self.exp.burn_state = "IDLE".into();
                self.exp.burn_active_wire = None;
                self.log(
                    Sub::Exp,
                    Level::Warn,
                    "burn outputs OFF; reservation released",
                );
            }
            "exp.burn" => {
                if !self.exp.motor
                    || !self
                        .exp
                        .burn_armed_until
                        .is_some_and(|(i, until)| i == self.exp.burn_sel && self.t < until)
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "burn_fire refused: selected wire not armed or MOTOR rail OFF",
                    );
                }
                self.exp.burn_armed_until = None;
                self.exp.burn_left =
                    self.exp.burn_setup[self.exp.burn_sel].burn_time_ms as f32 / 1000.0;
                self.exp.burn_count += 1;
                self.exp.burn_state = "FIRING".into();
                self.exp.burn_active_wire = Some(self.exp.burn_sel);
                self.exp.burn_attempt[self.exp.burn_sel] += 1;
                self.exp.burn_last[self.exp.burn_sel] = "IN_PROGRESS".into();
                self.log(Sub::Exp, Level::Warn, "burn_fire: burn wire FIRING");
            }
            "exp.pc_map_apply" => {
                let v = self.exp.pc_valves;
                if (0..4).any(|i| (i + 1..4).any(|j| v[i] == v[j])) {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "PC valve map refused: duplicate channel",
                    );
                }
                self.log(Sub::Exp, Level::Ok, format!("PC valve map applied: {v:?}"));
            }
            "exp.pc_manual_valves_apply" => {
                if !self.exp.pc_activated
                    || !matches!(self.exp.pc_state.as_str(), "STANDBY" | "ONGROUND")
                    || self.exp.u11.en
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "manual valves require PC activated in STANDBY/ONGROUND with pump OFF",
                    );
                }
                let mask = self
                    .exp
                    .pc_manual_valves
                    .iter()
                    .enumerate()
                    .fold(0u8, |bits, (i, &v)| bits | ((v as u8) << i));
                self.exp.pc_manual_actual = mask;
                self.exp.pc_manual_valid = 0x0f;
                self.exp.pc_manual_edited = false;
                self.exp.pc_state = "ONGROUND".into();
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("manual valves V1–V4 mask=0x{mask:X}"),
                );
            }
            "exp.pc_manual_pump_init" => {
                if self.exp.pc_activated || !self.exp.sln || !self.exp.u11.ptype_configured {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "manual U11 init requires PC deactivated, SLN/HD ON and pump type selected",
                    );
                }
                self.exp.u11.inited = true;
                self.log(Sub::Exp, Level::Ok, "manual U11 initialized");
            }
            "exp.pc_manual_pump_freq_set"
            | "exp.pc_manual_pump_volt_set"
            | "exp.pc_manual_pump_on"
            | "exp.pc_manual_pump_off" => {
                if self.exp.pc_activated
                    || !self.exp.sln
                    || (!self.exp.u11.inited && id != "exp.pc_manual_pump_off")
                {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "manual U11 requires PC deactivated, SLN/HD ON and U11 initialized",
                    );
                }
                match id {
                    "exp.pc_manual_pump_freq_set" => self.exp.u11.freq = self.exp.u11.freq_staged,
                    "exp.pc_manual_pump_volt_set" => self.exp.u11.volt = self.exp.u11.volt_staged,
                    "exp.pc_manual_pump_on" => self.exp.u11.en = true,
                    _ => self.exp.u11.en = false,
                }
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!(
                        "manual U11: {} Hz {} Vpp {}",
                        self.exp.u11.freq,
                        self.exp.u11.volt,
                        if self.exp.u11.en { "ON" } else { "OFF" }
                    ),
                );
            }
            "exp.pc_manual_pump_status" => self.log(
                Sub::Exp,
                Level::Info,
                format!(
                    "U11: {} Hz {} Vpp {}",
                    self.exp.u11.freq,
                    self.exp.u11.volt,
                    if self.exp.u11.en { "ON" } else { "OFF" }
                ),
            ),
            "exp.pc_profile_apply" => {
                if self.exp.pc_state != "STANDBY" {
                    return self.log(Sub::Exp, Level::Warn, "stop PC before changing profile");
                }
                let target = self.exp.pc_target;
                let mut changed = 0;
                for i in 0..16 {
                    if self.exp.pc_dirty[target][i] && pc_field_allowed(target, i) {
                        self.exp.pc_applied[target][i] = Some(self.exp.pc_fields[target][i]);
                        self.exp.pc_dirty[target][i] = false;
                        changed += 1;
                    }
                }
                if target == 0 && self.exp.pc_name_dirty {
                    self.exp.pc_name_applied = Some(self.exp.pc_name_draft.clone());
                    self.exp.pc_profile_name = self.exp.pc_name_draft.clone();
                    self.exp.pc_name_dirty = false;
                    changed += 1;
                }
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!("PC profile: {changed} changed fields applied (simulated)"),
                );
            }
            "exp.pc_pulse_apply"
            | "exp.pc_route_apply"
            | "exp.pc_flow_apply"
            | "exp.pc_log_ms_apply"
            | "exp.pc_flow_policy_apply"
            | "exp.pc_profile_read" => {
                self.log(Sub::Exp, Level::Ok, format!("{id}: simulated PC setting"))
            }
            "exp.pc_status" => self.log(
                Sub::Exp,
                Level::Info,
                format!(
                    "PC {} activated={} error={}",
                    self.exp.pc_state, self.exp.pc_activated, self.exp.pc_error
                ),
            ),
            "exp.pc_activate" => {
                if !self.exp.u11.ptype_configured {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "Select the U11 pump driver and press Set type before PC Activate",
                    );
                }
                self.exp.pc_activated = true;
                self.exp.sln = true;
                self.exp.pc_state = "STANDBY".into();
                self.log(Sub::Exp, Level::Ok, "PC activated; STANDBY");
            }
            "exp.pc_deactivate" => {
                if self.exp.pc_state != "STANDBY" {
                    return self.log(Sub::Exp, Level::Warn, "PC must be in STANDBY to deactivate");
                }
                self.exp.pc_activated = false;
                self.exp.sln = false;
                self.exp.u11.en = false;
                self.exp.u11.inited = false;
                self.log(Sub::Exp, Level::Ok, "PC deactivated");
            }
            "exp.pc_test" | "exp.pc_replace" | "exp.pc_circulate" => {
                if !self.exp.pc_activated || self.exp.pc_state != "STANDBY" {
                    return self.log(
                        Sub::Exp,
                        Level::Warn,
                        "PC run refused: activate and enter STANDBY first",
                    );
                }
                self.exp.pc_state = match id {
                    "exp.pc_test" => "TEST",
                    "exp.pc_replace" => "REPLACE",
                    _ => "CIRCULATION",
                }
                .into();
                self.exp.pc_running = true;
                self.exp.pc_progress = 0.0;
                self.log(
                    Sub::Exp,
                    Level::Ok,
                    format!(
                        "{} started, case {}",
                        self.exp.pc_state,
                        ["both", "A", "B"][self.exp.pc_case]
                    ),
                );
            }
            "exp.pc_ground_apply" => {
                if !self.exp.pc_activated {
                    return self.log(Sub::Exp, Level::Warn, "activate PC first");
                }
                self.exp.pc_state = "ONGROUND".into();
                self.log(Sub::Exp, Level::Ok, "PC ONGROUND route selected");
            }
            "exp.pc_stop" | "exp.pc_standby" | "exp.pc_reset_error" => {
                self.exp.pc_running = false;
                self.exp.pc_state = "STANDBY".into();
                self.exp.pc_error = "NONE".into();
                self.log(Sub::Exp, Level::Ok, "PC STANDBY");
            }
            "exp.reset" => {
                let e = &mut self.exp;
                e.sln = false;
                e.motor = false;
                e.exp12v = false;
                e.heater = [0; 4];
                e.u11 = Pump1 {
                    ptype: e.u11.ptype,
                    detected: false,
                    inited: false,
                    en: false,
                    ..e.u11
                };
                e.u15 = Pump4 {
                    detected: false,
                    inited: false,
                    en: false,
                    volt: [0; 4],
                    ..e.u15
                };
                e.drv = Drv {
                    detected: false,
                    inited: false,
                    fault: false,
                };
                for m in e.motors.iter_mut() {
                    m.running = false;
                    m.homing = false;
                    m.holding = false;
                    m.pos = 0.0;
                }
                e.ir = [false; 2];
                e.ir_actual_duty = [0; 2];
                e.ir_configured = [false; 2];
                e.burn_armed_until = None;
                e.burn_left = 0.0;
                e.burn_state = "IDLE".into();
                e.burn_active_wire = None;
                e.pc_running = false;
                e.uptime = 0.0;
                self.log(
                    Sub::Exp,
                    Level::Warn,
                    "EXP MCU reset, rails at power-on state",
                );
            }
            "pdu.fault_clear" => {
                if self.pdu.trans_fault {
                    return self.log(
                        Sub::Pdu,
                        Level::Warn,
                        "fault still present, latch not cleared",
                    );
                }
                self.pdu.latched = false;
                self.log(Sub::Pdu, Level::Ok, "latched faults cleared");
            }
            "pdu.retry" => {
                if self.pdu.trans_fault {
                    return self.log(Sub::Pdu, Level::Warn, "retry failed: fault still present");
                }
                self.pdu.latched = false;
                self.log(Sub::Pdu, Level::Ok, "retry: back to NORMAL");
            }
            "pdu.bootloader" => {
                let p = &mut self.pdu;
                p.vout_en = false;
                p.trans_en = false;
                p.boot_count += 1;
                p.boot_cause = "COMMAND";
                self.log(
                    Sub::Pdu,
                    Level::Warn,
                    "PDU entered bootloader (simulated, firmware restarted)",
                );
            }
            _ => {}
        }
    }
}

const PUMP_TYPES: [&str; 2] = ["lowdriver", "highdriver"];

/// Trailing digit of ids like `obc.usb2` / `exp.heater3`.
fn idx(id: &str) -> usize {
    id.chars().last().and_then(|c| c.to_digit(10)).unwrap_or(0) as usize
}

fn sub_of(id: &str) -> Sub {
    if id.starts_with("obc.") {
        Sub::Obc
    } else if id.starts_with("exp.") {
        Sub::Exp
    } else {
        Sub::Pdu
    }
}

pub fn civil(ts: i64) -> String {
    let days = ts.div_euclid(86_400);
    let secs = ts.rem_euclid(86_400);
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    format!(
        "{y:04}-{m:02}-{d:02} {:02}:{:02}:{:02}",
        secs / 3600,
        (secs / 60) % 60,
        secs % 60
    )
}

/// Parse an exact UTC clock value accepted by the 32-bit RTC flight command.
pub fn parse_utc(input: &str) -> Result<u32, &'static str> {
    let b = input.as_bytes();
    if b.len() != 19
        || b[4] != b'-'
        || b[7] != b'-'
        || b[10] != b' '
        || b[13] != b':'
        || b[16] != b':'
    {
        return Err("use YYYY-MM-DD HH:MM:SS (UTC)");
    }
    let field = |start: usize, end: usize| -> Option<i64> {
        let s = &b[start..end];
        if !s.iter().all(u8::is_ascii_digit) {
            return None;
        }
        std::str::from_utf8(s).ok()?.parse().ok()
    };
    let (y, m, d, h, min, sec) = (
        field(0, 4).ok_or("invalid UTC digits")?,
        field(5, 7).ok_or("invalid UTC digits")?,
        field(8, 10).ok_or("invalid UTC digits")?,
        field(11, 13).ok_or("invalid UTC digits")?,
        field(14, 16).ok_or("invalid UTC digits")?,
        field(17, 19).ok_or("invalid UTC digits")?,
    );
    if !(1..=12).contains(&m) || h > 23 || min > 59 || sec > 59 {
        return Err("UTC date or time is out of range");
    }
    let leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    let days_in_month = [
        31,
        if leap { 29 } else { 28 },
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31,
    ];
    if d < 1 || d > days_in_month[(m - 1) as usize] {
        return Err("UTC day is out of range");
    }
    // Inverse of `civil`, using days since 1970-01-01.
    let yr = y - if m <= 2 { 1 } else { 0 };
    let era = yr.div_euclid(400);
    let yoe = yr - era * 400;
    let mp = m + if m > 2 { -3 } else { 9 };
    let doy = (153 * mp + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    let epoch = (era * 146_097 + doe - 719_468) * 86_400 + h * 3600 + min * 60 + sec;
    u32::try_from(epoch).map_err(|_| "UTC time must be within 1970-01-01 to 2106-02-07")
}

#[cfg(test)]
mod utc_tests {
    use super::*;

    #[test]
    fn parses_utc_and_rejects_invalid_dates() {
        for epoch in [0, 1_709_251_200, u32::MAX] {
            assert_eq!(parse_utc(&civil(epoch as i64)), Ok(epoch));
        }
        assert!(parse_utc("2025-02-29 12:00:00").is_err());
        assert!(parse_utc("2024-02-29 24:00:00").is_err());
        assert!(parse_utc("2106-02-08 00:00:00").is_err());
    }
}

#[cfg(test)]
mod exp_output_tests {
    use super::*;

    #[test]
    fn ir_requires_limits_and_exp_12v_before_it_can_turn_on() {
        let mut sim = Sim::new();
        sim.set_bool("exp.ir0", true);
        assert!(!sim.exp.ir[0]);
        sim.set_bool("exp.exp12v", true);
        sim.set_bool("exp.ir0", true);
        assert!(!sim.exp.ir[0]);
        sim.set_num("exp.ir_max_duty", 20);
        sim.set_num("exp.ir_max_on", 50);
        sim.run("exp.ir_apply");
        sim.set_bool("exp.ir0", true);
        assert!(sim.exp.ir[0]);
        sim.set_bool("exp.ir1", true);
        assert!(!sim.exp.ir[1]);
    }

    #[test]
    fn burn_requires_valid_config_motor_rail_and_recent_arm() {
        let mut sim = Sim::new();
        sim.run("exp.burn");
        assert_eq!(sim.exp.burn_count, 0);
        sim.exp.burn_setup[0] = BurnSetup {
            duty: 25,
            burn_time_ms: 100,
            max_total_current_ma: 1000,
        };
        sim.run("exp.burn_apply");
        sim.run("exp.burn_arm");
        assert!(sim.exp.burn_armed_until.is_none());
        sim.set_bool("exp.motor", true);
        sim.run("exp.burn_arm");
        assert!(sim.exp.burn_armed_until.is_some());
        sim.run("exp.burn");
        assert_eq!(sim.exp.burn_count, 1);
        assert_eq!(sim.exp.burn_left, 0.1);
    }

    #[test]
    fn relative_motor_move_stops_after_requested_steps() {
        let mut sim = Sim::new();
        sim.set_bool("exp.motor", true);
        sim.run("exp.drv_detect");
        sim.run("exp.drv_init");
        sim.exp.msps = 20;
        sim.exp.move_steps[0] = 30;
        sim.exp.mdir = 1;
        sim.run("exp.mmove");
        sim.tick(1.0);
        assert_eq!(sim.exp.motors[0].pos, -20.0);
        assert!(sim.exp.motors[0].running);
        sim.tick(1.0);
        assert_eq!(sim.exp.motors[0].pos, -30.0);
        assert!(!sim.exp.motors[0].running);
    }

    #[test]
    fn heater_manual_duration_and_auto_profile_state() {
        let mut sim = Sim::new();
        sim.set_bool("exp.exp12v", true);
        sim.set_num("exp.ht_duration", 500);
        sim.run("exp.ht_manual_set");
        assert_eq!(sim.exp.heater[0], 10);
        sim.tick(0.6);
        assert_eq!(sim.exp.heater[0], 0);
        sim.run("exp.ht_profile_apply");
        sim.run("exp.ht_start");
        assert!(sim.exp.ht_running[0]);
        assert_eq!(sim.exp.ht_phase[0], 1);
        sim.tick(1.0);
        assert!(sim.exp.heater[0] > 0);
        sim.run("exp.ht_stop");
        assert!(!sim.exp.ht_running[0]);
        assert_eq!(sim.exp.heater[0], 0);
    }

    #[test]
    fn per_profile_button_controls_its_own_heater() {
        let mut sim = Sim::new();
        sim.set_bool("exp.exp12v", true);
        sim.set_idx("exp.ht_profile_sel", 2);
        assert_eq!(sim.exp.ht_sel, 2);
        sim.run("exp.ht_profile_apply");
        sim.set_idx("exp.ht_profile_sel", 0);
        sim.run("exp.ht_toggle2");
        assert!(sim.exp.ht_running[2]);
        assert!(!sim.exp.ht_running[0]);
        sim.run("exp.ht_toggle2");
        assert!(!sim.exp.ht_running[2]);
    }

    #[test]
    fn running_profile_blocks_manual_heater_control() {
        let mut sim = Sim::new();
        sim.set_bool("exp.exp12v", true);
        sim.run("exp.ht_profile_apply");
        sim.run("exp.ht_start");
        sim.run("exp.ht_manual_set");
        assert_eq!(sim.exp.heater[0], 0);
        sim.run("exp.ht_manual_off");
        assert!(sim.exp.ht_running[0]);
    }

    #[test]
    fn stop_motor_holds_until_next_action_or_second_stop() {
        let mut sim = Sim::new();
        sim.exp.motor = true;
        sim.exp.drv.inited = true;
        sim.exp.motors[0].running = true;
        sim.run("exp.mstop");
        assert!(!sim.exp.motors[0].running && sim.exp.motors[0].holding);
        sim.run("exp.mjog");
        assert!(sim.exp.motors[0].running && !sim.exp.motors[0].holding);
        sim.run("exp.mstop");
        assert!(sim.exp.motors[0].holding);
        sim.run("exp.mstop");
        assert!(!sim.exp.motors[0].holding);
    }

    #[test]
    fn motor_mode_switch_selects_command_panel() {
        let mut sim = Sim::new();
        sim.set_idx("exp.mmode", 1);
        sim.run("exp.mmode_apply");
        assert_eq!(sim.exp.motor_mode[0], 1);
        sim.run("exp.mjog");
        assert!(!sim.exp.motors[0].running);
        sim.run("exp.mmode_read");
        assert!(sim
            .take_logs()
            .iter()
            .any(|entry| entry.text.contains("mode=OPERATION")));
    }
}
