//! UI state and key handling. No drawing here.

use crate::controls::{Kind, Sub};
use crate::link::Mode;
use crate::sim::{Level, Sim, Val};
use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};
use ratatui::layout::Rect;
use std::time::Instant;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Tab {
    Home,
    Obc,
    Exp,
    Pdu,
    Log,
    Cfg,
}

#[cfg(test)]
mod pwm_editor_tests {
    use super::*;

    #[test]
    fn enter_edits_frequency_and_rejects_out_of_range_value() {
        let mut st = State::new();
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 4;
        let i = Sub::Exp
            .controls(4)
            .iter()
            .position(|c| c.id == "exp.ir_pwm")
            .unwrap();
        st.activate(Sub::Exp, i, 0, false);
        assert_eq!(st.pwm_edit.as_deref(), Some("100000"));
        st.on_key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL));
        for c in "12345".chars() {
            st.on_key(KeyEvent::new(KeyCode::Char(c), KeyModifiers::NONE));
        }
        st.on_key(KeyEvent::new(KeyCode::Enter, KeyModifiers::NONE));
        assert!(st.pwm_edit.is_none());
        assert!(matches!(st.sim.value("exp.ir_pwm"), Val::Num(12345)));

        st.activate(Sub::Exp, i, 0, false);
        st.on_key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL));
        for c in "99".chars() {
            st.on_key(KeyEvent::new(KeyCode::Char(c), KeyModifiers::NONE));
        }
        st.on_key(KeyEvent::new(KeyCode::Enter, KeyModifiers::NONE));
        assert!(st.pwm_edit.is_some());
        assert!(matches!(st.sim.value("exp.ir_pwm"), Val::Num(12345)));
    }
}

#[cfg(test)]
mod motor_number_editor_tests {
    use super::*;

    #[test]
    fn step_count_and_speed_accept_direct_numeric_input() {
        let mut st = State::new();
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 3;
        for (id, value) in [("exp.msteps", "250"), ("exp.msps", "150")] {
            let i = Sub::Exp
                .controls(3)
                .iter()
                .position(|c| c.id == id)
                .unwrap();
            st.activate(Sub::Exp, i, 0, false);
            st.on_key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL));
            for c in value.chars() {
                st.on_key(KeyEvent::new(KeyCode::Char(c), KeyModifiers::NONE));
            }
            st.on_key(KeyEvent::new(KeyCode::Enter, KeyModifiers::NONE));
            assert!(st.num_edit.is_none());
            assert!(matches!(st.sim.value(id), Val::Num(n) if n == value.parse::<i32>().unwrap()));
        }
    }

    #[test]
    fn duty_cycle_typing_replaces_current_value_for_selected_motor() {
        let mut st = State::new();
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 3;
        st.sim.exp.mc = 1;
        let i = Sub::Exp
            .controls(3)
            .iter()
            .position(|c| c.id == "exp.mdrive_pct")
            .unwrap();
        st.activate(Sub::Exp, i, 0, false);
        assert_eq!(
            st.num_edit.as_ref().map(|(_, value)| value.as_str()),
            Some("20")
        );
        for c in "35".chars() {
            st.on_key(KeyEvent::new(KeyCode::Char(c), KeyModifiers::NONE));
        }
        st.on_key(KeyEvent::new(KeyCode::Enter, KeyModifiers::NONE));
        assert!(st.num_edit.is_none());
        assert!(matches!(st.sim.value("exp.mdrive_pct"), Val::Num(35)));
        assert_eq!(st.sim.exp.motor_drive_pct, [20, 35]);
        assert_eq!(st.sim.exp.motor_drive_applied_pct, [20, 20]);
        assert!(st.sim.exp.motor_drive_dirty[1]);
    }

    #[test]
    fn inactive_motor_command_panel_blocks_action() {
        let mut st = State::new();
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 3;
        st.sim.exp.motor_mode[0] = 1;
        let jog = Sub::Exp
            .controls(3)
            .iter()
            .position(|c| c.id == "exp.mjog")
            .unwrap();
        st.activate(Sub::Exp, jog, 0, false);
        assert!(!st.sim.exp.motors[0].running);
        assert!(st
            .log
            .last()
            .is_some_and(|line| line.text.contains("MANUAL")));
    }
}

#[cfg(test)]
mod pc_profile_editor_tests {
    use super::*;

    #[test]
    fn pc_manual_controls_share_operation_panel() {
        for id in [
            "exp.pc_manual_v1",
            "exp.pc_manual_valves_apply",
            "exp.pc_manual_pump_on",
        ] {
            let i = Sub::Exp
                .controls(6)
                .iter()
                .position(|c| c.id == id)
                .unwrap();
            assert_eq!(panel_of(Sub::Exp, 6, i), 1);
        }
    }

    #[test]
    fn target_volume_can_be_entered_above_mcu_default() {
        let mut st = State::new();
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 6;
        let i = Sub::Exp
            .controls(6)
            .iter()
            .position(|c| c.id == "exp.pc_volume")
            .unwrap();
        st.activate(Sub::Exp, i, 0, false);
        st.on_key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL));
        for c in "1500250".chars() {
            st.on_key(KeyEvent::new(KeyCode::Char(c), KeyModifiers::NONE));
        }
        st.on_key(KeyEvent::new(KeyCode::Enter, KeyModifiers::NONE));
        assert!(st.num_edit.is_none());
        assert!(matches!(st.sim.value("exp.pc_volume"), Val::Num(1_500_250)));
        assert!(st.sim.exp.pc_dirty[1][5]);
    }
}

impl Tab {
    pub const ALL: &'static [Tab] = &[Tab::Home, Tab::Obc, Tab::Exp, Tab::Pdu, Tab::Log, Tab::Cfg];

    pub fn label(self) -> &'static str {
        match self {
            Tab::Home => "HOME",
            Tab::Obc => "OBC",
            Tab::Exp => "EXP",
            Tab::Pdu => "PDU",
            Tab::Log => "LOG",
            Tab::Cfg => "CFG",
        }
    }
    pub fn sub(self) -> Option<Sub> {
        match self {
            Tab::Obc => Some(Sub::Obc),
            Tab::Exp => Some(Sub::Exp),
            Tab::Pdu => Some(Sub::Pdu),
            _ => None,
        }
    }
    fn step(self, d: isize) -> Tab {
        let n = Tab::ALL.len() as isize;
        let i = Tab::ALL.iter().position(|&t| t == self).unwrap_or(0) as isize;
        Tab::ALL[((i + d).rem_euclid(n)) as usize]
    }
}

pub struct LogLine {
    pub clock: String,
    pub sub: Sub,
    pub level: Level,
    pub text: String,
}

pub struct State {
    pub tab: Tab,
    pub sim: Sim,
    /// Current page per subsystem.
    pub page: [usize; 3],
    /// Selected control per subsystem and page (index into `Sub::controls`).
    pub sel: [[usize; 9]; 3],
    /// Last selected control in each of the two visible control panels.
    pub panel_sel: [[[usize; 2]; 9]; 3],
    pub confirm: Option<(Sub, usize)>,
    pub log: Vec<LogLine>,
    pub log_scroll: usize,
    pub cfg_sel: usize,
    pub cfg_ports: bool,
    pub cfg_port_sel: usize,
    /// HOME row: 0 = Update Housekeeping mode, 1 = UPDATE button (MANUAL only).
    pub home_sel: usize,
    pub cfg_edit: Option<String>,
    pub rtc_edit: Option<String>,
    pub pc_name_edit: Option<String>,
    pub pwm_edit: Option<String>,
    pub num_edit: Option<(&'static str, String)>,
    pub num_edit_replace: bool,
    pub choice_edit: Option<(&'static str, usize)>,
    pub cfg_scroll: usize,
    pub quit: bool,
    pub last_area: Rect,
    pub started: Instant,
}

const LOG_CAP: usize = 500;

#[cfg(test)]
mod panel_navigation_tests {
    use super::*;

    fn key(code: KeyCode) -> KeyEvent {
        KeyEvent::new(code, KeyModifiers::NONE)
    }

    #[test]
    fn pc_settings_and_operations_keep_independent_selection() {
        let mut st = State::new();
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 6;
        st.on_key(key(KeyCode::Down));
        let saved = st.sel[Sub::Exp.idx()][6];
        st.on_key(key(KeyCode::Char('>')));
        assert_eq!(
            Sub::Exp.controls(6)[st.sel[Sub::Exp.idx()][6]].group,
            "PC operation"
        );
        st.on_key(key(KeyCode::Down));
        assert_eq!(
            Sub::Exp.controls(6)[st.sel[Sub::Exp.idx()][6]].group,
            "PC operation"
        );
        st.on_key(key(KeyCode::Char('<')));
        assert_eq!(st.sel[Sub::Exp.idx()][6], saved);
    }

    #[test]
    fn camera_and_motor_panels_use_the_same_focus_keys() {
        let mut st = State::new();
        st.tab = Tab::Obc;
        st.page[Sub::Obc.idx()] = 1;
        st.on_key(key(KeyCode::Char('>')));
        assert!(st.sel[Sub::Obc.idx()][1] >= 7);
        st.on_key(key(KeyCode::Char('<')));
        assert!(st.sel[Sub::Obc.idx()][1] < 7);
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 3;
        st.on_key(key(KeyCode::Char('>')));
        assert_eq!(panel_of(Sub::Exp, 3, st.sel[Sub::Exp.idx()][3]), 1);
    }

    #[test]
    fn enter_edits_frequency_without_changing_panel() {
        let mut st = State::new();
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 1;
        st.sel[Sub::Exp.idx()][1] = Sub::Exp
            .controls(1)
            .iter()
            .position(|c| c.id == "exp.u11_freq")
            .unwrap();
        st.on_key(key(KeyCode::Enter));
        assert_eq!(
            st.num_edit.as_ref().map(|(id, _)| *id),
            Some("exp.u11_freq")
        );
        st.on_key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL));
        for c in "432".chars() {
            st.on_key(key(KeyCode::Char(c)));
        }
        st.on_key(key(KeyCode::Enter));
        assert!(st.num_edit.is_none());
        assert!(matches!(st.sim.value("exp.u11_freq"), Val::Num(432)));
    }

    #[test]
    fn enter_opens_a_choice_picker() {
        let mut st = State::new();
        st.tab = Tab::Exp;
        st.page[Sub::Exp.idx()] = 1;
        st.sel[Sub::Exp.idx()][1] = Sub::Exp
            .controls(1)
            .iter()
            .position(|c| c.id == "exp.u11_type")
            .unwrap();
        st.on_key(key(KeyCode::Enter));
        assert!(st.choice_edit.is_some());
        st.on_key(key(KeyCode::Down));
        st.on_key(key(KeyCode::Enter));
        assert!(st.choice_edit.is_none());
        assert!(matches!(st.sim.value("exp.u11_type"), Val::Idx(1)));
    }

    #[test]
    fn numeric_editor_accepts_negative_values_within_control_bounds() {
        let mut st = State::new();
        st.tab = Tab::Pdu;
        st.sel[Sub::Pdu.idx()][0] = Sub::Pdu
            .controls(0)
            .iter()
            .position(|c| c.id == "pdu.temp_limit")
            .unwrap();
        st.on_key(key(KeyCode::Enter));
        st.on_key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL));
        for c in "-20".chars() {
            st.on_key(key(KeyCode::Char(c)));
        }
        st.on_key(key(KeyCode::Enter));
        assert!(matches!(st.sim.value("pdu.temp_limit"), Val::Num(-20)));
    }

    #[test]
    fn cfg_poll_interval_uses_enter_and_sections_use_arrows() {
        let mut st = State::new();
        st.tab = Tab::Cfg;
        st.cfg_sel = 3;
        st.on_key(key(KeyCode::Enter));
        st.on_key(KeyEvent::new(KeyCode::Char('u'), KeyModifiers::CONTROL));
        for c in "1.5".chars() {
            st.on_key(key(KeyCode::Char(c)));
        }
        st.on_key(key(KeyCode::Enter));
        assert_eq!(st.sim.live.poll_every, 1.5);
        st.on_key(key(KeyCode::Char('>')));
        assert!(st.cfg_ports);
        st.on_key(key(KeyCode::Char('<')));
        assert!(!st.cfg_ports);
    }
}

pub(crate) fn control_by_id(id: &str) -> Option<&'static crate::controls::Control> {
    for sub in Sub::ALL {
        for page in 0..sub.pages().len() {
            if let Some(control) = sub.controls(page).iter().find(|c| c.id == id) {
                return Some(control);
            }
        }
    }
    None
}

fn panel_count(sub: Sub, page: usize) -> usize {
    if matches!(
        (sub, page),
        (Sub::Obc, 1) | (Sub::Exp, 3) | (Sub::Exp, 6) | (Sub::Exp, 7)
    ) {
        2
    } else {
        1
    }
}

fn panel_of(sub: Sub, page: usize, index: usize) -> usize {
    match (sub, page) {
        (Sub::Obc, 1) => usize::from(index >= 7),
        (Sub::Exp, 3) => usize::from(matches!(
            sub.controls(page)[index].group,
            "Limit mapping" | "Limit polarity"
        )),
        (Sub::Exp, 6) => usize::from(matches!(
            sub.controls(page)[index].group,
            "PC operation" | "Manual valves" | "Manual pump U11"
        )),
        (Sub::Exp, 7) => usize::from(sub.controls(page)[index].group != "Heater manual"),
        _ => 0,
    }
}

fn panel_indices(sub: Sub, page: usize, panel: usize) -> Vec<usize> {
    (0..sub.controls(page).len())
        .filter(|&i| panel_of(sub, page, i) == panel)
        .collect()
}

impl State {
    pub fn new() -> State {
        let mut st = State {
            tab: Tab::Home,
            sim: Sim::new(),
            page: [0; 3],
            sel: [[0; 9]; 3],
            panel_sel: [[[0; 2]; 9]; 3],
            confirm: None,
            log: Vec::new(),
            log_scroll: 0,
            cfg_sel: 0,
            cfg_ports: false,
            cfg_port_sel: 0,
            home_sel: 0,
            cfg_edit: None,
            rtc_edit: None,
            pc_name_edit: None,
            pwm_edit: None,
            num_edit: None,
            num_edit_replace: false,
            choice_edit: None,
            cfg_scroll: 0,
            quit: false,
            last_area: Rect::default(),
            started: Instant::now(),
        };
        st.pump_logs();
        st
    }

    pub fn tick(&mut self, dt: f64) {
        self.sim.tick(dt);
        self.pump_logs();
    }

    fn pump_logs(&mut self) {
        let clock = crate::ui::hms(self.sim.rtc_now());
        for m in self.sim.take_logs() {
            self.log.push(LogLine {
                clock: clock.clone(),
                sub: m.sub,
                level: m.level,
                text: m.text,
            });
        }
        if self.log.len() > LOG_CAP {
            self.log.drain(..self.log.len() - LOG_CAP);
        }
    }

    pub fn status_line(&self) -> (Level, String) {
        match self.log.last() {
            Some(l) => (l.level, format!("[{}] {}", l.sub.name(), l.text)),
            None => (Level::Info, String::new()),
        }
    }

    // ── keys ─────────────────────────────────────────────────────────────────

    /// Switch the data source. Leaving SIM discards the simulated state.
    pub fn set_mode(&mut self, mode: Mode) {
        if mode == self.sim.live.mode {
            return;
        }
        if mode == Mode::Sim {
            let old = &self.sim.live;
            let (c, r, b, p, a, control_only) = (
                old.console_path.clone(),
                old.rs422_path.clone(),
                old.baud,
                old.poll_every,
                old.auto,
                old.control_only,
            );
            self.sim = Sim::new();
            self.sim.live.console_path = c;
            self.sim.live.rs422_path = r;
            self.sim.live.baud = b;
            self.sim.live.poll_every = p;
            self.sim.live.auto = a;
            self.sim.live.control_only = control_only;
        } else if self.sim.live.mode == Mode::Sim {
            self.sim.enter_live();
            self.log.clear();
        }
        self.sim.live.set_mode(mode);
        if mode != Mode::Sim {
            self.sim.exp.motor_mode_verified = [false; 2];
            self.sim.exp.motor_limits_verified = [false; 2];
        }
        let what = if mode == Mode::Sim {
            "simulation".to_string()
        } else {
            format!("{} -> {}", mode.label(), self.sim.live.target())
        };
        self.push_local(Sub::Obc, Level::Info, &format!("data source: {what}"));
    }

    fn cfg_key(&mut self, k: KeyEvent) {
        if self.cfg_ports {
            return self.cfg_port_key(k);
        }
        if let Some(buf) = self.cfg_edit.as_mut() {
            match k.code {
                KeyCode::Esc => self.cfg_edit = None,
                KeyCode::Enter => {
                    let v = self.cfg_edit.take().unwrap_or_default();
                    let v = v.trim().to_string();
                    if !v.is_empty() {
                        if self.cfg_sel == 1 {
                            self.sim.live.console_path = v;
                            self.sim.live.reconnect();
                        } else if self.cfg_sel == 2 {
                            self.sim.live.rs422_path = v;
                            self.sim.live.reconnect();
                        } else if self.cfg_sel == 3 {
                            match v.parse::<f64>() {
                                Ok(seconds) if (0.5..=10.0).contains(&seconds) => {
                                    self.sim.live.poll_every = seconds
                                }
                                _ => {
                                    self.cfg_edit = Some(v);
                                    self.push_local(
                                        Sub::Obc,
                                        Level::Err,
                                        "Poll interval must be 0.5–10 seconds",
                                    );
                                }
                            }
                        }
                    }
                }
                KeyCode::Backspace => {
                    buf.pop();
                }
                KeyCode::Char('u') if k.modifiers.contains(KeyModifiers::CONTROL) => buf.clear(),
                KeyCode::Char(c) => buf.push(c),
                _ => {}
            }
            return;
        }
        let step = |m: Mode, d: isize| {
            Mode::ALL[((Mode::ALL.iter().position(|&x| x == m).unwrap_or(0) as isize + d)
                .rem_euclid(3)) as usize]
        };
        match k.code {
            KeyCode::Char('p')
            | KeyCode::Char('>')
            | KeyCode::Char('<')
            | KeyCode::Right
            | KeyCode::Left
            | KeyCode::Char('h')
            | KeyCode::Char('l') => {
                self.cfg_ports = true;
                self.read_ports();
            }
            KeyCode::Up | KeyCode::Char('k') => self.cfg_sel = (self.cfg_sel + 5) % 6,
            KeyCode::Down | KeyCode::Char('j') => self.cfg_sel = (self.cfg_sel + 1) % 6,
            KeyCode::PageUp => {
                self.cfg_scroll =
                    (self.cfg_scroll + 5).min(self.sim.live.hist.len().saturating_sub(1))
            }
            KeyCode::PageDown => self.cfg_scroll = self.cfg_scroll.saturating_sub(5),
            KeyCode::Enter | KeyCode::Char(' ') => match self.cfg_sel {
                0 => self.set_mode(step(self.sim.live.mode, 1)),
                1 | 2 => {
                    self.cfg_edit = Some(if self.cfg_sel == 1 {
                        self.sim.live.console_path.clone()
                    } else {
                        self.sim.live.rs422_path.clone()
                    });
                }
                3 => self.cfg_edit = Some(format!("{:.1}", self.sim.live.poll_every)),
                4 => {
                    self.sim.live.reconnect();
                    self.push_local(Sub::Obc, Level::Info, "link re-opened");
                }
                5 => {
                    self.sim.live.hist.clear();
                    self.cfg_scroll = 0;
                }
                _ => {}
            },
            _ => {}
        }
    }

    fn read_ports(&mut self) {
        if self.sim.live.mode == Mode::Console {
            self.sim.live.send(
                crate::proto::port_cfg("get"),
                crate::link::Why::Action("cfg.ports".into()),
            );
        } else {
            self.push_local(
                Sub::Obc,
                Level::Warn,
                "Flight ports require the local CONSOLE link",
            );
        }
    }

    fn cfg_port_key(&mut self, k: KeyEvent) {
        use crate::ui::cfg::PORT_KEYS;
        if let Some(buf) = self.cfg_edit.as_mut() {
            match k.code {
                KeyCode::Esc => self.cfg_edit = None,
                KeyCode::Backspace => {
                    buf.pop();
                }
                KeyCode::Char('u') if k.modifiers.contains(KeyModifiers::CONTROL) => buf.clear(),
                KeyCode::Char(c) if buf.len() < 127 => buf.push(c),
                KeyCode::Enter => {
                    let value = self.cfg_edit.take().unwrap_or_default().trim().to_string();
                    let i = self.cfg_port_sel;
                    let valid = if matches!(i, 0 | 1 | 3 | 5) {
                        value.starts_with("/dev/")
                            && value.len() > 5
                            && value.len() <= 127
                            && value[5..].chars().all(|c| {
                                c.is_ascii_alphanumeric() || matches!(c, '/' | '_' | '-' | '.')
                            })
                    } else {
                        let range = match i {
                            2 => 9600..=3_000_000,
                            6 => 9600..=1_000_000,
                            4 => 1..=247,
                            7 => 1..=30,
                            _ => 0..=1,
                        };
                        value
                            .parse::<u32>()
                            .ok()
                            .is_some_and(|n| range.contains(&n))
                    };
                    if !valid {
                        self.push_local(
                            Sub::Obc,
                            Level::Err,
                            "Invalid port value; use /dev/... or a value in the shown range",
                        );
                    } else if self.sim.live.mode == Mode::Console {
                        self.sim.live.send(
                            crate::proto::port_cfg(&format!("set {} {}", PORT_KEYS[i], value)),
                            crate::link::Why::Action("cfg.ports".into()),
                        );
                    } else {
                        self.push_local(
                            Sub::Obc,
                            Level::Warn,
                            "Switch to CONSOLE before saving flight ports",
                        );
                    }
                }
                _ => {}
            }
            return;
        }
        match k.code {
            KeyCode::Char('p')
            | KeyCode::Esc
            | KeyCode::Char('>')
            | KeyCode::Char('<')
            | KeyCode::Right
            | KeyCode::Left
            | KeyCode::Char('h')
            | KeyCode::Char('l') => self.cfg_ports = false,
            KeyCode::Up | KeyCode::Char('k') => self.cfg_port_sel = (self.cfg_port_sel + 10) % 11,
            KeyCode::Down | KeyCode::Char('j') => self.cfg_port_sel = (self.cfg_port_sel + 1) % 11,
            KeyCode::Enter => match self.cfg_port_sel {
                0..=8 if self.sim.live.mode == Mode::Console && self.sim.live.port_loaded => {
                    self.cfg_edit = Some(self.sim.live.port_values[self.cfg_port_sel].clone())
                }
                0..=8 if self.sim.live.mode == Mode::Console => {
                    self.push_local(Sub::Obc, Level::Warn, "Read saved settings before editing")
                }
                0..=8 => self.push_local(
                    Sub::Obc,
                    Level::Warn,
                    "Switch to CONSOLE to edit flight ports",
                ),
                9 => self.read_ports(),
                10 if self.sim.live.mode == Mode::Console => self.sim.live.send(
                    crate::proto::port_cfg("restart"),
                    crate::link::Why::Action("cfg.ports".into()),
                ),
                _ => self.push_local(
                    Sub::Obc,
                    Level::Warn,
                    "Switch to CONSOLE to restart flight software",
                ),
            },
            _ => {}
        }
    }

    /// UPDATE: one housekeeping round now (live sources only).
    pub fn update_now(&mut self) {
        if self.sim.live.control_only && self.sim.live.mode != Mode::Sim {
            self.push_local(
                Sub::Obc,
                Level::Info,
                "control-only mode: housekeeping reads are disabled",
            );
        } else if self.sim.live.mode == Mode::Sim {
            self.push_local(
                Sub::Obc,
                Level::Info,
                "simulator: nothing to update, pick CONSOLE or RS-422 on CFG",
            );
        } else {
            self.sim.live.request_update();
            self.push_local(Sub::Obc, Level::Info, "housekeeping update requested");
        }
    }

    fn set_auto(&mut self, auto: bool) {
        if self.sim.live.control_only {
            self.push_local(
                Sub::Obc,
                Level::Info,
                "control-only mode: restart with --poll or --manual to enable reads",
            );
            return;
        }
        if auto != self.sim.live.auto {
            self.sim.live.set_auto(auto);
            self.home_sel = 0;
            self.push_local(
                Sub::Obc,
                Level::Info,
                if auto {
                    "housekeeping update: AUTO"
                } else {
                    "housekeeping update: MANUAL (press UPDATE or u)"
                },
            );
        }
    }

    fn home_key(&mut self, code: KeyCode) {
        let rows = if self.sim.live.auto || self.sim.live.control_only {
            1
        } else {
            2
        };
        match code {
            KeyCode::Up | KeyCode::Char('k') => self.home_sel = (self.home_sel + rows - 1) % rows,
            KeyCode::Down | KeyCode::Char('j') => self.home_sel = (self.home_sel + 1) % rows,
            KeyCode::Left | KeyCode::Char('h') if self.home_sel == 0 => self.set_auto(true),
            KeyCode::Right | KeyCode::Char('l') if self.home_sel == 0 => self.set_auto(false),
            KeyCode::Enter | KeyCode::Char(' ') if self.home_sel == 0 => {
                let auto = !self.sim.live.auto;
                self.set_auto(auto);
            }
            KeyCode::Enter | KeyCode::Char(' ') => self.update_now(),
            _ => {}
        }
    }

    pub fn on_key(&mut self, k: KeyEvent) {
        if let Some(buf) = self.pc_name_edit.as_mut() {
            match k.code {
                KeyCode::Esc => self.pc_name_edit = None,
                KeyCode::Enter => {
                    let name = buf.clone();
                    if name.is_empty() {
                        self.push_local(Sub::Exp, Level::Err, "profile name cannot be empty");
                    } else {
                        self.pc_name_edit = None;
                        self.sim.stage_pc_name(name);
                    }
                }
                KeyCode::Backspace => {
                    buf.pop();
                }
                KeyCode::Char('u') if k.modifiers.contains(KeyModifiers::CONTROL) => buf.clear(),
                KeyCode::Char(c) if c.is_ascii_graphic() && buf.len() < 23 => buf.push(c),
                _ => {}
            }
            return;
        }
        if let Some((id, buf)) = self.num_edit.as_mut() {
            let id = *id;
            match k.code {
                KeyCode::Esc => self.num_edit = None,
                KeyCode::Enter => {
                    let (min, max) = match control_by_id(id).map(|c| &c.kind) {
                        Some(Kind::Step { min, max, .. }) => (*min, *max),
                        _ => return,
                    };
                    match buf.parse::<i32>() {
                        Ok(value) if (min..=max).contains(&value) => {
                            self.num_edit = None;
                            self.sim.set_num(id, value);
                            self.pump_logs();
                        }
                        _ => self.push_local(
                            self.tab.sub().unwrap_or(Sub::Exp),
                            Level::Err,
                            &format!("enter a value from {min} to {max}"),
                        ),
                    }
                }
                KeyCode::Backspace => {
                    if self.num_edit_replace {
                        buf.clear();
                        self.num_edit_replace = false;
                    } else {
                        buf.pop();
                    }
                }
                KeyCode::Char('u') if k.modifiers.contains(KeyModifiers::CONTROL) => {
                    buf.clear();
                    self.num_edit_replace = false;
                }
                KeyCode::Char('-') if buf.is_empty() || self.num_edit_replace => {
                    if self.num_edit_replace {
                        buf.clear();
                        self.num_edit_replace = false;
                    }
                    buf.push('-');
                }
                KeyCode::Char(c) if c.is_ascii_digit() => {
                    if self.num_edit_replace {
                        buf.clear();
                        self.num_edit_replace = false;
                    }
                    if buf.len() < 11 {
                        buf.push(c);
                    }
                }
                _ => {}
            }
            return;
        }
        if let Some((id, draft)) = self.choice_edit.as_mut() {
            let id = *id;
            let options = match control_by_id(id).map(|c| &c.kind) {
                Some(Kind::Choice(options)) => *options,
                _ => return,
            };
            match k.code {
                KeyCode::Esc => self.choice_edit = None,
                KeyCode::Up | KeyCode::Char('k') | KeyCode::Left | KeyCode::Char('h') => {
                    *draft = (*draft + options.len() - 1) % options.len()
                }
                KeyCode::Down | KeyCode::Char('j') | KeyCode::Right | KeyCode::Char('l') => {
                    *draft = (*draft + 1) % options.len()
                }
                KeyCode::Enter | KeyCode::Char(' ') => {
                    let value = *draft;
                    self.choice_edit = None;
                    self.sim.set_idx(id, value);
                    if self.sim.live.mode != Mode::Sim {
                        let command = match id {
                            "exp.mc" => Some(format!("motor5_cfg_get {}", self.sim.exp.mc)),
                            "exp.map_motor" => {
                                Some(format!("motor5_limit_get {}", self.sim.exp.map_motor))
                            }
                            _ => None,
                        };
                        if let Some(command) = command {
                            self.sim
                                .live
                                .send(crate::proto::exp_run(&command), crate::link::Why::Refresh);
                        }
                    }
                    self.pump_logs();
                }
                _ => {}
            }
            return;
        }
        if let Some(buf) = self.pwm_edit.as_mut() {
            match k.code {
                KeyCode::Esc => self.pwm_edit = None,
                KeyCode::Enter => match buf.parse::<i32>() {
                    Ok(hz) if (100..=100_000).contains(&hz) => {
                        self.pwm_edit = None;
                        self.sim.set_num("exp.ir_pwm", hz);
                        self.pump_logs();
                    }
                    _ => {
                        self.push_local(Sub::Exp, Level::Err, "PWM frequency must be 100–100000 Hz")
                    }
                },
                KeyCode::Backspace => {
                    buf.pop();
                }
                KeyCode::Char('u') if k.modifiers.contains(KeyModifiers::CONTROL) => buf.clear(),
                KeyCode::Char(c) if c.is_ascii_digit() && buf.len() < 6 => buf.push(c),
                _ => {}
            }
            return;
        }
        if let Some(buf) = self.rtc_edit.as_mut() {
            match k.code {
                KeyCode::Esc => self.rtc_edit = None,
                KeyCode::Enter => match crate::sim::parse_utc(buf) {
                    Ok(epoch) => {
                        self.rtc_edit = None;
                        self.sim.set_rtc(epoch);
                        self.pump_logs();
                    }
                    Err(reason) => self.push_local(Sub::Obc, Level::Err, reason),
                },
                KeyCode::Backspace => {
                    buf.pop();
                }
                KeyCode::Char('u') if k.modifiers.contains(KeyModifiers::CONTROL) => buf.clear(),
                KeyCode::Char(c) if c.is_ascii_digit() || matches!(c, '-' | ':' | ' ') => {
                    if buf.len() < 19 {
                        buf.push(c);
                    }
                }
                _ => {}
            }
            return;
        }
        if self.tab == Tab::Cfg && self.cfg_edit.is_some() {
            return self.cfg_key(k);
        }
        if let Some((sub, i)) = self.confirm.take() {
            if matches!(k.code, KeyCode::Char('y') | KeyCode::Char('Y')) {
                let id = sub.controls(self.page[sub.idx()])[i].id;
                self.sim.run(id);
                self.pump_logs();
            } else {
                self.push_local(sub, Level::Info, "cancelled");
            }
            return;
        }

        let coarse = k.modifiers.contains(KeyModifiers::SHIFT);
        match k.code {
            KeyCode::Char('q') | KeyCode::Char('Q') => self.quit = true,
            KeyCode::Char('c') if k.modifiers.contains(KeyModifiers::CONTROL) => self.quit = true,
            KeyCode::Tab | KeyCode::Char(']') => self.tab = self.tab.step(1),
            KeyCode::BackTab | KeyCode::Char('[') => self.tab = self.tab.step(-1),
            KeyCode::Char(c @ '1'..='6') => self.tab = Tab::ALL[c as usize - '1' as usize],
            KeyCode::Char('u') | KeyCode::Char('U') => self.update_now(),
            KeyCode::Char('p') if self.sim.live.mode == Mode::Sim && self.tab != Tab::Cfg => {
                self.sim.paused = !self.sim.paused;
                let msg = if self.sim.paused {
                    "simulation paused"
                } else {
                    "simulation resumed"
                };
                self.push_local(Sub::Obc, Level::Info, msg);
            }
            _ => match self.tab {
                Tab::Log => self.log_key(k.code),
                Tab::Cfg => self.cfg_key(k),
                Tab::Home => self.home_key(k.code),
                _ => self.control_key(k.code, coarse),
            },
        }
    }

    pub fn note(&mut self, text: &str) {
        self.push_local(Sub::Obc, Level::Info, text);
    }

    fn push_local(&mut self, sub: Sub, level: Level, text: &str) {
        let clock = crate::ui::hms(self.sim.rtc_now());
        self.log.push(LogLine {
            clock,
            sub,
            level,
            text: text.into(),
        });
    }

    fn log_key(&mut self, code: KeyCode) {
        let max = self.log.len().saturating_sub(1);
        match code {
            KeyCode::Up | KeyCode::Char('k') => self.log_scroll = (self.log_scroll + 1).min(max),
            KeyCode::Down | KeyCode::Char('j') => {
                self.log_scroll = self.log_scroll.saturating_sub(1)
            }
            KeyCode::PageUp => self.log_scroll = (self.log_scroll + 10).min(max),
            KeyCode::PageDown => self.log_scroll = self.log_scroll.saturating_sub(10),
            KeyCode::Char('g') | KeyCode::Home => self.log_scroll = max,
            KeyCode::Char('G') | KeyCode::End => self.log_scroll = 0,
            KeyCode::Char('c') => {
                self.log.clear();
                self.log_scroll = 0;
            }
            _ => {}
        }
    }

    fn control_key(&mut self, code: KeyCode, _coarse: bool) {
        let Some(sub) = self.tab.sub() else { return };
        let si = sub.idx();
        let np = sub.pages().len() as isize;
        let pg = self.page[si];
        let cur = self.sel[si][pg];
        let panel = panel_of(sub, pg, cur);
        let indices = panel_indices(sub, pg, panel);
        let pos = indices.iter().position(|&i| i == cur).unwrap_or(0);
        match code {
            KeyCode::Char('.') => self.page[si] = (pg as isize + 1).rem_euclid(np) as usize,
            KeyCode::Char(',') => self.page[si] = (pg as isize - 1).rem_euclid(np) as usize,
            KeyCode::Char('>') | KeyCode::Right | KeyCode::Char('l') | KeyCode::Char('L') => {
                self.move_panel(sub, 1)
            }
            KeyCode::Char('<') | KeyCode::Left | KeyCode::Char('h') | KeyCode::Char('H') => {
                self.move_panel(sub, -1)
            }
            KeyCode::Up | KeyCode::Char('k') => {
                self.sel[si][pg] = indices[(pos + indices.len() - 1) % indices.len()]
            }
            KeyCode::Down | KeyCode::Char('j') => {
                self.sel[si][pg] = indices[(pos + 1) % indices.len()]
            }
            KeyCode::Home | KeyCode::Char('g') => self.sel[si][pg] = indices[0],
            KeyCode::End | KeyCode::Char('G') => self.sel[si][pg] = *indices.last().unwrap(),
            KeyCode::Char('f') | KeyCode::Char('F') => {
                let on = !self.sim.inject[si];
                self.sim.inject[si] = on;
                let what = match sub {
                    Sub::Obc => "over-temperature on NTC1",
                    Sub::Exp => "CAN link loss",
                    Sub::Pdu => "TRANS eFuse fault",
                };
                let msg = format!("fault injection {}: {what}", if on { "ON" } else { "OFF" });
                self.push_local(sub, Level::Warn, &msg);
            }
            KeyCode::Enter | KeyCode::Char(' ') => self.activate(sub, cur, 0, false),
            _ => return,
        }
        let new_page = self.page[si];
        if new_page == pg {
            let selected = self.sel[si][pg];
            let selected_panel = panel_of(sub, pg, selected);
            self.panel_sel[si][pg][selected_panel] = selected;
        }
        if sub == Sub::Exp && self.page[si] != pg && self.sim.live.mode != Mode::Sim {
            let status = match self.page[si] {
                3 => Some(format!("motor5_cfg_get {}", self.sim.exp.mc)),
                4 => Some("ir_status".to_string()),
                5 => Some("burn_status".to_string()),
                6 => Some("pc_status".to_string()),
                7 => None,
                _ => None,
            };
            if let Some(command) = status {
                self.sim
                    .live
                    .send(crate::proto::exp_run(&command), crate::link::Why::Refresh);
            }
            if self.page[si] == 6 {
                self.sim
                    .live
                    .send(crate::proto::fld_stat(), crate::link::Why::Refresh);
                self.sim.live.send(
                    crate::proto::exp_run("pc_profile_show"),
                    crate::link::Why::Refresh,
                );
            }
            if self.page[si] == 3 {
                for motor in 0..2 {
                    self.sim.live.send(
                        crate::proto::exp_run(&format!("motor5_limit_get {motor}")),
                        crate::link::Why::Refresh,
                    );
                }
            }
        }
        self.pump_logs();
    }

    fn move_panel(&mut self, sub: Sub, direction: isize) {
        let si = sub.idx();
        let page = self.page[si];
        let count = panel_count(sub, page);
        let current = panel_of(sub, page, self.sel[si][page]);
        let next = current as isize + direction;
        if next < 0 || next >= count as isize {
            self.page[si] =
                (page as isize + direction).rem_euclid(sub.pages().len() as isize) as usize;
            return;
        }
        self.panel_sel[si][page][current] = self.sel[si][page];
        let next = next as usize;
        let indices = panel_indices(sub, page, next);
        let saved = self.panel_sel[si][page][next];
        self.sel[si][page] = if indices.contains(&saved) {
            saved
        } else {
            indices[0]
        };
    }

    /// Enter or Space activates the selected control; navigation never edits it.
    fn activate(&mut self, sub: Sub, i: usize, _dir: i32, _coarse: bool) {
        let c = &sub.controls(self.page[sub.idx()])[i];
        if let Some(reason) = crate::live::motor_group_reason(&self.sim, c.group) {
            self.push_local(Sub::Exp, Level::Warn, reason);
            return;
        }
        match &c.kind {
            Kind::Toggle => {
                if let Val::Bool(b) = self.sim.value(c.id) {
                    self.sim.set_bool(c.id, !b);
                }
            }
            Kind::Step { .. } => {
                if let Val::Num(v) = self.sim.value(c.id) {
                    if c.id == "exp.ir_pwm" {
                        self.pwm_edit = Some(v.to_string());
                        return;
                    }
                    self.num_edit = Some((c.id, v.to_string()));
                    self.num_edit_replace = true;
                }
            }
            Kind::Choice(_) => {
                if let Val::Idx(v) = self.sim.value(c.id) {
                    self.choice_edit = Some((c.id, v));
                }
            }
            Kind::Action => {
                if c.id == "obc.rtc_set" {
                    self.rtc_edit = Some(crate::sim::civil(self.sim.rtc_now()));
                } else if c.id == "exp.pc_name" {
                    if self.sim.exp.pc_target == 0 {
                        self.pc_name_edit = Some(self.sim.exp.pc_name_draft.clone());
                    } else {
                        self.push_local(
                            Sub::Exp,
                            Level::Warn,
                            "select Global profile to edit its name",
                        );
                    }
                } else {
                    self.sim.run(c.id);
                }
            }
            Kind::Danger => {
                self.confirm = Some((sub, i));
            }
        }
    }
}
