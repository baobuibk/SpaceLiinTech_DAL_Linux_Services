//! Glue between the UI model (`Sim` structs) and the real flight software.
//!
//! Outgoing: `plan()` turns a control id and action into flight commands.
//! Incoming: `apply()` decodes each reply into the same `Obc`/`Exp`/`Pdu` fields
//! the simulator fills, so the panels need no knowledge of the data source.

use crate::controls::Sub;
use crate::json::Json;
use crate::link::{Done, Link, Mode, Reply, Why, DEFAULT_CONSOLE, DEFAULT_RS422};
use crate::proto::{self, Board, Cmd};
use crate::sim::{unix_now, Level, Shot, ShotState, Sim};
use std::collections::VecDeque;
use std::time::Instant;

/// Marker for a counter the active data source does not provide.
pub const NA_U32: u32 = u32::MAX;

#[derive(Clone)]
pub struct Txn {
    pub clock: i64,
    pub why: String,
    pub cmd: String,
    pub ty: u8,
    pub addr: u8,
    pub tx: String,
    pub rx: String,
    pub ms: u64,
    pub ok: bool,
    pub summary: String,
}

/// One housekeeping round in flight (every command of `poll_set` plus `gui_state`).
pub struct Round {
    pub sent: usize,
    pub done: usize,
    pub ok: usize,
    pub manual: bool,
    pub t0: Instant,
}

/// Result of the last finished round, shown on HOME.
#[derive(Clone)]
pub struct Updated {
    pub clock: i64,
    pub sent: usize,
    pub ok: usize,
    pub ms: u64,
}

pub struct Live {
    pub mode: Mode,
    pub console_path: String,
    pub rs422_path: String,
    pub baud: u32,
    pub port_values: [String; 9],
    pub port_loaded: bool,
    pub port_restart_pending: bool,
    pub link: Option<Link>,
    pub pending: usize,
    pub poll_every: f64,
    pub poll_clock: f64,
    /// Housekeeping update: periodic (true) or only when the operator asks (false).
    pub auto: bool,
    /// Live control mode: only explicit operator actions may reach the FSW.
    pub control_only: bool,
    /// The operator pressed UPDATE; the round starts as soon as the link is idle.
    pub update_req: bool,
    pub round: Option<Round>,
    pub last_update: Option<Updated>,
    pub connected: bool,
    pub last_err: String,
    pub hist: VecDeque<Txn>,
    pub ok_count: u32,
    pub err_count: u32,
    /// Pending USB capture request per camera: (request id, sent at).
    pub uvc: [Option<(u32, f64)>; 4],
    pub next_req: u32,
    pub uvc_clock: f64,
    /// Explicit EXP thermal-camera operations awaiting their asynchronous MCU result.
    pub cam_wait: [Option<f64>; 2],
    /// Capture whose archived filename has not yet been reported by FSW.
    pub cam_download_wait: [Option<f64>; 2],
    pub cam_clock: f64,
    /// Pending asynchronous pump detect: (request id, target 0 = U11 / 1 = U15, sent at).
    pub pump_req: Option<(u32, u8, f64)>,
    pub valve_verify_at: Option<f64>,
    /// MCU-confirmed heater duty while the periodic table-12 cache catches up.
    pub heater_pending: [Option<(u8, f64)>; 4],
    /// One cache read after the next FSW housekeeping cycle following profile control.
    pub thermal_verify_at: Option<f64>,
    /// Profile button waiting for one fresh table-3 cache reply.
    pub heater_preflight: Option<String>,
    /// Latest `gui_state` for the CFG page.
    pub gui_state_age: f64,
    pub ir_verify_at: f64,
    /// Short status follow-up after a burn; the immediate reply can still say FIRING.
    pub burn_verify_until: Option<f64>,
    pub burn_verify_at: f64,
}

pub enum Act {
    Bool(bool),
    Num(i32),
    Epoch(u32),
    Run,
}

const HIST_CAP: usize = 120;

impl Live {
    pub fn new(mode: Mode, console_path: &str, rs422_path: &str) -> Live {
        let mut l = Live {
            mode: Mode::Sim,
            console_path: console_path.into(),
            rs422_path: rs422_path.into(),
            baud: 921_600,
            port_values: [
                "/dev/ttyS1",
                "/dev/ttyS4",
                "250000",
                "/dev/ttyS3",
                "1",
                "/dev/ttyS2",
                "115200",
                "2",
                "1",
            ]
            .map(str::to_string),
            port_loaded: false,
            port_restart_pending: false,
            link: None,
            pending: 0,
            poll_every: 2.0,
            poll_clock: 0.0,
            auto: true,
            control_only: false,
            update_req: false,
            round: None,
            last_update: None,
            connected: false,
            last_err: String::new(),
            hist: VecDeque::new(),
            ok_count: 0,
            err_count: 0,
            uvc: [None; 4],
            next_req: (unix_now() as u32) & 0x00FF_FFFF | 1,
            uvc_clock: 0.0,
            cam_wait: [None; 2],
            cam_download_wait: [None; 2],
            cam_clock: 0.0,
            pump_req: None,
            valve_verify_at: None,
            heater_pending: [None; 4],
            thermal_verify_at: None,
            heater_preflight: None,
            gui_state_age: -1.0,
            ir_verify_at: 0.0,
            burn_verify_until: None,
            burn_verify_at: 0.0,
        };
        l.set_mode(mode);
        l
    }

    pub fn target(&self) -> &str {
        match self.mode {
            Mode::Rs422 => &self.rs422_path,
            _ => &self.console_path,
        }
    }

    /// Drop the old link (closing its socket) and open one for `mode`.
    pub fn set_mode(&mut self, mode: Mode) {
        self.link = None;
        self.mode = mode;
        self.pending = 0;
        self.connected = false;
        self.last_err.clear();
        self.port_loaded = false;
        self.uvc = [None; 4];
        self.cam_wait = [None; 2];
        self.cam_download_wait = [None; 2];
        self.pump_req = None;
        self.valve_verify_at = None;
        self.heater_pending = [None; 4];
        self.update_req = false;
        self.round = None;
        self.last_update = None;
        self.poll_clock = self.poll_every; // poll at the next tick
        self.ir_verify_at = 0.0;
        self.burn_verify_until = None;
        self.burn_verify_at = 0.0;
        if mode != Mode::Sim {
            let target = self.target().to_string();
            self.link = Some(Link::start(mode, &target, self.baud));
        }
    }

    /// AUTO polls every `poll_every`; MANUAL sends nothing until `request_update`.
    pub fn set_auto(&mut self, auto: bool) {
        self.auto = auto;
        self.update_req = false;
        if auto {
            self.poll_clock = self.poll_every; // refresh right away
        }
    }

    /// Ask for one housekeeping round; it starts when the link has no command in flight.
    pub fn request_update(&mut self) {
        self.update_req = true;
    }

    pub fn reconnect(&mut self) {
        let m = self.mode;
        self.set_mode(m);
    }

    fn record(&mut self, t: Txn) {
        if t.ok {
            self.ok_count += 1;
        } else {
            self.err_count += 1;
        }
        self.hist.push_back(t);
        while self.hist.len() > HIST_CAP {
            self.hist.pop_front();
        }
    }

    pub fn send(&mut self, cmd: Cmd, why: Why) {
        if self.control_only && !matches!(why, Why::Action(_)) {
            return;
        }
        if let Some(l) = &self.link {
            l.send(cmd, why);
            self.pending += 1;
        }
    }
}

// ── Polling ───────────────────────────────────────────────────────────────────

/// One telemetry round. The first column names the UI field family it feeds.
fn poll_set() -> Vec<Cmd> {
    vec![
        proto::ping(),               // OBC uptime
        proto::sys_info(),           // board online flags and versions
        proto::sys_rtc_get(),        // RTC
        proto::tm_obc_get(),         // OBC onboard temp, NTC1-4, BME688, PAC U6/U2
        proto::pwr_stat(Board::Obc), // eFuse, USB and MIPI switch states
        proto::pwr_stat(Board::Pdu), // VOUT, TRANS, input rails
        proto::pdu_vout_get(),       // VOUT setpoint
        proto::pdu_heartbeat_get(),  // heartbeat enable and on/off periods
        proto::pwr_stat(Board::Exp), // EXP switches and PAC rails
        proto::thm_stat(),           // heaters and IR LEDs
        proto::exp_csp_snapshot(3),  // four heater profiles from FSW housekeeping cache
        proto::fld_stat(),           // pump U11 / U15
        proto::rba_stat(),           // DRV8910 and motors
        proto::pc_stat(),            // PC experiment
    ]
}

impl Sim {
    /// Called when leaving the simulator: values the real system does not report become n/a.
    pub fn enter_live(&mut self) {
        let o = &mut self.obc;
        o.cpu_pct = f32::NAN;
        o.mem_pct = f32::NAN;
        o.disk_pct = f32::NAN;
        o.rx_frames = NA_U32;
        o.tx_frames = NA_U32;
        o.fsw_up = false;
        let e = &mut self.exp;
        e.link = false;
        e.board_t = f32::NAN;
        e.bme_t = f32::NAN;
        e.ntc = [f32::NAN; 4];
        e.flow = [f32::NAN; 2];
        e.ir = [false; 2];
        e.ir_actual_duty = [0; 2];
        e.ir_status_seen = [false; 2];
        e.ir_applied = [None; 2];
        for c in &mut e.camera {
            c.confirmed = false;
            c.buffer_enabled = None;
            c.initialized = false;
            c.data_exist = false;
            c.request_id = 0;
            c.object_id = 0;
            c.saved_file = None;
            c.result = "not read".into();
            c.temp_centi = None;
            c.mean_centi = None;
            c.peak = None;
        }
        e.ht_status_seen = [false; 4];
        e.ht_status_at = None;
        e.ht_main_temp = [f32::NAN; 4];
        e.ht_sec_temp = [f32::NAN; 4];
        e.motor_mode_verified = [false; 2];
        e.motor_limits_verified = [false; 2];
        e.burn_state = "UNKNOWN".into();
        e.burn_active_wire = None;
        e.burn_status_seen = [false; 2];
        e.faults.clear();
        let p = &mut self.pdu;
        p.online = false;
        p.temp = f32::NAN;
        p.boot_cause = "n/a";
        p.event_seq = NA_U32;
        p.last_event = "n/a".into();
        p.boot_count = 0;
        self.inject = [false; 3];
        self.paused = false;
        let _ = self.take_logs();
        self.obc.usb_shot = [Shot::idle(), Shot::idle(), Shot::idle(), Shot::idle()];
    }

    /// Live-mode replacement for the physics step.
    pub fn live_tick(&mut self, dt: f64) {
        self.t += dt;
        if self.exp.ht_status_at.is_some_and(|at| self.t - at > 3.0) {
            self.exp.ht_status_seen = [false; 4];
        }
        let done = match &self.live.link {
            Some(l) => l.drain(),
            None => Vec::new(),
        };
        for d in done {
            self.live.pending = self.live.pending.saturating_sub(1);
            if d.why == Why::Poll {
                let good = match (&d.state, &d.result) {
                    (Some(st), _) => st.is_ok(),
                    (None, Ok(r)) => !r.is_err(),
                    _ => false,
                };
                self.round_progress(good);
            }
            apply(self, d);
        }
        self.obc.uptime += dt;
        self.live.poll_clock += dt;
        self.live.uvc_clock += dt;
        self.live.cam_clock += dt;
        self.live.gui_state_age += dt;
        self.obc.snapshot_age += dt;

        // Read back explicitly requested IR field changes, including control-only mode.
        for i in 0..2 {
            if self.exp.ir_set_pending[i].is_some()
                && self.exp.ir_config_pending[i].is_some_and(|t| self.t - t >= 10.0)
            {
                self.exp.ir_set_pending[i] = None;
                self.exp.ir_config_pending[i] = None;
                self.log(
                    Sub::Exp,
                    Level::Warn,
                    format!("IR {i} setting not confirmed; Read status for actual values"),
                );
            }
        }
        if self.exp.ir_set_pending.iter().any(Option::is_some)
            && self.t >= self.live.ir_verify_at
            && self.live.pending == 0
        {
            self.live.send(
                proto::exp_run("ir_status"),
                Why::Action("exp.ir0.read".into()),
            );
            self.live.ir_verify_at = self.t + 1.0;
        }
        // Complete an explicitly requested camera capture even with background polling off.
        if self.live.cam_clock >= 1.0 && self.live.pending == 0 {
            self.live.cam_clock = 0.0;
            for camera in 0..2 {
                if let Some(since) = self.live.cam_wait[camera] {
                    if self.t - since > 30.0 {
                        self.live.cam_wait[camera] = None;
                        self.exp.camera[camera].result = "timeout waiting for capture".into();
                    } else {
                        self.live.send(
                            proto::exp_run(&format!("camera_status {camera}")),
                            Why::Action("exp.cam_status".into()),
                        );
                    }
                }
            }
            let mut check_download = false;
            for camera in 0..2 {
                if let Some(since) = self.live.cam_download_wait[camera] {
                    if self.t - since > 120.0 {
                        self.live.cam_download_wait[camera] = None;
                    } else {
                        check_download = true;
                    }
                }
            }
            if check_download {
                self.live.send(
                    proto::exp_csp_snapshot(6),
                    Why::Action("exp.cam_status".into()),
                );
            }
        }
        if self.live.control_only {
            return;
        }

        if self.live.link.is_none() {
            return;
        }
        if self.live.thermal_verify_at.is_some_and(|at| self.t >= at) && self.live.pending == 0 {
            self.live.thermal_verify_at = None;
            self.live.send(proto::exp_csp_snapshot(3), Why::Refresh);
        }
        for i in 0..2 {
            if let Some(since) = self.exp.ir_config_pending[i] {
                if self.t - since > 10.0 {
                    self.exp.ir_config_pending[i] = None;
                    self.log(
                        Sub::Exp,
                        Level::Warn,
                        format!("IR {i} limits not confirmed by MCU; check Read status"),
                    );
                }
            }
        }
        if self.exp.ir_config_pending.iter().any(Option::is_some)
            && self.t >= self.live.ir_verify_at
            && self.live.pending == 0
        {
            self.live.send(proto::exp_run("ir_status"), Why::Refresh);
            self.live.ir_verify_at = self.t + 1.0;
        }
        // USB capture: poll the request until it is saved or failed.
        if self.live.uvc_clock >= 1.0 && self.live.pending == 0 {
            self.live.uvc_clock = 0.0;
            for cam in 0..4 {
                if let Some((id, since)) = self.live.uvc[cam] {
                    if self.t - since > 60.0 {
                        self.live.uvc[cam] = None;
                        self.obc.usb_shot[cam].state = ShotState::Failed;
                        self.obc.usb_shot[cam].note = "no result after 60 s".into();
                    } else {
                        self.live
                            .send(proto::uvc_stat(cam as u8 + 1, id), Why::Refresh);
                    }
                }
            }
        }
        if let Some((id, _, since)) = self.live.pump_req {
            if self.t - since > 30.0 {
                self.live.pump_req = None;
            } else if self.live.uvc_clock >= 1.0 && self.live.pending == 0 {
                self.live.uvc_clock = 0.0;
                self.live.send(proto::fld_pump_result(id), Why::Refresh);
            }
        }
        if self.live.valve_verify_at.is_some_and(|at| self.t >= at) && self.live.pending == 0 {
            self.live.valve_verify_at = None;
            self.live.send(proto::fld_stat(), Why::Refresh);
            self.live.send(proto::pc_stat(), Why::Refresh);
        }
        if let Some(until) = self.live.burn_verify_until {
            if self.t >= until {
                self.live.burn_verify_until = None;
                self.log(
                    Sub::Exp,
                    Level::Warn,
                    "burn status has not reached IDLE; inspect EXP and read status again",
                );
            } else if self.t >= self.live.burn_verify_at && self.live.pending == 0 {
                self.live.send(proto::exp_run("burn_status"), Why::Refresh);
                self.live.burn_verify_at = self.t + 0.5;
            }
        }
        let due = if self.live.auto {
            self.live.poll_clock >= self.live.poll_every
        } else {
            self.live.update_req
        };
        if due && self.live.pending == 0 && self.live.burn_verify_until.is_none() {
            self.live.poll_clock = 0.0;
            let manual = self.live.update_req;
            self.live.update_req = false;
            let cmds = poll_set();
            let mut sent = cmds.len();
            for c in cmds {
                self.live.send(c, Why::Poll);
            }
            if self.live.mode == Mode::Console {
                if let Some(l) = &self.live.link {
                    l.request_state();
                    self.live.pending += 1;
                    sent += 1;
                }
            }
            self.live.round = Some(Round {
                sent,
                done: 0,
                ok: 0,
                manual,
                t0: Instant::now(),
            });
        }
    }

    /// Count one finished poll command; when the round is complete keep its summary.
    fn round_progress(&mut self, good: bool) {
        let Some(r) = self.live.round.as_mut() else {
            return;
        };
        r.done += 1;
        r.ok += good as usize;
        if r.done < r.sent {
            return;
        }
        let (sent, ok, manual, ms) = (r.sent, r.ok, r.manual, r.t0.elapsed().as_millis() as u64);
        self.live.round = None;
        self.live.last_update = Some(Updated {
            clock: self.rtc_now(),
            sent,
            ok,
            ms,
        });
        if manual {
            let lvl = if ok == sent { Level::Info } else { Level::Warn };
            self.log(
                Sub::Obc,
                lvl,
                format!("housekeeping update: {ok}/{sent} replies OK in {ms} ms (details on CFG)"),
            );
        }
    }

    /// Operator action in live mode. Returns false if the id is local-only
    /// (staged value or selector), so the caller applies it to the model.
    pub fn live_action(&mut self, id: &str, act: Act) -> bool {
        if is_local(id) {
            return false;
        }
        if id.starts_with("exp.ht_toggle")
            || matches!(id, "exp.ht_manual_set" | "exp.ht_manual_off")
        {
            let i = if id.starts_with("exp.ht_toggle") {
                (id.as_bytes()[id.len() - 1] - b'0') as usize
            } else {
                self.exp.ht_sel
            };
            if !self.exp.ht_status_seen[i] {
                if self.live.control_only {
                    self.log(
                        Sub::Exp,
                        Level::Warn,
                        "heater status unavailable; use Read status before control",
                    );
                    return true;
                }
                if self.live.heater_preflight.is_some() {
                    self.log(
                        Sub::Exp,
                        Level::Warn,
                        "heater status refresh already pending",
                    );
                } else {
                    self.live.heater_preflight = Some(id.to_string());
                    self.live.send(proto::exp_csp_snapshot(3), Why::Refresh);
                }
                return true;
            }
        }
        match plan(self, id, &act) {
            Ok(cmds) => {
                let sub = sub_of(id);
                if matches!(
                    id,
                    "exp.cam_capture" | "exp.cam_init" | "exp.cam_detect" | "exp.cam_address_set"
                ) {
                    let camera = self.exp.cam_sel;
                    self.live.cam_wait[camera] = Some(self.t);
                    self.exp.camera[camera].result = "request queued".into();
                    if id == "exp.cam_capture" {
                        self.exp.camera[camera].temp_centi = None;
                        self.exp.camera[camera].mean_centi = None;
                        self.exp.camera[camera].peak = None;
                        self.exp.camera[camera].object_id = 0;
                        self.exp.camera[camera].saved_file = None;
                        self.live.cam_download_wait[camera] = Some(self.t);
                    }
                }
                if id == "exp.pc_profile_apply" {
                    self.exp.pc_apply_pending = cmds.len();
                }
                if let Some((i, field)) = crate::controls::ir_field(id) {
                    let c = self.exp.ir_setup[i];
                    self.exp.ir_set_pending[i] = match field {
                        "set_max" => Some(("max_duty_pct", c.max_duty)),
                        "set_duty" => Some(("set_duty_pct", c.duty)),
                        "set_pwm" => Some(("pwm_hz", c.pwm_hz)),
                        "set_time" => Some(("on_ms", c.on_ms)),
                        _ => self.exp.ir_set_pending[i],
                    };
                    if field.starts_with("set_") {
                        self.exp.ir_config_pending[i] = Some(self.t);
                        self.live.ir_verify_at = self.t + 1.0;
                    }
                }
                if id == "exp.ir_apply" {
                    let i = self.exp.ir_sel;
                    self.exp.ir_config_pending[i] = Some(self.t);
                    self.exp.ir_configured[i] = false;
                    self.live.ir_verify_at = self.t + 1.0;
                }
                for c in cmds {
                    self.log(sub, Level::Info, format!("send: {}", c.text));
                    self.live.send(c, Why::Action(id.to_string()));
                }
                // Re-read what the action may have changed.
                if id == "exp.pc_flow_policy_apply" {
                    self.live
                        .send(proto::exp_run("pc_profile_show"), Why::Refresh);
                }
                if id == "exp.pc_manual_valves_apply" {
                    self.live.valve_verify_at = Some(self.t + 1.2);
                }
                if !matches!(
                    id,
                    "obc.led_pwm"
                        | "obc.mipi_route"
                        | "obc.mipi_capture"
                        | "exp.burn_arm"
                        | "exp.burn"
                        | "exp.burn_abort"
                        | "exp.burn_apply"
                        | "exp.mmax_apply"
                        | "exp.mdrive_apply"
                        | "exp.mpwm_apply"
                        | "exp.mlimits_apply"
                        | "exp.mlevels_apply"
                        | "exp.msettings_read"
                        | "exp.mmode_apply"
                        | "exp.mmode_read"
                        | "exp.mmap_read"
                ) && !id.starts_with("exp.pc_")
                    && !id.starts_with("exp.ht_")
                {
                    for c in refresh_after(sub) {
                        self.live.send(c, Why::Refresh);
                    }
                }
                if matches!(id, "exp.ht_manual_set" | "exp.ht_manual_off") {
                    self.live.send(proto::thm_stat(), Why::Refresh);
                }
                if id == "exp.cam_apply" {
                    self.live.send(
                        proto::exp_run(&format!("camera_status {}", self.exp.cam_sel)),
                        Why::Refresh,
                    );
                }
                if id == "exp.cam_status" {
                    self.live
                        .send(proto::exp_csp_snapshot(6), Why::Action(id.to_string()));
                }
                if matches!(
                    id,
                    "exp.ht_start" | "exp.ht_stop" | "exp.ht_resume" | "exp.ht_profile_apply"
                ) || id.starts_with("exp.ht_toggle")
                {
                    self.live.thermal_verify_at = Some(self.t + 1.2);
                }
                if matches!(id, "exp.burn" | "exp.burn_abort" | "exp.burn_apply") {
                    self.live.send(proto::exp_run("burn_status"), Why::Refresh);
                }
                let motor = if matches!(id, "exp.mlimits_apply" | "exp.mlevels_apply") {
                    self.exp.map_motor
                } else {
                    self.exp.mc
                };
                let verify = match id {
                    "exp.mmax_apply" => Some(format!("motor5_cfg_get {motor}")),
                    "exp.mdrive_apply" => Some(format!("motor5_cfg_get {motor}")),
                    "exp.mmode_apply" => Some(format!("motor5_cfg_get {motor}")),
                    "exp.mpwm_apply" => Some(format!("motor5_pwm_freq_get {motor}")),
                    "exp.mlimits_apply" | "exp.mlevels_apply" => {
                        Some(format!("motor5_limit_get {motor}"))
                    }
                    _ => None,
                };
                if let Some(command) = verify {
                    self.live.send(proto::exp_run(&command), Why::Refresh);
                }
            }
            Err(why) => self.log(sub_of(id), Level::Warn, format!("{id}: {why}")),
        }
        true
    }
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

/// Controls with no flight command behind them. `None` = wired.
pub fn unwired(id: &str) -> Option<&'static str> {
    match id {
        "obc.reboot" => Some("The flight software has no reboot command."),
        "exp.reset" => Some("No wired flight command yet for EXP reset."),
        "pdu.vout_en" | "pdu.trans_en" | "pdu.vout_set" | "pdu.fault_clear" | "pdu.heartbeat"
        | "pdu.led_on" | "pdu.led_off" | "pdu.bootloader" => None,
        _ if id.starts_with("pdu.") => Some("PDU register not exposed by the flight software."),
        _ => None,
    }
}

pub fn unwired_in(mode: Mode, id: &str) -> Option<&'static str> {
    if mode == Mode::Rs422 && matches!(id, "obc.mipi_route" | "obc.mipi_capture") {
        Some("This hardware action requires the local console socket.")
    } else {
        unwired(id)
    }
}

/// Selectors and staged values only edit the draft; the matching Set action sends them.
fn is_local(id: &str) -> bool {
    if let Some((_, field)) = crate::controls::ir_field(id) {
        return matches!(field, "max" | "duty" | "pwm" | "time");
    }
    if crate::sim::pc_field_index(id).is_some() {
        return true;
    }
    matches!(
        id,
        "obc.usb_sel"
            | "obc.mipi_sel"
            | "obc.mipi_sensor"
            | "exp.u11_type"
            | "exp.u11_freq"
            | "exp.u11_volt"
            | "exp.u15_ch"
            | "exp.u15_freq"
            | "exp.u15_volt"
            | "exp.mc"
            | "exp.mdir"
            | "exp.msps"
            | "exp.msteps"
            | "exp.mmode"
            | "exp.map_motor"
            | "exp.mmax_sps"
            | "exp.mdrive_pct"
            | "exp.mpwm"
            | "exp.mbank"
            | "exp.mmin"
            | "exp.mmax"
            | "exp.mhome_end"
            | "exp.mmin_level"
            | "exp.mmax_level"
            | "exp.ht_sel"
            | "exp.ht_profile_sel"
            | "exp.ht_duty"
            | "exp.ht_duration"
            | "exp.ht_max_duty"
            | "exp.ht_manual_slew"
            | "exp.ht_max_on"
            | "exp.ht_main_ntc"
            | "exp.ht_sec_ntc"
            | "exp.ht_target"
            | "exp.ht_delta"
            | "exp.ht_cutoff"
            | "exp.ht_timeout"
            | "exp.ht_resume_boot"
            | "exp.ht_slew_up"
            | "exp.ht_slew_down"
            | "exp.ht_kp"
            | "exp.ht_ki"
            | "exp.ht_kd"
            | "exp.ir_sel"
            | "exp.ir_duty"
            | "exp.ir_max_duty"
            | "exp.ir_max_on"
            | "exp.ir_pwm"
            | "exp.cam_sel"
            | "exp.cam_model"
            | "exp.cam_addr"
            | "exp.cam_format"
            | "exp.cam_hz"
            | "exp.cam_emiss"
            | "exp.cam_x"
            | "exp.cam_y"
            | "exp.cam_delta"
            | "exp.burn_sel"
            | "exp.burn_duty"
            | "exp.burn_time"
            | "exp.burn_max_total_current"
            | "exp.pc_v1"
            | "exp.pc_v2"
            | "exp.pc_v3"
            | "exp.pc_v4"
            | "exp.pc_pulse"
            | "exp.pc_flow_ch"
            | "exp.pc_log_ms"
            | "exp.pc_route"
            | "exp.pc_route_case"
            | "exp.pc_rv1"
            | "exp.pc_rv2"
            | "exp.pc_rv3"
            | "exp.pc_rv4"
            | "exp.pc_manual_v1"
            | "exp.pc_manual_v2"
            | "exp.pc_manual_v3"
            | "exp.pc_manual_v4"
            | "exp.pc_target"
            | "exp.pc_field"
            | "exp.pc_value"
            | "exp.pc_minflow"
            | "exp.pc_flow_action"
            | "exp.pc_flow_check"
            | "exp.pc_flow_delay"
            | "exp.pc_fail_samples"
            | "exp.pc_case"
            | "exp.pc_ground"
    )
}

fn refresh_after(sub: Sub) -> Vec<Cmd> {
    match sub {
        Sub::Obc => vec![proto::pwr_stat(Board::Obc)],
        Sub::Exp => vec![
            proto::pwr_stat(Board::Exp),
            proto::thm_stat(),
            proto::fld_stat(),
            proto::rba_stat(),
            proto::pc_stat(),
        ],
        Sub::Pdu => vec![
            proto::pwr_stat(Board::Pdu),
            proto::pdu_vout_get(),
            proto::pdu_heartbeat_get(),
        ],
    }
}

pub fn motor_group_reason(s: &Sim, group: &str) -> Option<&'static str> {
    let required = match group {
        "MANUAL commands" => 0,
        "OPERATION commands" => 1,
        _ => return None,
    };
    let i = s.exp.mc;
    if !s.exp.motor_mode_verified[i] {
        Some("Read mode from MCU before using this command panel")
    } else if s.exp.motor_mode[i] != required {
        Some(if required == 0 {
            "Set motor to MANUAL first"
        } else {
            "Set motor to OPERATION first"
        })
    } else {
        None
    }
}

fn motor_command_group(id: &str) -> Option<&'static str> {
    match id {
        "exp.mmove" | "exp.mjog" | "exp.mhome" => Some("MANUAL commands"),
        "exp.mmove_op" | "exp.mhome_op" => Some("OPERATION commands"),
        _ => None,
    }
}

fn idx(id: &str) -> usize {
    id.chars().last().and_then(|c| c.to_digit(10)).unwrap_or(0) as usize
}

fn pc_profile_setting(target: usize, field: u8, value: u32, name: &str, display: String) -> Cmd {
    let mut p = [0u8; 32];
    p[1] = 6;
    p[2] = target as u8;
    p[3] = field;
    p[4..8].copy_from_slice(&value.to_le_bytes());
    let profile = ["global", "test", "replace", "circulation"][target];
    proto::pc_cfg(format!("pc_cfg profile {profile} {name} {display}"), p)
}

fn pc_profile_name_setting(name: &str) -> Cmd {
    let mut p = [0u8; 32];
    p[1] = 6;
    p[3] = 17;
    p[4..4 + name.len()].copy_from_slice(name.as_bytes());
    proto::pc_cfg(format!("pc_cfg profile global name {name}"), p)
}

fn sync_pc_field(exp: &mut crate::sim::Exp, target: usize, index: usize, value: i32) {
    if !crate::sim::pc_field_allowed(target, index) {
        return;
    }
    // pc_profile_show prints minflow/volume to two decimal places, while the
    // command carries three. Keep the exact value confirmed by the set ACK.
    let value = if matches!(index, 1 | 5) {
        exp.pc_applied[target][index]
            .filter(|old| (old - value).abs() <= 5)
            .unwrap_or(value)
    } else {
        value
    };
    exp.pc_applied[target][index] = Some(value);
    if exp.pc_dirty[target][index] {
        exp.pc_dirty[target][index] = exp.pc_fields[target][index] != value;
    } else {
        exp.pc_fields[target][index] = value;
    }
}

/// Control id + action -> flight commands. `Err` explains why nothing is sent.
pub fn plan(s: &mut Sim, id: &str, act: &Act) -> Result<Vec<Cmd>, String> {
    if let Some((i, field)) = crate::controls::ir_field(id) {
        let c = s.exp.ir_setup[i];
        let command = match field {
            "set_max" if (0..=100).contains(&c.max_duty) => {
                format!("ir_cfg_set {i} max_duty_pct {}", c.max_duty)
            }
            "set_duty" if (0..=100).contains(&c.duty) => {
                format!("ir_cfg_set {i} duty_pct {}", c.duty)
            }
            "set_pwm" if (100..=100000).contains(&c.pwm_hz) => {
                format!("ir_cfg_set {i} pwm_hz {}", c.pwm_hz)
            }
            "set_time" if (0..=5000).contains(&c.on_ms) => {
                format!("ir_cfg_set {i} on_ms {}", c.on_ms)
            }
            "on" => {
                let c =
                    s.exp.ir_applied[i].ok_or("Read status first to obtain the applied duty")?;
                format!("ir_set {i} {}", c.duty)
            }
            "off" => format!("ir_off {i}"),
            "read" => return Ok(vec![proto::exp_run("ir_status")]),
            _ => return Err("IR field is local or outside its allowed range".into()),
        };
        return Ok(vec![proto::exp_run(&command), proto::exp_run("ir_status")]);
    }

    if let Some(why) = unwired_in(s.live.mode, id) {
        return Err(why.into());
    }
    if let Some(group) = motor_command_group(id) {
        if let Some(why) = motor_group_reason(s, group) {
            return Err(why.into());
        }
    }
    let on = matches!(act, Act::Bool(true));
    let num = if let Act::Num(v) = act { *v } else { 0 };
    let e = &s.exp;
    let one = |c: Cmd| Ok(vec![c]);
    let exp = |t: String| Ok(vec![proto::exp_run(&t)]);
    match id {
        "exp.cam_apply" => {
            let c = &e.camera[e.cam_sel];
            exp(format!(
                "camera_config {} {} {} {} {} {}",
                e.cam_sel,
                ["mlx90640", "mlx90642"][c.model],
                c.address,
                ["int16", "float32"][c.format],
                [2, 4, 8, 16][c.hz],
                c.emissivity
            ))
        }
        "exp.cam_buffer_on" => exp(format!("camera_buffer_enable {}", e.cam_sel)),
        "exp.cam_buffer_off" => exp(format!("camera_buffer_disable {}", e.cam_sel)),
        "exp.cam_address_set" => exp(format!(
            "camera_address_set {} {}",
            e.cam_sel, e.camera[e.cam_sel].address
        )),
        "exp.cam_detect" => exp(format!("camera_detect {}", e.cam_sel)),
        "exp.cam_init" => exp(format!("camera_init {}", e.cam_sel)),
        "exp.cam_capture" => exp(format!("camera_capture {}", e.cam_sel)),
        "exp.cam_status" => exp(format!("camera_status {}", e.cam_sel)),
        "exp.cam_temp" | "exp.cam_mean" => {
            let c = &e.camera[e.cam_sel];
            let (x, y, w, h) = c.point_region().ok_or("invalid point measurement region")?;
            exp(format!(
                "camera_temp {} {} {} {} {}{}",
                e.cam_sel,
                x,
                y,
                w,
                h,
                if id == "exp.cam_mean" { " mean" } else { "" }
            ))
        }
        // OBC power switches: pwr_en OBC <output> on|off (TYPE 01 / ADDR 03)
        "obc.ai_efuse" => one(proto::pwr_en(Board::Obc, 0, on, false)),
        "obc.fpga_efuse" => one(proto::pwr_en(Board::Obc, 1, on, false)),
        "obc.usb0" | "obc.usb1" | "obc.usb2" | "obc.usb3" => {
            one(proto::pwr_en(Board::Obc, 2 + idx(id), on, false))
        }
        "obc.mipi_power" if s.live.mode == Mode::Console => one(proto::gui_hw(
            &format!(
                "mipi-power {} {}",
                s.obc.mipi_sel + 1,
                if on { "on" } else { "off" }
            ),
            15,
        )),
        "obc.mipi_power" => one(proto::pwr_en(Board::Obc, 6 + s.obc.mipi_sel, on, false)),
        "obc.led_pwm" => one(proto::uvc_led_pwm(
            s.obc.usb_sel as u8 + 1,
            num.clamp(0, 100) as u8,
        )),
        "obc.mipi_route" => one(proto::gui_hw(
            &format!(
                "mipi-route {} {}",
                s.obc.mipi_sel + 1,
                s.obc.mipi_sensor[s.obc.mipi_sel]
            ),
            15,
        )),
        "obc.mipi_capture" => {
            if s.obc.mipi_shot.state == ShotState::Capturing {
                return Err("MIPI capture already in progress".into());
            }
            s.obc.mipi_shot = Shot {
                state: ShotState::Capturing,
                path: String::new(),
                bytes: 0,
                left: 0.0,
                note: format!(
                    "camera {} sensor {}",
                    s.obc.mipi_sel + 1,
                    s.obc.mipi_sensor[s.obc.mipi_sel]
                ),
            };
            one(proto::gui_hw(
                &format!(
                    "mipi-capture {} {}",
                    s.obc.mipi_sel + 1,
                    s.obc.mipi_sensor[s.obc.mipi_sel]
                ),
                120,
            ))
        }
        "obc.rtc_set" => match act {
            Act::Epoch(epoch) => one(proto::sys_rtc_set(*epoch)),
            _ => Err("UTC time is required".into()),
        },
        "obc.hk_refresh" => one(proto::tm_obc_get()),
        "obc.capture" => {
            let cam = s.obc.usb_sel;
            if s.live.uvc[cam].is_some() {
                return Err(format!("camera {} is already capturing", cam + 1));
            }
            s.live.next_req = s.live.next_req.wrapping_add(1).max(1);
            let rid = s.live.next_req;
            s.live.uvc[cam] = Some((rid, s.t));
            s.obc.usb_shot[cam] = Shot {
                state: ShotState::Capturing,
                path: String::new(),
                bytes: 0,
                left: 0.0,
                note: format!("request {rid}"),
            };
            one(proto::uvc_capture(cam as u8 + 1, rid))
        }

        // PC experiment: native ICD 02/01 configuration and 02/02 operations.
        "exp.pc_map_apply" => {
            let v = e.pc_valves;
            if v.iter().any(|x| *x > 3) || (0..4).any(|i| (i + 1..4).any(|j| v[i] == v[j])) {
                return Err("V1–V4 must map to four unique channels 0–3".into());
            }
            let mut p = [0u8; 32];
            p[1] = 1;
            for i in 0..4 {
                p[2 + i] = v[i] as u8;
            }
            one(proto::pc_cfg(
                format!("pc_cfg valve map {} {} {} {}", v[0], v[1], v[2], v[3]),
                p,
            ))
        }
        "exp.pc_pulse_apply" => {
            let mut p = [0u8; 32];
            p[1] = 2;
            p[4..8].copy_from_slice(&(e.pc_pulse as u32).to_le_bytes());
            one(proto::pc_cfg(
                format!("pc_cfg valve pulse {}", e.pc_pulse),
                p,
            ))
        }
        "exp.pc_route_apply" => {
            let route = e.pc_route;
            let selection = e.pc_route_case;
            if (1..=3).contains(&route) && selection > 1 {
                return Err("Test, Replace and Circulation routes only have cases A/B".into());
            }
            let mut p = [0u8; 32];
            p[1] = 5;
            p[2] = route as u8;
            p[3] = selection as u8;
            for i in 0..4 {
                if e.pc_route_valves[i] {
                    p[4] |= 1 << i;
                }
            }
            let kind = ["standby", "test", "replace", "circulation", "ground"][route];
            let selection_text = if route == 0 {
                "".to_string()
            } else if route == 4 {
                format!(
                    " {}",
                    ["protein_a", "protein_b", "precip_respol", "precip_rsvb"][selection]
                )
            } else {
                format!(" {}", ["a", "b"][selection])
            };
            let bits = e.pc_route_valves.map(|v| if v { 1 } else { 0 });
            one(proto::pc_cfg(
                format!(
                    "pc_cfg route {kind}{selection_text} {} {} {} {}",
                    bits[0], bits[1], bits[2], bits[3]
                ),
                p,
            ))
        }
        "exp.pc_flow_apply" => {
            let mut p = [0u8; 32];
            p[1] = 3;
            p[4] = e.pc_flow_ch as u8;
            one(proto::pc_cfg(
                format!("pc_cfg flow channel {}", e.pc_flow_ch),
                p,
            ))
        }
        "exp.pc_log_ms_apply" => {
            let mut p = [0u8; 32];
            p[1] = 4;
            p[4..8].copy_from_slice(&(e.pc_log_ms as u32).to_le_bytes());
            one(proto::pc_cfg(
                format!("pc_cfg flow interval {}", e.pc_log_ms),
                p,
            ))
        }
        "exp.pc_flow_policy_apply" => {
            let target = e.pc_target;
            if target == 0 {
                return Err("select Test, Replace or Circulation profile".into());
            }
            if e.pc_state != "STANDBY" {
                return Err("stop PC and confirm STANDBY before changing flow policy".into());
            }
            let i = target - 1;
            let minflow = e.pc_minflow_milli[i];
            let delay = e.pc_flow_delay_ms[i];
            if !(0..=1_000_000).contains(&minflow) || !(0..=1_000_000).contains(&delay) {
                return Err("min flow or grace period is outside the configured range".into());
            }
            let action = e.pc_flow_action[i];
            if action > 2 {
                return Err("invalid low-flow action".into());
            }
            let mut commands = vec![
                pc_profile_setting(
                    target,
                    2,
                    minflow as u32,
                    "minflow",
                    format!("{}.{:03}", minflow / 1000, minflow % 1000),
                ),
                pc_profile_setting(target, 14, delay as u32, "flow_delay", delay.to_string()),
            ];
            if target < 3 {
                let count = e.pc_fail_samples[i];
                if !(1..=100).contains(&count) {
                    return Err("fail samples must be 1–100".into());
                }
                commands.push(pc_profile_setting(
                    target,
                    15,
                    count as u32,
                    "fail_samples",
                    count.to_string(),
                ));
            }
            commands.push(pc_profile_setting(
                target,
                3,
                action as u32,
                "flow_action",
                ["none", "warn", "stop"][action].into(),
            ));
            commands.push(pc_profile_setting(
                target,
                13,
                e.pc_flow_check[i] as u32,
                "flow_check",
                (e.pc_flow_check[i] as u8).to_string(),
            ));
            Ok(commands)
        }
        "exp.pc_profile_apply" => {
            if e.pc_state != "STANDBY" {
                return Err("stop PC and confirm STANDBY before changing profile".into());
            }
            if e.pc_apply_pending != 0 {
                return Err("profile Apply is still in progress".into());
            }
            let target = e.pc_target;
            let mut commands = Vec::new();
            for (i, name) in crate::sim::PC_FIELDS.iter().enumerate() {
                if !crate::sim::pc_field_allowed(target, i) || !e.pc_dirty[target][i] {
                    continue;
                }
                let value = e.pc_fields[target][i];
                let max = if i == 5 { i32::MAX } else { 1_000_000 };
                if !(0..=max).contains(&value)
                    || (i == 14 && !(1..=100).contains(&value))
                    || (i == 3 && !(8..=1_992).contains(&value))
                    || (i == 4 && value > 250)
                    || (i == 5 && value == 0)
                    || (matches!(i, 12 | 15 | 10) && value > 1)
                    || (i == 2 && value > 2)
                {
                    return Err(format!("{name} is outside its allowed range"));
                }
                let display = match i {
                    1 | 5 => format!("{}.{:03}", value / 1000, value % 1000),
                    2 => ["none", "warn", "stop"][value as usize].to_string(),
                    10 => ["time", "volume"][value as usize].to_string(),
                    _ => value.to_string(),
                };
                commands.push(pc_profile_setting(
                    target,
                    (i + 1) as u8,
                    value as u32,
                    name,
                    display,
                ));
            }
            if target == 0 && e.pc_name_dirty {
                let name = &e.pc_name_draft;
                if name.is_empty()
                    || name.len() > 23
                    || !name.bytes().all(|b| (0x21..=0x7e).contains(&b))
                {
                    return Err(
                        "profile name must be 1–23 printable ASCII characters without spaces"
                            .into(),
                    );
                }
                commands.push(pc_profile_name_setting(name));
            }
            if commands.is_empty() {
                return Err("no changed profile fields to apply".into());
            }
            Ok(commands)
        }
        "exp.pc_datalog" | "exp.pc_resume" => {
            let mut p = [0u8; 32];
            p[1] = if id == "exp.pc_datalog" { 10 } else { 7 };
            p[4] = on as u8;
            let text = if id == "exp.pc_datalog" {
                format!("pc_cfg datalog {}", if on { "on" } else { "off" })
            } else {
                format!("pc_cfg resume {}", on as u8)
            };
            one(proto::pc_cfg(text, p))
        }
        "exp.pc_status" => exp("pc_status".into()),
        "exp.pc_profile_read" => exp("pc_profile_show".into()),
        "exp.pc_activate" => one(proto::pc_ctl(1, 0, "pc_ctl activate".into())),
        "exp.pc_deactivate" => one(proto::pc_ctl(2, 0, "pc_ctl deactivate".into())),
        "exp.pc_test" | "exp.pc_replace" | "exp.pc_circulate" => {
            let (op, verb) = match id {
                "exp.pc_test" => (3, "test"),
                "exp.pc_replace" => (4, "replace"),
                _ => (5, "circulation"),
            };
            let choice = e.pc_case as u8;
            one(proto::pc_ctl(
                op,
                choice,
                format!("pc_ctl {verb} {}", ["both", "a", "b"][e.pc_case]),
            ))
        }
        "exp.pc_ground_apply" => {
            let route = ["protein_a", "protein_b", "precip_respol", "precip_rsvb"][e.pc_ground];
            one(proto::pc_ctl(
                6,
                e.pc_ground as u8,
                format!("pc_ctl ground {route}"),
            ))
        }
        "exp.pc_standby" => one(proto::pc_ctl(7, 0, "pc_ctl standby".into())),
        "exp.pc_stop" => one(proto::pc_ctl(8, 0, "pc_ctl stop".into())),
        "exp.pc_reset_error" => one(proto::pc_ctl(9, 0, "pc_ctl reset_error".into())),
        "exp.pc_manual_valves_apply" => {
            if !e.pc_activated || !matches!(e.pc_state.as_str(), "STANDBY" | "ONGROUND") {
                return Err(
                    "activate PC and enter STANDBY/ONGROUND before manual valve control".into(),
                );
            }
            if e.u11.en {
                return Err("stop the pump before changing manual valve directions".into());
            }
            let mask = e
                .pc_manual_valves
                .iter()
                .enumerate()
                .fold(0u8, |bits, (i, &v)| bits | ((v as u8) << i));
            one(proto::fld_valve_mask(mask))
        }
        "exp.pc_manual_pump_init" => {
            if e.pc_activated {
                return Err("deactivate PC before manual U11 control".into());
            }
            if !e.sln {
                return Err("turn on the SLN/HD rail first".into());
            }
            if !e.u11.ptype_configured {
                return Err("select the fitted U11 driver type on Pump U11 page first".into());
            }
            exp("pump_init".into())
        }
        "exp.pc_manual_pump_freq_set"
        | "exp.pc_manual_pump_volt_set"
        | "exp.pc_manual_pump_on"
        | "exp.pc_manual_pump_off" => {
            if e.pc_activated {
                return Err("deactivate PC before manual U11 control".into());
            }
            if !e.sln {
                return Err("turn on the SLN/HD rail first".into());
            }
            if !e.u11.inited && id != "exp.pc_manual_pump_off" {
                return Err("initialize U11 before manual pump control".into());
            }
            match id {
                "exp.pc_manual_pump_freq_set" => {
                    if !(8..=1992).contains(&e.u11.freq_staged) {
                        return Err("U11 frequency must be 8–1992 Hz".into());
                    }
                    one(proto::fld_pump(1, 2, 0, e.u11.freq_staged as u32))
                }
                "exp.pc_manual_pump_volt_set" => {
                    if !(0..=150).contains(&e.u11.volt_staged) {
                        return Err("U11 voltage must be 0–150 Vpp".into());
                    }
                    one(proto::fld_pump(1, 3, 0, e.u11.volt_staged as u32))
                }
                "exp.pc_manual_pump_on" => one(proto::fld_pump(1, 1, 0, 0)),
                _ => one(proto::fld_pump(1, 0, 0, 0)),
            }
        }
        "exp.pc_manual_pump_status" => exp("pump_state".into()),
        // EXP rails: pwr_en EXP <output> on|off
        "exp.exp12v" => one(proto::pwr_en(Board::Exp, 0, on, false)),
        "exp.sln" => one(proto::pwr_en(Board::Exp, 1, on, false)),
        "exp.motor" => one(proto::pwr_en(Board::Exp, 2, on, false)),
        "exp.power_all_on" => one(proto::pwr_en_mask(Board::Exp, 7, 7, false, None)),
        "exp.power_all_off" => one(proto::pwr_en_mask(Board::Exp, 7, 0, false, None)),
        "exp.ht_limits_read" => one(proto::thm_cfg_get()),
        "exp.ht_limits_apply" => {
            let p = e.ht_profile[e.ht_sel];
            if p.max_on % 10 != 0 {
                return Err("max ON must be divisible by 10 ms".into());
            }
            one(proto::thm_cfg_set(e.ht_sel as u8, &p))
        }
        "exp.ht_manual_set" => {
            if !e.ht_status_seen[e.ht_sel] {
                return Err("heater profile status unavailable; refresh table 3 first".into());
            }
            if e.ht_phase[e.ht_sel] != 0 {
                return Err(format!(
                    "profile {} owns heater {}; stop the profile before manual control",
                    e.ht_sel, e.ht_sel
                ));
            }
            let p = e.ht_profile[e.ht_sel];
            if p.duty > p.max_duty {
                return Err("manual duty exceeds staged duty limit".into());
            }
            one(proto::thm_ctl(
                e.ht_sel as u8,
                p.duty as u8,
                p.duration as u32,
            ))
        }
        "exp.ht_manual_off" => {
            if !e.ht_status_seen[e.ht_sel] {
                return Err("heater profile status unavailable; refresh table 3 first".into());
            }
            if e.ht_phase[e.ht_sel] != 0 {
                return Err(format!(
                    "profile {} owns heater {}; stop the profile before manual control",
                    e.ht_sel, e.ht_sel
                ));
            }
            one(proto::thm_ctl(e.ht_sel as u8, 0, 0))
        }
        "exp.ht_profile_read" => one(proto::thp_cfg_get(e.ht_sel as u8)),
        "exp.ht_profile_apply" => {
            let p = e.ht_profile[e.ht_sel];
            if p.cutoff <= p.target {
                return Err("cutoff must exceed target".into());
            }
            if p.slew_up < 1 || p.slew_down < 1 {
                return Err("profile slew must be 1..100".into());
            }
            one(proto::thp_cfg_set(e.ht_sel as u8, &p))
        }
        "exp.ht_status" => one(proto::exp_csp_snapshot(3)),
        "exp.ht_toggle0" | "exp.ht_toggle1" | "exp.ht_toggle2" | "exp.ht_toggle3" => {
            let i = id.as_bytes()[id.len() - 1] - b'0';
            if !e.ht_status_seen[i as usize] {
                return Err(format!(
                    "profile {i} status unavailable; refresh table 3 first"
                ));
            }
            if !e.ht_running[i as usize] && !e.exp12v {
                return Err("EXP 12 V is OFF; enable it before starting a heater profile".into());
            }
            if !e.ht_running[i as usize] && !e.ht_configured[i as usize] {
                return Err(format!("profile {i} is not configured on EXP"));
            }
            if !e.ht_running[i as usize]
                && (!e.ht_main_temp[i as usize].is_finite()
                    || !e.ht_sec_temp[i as usize].is_finite())
            {
                return Err(format!("profile {i} NTC readings are unavailable"));
            }
            one(proto::thp_ctl(
                i,
                if e.ht_running[i as usize] { 2 } else { 1 },
            ))
        }
        "exp.ht_start" => one(proto::thp_ctl(e.ht_sel as u8, 1)),
        "exp.ht_stop" => one(proto::thp_ctl(e.ht_sel as u8, 2)),
        "exp.ht_resume" => one(proto::thp_ctl(e.ht_sel as u8, 3)),
        // Heaters and IR: EXP CLI through exp_run (TYPE 02 / ADDR 0C)
        "exp.heater0" | "exp.heater1" | "exp.heater2" | "exp.heater3" => {
            if num > 0 {
                exp(format!("heater_set {} {} 60000", idx(id), num))
            } else {
                exp(format!("heater_stop {}", idx(id)))
            }
        }
        "exp.heater_stop" => exp("heater_stop all".into()),
        "exp.ir0" | "exp.ir1" => {
            if on {
                let duty = e.ir_setup[idx(id)].duty;
                if !(0..=100).contains(&duty) {
                    return Err("set IR duty on the IR LEDs page first".into());
                }
                exp(format!("ir_set {} {}", idx(id), duty))
            } else {
                exp(format!("ir_off {}", idx(id)))
            }
        }
        "exp.ir_apply" => {
            let c = e.ir_setup[e.ir_sel];
            if c.max_duty < 0
                || c.max_duty > 100
                || c.on_ms < 0
                || c.on_ms > 5000
                || c.pwm_hz < 100
                || c.pwm_hz > 100000
            {
                return Err("set max duty 0..100%, on time 0..5000 ms (0 = continuous), and PWM 100..100000 Hz before Set config".into());
            }
            if !(0..=c.max_duty).contains(&c.duty) {
                return Err("IR duty must be between 0 and Max duty".into());
            }
            exp(format!(
                "ir_config {} {} {} {} {}",
                e.ir_sel, c.max_duty, c.on_ms, c.pwm_hz, c.duty
            ))
        }
        "exp.ir_start" => exp(format!("ir_set {} {}", e.ir_sel, e.ir_setup[e.ir_sel].duty)),
        "exp.ir_stop" => exp(format!("ir_off {}", e.ir_sel)),
        "exp.ir_status" => exp("ir_status".into()),
        "exp.burn_apply" => {
            let c = e.burn_setup[e.burn_sel];
            if !c.valid() {
                return Err("burn configuration invalid; set duty 1..100%, time 50..1000 ms and max total current 1..3000 mA".into());
            }
            let fields = [
                ("duty_pct", c.duty),
                ("burn_time_ms", c.burn_time_ms),
                ("max_total_current_ma", c.max_total_current_ma),
            ];
            Ok(fields
                .into_iter()
                .map(|(key, value)| proto::burn_cfg_set(e.burn_sel, key, value))
                .collect())
        }
        "exp.burn_status" => exp("burn_status".into()),
        "exp.burn_arm" => exp(format!("burn_arm {}", e.burn_sel)),
        "exp.burn_abort" => exp("burn_abort".into()),
        "exp.burn" => {
            if !e
                .burn_armed_until
                .is_some_and(|(wire, until)| wire == e.burn_sel && s.t < until)
            {
                return Err("arm the selected wire, then fire within 5 seconds".into());
            }
            exp("burn_fire".into())
        }
        // Pump U11
        "exp.u11_type_set" => one(proto::fld_pump_type(e.u11.ptype_staged == 1)),
        "exp.u11_type_get" => exp("pump_type_get".into()),
        "exp.u11_detect" => one(proto::fld_pump_detect(0)),
        "exp.u11_init" => exp("pump_init".into()),
        "exp.u11_freq_set" => one(proto::fld_pump(1, 2, 0, e.u11.freq_staged as u32)),
        "exp.u11_volt_set" => one(proto::fld_pump(1, 3, 0, e.u11.volt_staged as u32)),
        "exp.u11_en" => one(proto::fld_pump(1, on as u8, 0, 0)),
        "exp.u11_state" => exp("pump_state".into()),
        // Pump U15
        "exp.u15_detect" => one(proto::fld_pump_detect(1)),
        "exp.u15_init" => exp("pump4_init".into()),
        "exp.u15_freq_set" => one(proto::fld_pump(2, 2, 0, e.u15.freq_staged as u32)),
        "exp.u15_volt_set" => one(proto::fld_pump(
            2,
            3,
            e.u15.ch as u8,
            e.u15.volt_staged as u32,
        )),
        "exp.u15_en" => one(proto::fld_pump(2, on as u8, 0, 0)),
        "exp.u15_state" => exp("pump4_state".into()),
        // Motors / DRV8910
        "exp.drv_detect" | "exp.drv_status" => exp("drv8910_status".into()),
        "exp.drv_init" => one(proto::rba_init()),
        "exp.drv_fault_clear" => exp("drv8910_fault_clear".into()),
        "exp.motor_stop_all" => one(proto::rba_stop(None)),
        "exp.mjog" => {
            if e.msps > e.motor_max_sps[e.mc] {
                return Err("jog speed exceeds selected motor's max speed".into());
            }
            exp(format!(
                "motor5_run {} {} {}",
                e.mc,
                ["cw", "ccw"][e.mdir],
                e.msps
            ))
        }
        "exp.mmove" | "exp.mmove_op" => {
            if id == "exp.mmove_op" && !e.motor_limits_verified[e.mc] {
                return Err(
                    "Read settings to confirm MIN/MAX mapping before an OPERATION move".into(),
                );
            }
            if e.msps > e.motor_max_sps[e.mc] {
                return Err("move speed exceeds selected motor's max speed".into());
            }
            let steps = if e.mdir == 1 {
                -e.move_steps[e.mc]
            } else {
                e.move_steps[e.mc]
            };
            exp(format!("motor5_move {} {} {}", e.mc, steps, e.msps))
        }
        "exp.mstop" | "exp.mstop_op" => one(if e.motors[e.mc].holding {
            proto::rba_stop(Some(e.mc as u8))
        } else {
            proto::rba_stop_hold(e.mc as u8)
        }),
        "exp.mhome" | "exp.mhome_op" => {
            if !e.motor_limits_verified[e.mc] {
                return Err("Read settings to confirm HOME limit mapping".into());
            }
            one(proto::rba_home(e.mc as u8))
        }
        "exp.mmode_apply" => exp(format!(
            "motor5_mode_set {} {}",
            e.mc,
            ["manual", "operation"][e.motor_mode_draft[e.mc]]
        )),
        "exp.mmode_read" => exp(format!("motor5_cfg_get {}", e.mc)),
        "exp.mmax_apply" => exp(format!(
            "motor5_cfg_set {} max_speed_sps {}",
            e.mc, e.motor_max_sps[e.mc]
        )),
        "exp.mdrive_apply" => exp(format!(
            "motor5_cfg_set {} drive_pct {}",
            e.mc, e.motor_drive_pct[e.mc]
        )),
        "exp.mpwm_apply" => exp(format!(
            "motor5_pwm_freq_set {} {}",
            e.mc,
            [80, 100, 200, 2000][e.motor_pwm[e.mc]]
        )),
        "exp.mlimits_apply" => {
            let i = e.map_motor;
            if e.motor_min[i] == e.motor_max[i] {
                return Err("MIN and MAX must use different inputs".into());
            }
            exp(format!(
                "motor5_limits_set {i} {} {} {} {}",
                ["pmu", "pml"][e.motor_bank[i]],
                e.motor_min[i],
                e.motor_max[i],
                ["min", "max"][e.motor_home_end[i]]
            ))
        }
        "exp.mlevels_apply" => {
            let i = e.map_motor;
            Ok(vec![
                proto::exp_run(&format!(
                    "limit_cfg_set {i} min active_level {}",
                    ["low", "high"][e.motor_min_level[i]]
                )),
                proto::exp_run(&format!(
                    "limit_cfg_set {i} max active_level {}",
                    ["low", "high"][e.motor_max_level[i]]
                )),
            ])
        }
        "exp.msettings_read" => {
            let i = e.mc;
            Ok([
                format!("motor5_cfg_get {i}"),
                format!("motor5_pwm_freq_get {i}"),
                format!("motor5_limit_get {i}"),
            ]
            .iter()
            .map(|line| proto::exp_run(line))
            .collect())
        }
        "exp.mmap_read" => exp(format!("motor5_limit_get {}", e.map_motor)),

        // PDU: pwr_en PDU VOUT|TRANS, pdu_vout set (TYPE 01 / ADDR 02, 03)
        "pdu.vout_en" => one(proto::pwr_en(Board::Pdu, 0, on, false)),
        "pdu.trans_en" => one(proto::pwr_en(Board::Pdu, 1, on, false)),
        "pdu.vout_set" => one(proto::pdu_vout_set(num.clamp(800, 20_000) as u16)),
        "pdu.fault_clear" => one(proto::pdu_fault_clear()),
        "pdu.heartbeat" => one(proto::pdu_heartbeat_set(1, on as u32)),
        "pdu.led_on" => one(proto::pdu_heartbeat_set(2, num.clamp(100, 5_000) as u32)),
        "pdu.led_off" => one(proto::pdu_heartbeat_set(3, num.clamp(100, 5_000) as u32)),
        "pdu.bootloader" => one(proto::pdu_bootloader_enter()),
        _ if id.starts_with("pdu.") => {
            Err("PDU register not exposed by the flight software (only VOUT/TRANS/setpoint)".into())
        }
        _ => Err("not wired to a flight command".into()),
    }
}

#[cfg(test)]
mod pump_type_tests {
    use super::*;

    #[test]
    fn pc_manual_valves_and_pump_use_existing_icd_with_mcu_ownership() {
        let mut sim = Sim::new();
        sim.exp.pc_manual_valves = [1, 0, 1, 0];
        assert!(plan(&mut sim, "exp.pc_manual_valves_apply", &Act::Run).is_err());
        sim.exp.pc_activated = true;
        sim.exp.pc_state = "STANDBY".into();
        let valve = plan(&mut sim, "exp.pc_manual_valves_apply", &Act::Run).unwrap();
        assert_eq!(valve[0].text, "fld_ctl valve mask 5");
        assert_eq!(valve[0].payload, [0, 0, 0, 5, 0, 0, 0, 0]);
        assert!(plan(&mut sim, "exp.pc_manual_pump_on", &Act::Run).is_err());
        sim.exp.pc_activated = false;
        sim.exp.sln = true;
        sim.exp.u11.ptype_configured = true;
        sim.exp.u11.inited = true;
        sim.exp.u11.freq_staged = 240;
        assert_eq!(
            plan(&mut sim, "exp.pc_manual_pump_freq_set", &Act::Run).unwrap()[0].text,
            "fld_ctl pump u11 frequency 240"
        );
        assert_eq!(
            plan(&mut sim, "exp.pc_manual_pump_on", &Act::Run).unwrap()[0].text,
            "fld_ctl pump u11 on"
        );
        let pump = proto::fld_pump(1, 1, 0, 0);
        let reply = Reply {
            ack: 1,
            code: 0x17,
            payload: 42u32.to_le_bytes().to_vec(),
            text: String::new(),
            ok: true,
            tx: String::new(),
            rx: String::new(),
            ms: 0,
        };
        decode(&mut sim, &pump, &reply);
        assert_eq!(sim.live.pump_req.map(|r| r.0), Some(42));
    }

    #[test]
    fn fld_status_updates_valves_without_overwriting_manual_draft() {
        let mut sim = Sim::new();
        let cmd = proto::fld_stat();
        let mut payload = vec![0; 16];
        payload[0] = 0b1010;
        payload[1] = 0x0f;
        let reply = |payload: Vec<u8>| Reply {
            ack: 1,
            code: 0x18,
            payload,
            text: String::new(),
            ok: true,
            tx: String::new(),
            rx: String::new(),
            ms: 0,
        };
        decode(&mut sim, &cmd, &reply(payload.clone()));
        assert_eq!(sim.exp.pc_manual_valves, [0, 1, 0, 1]);
        sim.exp.pc_manual_valves[0] = 1;
        sim.exp.pc_manual_edited = true;
        decode(&mut sim, &cmd, &reply(payload.clone()));
        assert_eq!(sim.exp.pc_manual_valves, [1, 1, 0, 1]);
        payload[0] = 0b1011;
        decode(&mut sim, &cmd, &reply(payload));
        assert!(!sim.exp.pc_manual_edited);
    }

    #[test]
    fn fld_status_distinguishes_unconfigured_low_and_high_driver() {
        let mut sim = Sim::new();
        let cmd = proto::fld_stat();
        for (wire_type, configured, selected) in [(0, false, 0), (1, true, 0), (2, true, 1)] {
            let mut payload = vec![0; 16];
            payload[3] = wire_type;
            let reply = Reply {
                ack: 1,
                code: 0x18,
                payload,
                text: String::new(),
                ok: true,
                tx: String::new(),
                rx: String::new(),
                ms: 0,
            };
            decode(&mut sim, &cmd, &reply);
            assert_eq!(sim.exp.u11.ptype_configured, configured);
            assert_eq!(sim.exp.u11.ptype, selected);
        }
    }

    #[test]
    fn simulated_pc_requires_pump_type_before_activation() {
        let mut sim = Sim::new();
        sim.run("exp.pc_activate");
        assert!(!sim.exp.pc_activated);
        sim.run("exp.u11_type_set");
        sim.run("exp.pc_activate");
        assert!(sim.exp.pc_activated);
    }
}

#[cfg(test)]
mod rtc_tests {
    use super::*;

    #[test]
    fn set_time_sends_entered_utc_epoch() {
        let mut sim = Sim::new();
        let epoch = crate::sim::parse_utc("2024-02-29 12:34:56").unwrap();
        let commands = plan(&mut sim, "obc.rtc_set", &Act::Epoch(epoch)).unwrap();
        assert_eq!(commands.len(), 1);
        assert_eq!(commands[0].text, format!("sys_rtc {epoch}"));
        assert_eq!(commands[0].payload, epoch.to_le_bytes());
    }

    #[test]
    fn camera_helpers_use_console_and_record_saved_png_path() {
        let mut sim = Sim::new();
        sim.live.mode = Mode::Console;
        sim.obc.mipi_sel = 1;
        sim.obc.mipi_sensor[1] = 3;
        let led = plan(&mut sim, "obc.led_pwm", &Act::Num(40)).unwrap();
        assert_eq!(led[0].text, "uvc_cfg 1 set led_pwm 40");
        assert_eq!((led[0].ty, led[0].addr), (1, 0x10));
        assert_eq!(led[0].payload[31], 40);
        sim.live.mode = Mode::Rs422;
        assert!(unwired_in(Mode::Rs422, "obc.led_pwm").is_none());
        assert_eq!(
            plan(&mut sim, "obc.led_pwm", &Act::Num(40)).unwrap()[0].payload,
            led[0].payload
        );
        sim.live.mode = Mode::Console;
        let route = plan(&mut sim, "obc.mipi_route", &Act::Run).unwrap();
        assert!(route[0].text.ends_with("--gui-hw mipi-route 2 3"));
        let capture = plan(&mut sim, "obc.mipi_capture", &Act::Run).unwrap();
        assert!(capture[0].text.ends_with("--gui-hw mipi-capture 2 3"));
        assert_eq!(sim.obc.mipi_shot.state, ShotState::Capturing);
        let reply = Reply { ack: 0xFE, code: 0, payload: vec![], text: "MIPI camera 2 sensor 3 selected\nSaved /home/bee/data/images/mipi/MIPI1.3_H_000001.raw (39321600 bytes)\nSaved /home/bee/data/images/mipi/MIPI1.3_H_000001.png (8000000 bytes)\n".into(), ok: true, tx: String::new(), rx: String::new(), ms: 1 };
        sim.obc.mipi_sel = 0; // Selection changed while capture was running.
        let (level, _) = apply_gui_hw(
            &mut sim,
            &Why::Action("obc.mipi_capture".into()),
            &capture[0],
            &reply,
        );
        assert_eq!(level, Level::Ok);
        assert_eq!(sim.obc.mipi_shot.state, ShotState::Saved);
        assert_eq!(sim.obc.mipi_shot.bytes, 8_000_000);
        assert_eq!(sim.obc.mipi_route, Some((1, 3)));
        assert_eq!(
            sim.obc.mipi_shot.path,
            "/home/bee/data/images/mipi/MIPI1.3_H_000001.png"
        );
        let failed = Reply { ok: false, text: "Saved /home/bee/data/images/mipi/MIPI1.3_H_000002.raw (39321600 bytes)\nMIPI PNG conversion failed\n".into(), ..reply };
        let (level, _) = apply_gui_hw(
            &mut sim,
            &Why::Action("obc.mipi_capture".into()),
            &capture[0],
            &failed,
        );
        assert_eq!(level, Level::Err);
        assert_eq!(sim.obc.mipi_shot.state, ShotState::Failed);
        sim.live.mode = Mode::Rs422;
        assert!(plan(&mut sim, "obc.mipi_capture", &Act::Run).is_err());
    }
}

// ── Incoming ──────────────────────────────────────────────────────────────────

fn first_line(s: &str) -> String {
    s.lines()
        .next()
        .unwrap_or("")
        .trim()
        .chars()
        .take(110)
        .collect()
}

fn heater_applied(text: &str, channel: usize) -> Option<u8> {
    let marker = format!("Heater[{channel}] Duty = ");
    let rest = text.split_once(&marker)?.1;
    let value = rest.split_once('%')?.0.parse::<u8>().ok()?;
    (value <= 100).then_some(value)
}

fn status_value(line: &str, key: &str) -> Option<i32> {
    let prefix = format!("{key}=");
    line.split_whitespace()
        .find_map(|part| part.strip_prefix(&prefix)?.parse().ok())
}

fn status_u32(line: &str, key: &str) -> Option<u32> {
    let prefix = format!("{key}=");
    line.split_whitespace()
        .find_map(|part| part.strip_prefix(&prefix)?.parse().ok())
}

fn status_word<'a>(line: &'a str, key: &str) -> Option<&'a str> {
    let prefix = format!("{key}=");
    line.split_whitespace()
        .find_map(|part| part.strip_prefix(&prefix))
}

fn apply_camera_reply(s: &mut Sim, command: &str, text: &str) {
    for line in text.lines() {
        if let Some(i) = status_u32(line, "buffer").filter(|v| *v < 2) {
            let value = if line.contains(" ENABLED ") {
                Some(true)
            } else if line.contains(" DISABLED ") {
                Some(false)
            } else {
                None
            };
            s.exp.camera[i as usize].buffer_enabled = value;
        }
    }
    let mut words = command.split_whitespace();
    if words.next() != Some("exp_run") {
        return;
    }
    let op = words.next().unwrap_or("");
    let Some(camera) = words
        .next()
        .and_then(|v| v.parse::<usize>().ok())
        .filter(|v| *v < 2)
    else {
        return;
    };
    if matches!(
        op,
        "camera_capture" | "camera_init" | "camera_detect" | "camera_address_set"
    ) {
        if let Some(line) = text.lines().find(|line| line.starts_with("CAMERA camera=")) {
            if let Some(result) = status_word(line, "result") {
                s.exp.camera[camera].result = result.into();
                if result != "QUEUED" {
                    s.live.cam_wait[camera] = None;
                }
            }
        }
    } else if op == "camera_config" {
        if text.lines().any(|line| line.trim() == "OK") {
            s.exp.camera[camera].initialized = false;
            s.exp.camera[camera].result = "configured; initialize".into();
            s.exp.camera[camera].confirmed = false;
        }
    } else if op == "camera_status" {
        if let Some(address_line) = text
            .lines()
            .find(|line| line.starts_with("CAMERA address "))
        {
            s.exp.camera[camera].address_restart_required =
                status_value(address_line, "restart_required") == Some(1);
        }
        let Some(line) = text.lines().find(|line| line.starts_with("CAMERA camera=")) else {
            return;
        };
        let c = &mut s.exp.camera[camera];
        if let (Some(model), Some(address), Some(format), Some(hz)) = (
            status_word(line, "model"),
            status_word(line, "address"),
            status_word(line, "format"),
            status_u32(line, "hz"),
        ) {
            c.actual_model = usize::from(model == "MLX90642");
            c.actual_address = i32::from_str_radix(address.trim_start_matches("0x"), 16)
                .unwrap_or(c.actual_address);
            c.actual_format = usize::from(format == "float32");
            c.actual_hz = hz as i32;
            c.confirmed = true;
        }
        if let Some(v) = status_value(line, "initialized") {
            c.initialized = v != 0;
        }
        if let Some(v) = status_value(line, "data_exist") {
            c.data_exist = v != 0;
        }
        if let Some(v) = status_u32(line, "request") {
            if c.request_id != v {
                c.temp_centi = None;
                c.mean_centi = None;
            }
            c.request_id = v;
        }
        if let Some(v) = status_u32(line, "object") {
            if c.object_id != v {
                c.saved_file = None;
            }
            c.object_id = v;
        }
        c.peak = text
            .lines()
            .find(|l| l.starts_with("CAMERA_PEAK "))
            .and_then(|l| {
                if status_value(l, "valid") != Some(1)
                    || status_u32(l, "request") != Some(c.request_id)
                {
                    return None;
                }
                let x = status_value(l, "x")?;
                let y = status_value(l, "y")?;
                if !(0..32).contains(&x) || !(0..24).contains(&y) {
                    return None;
                }
                Some((status_value(l, "max_cC")?, x as u8, y as u8))
            });
        if let Some(v) = status_word(line, "result") {
            c.result = v.into();
            if !matches!(v, "QUEUED" | "RUNNING") {
                s.live.cam_wait[camera] = None;
                if v != "OK" {
                    s.live.cam_download_wait[camera] = None;
                }
            }
        }
    } else if op == "camera_temp" {
        if let Some(line) = text.lines().find(|line| line.starts_with("CAMERA_TEMP ")) {
            let c = &mut s.exp.camera[camera];
            if status_u32(line, "request") != Some(c.request_id) {
                return;
            }
            let region: Vec<i32> = command
                .split_whitespace()
                .skip(3)
                .take(4)
                .filter_map(|v| v.parse().ok())
                .collect();
            let Some((x, y, w, h)) = c.point_region() else {
                return;
            };
            if region != [x, y, w, h] {
                return;
            }
            if command.ends_with(" mean") {
                c.mean_centi = status_value(line, "mean_cC");
            } else {
                c.temp_centi = status_value(line, "max_cC");
            }
            c.sample_ms = status_u32(line, "sample_ms").unwrap_or(0);
        }
    }
}

#[cfg(test)]
mod ir_camera_tests {
    use super::*;

    #[test]
    fn capture_peak_and_point_average_remain_separate() {
        let mut s = Sim::new();
        apply_camera_reply(&mut s, "exp_run camera_status 0",
            "CAMERA camera=0 initialized=1 request=9 op=2 result=OK object=7\nCAMERA_PEAK camera=0 request=9 valid=1 max_cC=3000 x=31 y=23 sample_ms=100");
        assert_eq!(s.exp.camera[0].peak, Some((3000, 31, 23)));
        s.exp.camera[0].x = 10;
        s.exp.camera[0].y = 10;
        s.exp.camera[0].delta = 2;
        let command = plan(&mut s, "exp.cam_mean", &Act::Run).unwrap()[0]
            .text
            .clone();
        assert_eq!(command, "exp_run camera_temp 0 8 8 5 5 mean");
        apply_camera_reply(
            &mut s,
            &command,
            "CAMERA_TEMP camera=0 request=9 roi=8,8,5,5 max_cC=2800 mean_cC=2500 sample_ms=100",
        );
        assert_eq!(s.exp.camera[0].mean_centi, Some(2500));
        assert_eq!(s.exp.camera[0].temp_centi, None);
        assert_eq!(s.exp.camera[0].peak, Some((3000, 31, 23)));
        s.exp.camera[0].mean_centi = None;
        s.exp.camera[0].delta = 0;
        apply_camera_reply(
            &mut s,
            &command,
            "CAMERA_TEMP camera=0 request=9 mean_cC=2500 sample_ms=100",
        );
        assert_eq!(s.exp.camera[0].mean_centi, None);
        apply_camera_reply(&mut s, "exp_run camera_status 0",
            "CAMERA camera=0 initialized=1 request=10 op=2 result=RUNNING object=7\nCAMERA_PEAK camera=0 request=10 valid=0");
        assert_eq!(s.exp.camera[0].peak, None);
    }

    #[test]
    fn saved_camera_name_requires_matching_boot_object_and_source() {
        let mut s = Sim::new();
        s.exp.camera[1].object_id = 17;
        s.exp.camera[1].result = "OK".into();
        let saved = br#"{"table":6,"fresh":true,"data":{"boot_id":9},"download":{"boot_id":9,"object_id":17,"type":3,"source":1,"file":"images/exp/EXP_C1_B00000009_O00000011_E2.bin"}}"#;
        apply_inventory_snapshot(&mut s, saved).unwrap();
        assert_eq!(
            s.exp.camera[1].saved_file.as_deref(),
            Some("images/exp/EXP_C1_B00000009_O00000011_E2.bin")
        );
        s.exp.camera[1].saved_file = None;
        let wrong_boot = br#"{"table":6,"fresh":true,"data":{"boot_id":10},"download":{"boot_id":9,"object_id":17,"type":3,"source":1,"file":"images/exp/old.bin"}}"#;
        apply_inventory_snapshot(&mut s, wrong_boot).unwrap();
        assert!(s.exp.camera[1].saved_file.is_none());
    }

    #[test]
    fn camera_commands_and_roi_measurement_use_exp_gateway() {
        let mut s = Sim::new();
        s.exp.cam_sel = 1;
        let cfg = plan(&mut s, "exp.cam_apply", &Act::Run).unwrap();
        assert_eq!(
            cfg[0].text,
            "exp_run camera_config 1 mlx90640 51 float32 8 1000"
        );
        assert_eq!(
            plan(&mut s, "exp.cam_capture", &Act::Run).unwrap()[0].text,
            "exp_run camera_capture 1"
        );
        assert_eq!(
            plan(&mut s, "exp.cam_address_set", &Act::Run).unwrap()[0].text,
            "exp_run camera_address_set 1 51"
        );
        assert_eq!(
            plan(&mut s, "exp.cam_temp", &Act::Run).unwrap()[0].text,
            "exp_run camera_temp 1 0 0 1 1"
        );
        s.exp.camera[1].x = 32;
        assert!(plan(&mut s, "exp.cam_temp", &Act::Run).is_err());
    }

    #[test]
    fn camera_reply_tracks_async_capture_and_temperature() {
        let mut s = Sim::new();
        s.live.cam_wait[0] = Some(1.0);
        apply_camera_reply(&mut s, "exp_run camera_status 0",
            "CAMERA camera=0 model=MLX90642 address=0x66 format=float32 hz=8 emissivity_milli=1000 initialized=1 data_exist=1 request=41 op=2 result=OK error=0 object=9 seq=41\nCAMERA address camera=0 requested=0x66 restart_required=1 verified=1\nCAMERA buffer=0 ENABLED result=0");
        assert_eq!(s.exp.camera[0].object_id, 9);
        assert!(s.exp.camera[0].confirmed);
        assert_eq!(s.exp.camera[0].actual_address, 0x66);
        assert!(s.exp.camera[0].address_restart_required);
        assert_eq!(s.exp.camera[0].buffer_enabled, Some(true));
        assert!(s.live.cam_wait[0].is_none());
        apply_camera_reply(
            &mut s,
            "exp_run camera_temp 0 0 0 1 1",
            "CAMERA_TEMP camera=0 request=41 roi=0,0,32,24 max_cC=2534 sample_ms=4294967294",
        );
        assert_eq!(s.exp.camera[0].temp_centi, Some(2534));
        assert_eq!(s.exp.camera[0].sample_ms, 4294967294);
    }
}

fn apply_exp_config_status(s: &mut Sim, command: &str, payload: &[u8]) {
    let Some((0, text)) = proto::dec_exp_text(payload) else {
        return;
    };
    if command == "exp_run ir_status" {
        for line in text.lines() {
            let line = line.trim();
            let Some(channel) = line
                .strip_prefix("IR")
                .and_then(|rest| rest.chars().next())
                .and_then(|c| c.to_digit(10))
                .filter(|&n| n < 2)
            else {
                continue;
            };
            let i = channel as usize;
            if let Some((field, wanted)) = s.exp.ir_set_pending[i] {
                if status_value(line, field) == Some(wanted) {
                    s.exp.ir_set_pending[i] = None;
                    s.exp.ir_config_pending[i] = None;
                }
            }
            let max_duty = status_value(line, "max_duty_pct");
            let max_on = status_value(line, "on_ms");
            let pwm = status_value(line, "pwm_hz");
            if s.exp.ir_config_pending[i].is_some() {
                let wanted = s.exp.ir_setup[i];
                if max_duty == Some(wanted.max_duty)
                    && max_on == Some(wanted.on_ms)
                    && pwm == Some(wanted.pwm_hz)
                {
                    s.exp.ir_config_pending[i] = None;
                    s.exp.ir_configured[i] = true;
                    s.log(
                        Sub::Exp,
                        Level::Ok,
                        format!("IR {i} limits confirmed by MCU"),
                    );
                }
            }
            if let (Some(max_duty), Some(on_ms), Some(pwm_hz), Some(duty)) =
                (max_duty, max_on, pwm, status_value(line, "set_duty_pct"))
            {
                s.exp.ir_applied[i] = Some(crate::sim::IrSetup {
                    max_duty,
                    on_ms,
                    pwm_hz,
                    duty,
                });
                if s.exp.ir_config_pending[i].is_none() {
                    s.exp.ir_configured[i] = true;
                }
            }
            if let Some(v) = status_value(line, "duty_pct") {
                s.exp.ir[i] = v > 0;
                s.exp.ir_actual_duty[i] = v;
            }
            s.exp.ir_status_seen[i] = true;
        }
    } else if command == "exp_run burn_status" {
        for line in text.lines() {
            let line = line.trim();
            if line.starts_with("burn state=") {
                if let Some(state) = status_word(line, "state") {
                    s.exp.burn_state = state.into();
                }
                s.exp.burn_active_wire = status_value(line, "active_wire")
                    .and_then(|wire| usize::try_from(wire).ok())
                    .filter(|&wire| wire < 2);
                if s.exp.burn_state != "ARMED" {
                    s.exp.burn_armed_until = None;
                }
                if s.exp.burn_state == "IDLE" {
                    s.live.burn_verify_until = None;
                }
            }
            let Some(wire) = line
                .strip_prefix("wire")
                .and_then(|rest| rest.chars().next())
                .and_then(|c| c.to_digit(10))
                .filter(|&n| n < 2)
            else {
                continue;
            };
            let i = wire as usize;
            let c = &mut s.exp.burn_setup[i];
            for (key, dst) in [
                ("duty_pct", &mut c.duty),
                ("burn_time_ms", &mut c.burn_time_ms),
                ("max_total_current_ma", &mut c.max_total_current_ma),
            ] {
                if let Some(v) = status_value(line, key) {
                    *dst = v;
                }
            }
            if let Some(configured) = status_value(line, "configured") {
                s.exp.burn_configured[i] = configured == 1;
                s.exp.burn_status_seen[i] = true;
            }
            if let Some(outcome) = status_word(line, "last") {
                s.exp.burn_last[i] = outcome.into();
            }
            if let Some(attempt) = status_value(line, "attempt") {
                s.exp.burn_attempt[i] = attempt as u32;
            }
            if let Some(peak) = status_value(line, "peak_delta_ma") {
                s.exp.burn_peak_ma[i] = peak as u32;
            }
        }
    }
}

fn apply_motor_settings(s: &mut Sim, command: &str, text: &str) {
    let Some(motor) = command
        .split_whitespace()
        .nth(2)
        .and_then(|value| value.parse::<usize>().ok())
        .filter(|&i| i < 2)
    else {
        return;
    };
    if command.starts_with("exp_run motor5_cfg_get ") {
        for line in text.lines() {
            if line.trim().starts_with(&format!("MOTOR{motor} ")) {
                if let Some(mode) = status_word(line, "mode") {
                    if let Some(index) = ["MANUAL", "OPERATION"]
                        .iter()
                        .position(|&name| name == mode)
                    {
                        if !s.exp.motor_mode_verified[motor] {
                            s.exp.motor_mode_draft[motor] = index;
                        }
                        s.exp.motor_mode[motor] = index;
                        s.exp.motor_mode_verified[motor] = true;
                    }
                }
            }
            if let Some(speed) = status_value(line, "max_speed_sps") {
                s.exp.motor_max_sps[motor] = speed as i32;
            }
            if let Some(duty) = status_value(line, "drive_pct") {
                s.exp.motor_drive_applied_pct[motor] = duty;
                if !s.exp.motor_drive_dirty[motor] || s.exp.motor_drive_pct[motor] == duty {
                    s.exp.motor_drive_pct[motor] = duty;
                    s.exp.motor_drive_dirty[motor] = false;
                }
            }
        }
    } else if command.starts_with("exp_run motor5_pwm_freq_get ") {
        for line in text.lines() {
            if let Some(hz) = status_value(line, "pwm_freq_hz") {
                if let Some(idx) = [80, 100, 200, 2000].iter().position(|&v| v == hz) {
                    s.exp.motor_pwm[motor] = idx;
                }
            }
        }
    } else if command.starts_with("exp_run motor5_limit_get ") {
        let mut inputs = [None, None, None];
        for line in text.lines() {
            let line = line.trim();
            if !line.starts_with(&format!("MOTOR{motor} ")) {
                continue;
            }
            for (role, name) in ["home", "min", "max"].iter().enumerate() {
                if let Some(input) = status_word(line, name) {
                    inputs[role] = Some(input.to_string());
                    if role > 0 && input.starts_with("pm") {
                        if let Some(bank) = input.get(..3) {
                            if bank == "pmu" || bank == "pml" {
                                s.exp.motor_bank[motor] = usize::from(bank == "pml");
                            }
                        }
                        if let Some(index) = input.chars().last().and_then(|c| c.to_digit(10)) {
                            if index < 2 {
                                if role == 1 {
                                    s.exp.motor_min[motor] = index as usize;
                                } else {
                                    s.exp.motor_max[motor] = index as usize;
                                }
                            }
                        }
                        let level = usize::from(line.contains("active_HIGH"));
                        if role == 1 {
                            s.exp.motor_min_level[motor] = level;
                        } else {
                            s.exp.motor_max_level[motor] = level;
                        }
                    }
                }
            }
        }
        if inputs[0].is_some() && inputs[0] == inputs[2] {
            s.exp.motor_home_end[motor] = 1;
        } else if inputs[0].is_some() && inputs[0] == inputs[1] {
            s.exp.motor_home_end[motor] = 0;
        }
        s.exp.motor_limits_verified[motor] =
            inputs[1].as_ref().is_some_and(|v| v.starts_with("pm"))
                && inputs[2].as_ref().is_some_and(|v| v.starts_with("pm"))
                && (inputs[0] == inputs[1] || inputs[0] == inputs[2]);
    }
}

fn rail_set(r: &mut crate::sim::Rail, mv: Option<i64>, ma: Option<i64>) {
    r.mv = mv.unwrap_or(0) as i32;
    r.ma = ma.unwrap_or(0) as i32;
}

fn apply_thermal_snapshot(s: &mut Sim, payload: &[u8]) -> Result<String, String> {
    let raw = std::str::from_utf8(payload).map_err(|_| "thermal snapshot is not UTF-8")?;
    let envelope = Json::parse(raw).map_err(|_| "thermal snapshot is not JSON")?;
    let fresh = envelope.get("fresh").bool() == Some(true);
    let data = envelope.get("data");
    if !fresh || data.get("schema").i64() != Some(6) {
        s.exp.ht_status_seen = [false; 4];
        return Err("thermal table 3 is stale or unavailable".into());
    }
    let mut phases = [0u8; 4];
    let mut reasons = [0u8; 4];
    let mut configured = [false; 4];
    let mut running = [false; 4];
    let mut main = [f32::NAN; 4];
    let mut secondary = [f32::NAN; 4];
    let mut target = [f32::NAN; 4];
    let mut requested = [0u8; 4];
    let mut applied = [0u8; 4];
    let mut limit = [0u8; 4];
    for i in 0..4 {
        let field = |name: &str| data.get(name).at(i).i64();
        let state = field("state").ok_or("missing heater state")?;
        phases[i] = match state {
            0 => 0,
            5 => 1,
            8 => 2,
            _ => return Err(format!("unknown heater state {state}")),
        };
        reasons[i] = field("fault")
            .ok_or("missing heater fault")?
            .try_into()
            .map_err(|_| "invalid heater fault")?;
        configured[i] = field("configured") == Some(1);
        running[i] = phases[i] == 1 && field("enabled") == Some(1);
        let sensors_valid = field("sensors_valid") == Some(1);
        if sensors_valid {
            main[i] = field("main_mC").ok_or("missing main temperature")? as f32 / 1000.0;
            secondary[i] = field("sec_mC").ok_or("missing secondary temperature")? as f32 / 1000.0;
        }
        target[i] = field("setpoint_mC").ok_or("missing setpoint")? as f32 / 1000.0;
        requested[i] = field("request_pct")
            .ok_or("missing requested duty")?
            .try_into()
            .map_err(|_| "invalid requested duty")?;
        applied[i] = field("applied_pct")
            .ok_or("missing applied duty")?
            .try_into()
            .map_err(|_| "invalid applied duty")?;
        limit[i] = field("limit_pct")
            .ok_or("missing duty limit")?
            .try_into()
            .map_err(|_| "invalid duty limit")?;
        if requested[i] > 100 || applied[i] > 100 || limit[i] > 100 || reasons[i] > 7 {
            return Err("out-of-range thermal status".into());
        }
    }
    s.exp.ht_phase = phases;
    s.exp.ht_reason = reasons;
    s.exp.ht_configured = configured;
    s.exp.ht_running = running;
    s.exp.ht_main_temp = main;
    s.exp.ht_sec_temp = secondary;
    s.exp.ht_target_actual = target;
    s.exp.ht_requested = requested;
    s.exp.ht_applied = applied;
    s.exp.ht_limit = limit;
    s.exp.ht_status_seen = [true; 4];
    s.exp.ht_status_at = Some(s.t);
    Ok(format!(
        "thermal table 3 seq={} heater {}: phase={} duty={}%",
        data.get("seq").i64().unwrap_or_default(),
        s.exp.ht_sel,
        phases[s.exp.ht_sel],
        applied[s.exp.ht_sel]
    ))
}

fn apply_inventory_snapshot(s: &mut Sim, payload: &[u8]) -> Result<String, String> {
    let raw = std::str::from_utf8(payload).map_err(|_| "inventory snapshot is not UTF-8")?;
    let envelope = Json::parse(raw).map_err(|_| "inventory snapshot is not JSON")?;
    let data = envelope.get("data");
    let saved = envelope.get("download");
    let boot = data.get("boot_id").i64();
    let saved_boot = saved.get("boot_id").i64();
    let object = saved.get("object_id").i64();
    let source = saved.get("source").i64();
    let file = saved.get("file").str();
    if envelope.get("fresh").bool() != Some(true)
        || boot.is_none()
        || boot != saved_boot
        || saved.get("type").i64() != Some(3)
        || !matches!(source, Some(0 | 1))
        || file.is_none_or(|f| !f.starts_with("images/exp/") || f.contains(".."))
    {
        return Ok("no new saved EXP image".into());
    }
    let camera = source.unwrap() as usize;
    let object = object.unwrap_or_default();
    if object > 0
        && s.exp.camera[camera].object_id == object as u32
        && s.exp.camera[camera].result == "OK"
    {
        let filename = file.unwrap().to_string();
        if s.exp.camera[camera].saved_file.as_deref() != Some(filename.as_str()) {
            s.log(Sub::Exp, Level::Ok, format!("EXP image saved: {filename}"));
        }
        s.exp.camera[camera].saved_file = Some(filename.clone());
        s.live.cam_download_wait[camera] = None;
        return Ok(format!("EXP image saved: {filename}"));
    }
    Ok("no matching saved EXP image".into())
}

/// Decode one reply into the model. Returns a one-line summary for the history.
fn decode(s: &mut Sim, cmd: &Cmd, r: &Reply) -> (Level, String) {
    let p = &r.payload;
    let bad = |what: &str| {
        (
            Level::Err,
            format!("{what}: unexpected payload ({} bytes)", p.len()),
        )
    };
    match (cmd.ty, cmd.addr) {
        (2, 0x0f) if cmd.payload == [3] => match apply_thermal_snapshot(s, p) {
            Ok(summary) => (Level::Info, summary),
            Err(reason) => {
                s.exp.ht_status_seen = [false; 4];
                (Level::Warn, reason)
            }
        },
        (2, 0x0f) if cmd.payload == [6] => match apply_inventory_snapshot(s, p) {
            Ok(summary) => (Level::Info, summary),
            Err(reason) => (Level::Warn, reason),
        },
        (1, 0x10) if p.len() == 32 && (1..=4).contains(&p[0]) => {
            let cam = (p[0] - 1) as usize;
            s.obc.usb_led_pwm[cam] = if p[3] & 0x10 != 0 { p[31] as i32 } else { 0 };
            (
                Level::Ok,
                format!("camera {} LED profile {}%", p[0], s.obc.usb_led_pwm[cam]),
            )
        }
        (0, 0) => {
            if p.len() >= 4 {
                s.obc.uptime = u32::from_le_bytes([p[0], p[1], p[2], p[3]]) as f64;
                (Level::Info, format!("uptime {} s", s.obc.uptime as u64))
            } else {
                bad("ping")
            }
        }
        (1, 0) => match proto::dec_sys_info(p) {
            Some(i) => {
                s.pdu.online = i[0].online;
                s.exp.link = i[1].online;
                s.obc.fsw_up = i[2].online;
                let f = |b: &proto::BoardInfo| {
                    if b.online {
                        format!("fw {}.{}.{}", b.fw[0], b.fw[1], b.fw[2])
                    } else {
                        "offline".into()
                    }
                };
                (
                    Level::Info,
                    format!("PDU {} | EXP {} | OBC {}", f(&i[0]), f(&i[1]), f(&i[2])),
                )
            }
            None => bad("sys_info"),
        },
        (1, 1) => {
            if p.len() == 4 {
                let e = u32::from_le_bytes([p[0], p[1], p[2], p[3]]) as i64;
                s.obc.rtc_offset = e - unix_now();
                (Level::Info, format!("epoch {e}"))
            } else {
                bad("sys_rtc")
            }
        }
        (4, 2) => match proto::dec_obc_hk(p) {
            Some(h) => {
                let o = &mut s.obc;
                o.boot_count = h.boot;
                o.temp_onboard = h.onboard_c;
                o.ntc = h.ntc_c;
                o.bme_t = h.bme_c;
                o.bme_p_pa = h.press_pa as i32;
                o.bme_rh = h.rh;
                o.bme_gas = h.gas_ohm as i32;
                for i in 0..4 {
                    o.u6[i].mv = h.mv[i] as i32;
                    o.u6[i].ma = h.ma[i] as i32;
                    o.u2[i].mv = h.mv[4 + i] as i32;
                    o.u2[i].ma = h.ma[4 + i] as i32;
                }
                o.snapshot_age = 0.0;
                (
                    Level::Info,
                    format!(
                        "onboard {:.2} C, NTC {:.2}/{:.2}/{:.2}/{:.2} C",
                        h.onboard_c, h.ntc_c[0], h.ntc_c[1], h.ntc_c[2], h.ntc_c[3]
                    ),
                )
            }
            None => bad("tm_obc_get"),
        },
        (1, 4) => match proto::dec_pwr_stat(p) {
            Some((Board::Obc, _, recs)) => {
                let st = |bit: u8| recs.iter().find(|x| x.bit == bit).map_or(false, |x| x.on);
                let o = &mut s.obc;
                o.ai_efuse = st(8);
                o.fpga_efuse = st(9);
                for i in 0..4 {
                    o.usb[i] = st(10 + i as u8);
                }
                o.mipi = [st(14), st(15)];
                (
                    Level::Info,
                    format!(
                        "AI {} FPGA {} USB {}{}{}{} MIPI {}{}",
                        st(8) as u8,
                        st(9) as u8,
                        st(10) as u8,
                        st(11) as u8,
                        st(12) as u8,
                        st(13) as u8,
                        st(14) as u8,
                        st(15) as u8
                    ),
                )
            }
            Some((Board::Exp, _, recs)) => {
                for x in &recs {
                    let (mv, ma) = (x.mv.map(|v| v as i64), x.ma.map(|v| v as i64));
                    match x.bit {
                        0..=3 => rail_set(&mut s.exp.pac[x.bit as usize], mv, ma),
                        4 => s.exp.exp12v = x.on,
                        5 => s.exp.sln = x.on,
                        6 => s.exp.motor = x.on,
                        _ => {}
                    }
                }
                let e = &s.exp;
                (
                    Level::Info,
                    format!(
                        "EXP12V {} SLN_HD {} MOTOR {}",
                        e.exp12v as u8, e.sln as u8, e.motor as u8
                    ),
                )
            }
            Some((Board::Pdu, _, recs)) => {
                for x in &recs {
                    let (mv, ma) = (x.mv.map(|v| v as i64), x.ma.map(|v| v as i64));
                    match x.bit {
                        0 => {
                            s.pdu.vout_en = x.on;
                            rail_set(&mut s.pdu.rails[0], mv, ma);
                        }
                        1 => {
                            s.pdu.trans_en = x.on;
                            rail_set(&mut s.pdu.rails[2], mv, ma);
                        }
                        2 => rail_set(&mut s.pdu.rails[1], mv, ma),
                        3 => rail_set(&mut s.pdu.rails[3], mv, ma),
                        _ => {}
                    }
                }
                s.pdu.poll_age = 0.0;
                (
                    Level::Info,
                    format!(
                        "VOUT {} TRANS {}",
                        s.pdu.vout_en as u8, s.pdu.trans_en as u8
                    ),
                )
            }
            None => bad("pwr_stat"),
        },
        (1, 3) => match proto::dec_pwr_en(p) {
            Some((b, on, known, failed)) => {
                if b == Board::Exp as u8 {
                    if known & 1 != 0 {
                        s.exp.exp12v = on & 1 != 0;
                    }
                    if known & 2 != 0 {
                        s.exp.sln = on & 2 != 0;
                    }
                    if known & 4 != 0 {
                        s.exp.motor = on & 4 != 0;
                    }
                }
                let lvl = if failed != 0 { Level::Err } else { Level::Ok };
                (
                    lvl,
                    format!("board {b} on=0x{on:04X} failed=0x{failed:04X}"),
                )
            }
            None => bad("pwr_en"),
        },
        (1, 2) => match proto::dec_pdu_vout(p) {
            Some(v) => {
                if let Some(sp) = v.set_mv {
                    s.pdu.vout_set = sp as i32;
                }
                s.pdu.vout_en = v.vout_on;
                s.pdu.trans_en = v.trans_on;
                (
                    Level::Info,
                    format!(
                        "setpoint {:?} mV, measured {:?} mV {:?} mA",
                        v.set_mv, v.mv, v.ma
                    ),
                )
            }
            None => bad("pdu_vout"),
        },
        (1, 0x1c) if p.as_slice() == [0] => {
            s.pdu.latched = false;
            (Level::Ok, "PDU fault latch cleared".into())
        }
        (1, 0x1d) if p.len() == 9 => {
            s.pdu.heartbeat = p[0] != 0;
            s.pdu.led_on = u32::from_le_bytes(p[1..5].try_into().unwrap()) as i32;
            s.pdu.led_off = u32::from_le_bytes(p[5..9].try_into().unwrap()) as i32;
            (
                Level::Info,
                format!(
                    "heartbeat {} on={} ms off={} ms",
                    if s.pdu.heartbeat { "on" } else { "off" },
                    s.pdu.led_on,
                    s.pdu.led_off
                ),
            )
        }
        (1, 0x1e) if p.as_slice() == [1] => (
            Level::Warn,
            "PDU accepted bootloader request; Modbus may go offline".into(),
        ),
        (1, 0x13) if p.len() == 32 && p[0] == 1 => {
            for i in 0..4 {
                let o = 2 + 4 * i;
                let hp = &mut s.exp.ht_profile[i];
                hp.max_duty = p[o] as i32;
                hp.manual_slew = p[o + 1] as i32;
                hp.max_on = u16::from_le_bytes([p[o + 2], p[o + 3]]) as i32 * 10;
            }
            (Level::Info, "heater limits read from EXP".into())
        }
        (1, 0x14) if p.len() == 4 => (Level::Ok, "heater manual command accepted".into()),
        (2, 0x03) if p.len() == 32 && p[1] == 0 && p[2] < 4 => {
            let i = p[2] as usize;
            let hp = &mut s.exp.ht_profile[i];
            hp.main_ntc = p[3] as usize;
            hp.sec_ntc = p[4] as usize;
            hp.target = i16::from_le_bytes([p[5], p[6]]) as i32;
            hp.delta = i16::from_le_bytes([p[7], p[8]]) as i32;
            hp.cutoff = i16::from_le_bytes([p[9], p[10]]) as i32;
            hp.timeout = u16::from_le_bytes([p[11], p[12]]) as i32;
            hp.resume_boot = p[13] != 0;
            hp.slew_up = p[14] as i32;
            hp.slew_down = p[15] as i32;
            hp.kp = (f32::from_le_bytes(p[16..20].try_into().unwrap()) * 1000.0).round() as i32;
            hp.ki = (f32::from_le_bytes(p[20..24].try_into().unwrap()) * 1000.0).round() as i32;
            hp.kd = (f32::from_le_bytes(p[24..28].try_into().unwrap()) * 1000.0).round() as i32;
            s.exp.ht_loaded[i] = true;
            (Level::Info, format!("heater {i} profile read from EXP"))
        }
        (2, 0x04) if p.len() == 1 => {
            let i = (cmd.payload[0] & 3) as usize;
            s.exp.ht_phase[i] = p[0] & 7;
            s.exp.ht_reason[i] = (p[0] >> 3) & 7;
            s.exp.ht_configured[i] = p[0] & 0x40 != 0;
            s.exp.ht_running[i] = p[0] & 0x80 != 0;
            let level = if s.exp.ht_phase[i] == 2 {
                Level::Warn
            } else {
                Level::Info
            };
            (
                level,
                format!(
                    "heater {i} phase={} reason={} configured={} running={}",
                    s.exp.ht_phase[i],
                    s.exp.ht_reason[i],
                    s.exp.ht_configured[i],
                    s.exp.ht_running[i]
                ),
            )
        }
        (1, 0x15) => match proto::dec_thm_stat(p) {
            Some(t) => {
                for i in 0..4 {
                    let measured = if t[i].0 { t[i].1 } else { 0 };
                    if let Some((expected, sent_at)) = s.live.heater_pending[i] {
                        if measured == expected || s.t - sent_at >= 3.0 {
                            s.live.heater_pending[i] = None;
                        } else {
                            continue;
                        }
                    }
                    s.exp.heater[i] = measured as i32;
                }
                s.exp.ir = [t[4].0, t[5].0];
                s.exp.ir_actual_duty = [
                    if t[4].0 { t[4].1 as i32 } else { 0 },
                    if t[5].0 { t[5].1 as i32 } else { 0 },
                ];
                (
                    Level::Info,
                    format!(
                        "heaters {}/{}/{}/{} %, IR {}/{}",
                        t[0].1, t[1].1, t[2].1, t[3].1, t[4].0 as u8, t[5].0 as u8
                    ),
                )
            }
            None => bad("thm_stat"),
        },
        (1, 0x18) => match proto::dec_fld_stat(p) {
            Some(f) => {
                s.exp.pc_manual_actual = f.valves & 0x0f;
                s.exp.pc_manual_valid = f.valves_valid & 0x0f;
                let staged = s
                    .exp
                    .pc_manual_valves
                    .iter()
                    .enumerate()
                    .fold(0u8, |bits, (i, &v)| bits | ((v as u8) << i));
                if s.exp.pc_manual_edited
                    && s.exp.pc_manual_valid == 0x0f
                    && staged == s.exp.pc_manual_actual
                {
                    s.exp.pc_manual_edited = false;
                }
                if !s.exp.pc_manual_edited {
                    for i in 0..4 {
                        if s.exp.pc_manual_valid & (1 << i) != 0 {
                            s.exp.pc_manual_valves[i] =
                                ((s.exp.pc_manual_actual >> i) & 1) as usize;
                        }
                    }
                }
                let u = &mut s.exp.u11;
                if f.u11_type != 0 {
                    let actual = (f.u11_type - 1).min(1) as usize;
                    if u.ptype_staged == u.ptype {
                        u.ptype_staged = actual;
                    }
                    u.ptype = actual;
                }
                u.ptype_configured = f.u11_type != 0;
                u.inited = f.u11_init;
                u.detected |= f.u11_init;
                u.en = f.u11_en;
                u.freq = f.u11_hz as i32;
                u.volt = f.u11_vpp as i32;
                let u = &mut s.exp.u15;
                u.inited = f.u15_init;
                u.detected |= f.u15_init;
                u.en = f.u15_en;
                u.freq = f.u15_hz as i32;
                for c in 0..4 {
                    u.volt[c] = f.u15_vpp[c] as i32;
                }
                (
                    Level::Info,
                    format!(
                        "U11 init {} en {} {} Hz {} Vpp | U15 init {} en {}",
                        f.u11_init as u8,
                        f.u11_en as u8,
                        f.u11_hz,
                        f.u11_vpp,
                        f.u15_init as u8,
                        f.u15_en as u8
                    ),
                )
            }
            None => bad("fld_stat"),
        },
        (1, 0x1B) => match proto::dec_rba_stat(p) {
            Some(m) => {
                s.exp.drv.inited = m.drv_init;
                s.exp.drv.detected |= m.drv_init;
                s.exp.drv.fault = m.drv_fault;
                for i in 0..2 {
                    let mo = &mut s.exp.motors[i];
                    mo.running = matches!(m.state[i], 1..=3);
                    mo.homing = m.state[i] == 3;
                    mo.holding = m.state[i] == 4;
                    mo.pos = m.pos[i] as f32;
                    mo.dir = if m.reverse[i] { -1 } else { 1 };
                    mo.sps = m.sps[i] as i32;
                }
                (
                    Level::Info,
                    format!(
                        "driver init {} fault {} | m0 st{} pos {} | m1 st{} pos {}",
                        m.drv_init as u8,
                        m.drv_fault as u8,
                        m.state[0],
                        m.pos[0],
                        m.state[1],
                        m.pos[1]
                    ),
                )
            }
            None => bad("rba_stat"),
        },
        (2, 0) => match proto::dec_pc_stat(p) {
            Some((st, act, err)) => {
                s.exp.pc_running = (1..=4).contains(&st);
                s.exp.pc_state = [
                    "STANDBY",
                    "TEST",
                    "REPLACE",
                    "CIRCULATION",
                    "STOPPING",
                    "ERROR",
                    "ONGROUND",
                ][(st as usize).min(6)]
                .into();
                s.exp.pc_activated = act;
                s.exp.pc_error = if err { "flagged" } else { "NONE" }.into();
                (
                    Level::Info,
                    format!("state {st} activated {} error {}", act as u8, err as u8),
                )
            }
            None => bad("pc_stat"),
        },
        (1, 0x11) | (1, 0x12) => match proto::dec_uvc(p) {
            Some((cam, st, err, id)) => {
                let c = (cam as usize).saturating_sub(1).min(3);
                let sh = &mut s.obc.usb_shot[c];
                match st {
                    1 | 2 => {
                        sh.state = ShotState::Capturing;
                        (
                            Level::Info,
                            format!(
                                "camera {cam} request {id}: {}",
                                ["", "queued", "capturing"][st as usize]
                            ),
                        )
                    }
                    3 => {
                        sh.state = ShotState::Saved;
                        // Console text carries the real path; RS-422 only the request id.
                        let path = r
                            .text
                            .split("path=")
                            .nth(1)
                            .map(|x| x.trim().to_string())
                            .unwrap_or_else(|| {
                                format!("/home/bee/data/images/usb/USB{}_{id:08}.jpg", cam - 1)
                            });
                        sh.path = path.clone();
                        sh.note = if r.text.contains("path=") {
                            "saved path reported by FSW"
                        } else {
                            "default path assumed (RS-422 does not send path)"
                        }
                        .into();
                        s.live.uvc[c] = None;
                        (Level::Ok, format!("camera {cam} saved {path}"))
                    }
                    4 => {
                        sh.state = ShotState::Failed;
                        sh.note = format!("error {err}");
                        s.live.uvc[c] = None;
                        (
                            Level::Err,
                            format!("camera {cam} capture failed (error {err})"),
                        )
                    }
                    _ => (
                        Level::Info,
                        format!("camera {cam} request {id}: state {st}"),
                    ),
                }
            }
            None => bad("uvc"),
        },
        (1, 0x16) => match proto::dec_pump_req(p) {
            Some((target, state, id)) => {
                let name = proto::PUMP_STATES[(state as usize).min(11)];
                let unit = if target == 0xFF {
                    s.live.pump_req.map_or(0, |x| x.1)
                } else {
                    target
                };
                if state < 2 {
                    s.live.pump_req = Some((id, unit, s.t));
                } else {
                    s.live.pump_req = None;
                    if state == 2 {
                        if unit == 0 {
                            s.exp.u11.detected = true;
                        } else {
                            s.exp.u15.detected = true;
                        }
                    }
                }
                (
                    if state <= 2 { Level::Ok } else { Level::Warn },
                    format!(
                        "pump {} request {id}: {name}",
                        if unit == 0 { "U11" } else { "U15" }
                    ),
                )
            }
            None => (Level::Ok, "configuration accepted".into()),
        },
        (1, 0x17) => match proto::dec_req_id(p) {
            Some(id) => {
                if let Some(&unit) = cmd.payload.first() {
                    if unit == 1 || unit == 2 {
                        s.live.pump_req = Some((id, unit - 1, s.t));
                    }
                }
                (Level::Ok, format!("queued, request id {id}"))
            }
            None => (Level::Ok, "accepted".into()),
        },
        (1, 0x1A) => match proto::dec_req_id(p) {
            Some(id) => (Level::Ok, format!("motor request id {id}")),
            None => (Level::Ok, format!("motor reply ({} bytes)", p.len())),
        },
        (2, 0x0A..=0x0C) => match proto::dec_exp_text(p) {
            Some((st, text)) => {
                let echoed = cmd.text.strip_prefix("exp_run ").unwrap_or("");
                let detail = text
                    .lines()
                    .map(str::trim)
                    .find(|line| !line.is_empty() && *line != echoed && !line.starts_with("DEBUG@"))
                    .unwrap_or("");
                let refused = detail.contains("REFUSED")
                    || detail.starts_with("ERROR")
                    || detail.starts_with("Error:");
                (
                    if st == 0 && !refused {
                        Level::Ok
                    } else {
                        Level::Warn
                    },
                    format!("EXP status {st}: {}", first_line(detail)),
                )
            }
            None => bad("exp gateway"),
        },
        _ => (Level::Info, first_line(&r.text)),
    }
}

#[cfg(test)]
mod power_reply_tests {
    use super::*;

    #[test]
    fn exp_power_reply_updates_known_rail_states() {
        let mut sim = Sim::new();
        sim.exp.sln = true;
        let cmd = proto::pwr_en_mask(Board::Exp, 7, 7, false, None);
        let reply = Reply {
            ack: 1,
            code: 3,
            payload: vec![4, 5, 0, 7, 0, 2, 0],
            text: String::new(),
            ok: true,
            tx: String::new(),
            rx: String::new(),
            ms: 1,
        };
        let (level, _) = decode(&mut sim, &cmd, &reply);
        assert_eq!(level, Level::Err);
        assert!(sim.exp.exp12v);
        assert!(!sim.exp.sln);
        assert!(sim.exp.motor);
    }
}

#[cfg(test)]
mod heater_reply_tests {
    use super::*;

    #[test]
    fn profile_owner_blocks_manual_heater_commands() {
        let mut sim = Sim::new();
        sim.exp.ht_status_seen = [true; 4];
        sim.exp.ht_phase[0] = 1;
        assert!(plan(&mut sim, "exp.ht_manual_set", &Act::Run)
            .unwrap_err()
            .contains("stop the profile"));
        assert!(plan(&mut sim, "exp.ht_manual_off", &Act::Run)
            .unwrap_err()
            .contains("stop the profile"));
        sim.exp.ht_phase[0] = 0;
        assert_eq!(
            plan(&mut sim, "exp.ht_manual_set", &Act::Run).unwrap()[0].text,
            "thm_ctl heater 0 set 10 1000"
        );
    }

    #[test]
    fn housekeeping_round_includes_all_heater_profiles() {
        assert!(poll_set()
            .iter()
            .any(|cmd| cmd.text == "exp_csp_snapshot 3"));
    }

    #[test]
    fn profile_buttons_address_each_heater_and_require_fresh_status() {
        let mut sim = Sim::new();
        sim.exp.ht_sel = 0;
        sim.exp.ht_status_seen = [true; 4];
        assert!(plan(&mut sim, "exp.ht_toggle3", &Act::Run)
            .unwrap_err()
            .contains("EXP 12 V is OFF"));
        sim.exp.exp12v = true;
        sim.exp.ht_configured[3] = true;
        assert_eq!(
            plan(&mut sim, "exp.ht_toggle3", &Act::Run).unwrap()[0].text,
            "thp_ctl heater 3 start"
        );
        sim.exp.ht_running[3] = true;
        assert_eq!(
            plan(&mut sim, "exp.ht_toggle3", &Act::Run).unwrap()[0].text,
            "thp_ctl heater 3 stop"
        );
        sim.exp.ht_status_seen[3] = false;
        assert!(plan(&mut sim, "exp.ht_toggle3", &Act::Run).is_err());
    }

    #[test]
    fn thermal_snapshot_updates_profiles_and_rejects_stale_data() {
        let mut sim = Sim::new();
        sim.enter_live();
        let data = r#"{"table":3,"fresh":true,"age_ms":150,"data":{"schema":6,"seq":53,"state":[5,0,8,0],"fault":[0,0,3,0],"configured":[1,1,1,1],"enabled":[1,0,0,0],"sensors_valid":[1,1,0,1],"main_mC":[24500,25000,0,26000],"sec_mC":[24700,25200,0,26200],"setpoint_mC":[30000,30000,30000,30000],"request_pct":[20,0,0,0],"applied_pct":[18,0,0,0],"limit_pct":[80,80,80,80]}}"#;
        let cmd = proto::exp_csp_snapshot(3);
        let reply = Reply {
            ack: 2,
            code: 15,
            payload: data.as_bytes().to_vec(),
            text: String::new(),
            ok: true,
            tx: String::new(),
            rx: String::new(),
            ms: 1,
        };
        let (level, _) = decode(&mut sim, &cmd, &reply);
        assert_eq!(level, Level::Info);
        assert_eq!(sim.exp.ht_phase, [1, 0, 2, 0]);
        assert_eq!(sim.exp.ht_applied[0], 18);
        assert_eq!(sim.exp.ht_main_temp[0], 24.5);
        assert!(sim.exp.ht_main_temp[2].is_nan());
        assert!(sim.exp.ht_status_seen[0]);
        sim.live_tick(3.1);
        assert!(!sim.exp.ht_status_seen[0]);
        sim.exp.exp12v = true;
        assert!(sim.live_action("exp.ht_toggle0", Act::Run));
        assert_eq!(sim.live.heater_preflight.as_deref(), Some("exp.ht_toggle0"));
        apply(
            &mut sim,
            Done {
                cmd: Some(cmd.clone()),
                why: Why::Refresh,
                result: Ok(reply.clone()),
                state: None,
            },
        );
        assert!(sim.live.heater_preflight.is_none());
        assert!(sim.live.thermal_verify_at.is_some());
        let stale = Reply {
            payload: br#"{"table":3,"fresh":false,"age_ms":4000,"data":null}"#.to_vec(),
            ..reply
        };
        let (level, _) = decode(&mut sim, &cmd, &stale);
        assert_eq!(level, Level::Warn);
        assert!(!sim.exp.ht_status_seen[0]);
        assert_eq!(sim.exp.ht_applied[0], 18);
        assert!(sim.exp.ht_status_at.is_some());
        assert_eq!(
            plan(&mut sim, "exp.ht_status", &Act::Run).unwrap()[0].text,
            "exp_csp_snapshot 3"
        );
    }

    #[test]
    fn confirmed_duty_survives_old_table_12_reply_until_telemetry_catches_up() {
        let mut sim = Sim::new();
        sim.exp.heater[0] = 5;
        sim.live.heater_pending[0] = Some((5, sim.t));
        let cmd = proto::thm_stat();
        let mut reply = Reply {
            ack: 1,
            code: 0x15,
            payload: vec![0; 6],
            text: String::new(),
            ok: true,
            tx: String::new(),
            rx: String::new(),
            ms: 1,
        };
        decode(&mut sim, &cmd, &reply);
        assert_eq!(sim.exp.heater[0], 5);
        reply.payload[0] = 0x85;
        decode(&mut sim, &cmd, &reply);
        assert_eq!(sim.exp.heater[0], 5);
        assert!(sim.live.heater_pending[0].is_none());
        assert_eq!(
            heater_applied("--> OK: Heater[0] Duty = 5% (requested 10%)", 0),
            Some(5)
        );
    }
}

#[cfg(test)]
mod exp_config_command_tests {
    use super::*;

    #[test]
    fn ir_field_set_targets_one_channel_and_one_field() {
        let mut sim = Sim::new();
        sim.exp.ir_setup[1].duty = 37;
        sim.exp.ir_setup[1].pwm_hz = 12000;
        assert!(is_local("exp.ir1.duty"));
        assert_eq!(
            plan(&mut sim, "exp.ir1.set_duty", &Act::Run).unwrap()[0].text,
            "exp_run ir_cfg_set 1 duty_pct 37"
        );
        assert_eq!(
            plan(&mut sim, "exp.ir1.set_pwm", &Act::Run).unwrap()[0].text,
            "exp_run ir_cfg_set 1 pwm_hz 12000"
        );
        sim.exp.ir_applied[1] = Some(crate::sim::IrSetup {
            duty: 20,
            max_duty: 100,
            pwm_hz: 100000,
            on_ms: 0,
        });
        assert_eq!(
            plan(&mut sim, "exp.ir1.on", &Act::Run).unwrap()[0].text,
            "exp_run ir_set 1 20"
        );
    }

    #[test]
    fn ir_and_burn_controls_send_current_firmware_commands() {
        let mut sim = Sim::new();
        sim.exp.ir_setup[0].max_duty = 20;
        sim.exp.ir_setup[0].on_ms = 50;
        let ir = plan(&mut sim, "exp.ir_apply", &Act::Run).unwrap();
        assert_eq!(ir.len(), 1);
        assert_eq!(ir[0].text, "exp_run ir_config 0 20 50 100000 10");
        sim.exp.ir_setup[0].max_duty = 5;
        assert!(plan(&mut sim, "exp.ir_apply", &Act::Run).is_err());
        sim.exp.ir_setup[0].max_duty = 100;
        assert_eq!(
            plan(&mut sim, "exp.ir0", &Act::Bool(true)).unwrap()[0].text,
            "exp_run ir_set 0 10"
        );
        sim.exp.burn_setup[0] = crate::sim::BurnSetup {
            duty: 25,
            burn_time_ms: 100,
            max_total_current_ma: 1000,
        };
        let burn = plan(&mut sim, "exp.burn_apply", &Act::Run).unwrap();
        assert_eq!(burn.len(), 3);
        assert_eq!(burn[0].text, "burn_cfg_set 0 duty_pct 25");
        assert_eq!(burn[0].payload, b"burn_cfg_set 0 duty_pct 25");
        assert_eq!(burn[2].text, "burn_cfg_set 0 max_total_current_ma 1000");
        assert_eq!(
            plan(&mut sim, "exp.burn_arm", &Act::Run).unwrap()[0].text,
            "exp_run burn_arm 0"
        );
        assert!(plan(&mut sim, "exp.burn", &Act::Run).is_err());
        sim.exp.burn_armed_until = Some((0, sim.t + 5.0));
        assert_eq!(
            plan(&mut sim, "exp.burn", &Act::Run).unwrap()[0].text,
            "exp_run burn_fire"
        );
        sim.t += 5.0;
        assert!(plan(&mut sim, "exp.burn", &Act::Run).is_err());
    }

    #[test]
    fn burn_status_reads_limits_split_across_firmware_lines() {
        let mut sim = Sim::new();
        let text = "burn state=ARMED active_wire=0\r\nwire0 configured=1 duty_pct=25 burn_time_ms=100 max_total_current_ma=1000\r\nwire0 last=COMPLETED attempt=2 peak_delta_ma=500\r\n";
        let mut payload = vec![0];
        payload.extend_from_slice(&(text.len() as u16).to_le_bytes());
        payload.extend_from_slice(text.as_bytes());
        apply_exp_config_status(&mut sim, "exp_run burn_status", &payload);
        assert!(sim.exp.burn_configured[0]);
        assert!(sim.exp.burn_setup[0].valid());
        assert_eq!(sim.exp.burn_setup[0].burn_time_ms, 100);
        assert_eq!(sim.exp.burn_setup[0].max_total_current_ma, 1000);
        assert_eq!(sim.exp.burn_state, "ARMED");
        assert_eq!(sim.exp.burn_active_wire, Some(0));
        assert_eq!(sim.exp.burn_last[0], "COMPLETED");
        assert_eq!(sim.exp.burn_attempt[0], 2);
        assert_eq!(sim.exp.burn_peak_ma[0], 500);
    }

    #[test]
    fn burn_status_stops_follow_up_when_idle() {
        let mut sim = Sim::new();
        sim.exp.burn_state = "FIRING".into();
        sim.live.burn_verify_until = Some(sim.t + 5.0);
        let text =
            "burn state=IDLE active_wire=-1\r\nwire0 last=COMPLETED attempt=3 peak_delta_ma=0\r\n";
        let mut payload = vec![0];
        payload.extend_from_slice(&(text.len() as u16).to_le_bytes());
        payload.extend_from_slice(text.as_bytes());
        apply_exp_config_status(&mut sim, "exp_run burn_status", &payload);
        assert_eq!(sim.exp.burn_state, "IDLE");
        assert_eq!(sim.exp.burn_last[0], "COMPLETED");
        assert!(sim.live.burn_verify_until.is_none());
    }

    #[test]
    fn motor_settings_use_mcu_commands_for_each_motor() {
        let mut sim = Sim::new();
        sim.exp.mc = 1;
        sim.exp.map_motor = 1;
        sim.exp.motor_max_sps[1] = 500;
        sim.exp.motor_drive_pct[1] = 35;
        sim.exp.motor_pwm[1] = 2;
        sim.live.mode = Mode::Console;
        assert!(is_local("exp.mdrive_pct"));
        assert!(!sim.live_action("exp.mdrive_pct", Act::Run));
        assert_eq!(
            plan(&mut sim, "exp.mmax_apply", &Act::Run).unwrap()[0].text,
            "exp_run motor5_cfg_set 1 max_speed_sps 500"
        );
        assert_eq!(
            plan(&mut sim, "exp.mdrive_apply", &Act::Run).unwrap()[0].text,
            "exp_run motor5_cfg_set 1 drive_pct 35"
        );
        assert_eq!(
            plan(&mut sim, "exp.mpwm_apply", &Act::Run).unwrap()[0].text,
            "exp_run motor5_pwm_freq_set 1 200"
        );
        assert_eq!(
            plan(&mut sim, "exp.mstop", &Act::Run).unwrap()[0].text,
            "rba_ctl stop_hold 1"
        );
        sim.exp.motors[1].holding = true;
        assert_eq!(
            plan(&mut sim, "exp.mstop", &Act::Run).unwrap()[0].text,
            "rba_ctl stop 1"
        );
        sim.exp.motors[1].holding = false;
        sim.exp.msps = 500;
        assert_eq!(
            plan(&mut sim, "exp.mjog", &Act::Run).unwrap()[0].text,
            "exp_run motor5_run 1 cw 500"
        );
        sim.exp.move_steps[1] = 250;
        assert_eq!(
            plan(&mut sim, "exp.mmove", &Act::Run).unwrap()[0].text,
            "exp_run motor5_move 1 250 500"
        );
        sim.exp.mdir = 1;
        assert_eq!(
            plan(&mut sim, "exp.mmove", &Act::Run).unwrap()[0].text,
            "exp_run motor5_move 1 -250 500"
        );
        sim.exp.motor_mode_draft[1] = 1;
        assert_eq!(
            plan(&mut sim, "exp.mmode_apply", &Act::Run).unwrap()[0].text,
            "exp_run motor5_mode_set 1 operation"
        );
        sim.exp.motor_mode[1] = 1;
        assert!(plan(&mut sim, "exp.mmove", &Act::Run).is_err());
        assert!(plan(&mut sim, "exp.mmove_op", &Act::Run).is_err());
        sim.exp.motor_limits_verified[1] = true;
        assert_eq!(
            plan(&mut sim, "exp.mmove_op", &Act::Run).unwrap()[0].text,
            "exp_run motor5_move 1 -250 500"
        );
        assert_eq!(
            plan(&mut sim, "exp.mlimits_apply", &Act::Run).unwrap()[0].text,
            "exp_run motor5_limits_set 1 pml 0 1 min"
        );
        let levels = plan(&mut sim, "exp.mlevels_apply", &Act::Run).unwrap();
        assert_eq!(
            levels[0].text,
            "exp_run limit_cfg_set 1 min active_level low"
        );
        assert_eq!(
            levels[1].text,
            "exp_run limit_cfg_set 1 max active_level low"
        );
        sim.exp.motor_max[1] = 0;
        assert!(plan(&mut sim, "exp.mlimits_apply", &Act::Run).is_err());
    }

    #[test]
    fn motor_settings_readback_updates_selected_motor() {
        let mut sim = Sim::new();
        apply_motor_settings(
            &mut sim,
            "exp_run motor5_cfg_get 1",
            "MOTOR1 mode=MANUAL STOPPED\r\nmax_speed_sps=500 accel_sps2=100 drive_pct=35",
        );
        apply_motor_settings(
            &mut sim,
            "exp_run motor5_pwm_freq_get 1",
            "motor=1 pwm_freq_hz=200 initialized=1",
        );
        apply_motor_settings(&mut sim, "exp_run motor5_limit_get 1",
            "MOTOR1 home=pml1 Light-ON active_HIGH\r\nMOTOR1 min=pml0 Light-ON active_LOW\r\nMOTOR1 max=pml1 Light-ON active_HIGH");
        assert_eq!(sim.exp.motor_max_sps[1], 500);
        assert_eq!(sim.exp.motor_drive_pct[1], 35);
        assert_eq!(sim.exp.motor_mode[1], 0);
        assert!(sim.exp.motor_mode_verified[1]);
        assert_eq!(sim.exp.motor_pwm[1], 2);
        assert_eq!(sim.exp.motor_bank[1], 1);
        assert_eq!(sim.exp.motor_home_end[1], 1);
        assert_eq!(sim.exp.motor_max_level[1], 1);
        assert!(sim.exp.motor_limits_verified[1]);
        assert_eq!(sim.exp.motor_max_sps[0], 100);
        assert_eq!(sim.exp.motor_drive_pct[0], 20);
        sim.exp.motor_mode_draft[1] = 1;
        apply_motor_settings(
            &mut sim,
            "exp_run motor5_cfg_get 1",
            "MOTOR1 mode=OPERATION STOPPED position=0 reference_valid=0",
        );
        assert_eq!(sim.exp.motor_mode[1], 1);
        assert_eq!(sim.exp.motor_mode_draft[1], 1);
    }

    #[test]
    fn motor_duty_draft_does_not_change_display_until_readback() {
        let mut sim = Sim::new();
        sim.exp.mc = 1;
        sim.set_num("exp.mdrive_pct", 35);
        assert_eq!(sim.exp.motor_drive_pct[1], 35);
        assert_eq!(sim.exp.motor_drive_applied_pct[1], 20);
        assert!(sim.exp.motor_drive_dirty[1]);

        apply_motor_settings(
            &mut sim,
            "exp_run motor5_cfg_get 1",
            "MOTOR1 mode=MANUAL STOPPED\r\nmax_speed_sps=100 drive_pct=20",
        );
        assert_eq!(sim.exp.motor_drive_pct[1], 35);
        assert_eq!(sim.exp.motor_drive_applied_pct[1], 20);
        assert!(sim.exp.motor_drive_dirty[1]);

        apply_motor_settings(
            &mut sim,
            "exp_run motor5_cfg_get 1",
            "MOTOR1 mode=MANUAL STOPPED\r\nmax_speed_sps=100 drive_pct=35",
        );
        assert_eq!(sim.exp.motor_drive_pct[1], 35);
        assert_eq!(sim.exp.motor_drive_applied_pct[1], 35);
        assert!(!sim.exp.motor_drive_dirty[1]);
        assert_eq!(sim.exp.motor_drive_applied_pct[0], 20);
    }

    #[test]
    fn mapping_selector_is_independent_of_motion_motor() {
        let mut sim = Sim::new();
        sim.exp.mc = 0;
        sim.exp.map_motor = 1;
        assert_eq!(
            plan(&mut sim, "exp.mlimits_apply", &Act::Run).unwrap()[0].text,
            "exp_run motor5_limits_set 1 pml 0 1 min"
        );
        assert_eq!(
            plan(&mut sim, "exp.mmap_read", &Act::Run).unwrap()[0].text,
            "exp_run motor5_limit_get 1"
        );
        assert_eq!(
            plan(&mut sim, "exp.mmode_read", &Act::Run).unwrap()[0].text,
            "exp_run motor5_cfg_get 0"
        );
    }

    #[test]
    fn ir_status_updates_output_and_configuration_status() {
        let mut sim = Sim::new();
        let text =
            "IR0 active=1 duty_pct=10 remaining_ms=0 pwm_hz=100000 max_duty_pct=20 on_ms=50 set_duty_pct=10\r\n";
        let mut payload = vec![0];
        payload.extend_from_slice(&(text.len() as u16).to_le_bytes());
        payload.extend_from_slice(text.as_bytes());
        apply_exp_config_status(&mut sim, "exp_run ir_status", &payload);
        assert!(sim.exp.ir[0]);
        assert_eq!(sim.exp.ir_actual_duty[0], 10);
        assert!(sim.exp.ir_status_seen[0]);
        assert!(sim.exp.ir_configured[0]);
    }

    #[test]
    fn ir_status_keeps_staged_limits_until_mcu_confirms_them() {
        let mut sim = Sim::new();
        sim.exp.ir_setup[0].max_duty = 20;
        sim.exp.ir_setup[0].on_ms = 50;
        sim.exp.ir_setup[0].pwm_hz = 12345;
        sim.exp.ir_config_pending[0] = Some(0.0);
        let payload = |text: &str| {
            let mut p = vec![0];
            p.extend_from_slice(&(text.len() as u16).to_le_bytes());
            p.extend_from_slice(text.as_bytes());
            p
        };
        let old = payload(
            "IR0 active=0 duty_pct=0 pwm_hz=100000 max_duty_pct=0 on_ms=0 set_duty_pct=0\r\n",
        );
        apply_exp_config_status(&mut sim, "exp_run ir_status", &old);
        assert_eq!(sim.exp.ir_setup[0].pwm_hz, 12345);
        assert!(!sim.exp.ir_configured[0]);
        assert!(sim.exp.ir_config_pending[0].is_some());
        let applied = payload(
            "IR0 active=0 duty_pct=0 pwm_hz=12345 max_duty_pct=20 on_ms=50 set_duty_pct=10\r\n",
        );
        apply_exp_config_status(&mut sim, "exp_run ir_status", &applied);
        assert!(sim.exp.ir_configured[0]);
        assert!(sim.exp.ir_config_pending[0].is_none());
    }
}

#[cfg(test)]
mod exp_snapshot_tests {
    use super::*;

    #[test]
    fn console_snapshot_maps_valid_exp_temperature_and_flow_groups() {
        let mut sim = Sim::new();
        sim.enter_live();
        let sample = Json::parse(
            r#"{"exp":{"online":1,"data_valid":1,
            "sensor_valid_mask":1023,"sensor_status":[2,2,2,2,2,2,2,2,2,2],
            "sensor_age_ms":[1,1,1,1,1,1,1,1,1,1],
            "ntc_c":[25,26,27,28],"board_temp_c":30,"bme_temp_c":23,
            "bme_press_pa":100000,"bme_hum_rh":45,"flow_ul_min":[1234,5678]}}"#,
        )
        .unwrap();
        apply_state(&mut sim, &sample);
        assert_eq!(sim.exp.ntc, [25.0, 26.0, 27.0, 28.0]);
        assert_eq!(sim.exp.board_t, 30.0);
        assert_eq!(sim.exp.bme_t, 23.0);
        assert!((sim.exp.flow[0] - 1.234).abs() < 0.0001);
        assert!((sim.exp.flow[1] - 5.678).abs() < 0.0001);

        let stale = Json::parse(
            r#"{"exp":{"online":1,"data_valid":1,
            "sensor_valid_mask":1023,"sensor_status":[2,2,2,2,2,2,2,2,2,3],
            "sensor_age_ms":[1,1,1,1,1,1,1,1,30001,1],
            "ntc_c":[25,26,27,28],"board_temp_c":30,"bme_temp_c":23,
            "flow_ul_min":[1234,5678]}}"#,
        )
        .unwrap();
        apply_state(&mut sim, &stale);
        assert!(sim.exp.flow[0].is_nan());
        assert!(sim.exp.flow[1].is_nan());
        assert_eq!(sim.exp.ntc[0], 25.0);
        apply_state(&mut sim, &Json::parse(r#"{"exp":null}"#).unwrap());
        assert!(sim.exp.ntc.iter().all(|v| v.is_nan()));
        assert!(sim.exp.board_t.is_nan());
        assert!(sim.exp.bme_t.is_nan());
    }
}

fn exp_group_valid(exp: &Json, group: usize) -> bool {
    let valid = exp.get("sensor_valid_mask").i64().unwrap_or(0);
    valid & (1 << group) != 0
        && exp.get("sensor_status").at(group).i64() == Some(2)
        && exp
            .get("sensor_age_ms")
            .at(group)
            .i64()
            .is_some_and(|age| (0..=30_000).contains(&age))
}

/// `gui_state` (console only): cached EXP sensors and board data.
fn apply_state(s: &mut Sim, j: &Json) {
    s.live.gui_state_age = 0.0;
    s.obc.fsw_up = true;
    let exp = j.get("exp");
    let available =
        exp.get("online").bool() == Some(true) && exp.get("data_valid").bool() == Some(true);
    for i in 0..4 {
        s.exp.ntc[i] = if available && exp_group_valid(exp, i) {
            exp.get("ntc_c").at(i).f64().map_or(f32::NAN, |v| v as f32)
        } else {
            f32::NAN
        };
    }
    s.exp.board_t = if available && exp_group_valid(exp, 4) {
        exp.get("board_temp_c").f64().map_or(f32::NAN, |v| v as f32)
    } else {
        f32::NAN
    };
    if available && exp_group_valid(exp, 5) {
        s.exp.bme_t = exp.get("bme_temp_c").f64().map_or(f32::NAN, |v| v as f32);
        s.exp.bme_p_pa = exp.get("bme_press_pa").i64().unwrap_or(0) as i32;
        s.exp.bme_rh = exp.get("bme_hum_rh").f64().unwrap_or(0.0) as f32;
    } else {
        s.exp.bme_t = f32::NAN;
    }
    for i in 0..2 {
        s.exp.flow[i] = if available && exp_group_valid(exp, i + 8) {
            exp.get("flow_ul_min")
                .at(i)
                .f64()
                .map_or(f32::NAN, |v| (v / 1000.0) as f32)
        } else {
            f32::NAN
        };
    }
    let pdu = j.get("pdu");
    if !pdu.is_null() {
        if let Some(t) = pdu.get("board_temp_c").f64() {
            s.pdu.temp = t as f32;
        }
        s.pdu.mode = match pdu.get("mode").str() {
            Some("NORMAL") => 0,
            Some("MANUAL") => 1,
            Some("WARNING") => 2,
            Some("ERROR") => 3,
            _ => s.pdu.mode,
        };
        s.pdu.mcu_fault = pdu.get("status_flags").i64().unwrap_or(0) != 0;
    }
    if let Some(d) = j.get("camera_led_duty_pct").f64() {
        s.obc.led_pwm = d as i32;
    }
    let m = j.get("mipi");
    if let (Some(b), Some(sn)) = (m.get("board").i64(), m.get("sensor").i64()) {
        s.obc.mipi_route = Some(((b.max(1) as usize - 1).min(1), (sn.max(0) as usize).min(3)));
    }
}

pub fn apply(s: &mut Sim, d: Done) {
    let clock = s.rtc_now();
    // gui_state
    if let Some(st) = d.state {
        match st {
            Ok(j) => {
                apply_state(s, &j);
                s.live.connected = true;
                s.live.record(Txn {
                    clock,
                    why: "poll".into(),
                    cmd: "gui_state".into(),
                    ty: 0,
                    addr: 0,
                    tx: "gui_state".into(),
                    rx: format!(
                        "pdu={} obc={}",
                        if j.get("pdu").is_null() { "null" } else { "ok" },
                        if j.get("obc").is_null() { "null" } else { "ok" }
                    ),
                    ms: 0,
                    ok: true,
                    summary: "JSON snapshot".into(),
                });
            }
            Err(e) => note_link_error(s, "gui_state", &e),
        }
        return;
    }
    let cmd = match d.cmd {
        Some(c) => c,
        None => return,
    };
    let why = match &d.why {
        Why::Poll => "poll".to_string(),
        Why::Refresh => "refresh".to_string(),
        Why::Action(id) => id.clone(),
    };
    match d.result {
        Err(e) => {
            if matches!(&d.why, Why::Action(id) if matches!(id.as_str(), "exp.cam_capture" | "exp.cam_init" | "exp.cam_detect" | "exp.cam_address_set"))
            {
                let camera = cmd
                    .text
                    .split_whitespace()
                    .nth(2)
                    .and_then(|v| v.parse::<usize>().ok())
                    .unwrap_or(0)
                    .min(1);
                s.live.cam_wait[camera] = None;
                s.live.cam_download_wait[camera] = None;
                s.exp.camera[camera].result = e.clone();
            }
            if cmd.text == "exp_csp_snapshot 3" {
                s.exp.ht_status_seen = [false; 4];
                if let Some(id) = s.live.heater_preflight.take() {
                    s.log(
                        Sub::Exp,
                        Level::Err,
                        format!("{id}: heater status refresh failed"),
                    );
                }
            }
            if matches!(&d.why, Why::Action(id) if id == "obc.mipi_capture") {
                s.obc.mipi_shot.state = ShotState::Failed;
                s.obc.mipi_shot.note = e.clone();
            }
            s.live.record(Txn {
                clock,
                why,
                cmd: cmd.text.clone(),
                ty: cmd.ty,
                addr: cmd.addr,
                tx: String::new(),
                rx: String::new(),
                ms: 0,
                ok: false,
                summary: e.clone(),
            });
            if let Why::Action(id) = &d.why {
                s.log(sub_of(id), Level::Err, format!("{}: {e}", cmd.text));
            }
            note_link_error(s, &cmd.text, &e);
        }
        Ok(r) => {
            if r.is_err()
                && matches!(&d.why, Why::Action(id) if matches!(id.as_str(), "exp.cam_capture" | "exp.cam_init" | "exp.cam_detect" | "exp.cam_address_set"))
            {
                let camera = cmd
                    .text
                    .split_whitespace()
                    .nth(2)
                    .and_then(|v| v.parse::<usize>().ok())
                    .unwrap_or(0)
                    .min(1);
                s.live.cam_wait[camera] = None;
                s.live.cam_download_wait[camera] = None;
                s.exp.camera[camera].result = format!("flight error 0x{:02X}", r.code);
            }
            s.live.connected = true;
            s.live.last_err.clear();
            if cmd.text == "exp_csp_snapshot 3" && r.is_err() {
                s.exp.ht_status_seen = [false; 4];
            }
            if cmd.text == "port_cfg get" {
                let mut seen = 0usize;
                for line in r.text.lines() {
                    if let Some((key, value)) = line.split_once('=') {
                        if let Some(i) = crate::ui::cfg::PORT_KEYS.iter().position(|k| *k == key) {
                            s.live.port_values[i] = value.to_string();
                            seen += 1;
                        }
                    }
                }
                s.live.port_loaded = seen == crate::ui::cfg::PORT_KEYS.len();
            } else if cmd.text.starts_with("port_cfg set ") && r.ok {
                s.live.port_restart_pending = true;
                s.live.send(proto::port_cfg("get"), Why::Refresh);
            } else if cmd.text == "port_cfg restart" && r.ok {
                s.live.port_restart_pending = false;
            }
            let mut mcu_error = false;
            let (mut lvl, mut summary) = if r.is_err() {
                (Level::Err, format!("flight error 0x{:02X}", r.code))
            } else if cmd.ty == 0xFE {
                apply_gui_hw(s, &d.why, &cmd, &r)
            } else {
                decode(s, &cmd, &r)
            };
            if r.is_err() && matches!(&d.why, Why::Action(id) if id == "exp.pc_activate") {
                summary.push_str("; check U11 pump type (Pump U11 → Set type) and MCU [PC] log");
            }
            if r.is_err() && r.code == 1 && cmd.ty == 2 && cmd.addr == 4 {
                summary.push_str(
                    "; MCU refused thermal profile control: check EXP 12 V, NTC limits, profile configuration and MCU log",
                );
            }
            if r.is_err() && r.code == 1 && cmd.ty == 1 && cmd.addr == 0x14 {
                summary.push_str(
                    "; MCU refused manual heater control: check profile ownership and heater limits",
                );
            }
            if !r.is_err() && cmd.text == "exp_run pc_status" {
                if let Some((0, report)) = proto::dec_exp_text(&r.payload) {
                    for line in report.lines() {
                        let Some((key, value)) = line.trim().split_once(':') else {
                            continue;
                        };
                        let value = value.trim();
                        match key.trim() {
                            "state" => s.exp.pc_state = value.into(),
                            "error" => s.exp.pc_error = value.into(),
                            "activated" => s.exp.pc_activated = value == "YES",
                            "datalog" => s.exp.pc_datalog = value == "ON",
                            "auto_resume" => s.exp.pc_resume = value == "ON",
                            "log_every" => {
                                if let Some(ms) =
                                    value.split_whitespace().next().and_then(|x| x.parse().ok())
                                {
                                    s.exp.pc_log_ms = ms;
                                }
                            }
                            "case_mode" => {
                                s.exp.pc_case = match value.to_ascii_uppercase().as_str() {
                                    "A" => 1,
                                    "B" => 2,
                                    _ => 0,
                                }
                            }
                            "meas_flow" => {
                                if let Some(flow) =
                                    value.split_whitespace().next().and_then(|x| x.parse().ok())
                                {
                                    s.exp.flow[s.exp.pc_flow_ch] = flow;
                                }
                            }
                            "run_mode" => s.exp.pc_run_mode = value.into(),
                            "volume" => s.exp.pc_volume = value.into(),
                            "remaining" => s.exp.pc_remaining = value.into(),
                            "pump_on" => s.exp.pc_detail = format!("pump_on: {value}"),
                            _ => {}
                        }
                    }
                }
            }
            if !r.is_err() && cmd.text == "exp_run pc_profile_show" {
                if let Some((0, report)) = proto::dec_exp_text(&r.payload) {
                    let mut section = "";
                    for line in report.lines() {
                        let line = line.trim();
                        if line.starts_with('[') && line.ends_with(']') {
                            section = line;
                            continue;
                        }
                        let Some((key, raw)) = line.split_once(':') else {
                            continue;
                        };
                        let key = key.trim();
                        let value = raw.trim();
                        if key == "Name" {
                            s.exp.pc_profile_name = value.into();
                            s.exp.pc_name_applied = Some(value.into());
                            if s.exp.pc_name_dirty {
                                s.exp.pc_name_dirty = s.exp.pc_name_draft != value;
                            } else {
                                s.exp.pc_name_draft = value.into();
                            }
                        }
                        let op = match section {
                            "[Test]" => Some(0),
                            "[Replace]" => Some(1),
                            "[Circulation]" => Some(2),
                            _ => None,
                        };
                        let target = match section {
                            "[Global]" => Some(0),
                            "[Test]" => Some(1),
                            "[Replace]" => Some(2),
                            "[Circulation]" => Some(3),
                            _ => None,
                        };
                        if let Some(target) = target {
                            if let Some(index) =
                                crate::sim::PC_FIELDS.iter().position(|field| *field == key)
                            {
                                let first = value.split_whitespace().next().unwrap_or("");
                                let parsed = match index {
                                    1 | 5 => first
                                        .parse::<f64>()
                                        .ok()
                                        .map(|n| (n * 1000.0).round() as i32),
                                    2 => match first.to_ascii_uppercase().as_str() {
                                        "NONE" => Some(0),
                                        "WARN" | "WARNING" => Some(1),
                                        "STOP" => Some(2),
                                        _ => None,
                                    },
                                    10 => match first.to_ascii_uppercase().as_str() {
                                        "TIME" => Some(0),
                                        "VOLUME" => Some(1),
                                        _ => None,
                                    },
                                    12 | 15 => match first.to_ascii_lowercase().as_str() {
                                        "enabled" | "on" | "yes" => Some(1),
                                        "disabled" | "off" | "no" => Some(0),
                                        _ => first.parse::<i32>().ok(),
                                    },
                                    _ => first.parse::<i32>().ok(),
                                };
                                if let Some(v) = parsed {
                                    sync_pc_field(&mut s.exp, target, index, v);
                                }
                            }
                        }
                        if let Some(i) = op {
                            if key == "minflow" {
                                if let Some(flow) = value
                                    .split_whitespace()
                                    .next()
                                    .and_then(|x| x.parse::<f32>().ok())
                                {
                                    s.exp.pc_minflow_milli[i] = (flow * 1000.0).round() as i32;
                                }
                            } else if key == "flow_chk" {
                                s.exp.pc_flow_check[i] = value.starts_with("enabled");
                                sync_pc_field(
                                    &mut s.exp,
                                    i + 1,
                                    12,
                                    value.starts_with("enabled") as i32,
                                );
                                for token in value.split_whitespace() {
                                    if let Some(v) = token
                                        .strip_prefix("delay=")
                                        .and_then(|x| x.strip_suffix("ms"))
                                        .and_then(|x| x.parse::<i32>().ok())
                                    {
                                        s.exp.pc_flow_delay_ms[i] = v;
                                        sync_pc_field(&mut s.exp, i + 1, 13, v);
                                    } else if let Some(v) = token.strip_prefix("samples=") {
                                        if i < 2 {
                                            if let Ok(n) = v.parse::<i32>() {
                                                s.exp.pc_fail_samples[i] = n;
                                                sync_pc_field(&mut s.exp, i + 1, 14, n);
                                            }
                                        }
                                    } else if let Some(v) = token.strip_prefix("action=") {
                                        s.exp.pc_flow_action[i] = match v {
                                            "NONE" => 0,
                                            "WARNING" => 1,
                                            "STOP" => 2,
                                            _ => s.exp.pc_flow_action[i],
                                        };
                                        let action = s.exp.pc_flow_action[i] as i32;
                                        sync_pc_field(&mut s.exp, i + 1, 2, action);
                                    }
                                }
                            }
                        }
                        match (section, key) {
                            ("[Valve]", "channels") => {
                                let v: Vec<_> = value
                                    .split_whitespace()
                                    .take(4)
                                    .filter_map(|x| x.parse::<usize>().ok())
                                    .collect();
                                if v.len() == 4 {
                                    s.exp.pc_valves.copy_from_slice(&v);
                                }
                            }
                            ("[Valve]", "time delay") => {
                                if let Some(v) =
                                    value.split_whitespace().next().and_then(|x| x.parse().ok())
                                {
                                    s.exp.pc_pulse = v;
                                }
                            }
                            ("[Flow]", "channel") => {
                                if let Some(v) = value.parse::<usize>().ok().filter(|v| *v < 2) {
                                    s.exp.pc_flow_ch = v;
                                }
                            }
                            ("[Global]", "log_every") => {
                                if let Some(v) =
                                    value.split_whitespace().next().and_then(|x| x.parse().ok())
                                {
                                    s.exp.pc_log_ms = v;
                                }
                            }
                            ("[Global]", "run_mode") => s.exp.pc_run_mode = value.into(),
                            ("[Global]", "datalog/resume") => {
                                let flags: Vec<_> = value.split('/').collect();
                                if flags.len() == 2 {
                                    s.exp.pc_datalog = flags[0] == "ON";
                                    s.exp.pc_resume = flags[1] == "ON";
                                }
                            }
                            _ => {}
                        }
                    }
                    s.exp.pc_config_seen = true;
                }
            }
            if !r.is_err() && (cmd.text == "exp_run ir_status" || cmd.text == "exp_run burn_status")
            {
                apply_exp_config_status(s, &cmd.text, &r.payload);
            }
            if !r.is_err() {
                if let Some((0, text)) = proto::dec_exp_text(&r.payload) {
                    if cmd.text == "exp_run pump_type_get" {
                        if text.contains("Pump type: lowdriver") {
                            s.exp.u11.ptype = 0;
                            s.exp.u11.ptype_configured = true;
                        } else if text.contains("Pump type: highdriver") {
                            s.exp.u11.ptype = 1;
                            s.exp.u11.ptype_configured = true;
                        } else if text.contains("Pump type: unconfigured") {
                            s.exp.u11.ptype_configured = false;
                        }
                    }
                    if !text.contains("ERROR") {
                        apply_motor_settings(s, &cmd.text, &text);
                        apply_camera_reply(s, &cmd.text, &text);
                    }
                    if cmd.text.starts_with("exp_run burn_arm ") && text.contains("OK armed") {
                        let wire = cmd
                            .text
                            .split_whitespace()
                            .last()
                            .and_then(|value| value.parse::<usize>().ok())
                            .unwrap_or(s.exp.burn_sel);
                        s.exp.burn_state = "ARMED".into();
                        s.exp.burn_active_wire = Some(wire);
                        s.exp.burn_armed_until = Some((wire, s.t + 5.0));
                    } else if cmd.text == "exp_run burn_fire" && text.contains("OK firing") {
                        s.exp.burn_state = "FIRING".into();
                        s.exp.burn_armed_until = None;
                        s.live.burn_verify_until = Some(s.t + 5.0);
                        s.live.burn_verify_at = s.t + 0.5;
                    } else if cmd.text == "exp_run burn_abort"
                        && text.contains("OK both outputs off")
                    {
                        s.exp.burn_state = "IDLE".into();
                        s.exp.burn_active_wire = None;
                        s.exp.burn_armed_until = None;
                        s.live.burn_verify_until = None;
                    }
                    if matches!(d.why, Why::Action(_)) {
                        if let Some(error) = text
                            .lines()
                            .map(str::trim)
                            .find(|line| line.starts_with("ERROR"))
                        {
                            mcu_error = true;
                            lvl = Level::Warn;
                            summary = error.to_string();
                            if cmd.text.starts_with("exp_run camera_capture ")
                                || cmd.text.starts_with("exp_run camera_init ")
                                || cmd.text.starts_with("exp_run camera_detect ")
                                || cmd.text.starts_with("exp_run camera_address_set ")
                            {
                                let camera = cmd
                                    .text
                                    .split_whitespace()
                                    .last()
                                    .and_then(|v| v.parse::<usize>().ok())
                                    .unwrap_or(0)
                                    .min(1);
                                s.live.cam_wait[camera] = None;
                                s.exp.camera[camera].result = error.to_string();
                            }
                            if cmd.text.starts_with("exp_run burn_arm ") {
                                s.exp.burn_armed_until = None;
                            }
                        }
                        if [
                            "camera_capture",
                            "camera_init",
                            "camera_detect",
                            "camera_status",
                            "camera_address_set",
                        ]
                        .iter()
                        .any(|op| cmd.text.starts_with(&format!("exp_run {op} ")))
                        {
                            if let Some(result) =
                                text.lines().find_map(|line| status_word(line, "result"))
                            {
                                if !matches!(result, "OK" | "QUEUED" | "RUNNING") {
                                    mcu_error = true;
                                    lvl = Level::Warn;
                                    summary = format!("EXP camera {result}");
                                }
                            }
                        }
                    }
                }
            }
            if let Why::Action(id) = &d.why {
                if id.starts_with("exp.heater")
                    && id != "exp.heater_stop"
                    && (cmd.ty, cmd.addr) == (2, 0x0C)
                    && !r.is_err()
                {
                    let channel = idx(id);
                    let confirmed = proto::dec_exp_text(&r.payload).and_then(|(status, text)| {
                        if status == 0 {
                            heater_applied(&text, channel)
                        } else {
                            None
                        }
                    });
                    if let Some(duty) = confirmed {
                        s.exp.heater[channel] = duty as i32;
                        s.live.heater_pending[channel] = Some((duty, s.t));
                    } else {
                        lvl = Level::Warn;
                        summary = format!("heater {channel} duty not confirmed by EXP");
                    }
                }
            }
            if r.is_err() && (cmd.ty, cmd.addr) == (4, 2) {
                // Stale or missing housekeeping snapshot: show n/a rather than the last value.
                let o = &mut s.obc;
                o.temp_onboard = f32::NAN;
                o.ntc = [f32::NAN; 4];
                o.bme_t = f32::NAN;
            }
            if !r.text.is_empty() && matches!(d.why, Why::Action(_)) && !r.is_err() && cmd.ty == 2 {
                summary = format!(
                    "{} | {}",
                    summary,
                    first_line(r.text.split_once('\n').map_or("", |x| x.1))
                );
            }
            if matches!(d.why, Why::Action(_)) && !r.ok && !r.is_err() {
                lvl = Level::Warn;
            }
            s.live.record(Txn {
                clock,
                why: why.clone(),
                cmd: cmd.text.clone(),
                ty: cmd.ty,
                addr: cmd.addr,
                tx: r.tx.clone(),
                rx: r.rx.clone(),
                ms: r.ms,
                ok: !r.is_err() && !mcu_error,
                summary: summary.clone(),
            });
            if !r.is_err() && !mcu_error {
                if let Why::Action(id) = &d.why {
                    match id.as_str() {
                        "exp.pc_datalog" => s.exp.pc_datalog = cmd.payload.get(4) == Some(&1),
                        "exp.pc_resume" => s.exp.pc_resume = cmd.payload.get(4) == Some(&1),
                        "exp.mstop" | "exp.mstop_op" if (cmd.ty, cmd.addr) == (1, 0x1A) => {
                            if let (Some(&op), Some(&motor)) =
                                (cmd.payload.first(), cmd.payload.get(1))
                            {
                                if let Some(mo) = s.exp.motors.get_mut(motor as usize) {
                                    mo.holding = op == 9 && (mo.running || mo.homing);
                                    mo.running = false;
                                    mo.homing = false;
                                    mo.sps = 0;
                                }
                            }
                        }
                        _ => {}
                    }
                    if matches!(
                        id.as_str(),
                        "exp.mjog" | "exp.mmove" | "exp.mmove_op" | "exp.mhome" | "exp.mhome_op"
                    ) {
                        let mo = &mut s.exp.motors[s.exp.mc];
                        mo.holding = false;
                        mo.running = true;
                        mo.homing = matches!(id.as_str(), "exp.mhome" | "exp.mhome_op");
                    }
                    if cmd.ty == 2 && cmd.addr == 2 {
                        s.live.send(proto::exp_run("pc_status"), Why::Refresh);
                    } else if cmd.ty == 2
                        && cmd.addr == 1
                        && !matches!(
                            id.as_str(),
                            "exp.pc_flow_policy_apply" | "exp.pc_profile_apply"
                        )
                    {
                        s.live.send(proto::exp_run("pc_profile_show"), Why::Refresh);
                    }
                }
            }
            if let Why::Action(id) = &d.why {
                if id == "exp.pc_profile_apply" {
                    if !r.is_err() && !mcu_error && cmd.payload.len() >= 8 && cmd.payload[1] == 6 {
                        let target = cmd.payload[2] as usize;
                        let index = cmd.payload[3].saturating_sub(1) as usize;
                        if target == 0 && index == 16 {
                            let bytes = &cmd.payload[4..27];
                            let len = bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len());
                            if let Ok(name) = std::str::from_utf8(&bytes[..len]) {
                                s.exp.pc_name_applied = Some(name.into());
                                s.exp.pc_name_dirty = s.exp.pc_name_draft != name;
                            }
                        } else if target < 4 && index < 16 {
                            let value = i32::from_le_bytes(cmd.payload[4..8].try_into().unwrap());
                            sync_pc_field(&mut s.exp, target, index, value);
                        }
                    }
                    s.exp.pc_apply_pending = s.exp.pc_apply_pending.saturating_sub(1);
                    if s.exp.pc_apply_pending == 0 {
                        s.live.send(proto::exp_run("pc_profile_show"), Why::Refresh);
                    }
                }
            }
            if let Why::Action(id) = &d.why {
                s.log(
                    sub_of(id),
                    lvl,
                    format!("{} -> {summary} ({} ms)", cmd.text, r.ms),
                );
            }
            if cmd.text == "exp_csp_snapshot 3" {
                if let Some(id) = s.live.heater_preflight.take() {
                    let i = (id.as_bytes()[id.len() - 1] - b'0') as usize;
                    if s.exp.ht_status_seen[i] {
                        s.live_action(&id, Act::Run);
                    } else {
                        s.log(
                            Sub::Exp,
                            Level::Err,
                            format!("{id}: heater status unavailable"),
                        );
                    }
                }
            }
        }
    }
}

fn apply_gui_hw(s: &mut Sim, why: &Why, cmd: &Cmd, r: &Reply) -> (Level, String) {
    let id = match why {
        Why::Action(id) => id.as_str(),
        _ => "",
    };
    if id == "obc.mipi_capture" && !r.ok {
        s.obc.mipi_shot.state = ShotState::Failed;
        s.obc.mipi_shot.note = first_line(&r.text);
        return (Level::Err, "MIPI capture or PNG conversion failed".into());
    }
    match id {
        "obc.mipi_power" => {
            let c = cmd
                .text
                .split_whitespace()
                .rev()
                .nth(1)
                .and_then(|v| v.parse::<usize>().ok())
                .and_then(|v| v.checked_sub(1))
                .filter(|&v| v < 2)
                .unwrap_or(s.obc.mipi_sel);
            let on = cmd.text.ends_with(" on");
            s.obc.mipi[c] = on;
            if !on && s.obc.mipi_route.is_some_and(|(board, _)| board == c) {
                s.obc.mipi_route = None;
            }
        }
        "obc.mipi_route" | "obc.mipi_capture" => {
            let mut args = cmd.text.split_whitespace().rev();
            let sn = args
                .next()
                .and_then(|v| v.parse::<usize>().ok())
                .filter(|&v| v < 4)
                .unwrap_or(0);
            let c = args
                .next()
                .and_then(|v| v.parse::<usize>().ok())
                .and_then(|v| v.checked_sub(1))
                .filter(|&v| v < 2)
                .unwrap_or(0);
            s.obc.mipi[c] = true;
            s.obc.mipi_route = Some((c, sn));
            if id == "obc.mipi_capture" {
                let sh = &mut s.obc.mipi_shot;
                if let Some(saved) = r.text.rsplit_once("Saved ").map(|(_, tail)| tail.trim()) {
                    let (path, size) = saved.split_once(" (").unwrap_or((saved, ""));
                    sh.path = path.to_string();
                    sh.bytes = size
                        .split_whitespace()
                        .next()
                        .and_then(|v| v.parse().ok())
                        .unwrap_or(0);
                    sh.state = ShotState::Saved;
                    sh.note = format!("camera {} sensor {}, PNG from BG10 RAW", c + 1, sn);
                } else {
                    sh.state = ShotState::Failed;
                    sh.note = "capture returned no saved file path".into();
                    return (Level::Err, sh.note.clone());
                }
            }
        }
        _ => {}
    }
    (Level::Ok, first_line(&r.text))
}

fn note_link_error(s: &mut Sim, what: &str, e: &str) {
    let first = s.live.connected || s.live.last_err.is_empty();
    s.live.connected = false;
    if s.live.last_err != e {
        s.live.last_err = e.to_string();
        if first {
            s.log(Sub::Obc, Level::Err, format!("{what}: {e}"));
        }
    }
    s.obc.fsw_up = false;
    s.exp.link = false;
    s.pdu.online = false;
}

pub fn default_target(mode: Mode) -> String {
    match mode {
        Mode::Rs422 => {
            std::env::var("DAL_OBC_RS422_SOCKET").unwrap_or_else(|_| DEFAULT_RS422.into())
        }
        _ => std::env::var("DAL_FLIGHT_CONSOLE_SOCKET").unwrap_or_else(|_| DEFAULT_CONSOLE.into()),
    }
}

#[cfg(test)]
mod pc_panel_tests {
    use super::*;
    #[test]
    fn pc_controls_use_native_icd_frames() {
        let mut s = Sim::new();
        s.live.mode = Mode::Rs422;
        let map = plan(&mut s, "exp.pc_map_apply", &Act::Run)
            .unwrap()
            .remove(0);
        assert_eq!((map.ty, map.addr, map.payload.len()), (2, 1, 32));
        assert_eq!(&map.payload[..6], &[1, 1, 0, 1, 2, 3]);
        s.exp.pc_case = 2;
        let run = plan(&mut s, "exp.pc_test", &Act::Run).unwrap().remove(0);
        assert_eq!((run.ty, run.addr), (2, 2));
        assert_eq!(&run.payload[..2], &[3, 2]);
    }
    #[test]
    fn flow_policy_encodes_threshold_action_and_grace_in_pc_cfg() {
        let mut s = Sim::new();
        s.exp.pc_target = 1;
        s.exp.pc_minflow_milli[0] = 12_500;
        s.exp.pc_flow_delay_ms[0] = 800;
        s.exp.pc_fail_samples[0] = 3;
        s.exp.pc_flow_action[0] = 1;
        let cmds = plan(&mut s, "exp.pc_flow_policy_apply", &Act::Run).unwrap();
        assert_eq!(cmds.len(), 5);
        assert_eq!(
            cmds.iter().map(|c| c.payload[3]).collect::<Vec<_>>(),
            [2, 14, 15, 3, 13]
        );
        assert!(cmds
            .iter()
            .all(|c| (c.ty, c.addr, c.payload[2]) == (2, 1, 1)));
        assert_eq!(&cmds[0].payload[4..8], &12500u32.to_le_bytes());
        assert_eq!(cmds[0].text, "pc_cfg profile test minflow 12.500");
        assert_eq!(cmds[3].text, "pc_cfg profile test flow_action warn");
        s.exp.pc_target = 3;
        assert_eq!(
            plan(&mut s, "exp.pc_flow_policy_apply", &Act::Run)
                .unwrap()
                .len(),
            4
        );
        s.exp.pc_target = 0;
        assert!(plan(&mut s, "exp.pc_flow_policy_apply", &Act::Run).is_err());
    }

    #[test]
    fn invalid_pc_map_and_profile_are_rejected_before_send() {
        let mut s = Sim::new();
        s.exp.pc_valves = [0, 0, 2, 3];
        assert!(plan(&mut s, "exp.pc_map_apply", &Act::Run).is_err());
        s.exp.pc_target = 0;
        s.exp.pc_field = 0;
        assert!(plan(&mut s, "exp.pc_profile_apply", &Act::Run).is_err());
    }

    #[test]
    fn profile_apply_sends_only_changed_fields_for_selected_profile() {
        let mut s = Sim::new();
        s.exp.pc_target = 1;
        s.set_num("exp.pc_duration", 12_000);
        s.set_num("exp.pc_minflow", 12_500);
        s.set_idx("exp.pc_flow_action", 1);
        let cmds = plan(&mut s, "exp.pc_profile_apply", &Act::Run).unwrap();
        assert_eq!(
            cmds.iter().map(|c| c.payload[3]).collect::<Vec<_>>(),
            [1, 2, 3]
        );
        assert_eq!(cmds[1].text, "pc_cfg profile test minflow 12.500");
        sync_pc_field(&mut s.exp, 1, 0, 12_000);
        sync_pc_field(&mut s.exp, 1, 1, 12_500);
        let cmds = plan(&mut s, "exp.pc_profile_apply", &Act::Run).unwrap();
        assert_eq!(cmds.len(), 1);
        assert_eq!(cmds[0].text, "pc_cfg profile test flow_action warn");
        s.exp.pc_target = 2;
        assert!(plan(&mut s, "exp.pc_profile_apply", &Act::Run).is_err());
    }

    #[test]
    fn profile_readback_preserves_unapplied_draft() {
        let mut s = Sim::new();
        s.exp.pc_target = 1;
        sync_pc_field(&mut s.exp, 1, 13, 500);
        s.set_num("exp.pc_flow_delay", 800);
        sync_pc_field(&mut s.exp, 1, 13, 500);
        assert_eq!(s.exp.pc_fields[1][13], 800);
        assert!(s.exp.pc_dirty[1][13]);
        sync_pc_field(&mut s.exp, 1, 13, 800);
        assert!(!s.exp.pc_dirty[1][13]);
    }

    #[test]
    fn global_profile_name_is_sent_after_changed_numeric_fields() {
        let mut s = Sim::new();
        s.exp.pc_target = 0;
        s.set_num("exp.pc_pump_off_settle", 800);
        s.stage_pc_name("MISSION_A".into());
        let cmds = plan(&mut s, "exp.pc_profile_apply", &Act::Run).unwrap();
        assert_eq!(
            cmds.iter().map(|c| c.payload[3]).collect::<Vec<_>>(),
            [10, 17]
        );
        assert_eq!(cmds[1].text, "pc_cfg profile global name MISSION_A");
        assert_eq!(&cmds[1].payload[4..14], b"MISSION_A\0");
        s.stage_pc_name("bad name".into());
        assert!(plan(&mut s, "exp.pc_profile_apply", &Act::Run).is_err());
    }

    #[test]
    fn pump_and_volume_settings_are_configurable_for_each_pc_run() {
        let mut s = Sim::new();
        for (target, name) in [(1, "test"), (2, "replace"), (3, "circulation")] {
            s.exp.pc_target = target;
            s.set_num("exp.pc_pump_freq", 240);
            s.set_num("exp.pc_pump_volt", 110);
            s.set_num("exp.pc_volume", 1_500_250);
            let cmds = plan(&mut s, "exp.pc_profile_apply", &Act::Run).unwrap();
            assert_eq!(
                cmds.iter().map(|c| c.payload[3]).collect::<Vec<_>>(),
                [4, 5, 6]
            );
            assert!(cmds
                .iter()
                .all(|c| (c.ty, c.addr, c.payload[2]) == (2, 1, target as u8)));
            assert_eq!(cmds[0].text, format!("pc_cfg profile {name} pump_freq 240"));
            assert_eq!(cmds[1].text, format!("pc_cfg profile {name} pump_volt 110"));
            assert_eq!(
                cmds[2].text,
                format!("pc_cfg profile {name} volume 1500.250")
            );
            assert_eq!(&cmds[2].payload[4..8], &1_500_250u32.to_le_bytes());
        }
        s.exp.pc_target = 1;
        s.set_num("exp.pc_pump_freq", 2_000);
        assert!(plan(&mut s, "exp.pc_profile_apply", &Act::Run).is_err());
    }
}
