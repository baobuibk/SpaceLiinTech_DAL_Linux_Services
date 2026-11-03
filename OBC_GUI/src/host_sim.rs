//! In-process host simulation. No board files, device nodes, or utilities are used.
use crate::{model, Action};
use serde_json::{json, Value};
use std::time::{Duration, Instant};

pub struct HostSim {
    step: u64,
    last_tick: Instant,
    ai_power: bool,
    fpga_power: bool,
    usb_power: [bool; 4],
    pwm_duty: u8,
    rtc_epoch: u32,
    camera: Option<u8>,
    mipi_power: [bool; 2],
    mipi_i2c: [bool; 2],
    mipi_route: Option<(u8, u8)>,
    fault: u8,
}

impl Default for HostSim {
    fn default() -> Self {
        Self {
            step: 0,
            last_tick: Instant::now(),
            ai_power: true,
            fpga_power: true,
            usb_power: [false; 4],
            pwm_duty: 0,
            rtc_epoch: 1792939200,
            camera: None,
            mipi_power: [false; 2],
            mipi_i2c: [false; 2],
            mipi_route: None,
            fault: 0,
        }
    }
}

impl HostSim {
    pub fn cycle_fault(&mut self) -> &'static str {
        self.fault = (self.fault + 1) % 5;
        match self.fault {
            1 => "SIMULATED fault: BME688 sample invalid",
            2 => "SIMULATED fault: PAC U2 unavailable",
            3 => "SIMULATED fault: PDU offline",
            4 => "SIMULATED fault: EXP offline",
            _ => "SIMULATED fault cleared: all sensors valid",
        }
    }
    pub fn advance(&mut self) {
        self.last_tick = Instant::now();
        self.step += 1;
    }
    pub fn tick(&mut self) -> bool {
        if self.last_tick.elapsed() < Duration::from_secs(1) {
            return false;
        }
        self.advance();
        true
    }

    pub fn snapshots(&self) -> (Value, Value, Value) {
        let now = model::unix_now();
        let swing = ((self.step % 20) as f64 - 10.0) * 0.03;
        let obc = json!({
            "board":"obc", "schema_version":1, "online":1,
            "boot_count":42, "obc_timestamp":now,
            "temp_valid":1, "temp_sample_unix_s":now,
            "temp_onboard_c":26.4+swing,
            "temp_ntc_c":[24.1+swing, 25.0+swing, 24.7+swing, 25.3+swing],
            "bme_valid":if self.fault == 1 {0} else {1}, "bme_sample_unix_s":now,
            "bme_temp_c":25.8+swing, "bme_pressure_pa":101325.0,
            "bme_humidity_rh":47.2+swing,
            "bme_gas_valid":if self.fault == 1 {0} else {1}, "bme_gas_resistance_ohm":if self.fault == 1 {None} else {Some(124300.0+(self.step % 10) as f64 * 120.0)},
            "pac_valid":if self.fault == 2 {0} else {1}, "pac_sample_unix_s":now,
            "pac_u6_valid":1, "pac_u2_valid":if self.fault == 2 {0} else {1},
            "pwr_rail_names":model::RAILS,
            "pwr_volt_v":[5.04, 5.02, 3.31, 5.01, 3.30, 1.35, 1.15, 1.15],
            "pwr_curr_a":[0.41, 0.22, 0.16, 0.08, 0.18, 0.11, 0.29, 0.34]
            ,"gpio_states": {
                "pow_onoff_ai":self.ai_power, "pow_onoff_fpga":self.fpga_power,
                "usb_1_pwr_en":self.usb_power[0], "usb_2_pwr_en":self.usb_power[1],
                "usb_3_pwr_en":self.usb_power[2], "usb_4_pwr_en":self.usb_power[3]
                ,"pow_mipicam_1":self.mipi_power[0], "pow_mipicam_2":self.mipi_power[1],
                "en_i2c_mipicam_1":self.mipi_i2c[0], "en_i2c_mipicam_2":self.mipi_i2c[1],
                "sel_mipicam_on2_off1":self.mipi_route.is_some_and(|(board,_)|board==2)
            }, "camera_led_duty_pct":self.pwm_duty, "rtc_epoch":self.rtc_epoch,
            "mipi_route_board":self.mipi_route.map(|(board,_)|board),
            "mipi_route_lane":self.mipi_route.map(|(_,sensor)|sensor),
            "mipi_route_sensor":self.mipi_route.map(|(_,sensor)|sensor)
        });
        let exp = json!({
            "board":"exp", "schema_version":1, "node_addr":11,
            "online":if self.fault == 4 {0} else {1},
            "data_valid":if self.fault == 4 {0} else {1},
            "obc_timestamp":now, "rparam_schema":5,
            "sensor_seq":1000+self.step, "sensor_tick_ms":118000+self.step*1000,
            "sensor_supported_mask":1023, "sensor_valid_mask":if self.fault == 4 {0} else {1023},
            "sensor_status":[0,0,0,0,0,0,0,0,0,0],
            "sensor_age_ms":[100,100,100,100,100,150,150,100,200,200],
            "ntc_c":[23.8+swing,24.0+swing,24.4+swing,24.2+swing],
            "board_temp_c":27.1+swing,
            "bme_temp_c":26.6+swing, "bme_press_pa":101250,
            "bme_hum_rh":44.2,
            "imu_mask":7, "imu_accel_mg":[0,0,1000],
            "imu_gyro_dps":[0.01,0.02,0.0], "imu_temp_c":27.0,
            "flow_ul_min":[0.0,0.0], "flow_temp_c":[25.1,25.2],
            "pwr_rail_names":["12V_MOTOR","12V_EXP","5V_EXP","5V_SLN_HD"],
            "pwr_volt_mv":[12050,12010,5020,5010],
            "pwr_curr_ma":[120,180,260,90], "pwr_mw":[1446,2162,1305,451],
            "rail_enabled":[1,1,1]
        });
        let pdu = json!({
            "board":"pdu", "schema_version":1, "obc_timestamp":if self.fault == 3 {now-60} else {now},
            "board_temp_c":29.2+swing, "mode":"NORMAL", "vout_on":true,
            "trans_on":true, "vout_set_mv":12000, "vout_mv":11985,
            "vout_ma":320, "trans_mv":5020, "trans_ma":180,
            "input_12v_mv":12050, "input_5v_mv":5080, "status_flags":0,
        });
        (obc, exp, pdu)
    }

    pub fn apply(&mut self, action: Action, command: Option<&str>) -> String {
        match action {
            Action::XioList => {
                let (sel0, sel1) = self
                    .camera
                    .map(|n| ((n - 1) & 1, ((n - 1) >> 1) & 1))
                    .unwrap_or((0, 0));
                format!("SIMULATED board GPIO states\npow_onoff_ai  {}\npow_onoff_fpga  {}\nen_switch_usb      {}\nsel_usb_sw0        {sel0}\nsel_usb_sw1        {sel1}",
                    if self.ai_power {"ON"} else {"OFF"},
                    if self.fpga_power {"ON"} else {"OFF"},
                    if self.camera.is_some() {"0 (enabled)"} else {"1 (disabled)"})
            }
            Action::Efuse("pow_onoff_ai", on) => {
                self.ai_power = on;
                format!("SIMULATED: AI eFuse {}", if on { "ON" } else { "OFF" })
            }
            Action::Efuse(_, on) => {
                self.fpga_power = on;
                format!("SIMULATED: FPGA eFuse {}", if on { "ON" } else { "OFF" })
            }
            Action::UsbPower(n, on) => {
                self.usb_power[(n - 1) as usize] = on;
                format!(
                    "SIMULATED: camera {n} USB power {}",
                    if on { "ON" } else { "OFF" }
                )
            }
            Action::SetRtc => match command.unwrap_or("").parse::<u32>() {
                Ok(epoch) if epoch >= 946684800 => {
                    self.rtc_epoch = epoch;
                    format!("SIMULATED: RTC set to epoch {epoch}")
                }
                _ => "RTC epoch must be year 2000 or later".into(),
            },
            Action::CameraLed => match command.unwrap_or("").parse::<u8>() {
                Ok(duty) if duty <= 100 => {
                    self.pwm_duty = duty;
                    format!("SIMULATED: CAMERA_LED PWM duty {duty}%")
                }
                _ => "PWM duty must be 0..100".into(),
            },
            Action::Camera(n) => {
                self.camera = Some(n);
                self.usb_power[(n - 1) as usize] = true;
                let (sel0, sel1) = ((n - 1) & 1, ((n - 1) >> 1) & 1);
                format!("SIMULATED: camera {n} selected (SEL1={sel1}, SEL0={sel0}, OE=0); enumerated; 1280x720 capture simulated. No JPEG file was written.")
            }
            Action::MipiBoard(board) => format!("SIMULATED: MIPI camera {board} chosen"),
            Action::MipiSensor(sensor) => format!("SIMULATED: MIPI sensor {sensor} chosen"),
            Action::MipiPower(board, on) => {
                self.mipi_power[(board - 1) as usize] = on;
                if !on {
                    self.mipi_i2c[(board - 1) as usize] = false;
                }
                format!(
                    "SIMULATED: MIPI camera {board} power {}",
                    if on { "ON" } else { "OFF" }
                )
            }
            Action::MipiApply | Action::MipiCapture => {
                let (board, sensor) = command
                    .unwrap_or("1:0")
                    .split_once(':')
                    .and_then(|(b, s)| Some((b.parse::<u8>().ok()?, s.parse::<u8>().ok()?)))
                    .unwrap_or((1, 0));
                if board < 1 || board > 2 || sensor > 3 {
                    return "SIMULATED: invalid MIPI route".into();
                }
                self.mipi_power[(board - 1) as usize] = true;
                self.mipi_i2c[(board - 1) as usize] = true;
                self.mipi_i2c[(2 - board) as usize] = false;
                self.mipi_route = Some((board, sensor));
                if action == Action::MipiCapture {
                    format!("SIMULATED: MIPI camera {board} sensor {sensor} RAW capture saved virtually; no file was written")
                } else {
                    format!("SIMULATED: MIPI camera {board} sensor {sensor} route applied")
                }
            }
            Action::ExpPing => "SIMULATED: reply in 2 ms from node 11".into(),
            Action::ExpGetall => {
                "SIMULATED: UART tlm_json sensors; schema 2 report returned".into()
            }
            Action::ExpPreset(text) => {
                format!("SIMULATED firmware reply: {text} -> OK")
            }
            Action::ExpRun => format!(
                "SIMULATED firmware reply: {} -> OK",
                command.unwrap_or("command")
            ),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn power_and_camera_actions_change_only_simulated_state() {
        let mut sim = HostSim::default();
        assert_eq!(sim.snapshots().1["data_valid"], 1);
        sim.apply(Action::Efuse("pow_onoff_ai", false), None);
        assert_eq!(sim.snapshots().0["gpio_states"]["pow_onoff_ai"], false);
        assert_eq!(sim.snapshots().1["online"], 1);
        let reply = sim.apply(Action::Camera(3), None);
        assert!(reply.contains("SEL1=1, SEL0=0, OE=0"));
        sim.cycle_fault();
        assert_eq!(sim.snapshots().0["bme_valid"], 0);
        sim.cycle_fault();
        assert_eq!(sim.snapshots().0["pac_u2_valid"], 0);
    }
}
