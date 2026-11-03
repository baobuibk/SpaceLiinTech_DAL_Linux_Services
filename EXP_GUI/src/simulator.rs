//! Hardware-free EXP response generator for the terminal UI.
use crate::transport::{Link, Notice, Request};
use std::sync::{
    mpsc::{Receiver, Sender},
    Arc, Mutex,
};

#[derive(Default)]
pub struct State {
    motor_power: bool,
    exp_power: bool,
    sln_power: bool,
}

impl State {
    fn reply(&mut self, command: &str) -> String {
        let command = command.trim();
        match command {
            "power_motor_on" => self.motor_power = true,
            "power_motor_off" => self.motor_power = false,
            "power_exp12v_on" => self.exp_power = true,
            "power_exp12v_off" => self.exp_power = false,
            "power_sln_hd_on" => self.sln_power = true,
            "power_sln_hd_off" => self.sln_power = false,
            _ => {}
        }
        match command {
            "power_all_get" | "power_motor_get" | "power_exp12v_get" | "power_sln_hd_get"
            | "power_motor_on" | "power_motor_off" | "power_exp12v_on"
            | "power_exp12v_off" | "power_sln_hd_on" | "power_sln_hd_off" => format!(
                "SIMULATED power: MOTOR={}  12V_EXP={}  SLN_HD={}\n",
                on_off(self.motor_power), on_off(self.exp_power), on_off(self.sln_power)
            ),
            "tlm_json sensors" => "SIMULATED {\"board_temp_c\":26.8,\"bme_temp_c\":26.3,\"bme_press_pa\":101250,\"bme_hum_rh\":44.2,\"flow0_ul_min\":0.0,\"flow1_ul_min\":0.0}\n".into(),
            "sensor_status" => "SIMULATED sensors: BME688=valid IMU=valid flow0=valid flow1=valid PAC=valid\n".into(),
            "ntc_read_all" => "SIMULATED NTC[0] : 25.1 C\nSIMULATED NTC[1] : 25.4 C\nSIMULATED BOARD_TEMP : 26.8 C\n".into(),
            "bme688_read_all" => "SIMULATED [BME688] Temp : 26.3 C\nSIMULATED [BME688] Press: 101250 Pa\nSIMULATED [BME688] Hum  : 44.2 %RH\n".into(),
            "pc_status" => "SIMULATED PC experiment: IDLE, no active run\n".into(),
            "rtc_read" => "SIMULATED RTC: 2026-10-25 12:00:00 UTC\n".into(),
            "help" => "SIMULATED firmware command response. Browse command sections or type a command.\n".into(),
            _ if command.starts_with("rparam_show ") => format!("SIMULATED {command}: sample table data unavailable without EXP\n"),
            _ => format!("SIMULATED response to '{command}' (no hardware action)\n"),
        }
    }
}

fn on_off(value: bool) -> &'static str {
    if value {
        "ON"
    } else {
        "OFF"
    }
}

pub fn worker(
    _link: Link,
    state: Arc<Mutex<State>>,
    requests: Receiver<Request>,
    notices: Sender<Notice>,
) {
    let _ = notices.send(Notice::Connected(true));
    let _ = notices.send(Notice::Status("SIMULATED: no serial device opened".into()));
    while let Ok(request) = requests.recv() {
        match request {
            Request::Command(command) => {
                let _ = notices.send(Notice::Line(format!("SIMULATED TX > {command}")));
                let reply = state
                    .lock()
                    .expect("simulator state mutex poisoned")
                    .reply(&command);
                let _ = notices.send(Notice::Text(reply));
            }
            Request::Table(table) => {
                let _ = notices.send(Notice::Line(format!(
                    "SIMULATED RPARAM table {table}: no hardware data"
                )));
            }
            Request::Download(_, _) => {
                let _ = notices.send(Notice::DownloadFailed(
                    "SIMULATED mode does not create camera files".into(),
                ));
            }
            Request::Stop => break,
        }
    }
    let _ = notices.send(Notice::Connected(false));
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn power_commands_change_simulated_readback_without_hardware() {
        let mut state = State::default();
        assert!(state.reply("power_all_get").contains("MOTOR=OFF"));
        assert!(state.reply("power_motor_on").contains("MOTOR=ON"));
        assert!(state.reply("power_all_get").contains("MOTOR=ON"));
    }
}
