use crate::{panel_frame, title};
use eframe::egui;

pub struct MotorPanel {
    selected: usize,
    min_limit: [u8; 2],
    max_limit: [u8; 2],
    home_at_max: [bool; 2],
    speed_sps: u16,
    result_id: u32,
}

impl Default for MotorPanel {
    fn default() -> Self {
        Self {
            selected: 0,
            min_limit: [0, 2],
            max_limit: [1, 3],
            home_at_max: [false, false],
            speed_sps: 20,
            result_id: 1,
        }
    }
}

fn action(ui: &mut egui::Ui, commands: &mut Vec<String>, label: &str, command: String) {
    if ui.button(label).clicked() {
        commands.push(command);
    }
}

impl MotorPanel {
    pub fn show(
        &mut self,
        ui: &mut egui::Ui,
        status: &crate::motor_telemetry::Telemetry,
    ) -> Vec<String> {
        let mut commands = Vec::new();
        ui.label(title("MOTOR CONTROL / DRV8910"));
        ui.label(
            "Jog continues until STOP. Homing stops when its assigned optical limit activates.",
        );
        ui.horizontal(|ui| {
            ui.selectable_value(&mut self.selected, 0, "Motor 0 / J13");
            ui.selectable_value(&mut self.selected, 1, "Motor 1 / J14");
        });
        let motor = self.selected;
        status.show(ui);

        ui.horizontal_wrapped(|ui| {
            action(ui, &mut commands, "Refresh driver", "drv8910_status".into());
            action(
                ui,
                &mut commands,
                "Refresh motors",
                "motor5_status all".into(),
            );
        });

        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.strong(format!("MOTOR {motor} / DRIVER"));
            ui.horizontal_wrapped(|ui| {
                for (label, command) in [
                    ("Motor power ON", "power_motor_on"),
                    ("Initialize driver", "drv8910_init"),
                    ("Driver status", "drv8910_status"),
                    ("Fault status", "drv8910_fault_get"),
                    ("Clear faults", "drv8910_fault_clear"),
                    ("Motor power status", "power_motor_get"),
                    ("Motor power OFF", "power_motor_off"),
                ] {
                    action(ui, &mut commands, label, command.into());
                }
            });

            ui.separator();
            ui.strong("MODE");
            let reported_mode = status.motors[motor]
                .text
                .split_whitespace()
                .find_map(|field| field.strip_prefix("mode="))
                .unwrap_or("UNKNOWN");
            ui.label(format!("Reported mode: {reported_mode}"));
            ui.horizontal_wrapped(|ui| {
                action(
                    ui,
                    &mut commands,
                    "Set MANUAL",
                    format!("motor mode {motor} manual"),
                );
                action(
                    ui,
                    &mut commands,
                    "Set OPERATION",
                    format!("motor mode {motor} operation"),
                );
                action(
                    ui,
                    &mut commands,
                    "Save motor profiles",
                    "motor config save".into(),
                );
            });
            ui.label("MANUAL finite moves do not require mapped limits. OPERATION finite moves check mapped limits. Jog ignores limit mappings in both modes. Save motor profiles persists mode/profile changes.");

            ui.separator();
            ui.strong("LIMIT SWITCH MAPPING");
            let mapping_valid = self.min_limit[motor] != self.max_limit[motor]
                && (0..2).all(|other| {
                    other == motor
                        || (self.min_limit[motor] != self.min_limit[other]
                            && self.min_limit[motor] != self.max_limit[other]
                            && self.max_limit[motor] != self.min_limit[other]
                            && self.max_limit[motor] != self.max_limit[other])
                });
            ui.horizontal_wrapped(|ui| {
                egui::ComboBox::from_id_salt(("motor_min", motor))
                    .selected_text(format!("MIN: Optical {}", self.min_limit[motor]))
                    .show_ui(ui, |ui| {
                        for input in 0..=3 {
                            ui.selectable_value(
                                &mut self.min_limit[motor],
                                input,
                                format!("Optical {input}"),
                            );
                        }
                    });
                egui::ComboBox::from_id_salt(("motor_max", motor))
                    .selected_text(format!("MAX: Optical {}", self.max_limit[motor]))
                    .show_ui(ui, |ui| {
                        for input in 0..=3 {
                            ui.selectable_value(
                                &mut self.max_limit[motor],
                                input,
                                format!("Optical {input}"),
                            );
                        }
                    });
                egui::ComboBox::from_id_salt(("motor_home_endpoint", motor))
                    .selected_text(if self.home_at_max[motor] {
                        "HOME: MAX"
                    } else {
                        "HOME: MIN"
                    })
                    .show_ui(ui, |ui| {
                        ui.selectable_value(&mut self.home_at_max[motor], false, "HOME: MIN");
                        ui.selectable_value(&mut self.home_at_max[motor], true, "HOME: MAX");
                    });
                if ui
                    .add_enabled(mapping_valid, egui::Button::new("Apply mapping"))
                    .clicked()
                {
                    commands.push(format!(
                        "motor limits set {motor} {} {} {}",
                        self.min_limit[motor],
                        self.max_limit[motor],
                        if self.home_at_max[motor] { "max" } else { "min" }
                    ));
                }
                action(
                    ui,
                    &mut commands,
                    "Read mappings",
                    format!("motor5_limit_get {motor}"),
                );
            });
            ui.label("MIN and MAX must use different optical inputs. Each endpoint input can be assigned to only one motor. Configure each input's active level in Optical limits.");

            ui.separator();
            ui.strong("JOG / HOME");
            ui.horizontal_wrapped(|ui| {
                ui.label("Jog speed (steps/s)");
                ui.add(egui::DragValue::new(&mut self.speed_sps).range(1..=200));
                action(
                    ui,
                    &mut commands,
                    "JOG CW",
                    format!("motor5_run {motor} cw {}", self.speed_sps),
                );
                action(
                    ui,
                    &mut commands,
                    "JOG CCW",
                    format!("motor5_run {motor} ccw {}", self.speed_sps),
                );
                action(
                    ui,
                    &mut commands,
                    "STOP MOTOR",
                    format!("motor5_stop {motor}"),
                );
                action(
                    ui,
                    &mut commands,
                    "Motor status",
                    format!("motor5_status {motor}"),
                );
            });
            ui.horizontal_wrapped(|ui| {
                action(
                    ui,
                    &mut commands,
                    "HOME",
                    format!("motor5_home {motor}"),
                );
                action(
                    ui,
                    &mut commands,
                    "Read limit mapping",
                    format!("motor5_limit_get {motor}"),
                );
            });
            ui.label("HOME uses the selected MIN or MAX endpoint. It requires a valid, fresh switch input and runs until that input is active.");
            ui.horizontal(|ui| {
                ui.label("Request ID");
                ui.add(egui::DragValue::new(&mut self.result_id).range(1..=u32::MAX));
                action(
                    ui,
                    &mut commands,
                    "Poll result",
                    format!("motor5_result {}", self.result_id),
                );
            });
            ui.label("Jog is continuous and has no timeout or travel cutoff. Press STOP MOTOR to end it. QUEUED / ACCEPTED indicates request acceptance; check status or poll its request ID for updates.");
        });

        ui.add_space(8.0);
        panel_frame().show(ui, |ui| {
            ui.set_min_width(ui.available_width());
            ui.strong("STOP ALL MOTORS");
            action(ui, &mut commands, "STOP ALL", "motor5_stop all".into());
        });

        commands
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_map_distinct_endpoints_for_both_motors() {
        let panel = MotorPanel::default();
        assert_eq!(panel.min_limit, [0, 2]);
        assert_eq!(panel.max_limit, [1, 3]);
        assert_eq!(panel.home_at_max, [false, false]);
        assert_eq!(panel.speed_sps, 20);
    }
}
