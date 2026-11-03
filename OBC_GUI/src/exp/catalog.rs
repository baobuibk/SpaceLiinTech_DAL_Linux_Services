//! Static description of every page. Command syntax comes from the firmware help
//! strings in `cli_command.c`; reply needles come from the formats the desktop GUI parses.

pub const SECTIONS: [&str; 6] = ["HOME", "POWER", "PUMPS", "SENSORS", "ACTUATORS", "EXPERIMENT"];

/// One focusable control. `{key}` in a command template is replaced by the page value of `key`.
pub enum W {
    Btn(&'static str, &'static str),
    Field(&'static str, &'static str, &'static str),
    Pick(&'static str, &'static str, &'static [&'static str]),
    /// Label + latest value of a reply `needle` + buttons (label, command).
    Row(&'static str, &'static str, &'static [(&'static str, &'static str)]),
}

/// Read-only line: shows the text after `needle` in the latest reply containing it.
pub struct Read {
    pub label: &'static str,
    pub needle: &'static str,
    pub first_word: bool,
}

pub struct Group {
    pub title: &'static str,
    pub widgets: &'static [W],
    pub reads: &'static [Read],
    pub note: &'static str,
}

pub struct Page {
    pub section: usize,
    pub name: &'static str,
    pub groups: &'static [Group],
    pub camera: bool,
    /// Read-only commands sent periodically while the page is shown (status refresh).
    pub poll: &'static [&'static str],
}

const fn rd(label: &'static str, needle: &'static str) -> Read {
    Read { label, needle, first_word: false }
}
const fn rd1(label: &'static str, needle: &'static str) -> Read {
    Read { label, needle, first_word: true }
}

const IDX01: &[&str] = &["0", "1"];
const IDX03: &[&str] = &["0", "1", "2", "3"];
const CWCCW: &[&str] = &["cw", "ccw"];

macro_rules! camera_page {
    ($name:literal, $id:literal) => {
        Page {
            section: 4,
            name: $name,
            camera: true,
            poll: &[concat!("camera_status ", $id)],
            groups: &[
                Group {
                    title: "CAMERA STATUS / SELECTED LINK",
                    widgets: &[W::Btn("Status", concat!("camera_status ", $id))],
                    reads: &[rd("Status", "CAMERA camera=")],
                    note: "",
                },
                Group {
                    title: concat!("I2C", $id, " BUFFER & DEVICE"),
                    widgets: &[
                        W::Btn("Buffer 0 ON", "camera_buffer_enable 0"),
                        W::Btn("Buffer 0 OFF", "camera_buffer_disable 0"),
                        W::Btn("Buffer 1 ON", "camera_buffer_enable 1"),
                        W::Btn("Buffer 1 OFF", "camera_buffer_disable 1"),
                        W::Btn("Scan I2C", concat!("camera_scan ", $id)),
                        W::Btn("Detect", concat!("camera_detect ", $id)),
                        W::Btn("Initialize", concat!("camera_init ", $id)),
                        W::Btn("Capture image", concat!("camera_capture ", $id)),
                    ],
                    reads: &[],
                    note: "Scan/capture never switch buffers. Enable only the selected branch before programming an address.",
                },
                Group {
                    title: "CONFIGURATION",
                    widgets: &[
                        W::Pick("Model", "model", &["mlx90640", "mlx90642"]),
                        W::Field("I2C address", "addr", "0x33"),
                        W::Pick("Encoding", "enc", &["float32", "int16"]),
                        W::Field("Refresh Hz", "hz", "2"),
                        W::Field("Emissivity /1000", "emis", "1000"),
                        W::Btn(
                            "Apply config",
                            concat!("camera_config ", $id, " {model} {addr} {enc} {hz} {emis}"),
                        ),
                    ],
                    reads: &[],
                    note: "Apply config changes the firmware address setting, not the sensor EEPROM.",
                },
                Group {
                    title: "PROGRAM SENSOR I2C ADDRESS / EEPROM",
                    widgets: &[
                        W::Field("New sensor address", "newaddr", "0x34"),
                        W::Btn(
                            "Program sensor address",
                            concat!("camera_address_set ", $id, " {newaddr}"),
                        ),
                    ],
                    reads: &[],
                    note: "Isolate the branch first. MLX90640 needs a camera power cycle afterwards.",
                },
            ],
        }
    };
}

pub static PAGES: &[Page] = &[
    // ---- HOME ----
    Page {
        section: 0,
        name: "Overview",
        poll: &["power_all_get", "sensor_status", "ntc_read_all", "pac1934_read"],
        camera: false,
        groups: &[Group {
            title: "System checks",
            widgets: &[
                W::Btn("Help", "help"),
                W::Btn("Power", "power_all_get"),
                W::Btn("Sensors", "sensor_status"),
                W::Btn("RS485", "rs485_status"),
                W::Btn("PC", "pc_status"),
            ],
            reads: &[],
            note: "",
        },
        Group {
            title: "LIVE STATUS (auto refresh)",
            widgets: &[
                W::Row("SLN / HD", "Power SLN HD requested=", &[("ON", "power_sln_hd_on"), ("OFF", "power_sln_hd_off")]),
                W::Row("MOTOR", "Power MOTOR requested=", &[("ON", "power_motor_on"), ("OFF", "power_motor_off")]),
                W::Row("EXP 12 V", "Power 12V_EXP requested=", &[("ON", "power_exp12v_on"), ("OFF", "power_exp12v_off")]),
            ],
            reads: &[
                rd("Sensor health", "Sensors: "),
                rd("NTC 0", "NTC[0] : "),
                rd("NTC 1", "NTC[1] : "),
                rd("NTC 2", "NTC[2] : "),
                rd("NTC 3", "NTC[3] : "),
                rd("PCB temperature", "BOARD_TEMP : "),
                rd("PAC1934", "PAC1934 snapshot "),
            ],
            note: "",
        }],
    },
    Page {
        section: 0,
        name: "RPARAM tables",
        poll: &[],
        camera: false,
        groups: &[Group {
            title: "RPARAM tables 0-10",
            widgets: &[],
            reads: &[],
            note: "Needs the CSP link (RS485 / USB-CAN). Not available in exp_tui v1; use the desktop GUI.",
        }],
    },
    // ---- POWER ----
    Page {
        section: 1,
        name: "Switched rails",
        poll: &["power_all_get"],
        camera: false,
        groups: &[Group {
            title: "SWITCHED POWER RAILS",
            widgets: &[
                W::Btn("Read all rails", "power_all_get"),
                W::Btn("All ON", "power_all_on"),
                W::Btn("All OFF", "power_all_off"),
                W::Row(
                    "SLN / HD",
                    "Power SLN HD requested=",
                    &[("Read status", "power_sln_hd_get"), ("ON", "power_sln_hd_on"), ("OFF", "power_sln_hd_off")],
                ),
                W::Row(
                    "MOTOR",
                    "Power MOTOR requested=",
                    &[("Read status", "power_motor_get"), ("ON", "power_motor_on"), ("OFF", "power_motor_off")],
                ),
                W::Row(
                    "EXP 12 V",
                    "Power 12V_EXP requested=",
                    &[("Read status", "power_exp12v_get"), ("ON", "power_exp12v_on"), ("OFF", "power_exp12v_off")],
                ),
            ],
            reads: &[],
            note: "Last reported ON/OFF command state; rail voltage is not measured. Read status to refresh.",
        }],
    },
    Page {
        section: 1,
        name: "PAC1934",
        poll: &["pac1934_read"],
        camera: false,
        groups: &[Group {
            title: "PAC1934",
            widgets: &[W::Btn("Detect", "pac1934_detect"), W::Btn("Refresh", "pac1934_read")],
            reads: &[rd("Snapshot", "PAC1934 snapshot "), rd("Detect", "PAC1934 DONE")],
            note: "Refresh reads the sensor cache (V, A, W for 12V_MOTOR, 12V_EXP, 5V_EXP, 5V_SLN_HD).",
        }],
    },
    // ---- PUMPS ----
    Page {
        section: 2,
        name: "U11 single pump",
        poll: &["pump_state"],
        camera: false,
        groups: &[
            Group {
                title: "01 / DRIVER SETUP",
                widgets: &[
                    W::Pick("Fitted driver", "ptype", &["lowdriver", "highdriver"]),
                    W::Btn("Set type", "pump_type_set {ptype}"),
                    W::Btn("Read type", "pump_type_get"),
                    W::Btn("Detect", "pump_detect"),
                    W::Btn("Initialize", "pump_init"),
                ],
                reads: &[],
                note: "Change type with the SLN/HD rail OFF.",
            },
            Group {
                title: "02 / OUTPUT SETTINGS",
                widgets: &[
                    W::Field("Frequency / Hz", "freq", "200"),
                    W::Btn("Set frequency", "pump_set_freq {freq}"),
                    W::Field("Voltage / Vpp", "volt", "30"),
                    W::Btn("Set voltage", "pump_set_volt {volt}"),
                ],
                reads: &[],
                note: "",
            },
            Group {
                title: "03 / OPERATION & READBACK",
                widgets: &[
                    W::Btn("Enable", "pump_enable"),
                    W::Btn("Disable", "pump_disable"),
                    W::Btn("Read state", "pump_state"),
                    W::Btn("Hardware status", "pump_status"),
                ],
                reads: &[],
                note: "Set values do not indicate measured output.",
            },
        ],
    },
    Page {
        section: 2,
        name: "U15 four-channel",
        poll: &["pump4_state"],
        camera: false,
        groups: &[
            Group {
                title: "01 / DRIVER SETUP",
                widgets: &[W::Btn("Detect", "pump4_detect"), W::Btn("Initialize", "pump4_init")],
                reads: &[],
                note: "U15 / mp-Highdriver4 / fixed driver type",
            },
            Group {
                title: "02 / OUTPUT SETTINGS",
                widgets: &[
                    W::Pick("Output channel", "ch", IDX03),
                    W::Field("Frequency / Hz", "freq", "200"),
                    W::Btn("Set frequency", "pump4_set_freq {freq}"),
                    W::Field("Voltage / Vpp", "volt", "30"),
                    W::Btn("Set voltage", "pump4_set_volt {ch} {volt}"),
                ],
                reads: &[],
                note: "Frequency and enable are shared; voltage applies to the selected channel.",
            },
            Group {
                title: "03 / OPERATION & READBACK",
                widgets: &[
                    W::Btn("Enable", "pump4_enable"),
                    W::Btn("Disable", "pump4_disable"),
                    W::Btn("Read state", "pump4_state"),
                    W::Btn("Hardware status", "pump4_status"),
                ],
                reads: &[],
                note: "Set values do not indicate measured output.",
            },
        ],
    },
    // ---- SENSORS ----
    Page {
        section: 3,
        name: "NTC",
        poll: &["ntc_read_all"],
        camera: false,
        groups: &[Group {
            title: "NTC",
            widgets: &[W::Btn("Read all", "ntc_read_all")],
            reads: &[
                rd("NTC 0", "NTC[0] : "),
                rd("NTC 1", "NTC[1] : "),
                rd("NTC 2", "NTC[2] : "),
                rd("NTC 3", "NTC[3] : "),
                rd("PCB temperature", "BOARD_TEMP : "),
            ],
            note: "",
        }],
    },
    Page {
        section: 3,
        name: "BME688",
        poll: &["bme688_read_all"],
        camera: false,
        groups: &[Group {
            title: "BME688",
            widgets: &[W::Btn("Init (reset)", "bme688_init"), W::Btn("Read all", "bme688_read_all")],
            reads: &[
                rd("Temperature", "[BME688] Temp :"),
                rd("Pressure", "[BME688] Press:"),
                rd("Humidity", "[BME688] Hum  :"),
            ],
            note: "Reads the cached snapshot; no I2C traffic.",
        }],
    },
    Page {
        section: 3,
        name: "IMU",
        poll: &["lsm6d_read_all"],
        camera: false,
        groups: &[Group {
            title: "IMU LSM6DSOX",
            widgets: &[
                W::Btn("Probe", "lsm6d_probe"),
                W::Btn("Init (reset)", "lsm6d_init"),
                W::Btn("Read all", "lsm6d_read_all"),
            ],
            reads: &[rd("Probe", "LSM6DSOX "), rd("Sample", "IMU ")],
            note: "",
        }],
    },
    Page {
        section: 3,
        name: "Flow sensors",
        poll: &[],
        camera: false,
        groups: &[Group {
            title: "FLOW SENSORS",
            widgets: &[
                W::Pick("Sensor", "fs", IDX01),
                W::Btn("Detect / init", "flow_init {fs}"),
                W::Btn("Read", "flow_read_all {fs}"),
            ],
            reads: &[rd("Flow", "FLOW "), rd("Result", "flow ")],
            note: "Requests are queued; the reply carries a request id for flow_result.",
        }],
    },
    Page {
        section: 3,
        name: "RTC",
        poll: &["rtc_read"],
        camera: false,
        groups: &[Group {
            title: "RTC",
            widgets: &[
                W::Btn("Detect (ping)", "rtc_ping"),
                W::Btn("Read", "rtc_read"),
                W::Field("Hour", "h", "12"),
                W::Field("Minute", "m", "0"),
                W::Field("Second", "s", "0"),
                W::Field("Day", "d", "1"),
                W::Field("Month", "mo", "1"),
                W::Field("Year (YY)", "y", "26"),
                W::Btn("Set", "rtc_set {h} {m} {s} {d} {mo} {y}"),
            ],
            reads: &[rd("RTC", "RTC: ")],
            note: "Set sends rtc_set <h> <m> <s> <DD> <MM> <YY>; press Read to confirm.",
        }],
    },
    // ---- ACTUATORS ----
    Page {
        section: 4,
        name: "Heaters",
        poll: &["power_exp12v_get", "heater_get", "ntc_read_all"],
        camera: false,
        groups: &[
            Group {
                title: "HEATER SUPPLY / 12V_EXP",
                widgets: &[
                    W::Btn("Power ON", "power_exp12v_on"),
                    W::Btn("Power OFF", "power_exp12v_off"),
                    W::Btn("Refresh power", "power_exp12v_get"),
                    W::Btn("Stop all heaters", "heater_stop all"),
                ],
                reads: &[rd1("Supply", "Power 12V_EXP requested=")],
                note: "Shared with IR LEDs. Power OFF is refused while consumers own the rail.",
            },
            Group {
                title: "MANUAL HEATER",
                widgets: &[
                    W::Pick("Heater", "hc", IDX03),
                    W::Field("Duty %", "duty", "5"),
                    W::Field("ON time ms", "ms", "30000"),
                    W::Btn("ON / Apply duty", "heater_set {hc} {duty} {ms}"),
                    W::Btn("OFF", "heater_stop {hc}"),
                    W::Btn("Read status", "heater_get {hc}"),
                    W::Btn("Read all", "heater_get"),
                ],
                reads: &[
                    rd("Heater 0 duty", "Heater[0] Duty = "),
                    rd("Heater 1 duty", "Heater[1] Duty = "),
                    rd("Heater 2 duty", "Heater[2] Duty = "),
                    rd("Heater 3 duty", "Heater[3] Duty = "),
                ],
                note: "Ownership and thermal limits stay enforced by the firmware.",
            },
            Group {
                title: "NTC TEMPERATURES",
                widgets: &[W::Btn("Refresh NTC", "ntc_read_all")],
                reads: &[
                    rd("NTC 0", "NTC[0] : "),
                    rd("NTC 1", "NTC[1] : "),
                    rd("NTC 2", "NTC[2] : "),
                    rd("NTC 3", "NTC[3] : "),
                    rd("Board", "BOARD_TEMP : "),
                ],
                note: "",
            },
        ],
    },
    Page {
        section: 4,
        name: "Motors / DRV8910",
        poll: &["drv8910_status", "motor5_status all", "power_motor_get"],
        camera: false,
        groups: &[
            Group {
                title: "DRIVER SETUP",
                widgets: &[
                    W::Btn("Rail ON", "power_motor_on"),
                    W::Btn("Initialize", "drv8910_init"),
                    W::Btn("Driver status", "drv8910_status"),
                    W::Btn("Fault details", "drv8910_fault_get"),
                    W::Btn("Clear faults", "drv8910_fault_clear"),
                    W::Btn("Rail status", "power_motor_get"),
                    W::Btn("STOP ALL", "motor5_stop all"),
                    W::Btn("Rail OFF", "power_motor_off"),
                ],
                reads: &[rd("DRIVER", "DRV8910 "), rd1("Motor rail", "Power MOTOR requested=")],
                note: "",
            },
            Group {
                title: "MOTOR CONTROL",
                widgets: &[
                    W::Pick("Motor", "mc", IDX01),
                    W::Pick("Direction", "dir", CWCCW),
                    W::Field("Steps/s", "sps", "100"),
                    W::Btn("Jog", "motor5_run {mc} {dir} {sps}"),
                    W::Btn("STOP MOTOR", "motor5_stop {mc}"),
                    W::Btn("HOME", "motor5_home {mc}"),
                    W::Btn("Status", "motor5_status {mc}"),
                    W::Btn("Read limit", "motor5_limit_get {mc}"),
                ],
                reads: &[rd("Motor 0", "MOTOR0 "), rd("Motor 1", "MOTOR1 ")],
                note: "Jog continues until stopped (no timeout). Assign the optical limit before HOME.",
            },
        ],
    },
    Page {
        section: 4,
        name: "Optical limits",
        poll: &["limit_status all"],
        camera: false,
        groups: &[Group {
            title: "OPTICAL LIMITS 0-3",
            widgets: &[
                W::Pick("Input", "li", IDX03),
                W::Btn("Status", "limit_status {li}"),
                W::Btn("Read filtered", "limit_get {li}"),
                W::Btn("Read raw", "limit_raw {li}"),
                W::Pick("Setting", "lk", &["active_when", "debounce_ms", "invalid_timeout_ms"]),
                W::Field("Value", "lv", "1"),
                W::Btn("Set", "limit_cfg_set {li} {lk} {lv}"),
            ],
            reads: &[rd("Limit", "LIMIT")],
            note: "",
        }],
    },
    Page {
        section: 4,
        name: "IR LEDs",
        poll: &["ir_status", "power_exp12v_get"],
        camera: false,
        groups: &[
            Group {
                title: "IR LED / CAPS",
                widgets: &[
                    W::Pick("Channel", "ic", IDX01),
                    W::Pick("Cap", "ik", &["max_duty_pct", "max_on_ms", "pwm_hz"]),
                    W::Field("Value", "iv", "0"),
                    W::Btn("Set cap", "ir_cfg_set {ic} {ik} {iv}"),
                ],
                reads: &[],
                note: "Ranges: duty 0-25 %, on-time 0-100 ms, PWM 100-100000 Hz. Caps default to zero.",
            },
            Group {
                title: "PULSE / STOP",
                widgets: &[
                    W::Field("Duty %", "id", "1"),
                    W::Field("Duration ms", "idur", "10"),
                    W::Btn("Pulse", "ir_set {ic} {id} {idur}"),
                    W::Btn("Stop channel", "ir_off {ic}"),
                    W::Btn("Stop all", "ir_off all"),
                    W::Btn("Status", "ir_status"),
                    W::Btn("Power EXP 12V ON", "power_exp12v_on"),
                    W::Btn("Power EXP 12V OFF", "power_exp12v_off"),
                ],
                reads: &[rd("IR", "IR ")],
                note: "Poll ir_result <id> for queued requests.",
            },
        ],
    },
    camera_page!("IR camera 0", "0"),
    camera_page!("IR camera 1", "1"),
    Page {
        section: 4,
        name: "Bubble inputs",
        poll: &["bubble_raw 0", "bubble_raw 1"],
        camera: false,
        groups: &[Group {
            title: "BUBBLE DETECTORS BE-A301",
            widgets: &[W::Btn("Read 0", "bubble_raw 0"), W::Btn("Read 1", "bubble_raw 1")],
            reads: &[rd("Detector 0", "BUBBLE 0 "), rd("Detector 1", "BUBBLE 1 ")],
            note: "Raw OUT1/OUT2 levels only; no bubble verdict and no automatic pump stop.",
        }],
    },
    Page {
        section: 4,
        name: "Burn wire",
        poll: &["burn_status"],
        camera: false,
        groups: &[Group {
            title: "BURN WIRE",
            widgets: &[
                W::Pick("Wire", "bw", IDX01),
                W::Btn("Status", "burn_status"),
                W::Btn("Arm (5 s)", "burn_arm {bw}"),
                W::Btn("FIRE", "burn_fire"),
                W::Btn("ABORT", "burn_abort"),
                W::Field("Key", "bk", "pulse_ms"),
                W::Field("Value", "bv", "1000"),
                W::Btn("Persist setting", "burn_cfg_set {bw} {bk} {bv}"),
                W::Btn("PAC1934 read", "pac1934_read"),
            ],
            reads: &[rd("Burn", "BURN ")],
            note: "PAC readings are the shared 12V_MOTOR feed, not individual wire current.",
        }],
    },
    // ---- EXPERIMENT ----
    Page {
        section: 5,
        name: "PC experiment",
        poll: &["pc_status"],
        camera: false,
        groups: &[Group {
            title: "PC experiment",
            widgets: &[W::Btn("Status", "pc_status")],
            reads: &[rd("PC", "PC ")],
            note: "Only status is wired up, as in the desktop GUI.",
        }],
    },
];

pub fn pages_in(section: usize) -> Vec<usize> {
    PAGES.iter().enumerate().filter(|(_, p)| p.section == section).map(|(i, _)| i).collect()
}

/// Initial value of every focusable item of a page (fields and picks; others empty).
pub fn defaults(page: &Page) -> Vec<String> {
    let mut v = Vec::new();
    for g in page.groups {
        for w in g.widgets {
            match w {
                W::Field(_, _, d) => v.push((*d).to_string()),
                W::Pick(_, _, o) => v.push(o[0].to_string()),
                W::Btn(..) => v.push(String::new()),
                W::Row(_, _, b) => v.extend(b.iter().map(|_| String::new())),
            }
        }
    }
    v
}

/// Look up a `{key}` value among the page's fields/picks.
pub fn value_of(page: &Page, values: &[String], key: &str) -> Option<String> {
    let mut i = 0;
    for g in page.groups {
        for w in g.widgets {
            match w {
                W::Field(_, k, _) | W::Pick(_, k, _) => {
                    if *k == key {
                        return values.get(i).cloned();
                    }
                    i += 1;
                }
                W::Btn(..) => i += 1,
                W::Row(_, _, b) => i += b.len(),
            }
        }
    }
    None
}

pub fn expand(page: &Page, values: &[String], template: &str) -> String {
    let mut out = String::new();
    let mut rest = template;
    while let Some(s) = rest.find('{') {
        out.push_str(&rest[..s]);
        let Some(e) = rest[s..].find('}') else { break };
        let key = &rest[s + 1..s + e];
        out.push_str(&value_of(page, values, key).unwrap_or_default());
        rest = &rest[s + e + 1..];
    }
    out.push_str(rest);
    out
}

/// All distinct reply needles, for scanning incoming lines.
pub fn all_needles() -> Vec<&'static str> {
    let mut v = Vec::new();
    for p in PAGES {
        for g in p.groups {
            for r in g.reads {
                v.push(r.needle);
            }
            for w in g.widgets {
                if let W::Row(_, n, _) = w {
                    v.push(*n);
                }
            }
        }
    }
    v.sort();
    v.dedup();
    v
}
