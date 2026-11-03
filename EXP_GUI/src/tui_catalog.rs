// Direct EXP firmware commands mirrored from the control panels.
pub struct Command {
    pub section: &'static str,
    pub label: &'static str,
    pub text: &'static str,
}
pub const SECTIONS: &[&str] = &[
    "Overview", "Power", "Pumps", "Sensors", "Heaters", "Motors", "IR", "Camera", "Other", "RPARAM",
];
pub const COMMANDS: &[Command] = &[
    Command {
        section: "Overview",
        label: "Full telemetry",
        text: "tlm_json sensors",
    },
    Command {
        section: "Overview",
        label: "SYSTEM: firmware help",
        text: "help",
    },
    Command {
        section: "Overview",
        label: "SYSTEM: RS485 status",
        text: "rs485_status",
    },
    Command {
        section: "Power",
        label: "POWER: read all rails",
        text: "power_all_get",
    },
    Command {
        section: "Power",
        label: "POWER: SLN/HD on",
        text: "power_sln_hd_on",
    },
    Command {
        section: "Power",
        label: "POWER: SLN/HD off",
        text: "power_sln_hd_off",
    },
    Command {
        section: "Power",
        label: "POWER: SLN/HD status",
        text: "power_sln_hd_get",
    },
    Command {
        section: "Power",
        label: "POWER: motor on",
        text: "power_motor_on",
    },
    Command {
        section: "Power",
        label: "POWER: motor off",
        text: "power_motor_off",
    },
    Command {
        section: "Power",
        label: "POWER: motor status",
        text: "power_motor_get",
    },
    Command {
        section: "Power",
        label: "POWER: EXP 12V on",
        text: "power_exp12v_on",
    },
    Command {
        section: "Power",
        label: "POWER: EXP 12V off",
        text: "power_exp12v_off",
    },
    Command {
        section: "Power",
        label: "POWER: EXP 12V status",
        text: "power_exp12v_get",
    },
    Command {
        section: "Power",
        label: "PAC: detect",
        text: "pac1934_detect",
    },
    Command {
        section: "Power",
        label: "PAC: read",
        text: "pac1934_read",
    },
    Command {
        section: "Power",
        label: "PAC: poll result",
        text: "pac1934_result ",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: type read",
        text: "pump_type_get",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: type set",
        text: "pump_type_set ",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: detect",
        text: "pump_detect",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: initialize",
        text: "pump_init",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: set frequency",
        text: "pump_set_freq ",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: set voltage",
        text: "pump_set_volt ",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: enable",
        text: "pump_enable",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: disable",
        text: "pump_disable",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: state",
        text: "pump_state",
    },
    Command {
        section: "Pumps",
        label: "PUMP U11: hardware status",
        text: "pump_status",
    },
    Command {
        section: "Pumps",
        label: "PUMP U15: detect",
        text: "pump4_detect",
    },
    Command {
        section: "Pumps",
        label: "PUMP U15: initialize",
        text: "pump4_init",
    },
    Command {
        section: "Pumps",
        label: "PUMP U15: set frequency",
        text: "pump4_set_freq ",
    },
    Command {
        section: "Pumps",
        label: "PUMP U15: set channel voltage",
        text: "pump4_set_volt ",
    },
    Command {
        section: "Pumps",
        label: "PUMP U15: enable",
        text: "pump4_enable",
    },
    Command {
        section: "Pumps",
        label: "PUMP U15: disable",
        text: "pump4_disable",
    },
    Command {
        section: "Pumps",
        label: "PUMP U15: state",
        text: "pump4_state",
    },
    Command {
        section: "Pumps",
        label: "PUMP U15: hardware status",
        text: "pump4_status",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: status",
        text: "sensor_status",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: NTC read",
        text: "ntc_read_all",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: BME initialize",
        text: "bme688_init",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: BME result",
        text: "bme688_result ",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: BME read",
        text: "bme688_read_all",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: IMU probe",
        text: "lsm6d_probe",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: IMU initialize",
        text: "lsm6d_init",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: IMU result",
        text: "lsm6d_result ",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: IMU read",
        text: "lsm6d_read_all",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: flow initialize",
        text: "flow_init ",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: flow read",
        text: "flow_read_all ",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: flow result",
        text: "flow_result ",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: RTC detect",
        text: "rtc_ping",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: RTC read",
        text: "rtc_read",
    },
    Command {
        section: "Sensors",
        label: "SENSOR: RTC set",
        text: "rtc_set ",
    },
    Command {
        section: "Heaters",
        label: "HEATER: read",
        text: "heater_get",
    },
    Command {
        section: "Heaters",
        label: "HEATER: set duty/time",
        text: "heater_set ",
    },
    Command {
        section: "Heaters",
        label: "HEATER: stop channel/all",
        text: "heater_stop ",
    },
    Command {
        section: "Heaters",
        label: "HEATER: result",
        text: "heater_result ",
    },
    Command {
        section: "Motors",
        label: "MOTOR: driver initialize",
        text: "drv8910_init",
    },
    Command {
        section: "Motors",
        label: "MOTOR: driver status",
        text: "drv8910_status",
    },
    Command {
        section: "Motors",
        label: "MOTOR: fault status",
        text: "drv8910_fault_get",
    },
    Command {
        section: "Motors",
        label: "MOTOR: clear faults",
        text: "drv8910_fault_clear",
    },
    Command {
        section: "Motors",
        label: "MOTOR: save config",
        text: "motor config save",
    },
    Command {
        section: "Motors",
        label: "MOTOR: mode",
        text: "motor mode ",
    },
    Command {
        section: "Motors",
        label: "MOTOR: limits set",
        text: "motor limits set ",
    },
    Command {
        section: "Motors",
        label: "MOTOR: limit read",
        text: "motor5_limit_get ",
    },
    Command {
        section: "Motors",
        label: "MOTOR: run",
        text: "motor5_run ",
    },
    Command {
        section: "Motors",
        label: "MOTOR: stop",
        text: "motor5_stop ",
    },
    Command {
        section: "Motors",
        label: "MOTOR: home",
        text: "motor5_home ",
    },
    Command {
        section: "Motors",
        label: "MOTOR: status",
        text: "motor5_status ",
    },
    Command {
        section: "Motors",
        label: "MOTOR: result",
        text: "motor5_result ",
    },
    Command {
        section: "Motors",
        label: "LIMIT: status",
        text: "limit_status all",
    },
    Command {
        section: "Motors",
        label: "LIMIT: raw",
        text: "limit_raw all",
    },
    Command {
        section: "Motors",
        label: "LIMIT: get",
        text: "limit_get all",
    },
    Command {
        section: "Motors",
        label: "LIMIT: active level",
        text: "limit_cfg_set ",
    },
    Command {
        section: "IR",
        label: "IR: status",
        text: "ir_status",
    },
    Command {
        section: "IR",
        label: "IR: configure",
        text: "ir_cfg_set ",
    },
    Command {
        section: "IR",
        label: "IR: pulse",
        text: "ir_set ",
    },
    Command {
        section: "IR",
        label: "IR: stop",
        text: "ir_off ",
    },
    Command {
        section: "IR",
        label: "IR: result",
        text: "ir_result ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: detect",
        text: "camera_detect ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: initialize",
        text: "camera_init ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: scan",
        text: "camera_scan ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: status",
        text: "camera_status ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: configure",
        text: "camera_config ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: set I2C address",
        text: "camera_address_set ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: enable buffer",
        text: "camera_buffer_enable ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: disable buffer",
        text: "camera_buffer_disable ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: capture",
        text: "camera_capture ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: data inventory",
        text: "data_ready_collect",
    },
    Command {
        section: "Camera",
        label: "CAMERA: object metadata",
        text: "data_ready_collect ",
    },
    Command {
        section: "Camera",
        label: "CAMERA: data list",
        text: "data_list",
    },
    Command {
        section: "Camera",
        label: "CAMERA: data list page",
        text: "data_list ",
    },
    Command {
        section: "Other",
        label: "BUBBLE: raw",
        text: "bubble_raw ",
    },
    Command {
        section: "Other",
        label: "BURN: status",
        text: "burn_status",
    },
    Command {
        section: "Other",
        label: "BURN: configure",
        text: "burn_cfg_set ",
    },
    Command {
        section: "Other",
        label: "BURN: arm",
        text: "burn_arm ",
    },
    Command {
        section: "Other",
        label: "BURN: fire",
        text: "burn_fire",
    },
    Command {
        section: "Other",
        label: "BURN: abort",
        text: "burn_abort",
    },
    Command {
        section: "Other",
        label: "PC EXPERIMENT: status",
        text: "pc_status",
    },
    Command {
        section: "RPARAM",
        label: "Show RPARAM table",
        text: "rparam_show ",
    },
];
