//! Host simulator: answers CLI commands with replies shaped like the firmware's, so every
//! page can be exercised without hardware. Replies are labelled by the caller as simulated.

pub const W: usize = 32;
pub const H: usize = 24;

pub struct Sim {
    rails: [bool; 3], // SLN/HD, MOTOR, EXP12V
    duty: [u32; 4],
    pump_en: bool,
    motor_pos: [i32; 2],
    motor_run: [bool; 2],
    drv_init: bool,
    next_id: u32,
    buffers: [bool; 2],
    pub frame: Option<Vec<f32>>,
}

impl Sim {
    pub fn new() -> Self {
        Sim {
            rails: [false; 3],
            duty: [0; 4],
            pump_en: false,
            motor_pos: [0; 2],
            motor_run: [false; 2],
            drv_init: false,
            next_id: 1,
            buffers: [false; 2],
            frame: None,
        }
    }

    fn id(&mut self) -> u32 {
        self.next_id += 1;
        self.next_id - 1
    }

    fn rail_lines(&self) -> Vec<String> {
        let s = |b: bool| if b { "ON" } else { "OFF" };
        vec![
            format!("Power SLN HD requested={} observed=UNKNOWN", s(self.rails[0])),
            format!("Power MOTOR requested={} observed=UNKNOWN", s(self.rails[1])),
            format!("Power 12V_EXP requested={} observed=UNKNOWN", s(self.rails[2])),
        ]
    }

    pub fn reply(&mut self, cmd: &str) -> Vec<String> {
        let mut w = cmd.split_whitespace();
        let name = w.next().unwrap_or("");
        let args: Vec<&str> = w.collect();
        let arg = |i: usize| args.get(i).copied().unwrap_or("");
        let idx = |i: usize| arg(i).parse::<usize>().unwrap_or(0);
        match name {
            "help" => vec!["help: simulated command set (see Overview / page buttons)".into()],
            "power_all_get" => self.rail_lines(),
            "power_all_on" | "power_all_off" => {
                self.rails = [name == "power_all_on"; 3];
                self.rail_lines()
            }
            "power_sln_hd_on" | "power_sln_hd_off" | "power_sln_hd_get" => self.rail(0, name),
            "power_motor_on" | "power_motor_off" | "power_motor_get" => self.rail(1, name),
            "power_exp12v_on" | "power_exp12v_off" | "power_exp12v_get" => self.rail(2, name),
            "sensor_status" => vec!["Sensors: NTC OK BME688 OK IMU OK PAC1934 OK FLOW OK".into()],
            "rs485_status" => vec!["RS485: link=UP rx=0 tx=0 (simulated)".into()],
            "pc_status" => vec!["PC state=INACTIVE error=0 (simulated)".into()],
            "pac1934_detect" => {
                let id = self.id();
                vec![format!("PAC1934 QUEUED id={id}")]
            }
            "pac1934_result" => vec![format!("PAC1934 DONE id={}", arg(0))],
            "pac1934_read" => vec![
                "PAC1934 snapshot seq=7 ch1=12.01V/0.050A/0.60W ch2=12.00V/0.020A/0.24W ch3=5.01V/0.100A/0.50W ch4=5.00V/0.010A/0.05W"
                    .into(),
            ],
            "ntc_read_all" => vec![
                "NTC[0] : 25.30 C age=1 ms raw=2048".into(),
                "NTC[1] : 25.10 C age=1 ms raw=2051".into(),
                "NTC[2] : STALE".into(),
                "NTC[3] : INVALID".into(),
                "BOARD_TEMP : 27.0 C".into(),
            ],
            "bme688_init" => {
                let id = self.id();
                vec![format!("BME688 QUEUED id={id}")]
            }
            "bme688_read_all" => vec![
                "[BME688] Temp : 24.8 C".into(),
                "[BME688] Press: 1012.3 hPa".into(),
                "[BME688] Hum  : 41.2 %".into(),
            ],
            "lsm6d_probe" | "lsm6d_init" => vec!["LSM6DSOX DONE id=0 who_am_i=0x6C".into()],
            "lsm6d_read_all" => vec!["IMU accel=0.00,0.00,1.00 g gyro=0.0,0.0,0.0 dps temp=26.0 C".into()],
            "flow_init" | "flow_read_all" => {
                let id = self.id();
                vec![format!("FLOW QUEUED sensor={} id={id}", arg(0))]
            }
            "rtc_ping" => vec!["RTC: OK".into()],
            "rtc_read" => vec!["RTC: 2026-10-27 12:00:00 UTC".into()],
            "rtc_set" => vec!["RTC: OK".into()],
            "pump_type_get" => vec!["Pump type: lowdriver".into()],
            "pump_type_set" => vec![format!("Pump type: {}", arg(0))],
            "pump_detect" | "pump4_detect" | "pump_init" | "pump4_init" => vec![format!("{name}: OK")],
            "pump_enable" | "pump4_enable" => {
                self.pump_en = true;
                vec![format!("{name}: OK")]
            }
            "pump_disable" | "pump4_disable" => {
                self.pump_en = false;
                vec![format!("{name}: OK")]
            }
            "pump_set_freq" | "pump4_set_freq" | "pump_set_volt" | "pump4_set_volt" => {
                vec![format!("{name}: OK {}", args.join(" "))]
            }
            "pump_state" | "pump4_state" | "pump_status" | "pump4_status" => {
                vec![format!("{name}: enabled={} (simulated)", self.pump_en as u8)]
            }
            "heater_set" => {
                let c = idx(0).min(3);
                self.duty[c] = arg(1).parse().unwrap_or(0);
                vec![format!("Heater {c} request accepted duty={}", self.duty[c])]
            }
            "heater_stop" => {
                if arg(0) == "all" {
                    self.duty = [0; 4];
                } else {
                    self.duty[idx(0).min(3)] = 0;
                }
                vec!["Heater stop: OK".into()]
            }
            "heater_get" => {
                let one = |i: usize| vec![format!("Heater[{i}] owner=MANUAL profile=-1"), format!("Heater[{i}] Duty = {}%", self.duty[i])];
                if args.is_empty() {
                    (0..4).flat_map(one).collect()
                } else {
                    one(idx(0).min(3))
                }
            }
            "drv8910_init" => {
                self.drv_init = true;
                vec!["DRV8910 initialized=1 sleep=0 fault=0".into()]
            }
            "drv8910_status" => vec![format!("DRV8910 initialized={} sleep=0 fault=0 diagnostic=0", self.drv_init as u8)],
            "drv8910_fault_get" | "drv8910_fault_clear" => vec!["DRV8910 faults=none".into()],
            "motor5_run" => {
                let m = idx(0).min(1);
                self.motor_run[m] = true;
                let id = self.id();
                vec![format!("ACCEPTED request={id} position={}", self.motor_pos[m])]
            }
            "motor5_home" => {
                let m = idx(0).min(1);
                self.motor_pos[m] = 0;
                let id = self.id();
                vec![format!("ACCEPTED request={id} position=0")]
            }
            "motor5_stop" => {
                self.motor_run = [false; 2];
                vec!["MOTOR stop: OK".into()]
            }
            "motor5_status" | "motor5_limit_get" => (0..2)
                .map(|m| {
                    format!(
                        "MOTOR{m} {} position={} remaining=0",
                        if self.motor_run[m] { "RUNNING" } else { "STOPPED" },
                        self.motor_pos[m]
                    )
                })
                .collect(),
            "limit_status" | "limit_get" | "limit_raw" => (0..4).map(|i| format!("LIMIT {i} active=0 valid=1")).collect(),
            "limit_cfg_set" | "ir_cfg_set" | "burn_cfg_set" => vec![format!("{name}: OK")],
            "ir_set" => {
                let id = self.id();
                vec![format!("IR QUEUED id={id}")]
            }
            "ir_off" | "ir_status" => vec!["IR ch0 idle ch1 idle".into()],
            "ir_result" => vec![format!("IR DONE id={}", arg(0))],
            "camera_buffer_enable" => {
                self.buffers[idx(0).min(1)] = true;
                vec![format!("Camera buffer {} ENABLED", arg(0))]
            }
            "camera_buffer_disable" => {
                self.buffers[idx(0).min(1)] = false;
                vec![format!("Camera buffer {} DISABLED", arg(0))]
            }
            "camera_scan" => vec!["Camera scan ACK: 0x33".into()],
            "camera_detect" | "camera_init" | "camera_config" | "camera_address_set" => {
                vec![format!("{name}: OK camera={}", arg(0))]
            }
            "camera_capture" => {
                self.frame = Some(synthetic_frame());
                vec![format!(
                    "CAMERA camera={} model=MLX90640 address=0x33 format=float32 hz=2 initialized=1 data_exist=1 result=OK object=2",
                    arg(0)
                )]
            }
            "camera_status" => vec![format!(
                "CAMERA camera={} model=MLX90640 address=0x33 format=float32 hz=2 initialized=1 data_exist={}",
                arg(0),
                self.frame.is_some() as u8
            )],
            "bubble_raw" => vec![format!("BUBBLE {} out1=0 out2=0", arg(0))],
            "burn_status" => vec!["BURN idle wire0 pulse_ms=1000 wire1 pulse_ms=1000".into()],
            "burn_arm" | "burn_fire" | "burn_abort" => vec![format!("BURN {name}: OK")],
            "" => vec![],
            _ => vec![format!("Simulator: no model for '{name}' (accepted, no effect)")],
        }
    }

    fn rail(&mut self, i: usize, name: &str) -> Vec<String> {
        if name.ends_with("_on") {
            self.rails[i] = true;
        } else if name.ends_with("_off") {
            self.rails[i] = false;
        }
        let all = self.rail_lines();
        vec![all[i].clone()]
    }
}

/// 32x24 thermal frame: warm blob on a cool background (degrees C).
pub fn synthetic_frame() -> Vec<f32> {
    let mut f = vec![0.0f32; W * H];
    for y in 0..H {
        for x in 0..W {
            let dx = x as f32 - 20.0;
            let dy = y as f32 - 11.0;
            f[y * W + x] = 23.0 + 14.0 * (-(dx * dx + dy * dy) / 40.0).exp();
        }
    }
    f
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rails_follow_commands() {
        let mut s = Sim::new();
        assert!(s.reply("power_motor_on")[0].contains("MOTOR requested=ON"));
        assert!(s.reply("power_all_get")[1].contains("requested=ON"));
        s.reply("power_all_off");
        assert!(s.reply("power_motor_get")[0].contains("requested=OFF"));
    }
    #[test]
    fn capture_produces_frame() {
        let mut s = Sim::new();
        assert!(s.frame.is_none());
        s.reply("camera_capture 1");
        assert_eq!(s.frame.as_ref().unwrap().len(), W * H);
    }
}
