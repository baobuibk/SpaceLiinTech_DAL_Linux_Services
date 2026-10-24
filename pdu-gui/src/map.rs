//! Map verified against HW1.1 app_modbus.c; displayed addresses are one-based.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Bank {
    Coil,
    Discrete,
    Input,
    Holding,
}
impl Bank {
    pub fn function(self) -> u8 {
        match self {
            Self::Coil => 1,
            Self::Discrete => 2,
            Self::Holding => 3,
            Self::Input => 4,
        }
    }
    pub fn base(self) -> u16 {
        match self {
            Self::Coil => 1,
            Self::Discrete => 10001,
            Self::Input => 30001,
            Self::Holding => 40001,
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Self::Coil => "Coils",
            Self::Discrete => "Discrete inputs",
            Self::Input => "Telemetry",
            Self::Holding => "Settings & identity",
        }
    }
}
#[derive(Clone, Copy, Debug)]
pub enum Format {
    Unsigned,
    Signed,
    Temp,
    Power,
    Hex,
    Mode,
    Bool,
    Clock,
    Product,
    Version,
    Boot,
    ClockSource,
    Severity,
    Event,
}
#[derive(Clone, Copy, Debug)]
pub enum Rule {
    ReadOnly,
    Bool,
    Vout,
    Temp,
    Current,
    Mode,
    Retry,
    Positive32,
    Baud,
    Clock,
    Event,
    Bootloader,
}
#[derive(Clone, Debug)]
pub struct Row {
    pub bank: Bank,
    pub address: u16,
    pub count: u16,
    pub name: &'static str,
    pub unit: &'static str,
    pub format: Format,
    pub rule: Rule,
    pub hint: &'static str,
}
impl Row {
    pub fn offset(&self) -> u16 {
        self.address - self.bank.base()
    }
    pub fn writable(&self) -> bool {
        !matches!(self.rule, Rule::ReadOnly)
    }
    pub fn display(&self, words: &[u16]) -> String {
        if words.len() != self.count as usize {
            return "Unavailable".into();
        }
        let v = if self.count == 2 {
            ((words[0] as u32) << 16) | words[1] as u32
        } else {
            words[0] as u32
        };
        if (self.address == 30021 && v == 0x8000)
            || ((30023..=30031).contains(&self.address)
                || (30035..=30038).contains(&self.address)
                || self.address == 40001)
                && v == 0xffff
            || matches!(self.format, Format::Clock) && v == u32::MAX
        {
            return "Unavailable / unset".into();
        }
        let named = |names: &[&str]| {
            names
                .get(v as usize)
                .map(|s| s.to_string())
                .unwrap_or_else(|| format!("Unknown ({v})"))
        };
        match self.format {
            Format::Temp => format!("{:.1}", words[0] as i16 as f64 / 10.0),
            Format::Signed => (words[0] as i16).to_string(),
            Format::Power => (v * 10).to_string(),
            Format::Hex => format!("0x{v:04X}"),
            Format::Bool => if v == 0 { "Off / 0" } else { "On / 1" }.into(),
            Format::Mode => named(&["NORMAL", "MANUAL", "WARNING", "ERROR"]),
            Format::Boot => named(&[
                "UNKNOWN",
                "POWER_ON",
                "BROWN_OUT",
                "EXTERNAL",
                "AVR_WATCHDOG",
                "CLIENT_WATCHDOG",
                "COMMAND",
            ]),
            Format::ClockSource => named(&["UNSET", "RESTORED", "SET"]),
            Format::Severity => named(&["None", "INFO", "WARNING"]),
            Format::Event => named(&[
                "None",
                "TEMP",
                "VOUT_CURRENT",
                "TRANS_CURRENT",
                "I2C_TIMEOUT",
                "CONFIG_CHANGED",
                "SETPOINT_MISMATCH",
                "CLOCK_SET",
            ]),
            Format::Product => String::from_utf8_lossy(&v.to_be_bytes())
                .trim_end_matches('\0')
                .into(),
            Format::Version => format!("{}.{}", v >> 8, v & 255),
            _ => v.to_string(),
        }
    }
    pub fn parse(&self, text: &str) -> Result<Vec<u16>, String> {
        if !self.writable() {
            return Err("Read-only parameter".into());
        }
        let text = text.trim();
        let n: i64 = if matches!(self.rule, Rule::Temp) {
            let t: f64 = text.parse().map_err(|_| "Enter a temperature in °C")?;
            if !t.is_finite() || (t * 10.0 - (t * 10.0).round()).abs() > 0.00001 {
                return Err("Use 0.1 °C steps".into());
            }
            (t * 10.0).round() as i64
        } else if matches!(self.rule, Rule::Bool)
            && matches!(
                text.to_lowercase().as_str(),
                "on" | "off" | "true" | "false"
            )
        {
            i64::from(matches!(text.to_lowercase().as_str(), "on" | "true"))
        } else if let Some(hex) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
            i64::from_str_radix(hex, 16).map_err(|_| "Invalid hex value")?
        } else {
            text.parse().map_err(|_| "Enter an integer (or 0xHEX)")?
        };
        let valid = match self.rule {
            Rule::ReadOnly => false,
            Rule::Bool => (0..=1).contains(&n),
            Rule::Vout => (800..=20000).contains(&n) && n % 10 == 0,
            Rule::Temp => (-400..=1250).contains(&n),
            Rule::Current => (100..=5000).contains(&n),
            Rule::Mode => [0, 1, 4].contains(&n),
            Rule::Retry => (1..=60).contains(&n),
            Rule::Positive32 => (1..=u32::MAX as i64).contains(&n),
            Rule::Baud => [1200, 2400, 4800, 9600, 19200, 38400, 115200].contains(&n),
            Rule::Clock => (0..u32::MAX as i64).contains(&n),
            Rule::Event => (0..=65535).contains(&n),
            Rule::Bootloader => n == 0xb007,
        };
        if !valid {
            return Err(self.hint.into());
        }
        Ok(if self.count == 2 {
            vec![(n as u32 >> 16) as u16, n as u16]
        } else {
            vec![n as u16]
        })
    }
    pub fn write_request(&self, words: &[u16]) -> (u8, Vec<u8>) {
        let mut data = self.offset().to_be_bytes().to_vec();
        if self.bank == Bank::Coil {
            data.extend_from_slice(&(if words[0] == 0 { 0u16 } else { 0xff00u16 }).to_be_bytes());
            (5, data)
        } else if words.len() == 1 {
            data.extend_from_slice(&words[0].to_be_bytes());
            (6, data)
        } else {
            data.extend_from_slice(&(words.len() as u16).to_be_bytes());
            data.push((words.len() * 2) as u8);
            for word in words {
                data.extend_from_slice(&word.to_be_bytes());
            }
            (16, data)
        }
    }
}

pub fn rows() -> Vec<Row> {
    use Bank::*;
    use Format::*;
    let mut rows = Vec::new();
    macro_rules! row {
        ($bank:ident,$addr:expr,$count:expr,$name:expr,$unit:expr,$fmt:ident,$rule:ident,$hint:expr) => {
            rows.push(Row {
                bank: $bank,
                address: $addr,
                count: $count,
                name: $name,
                unit: $unit,
                format: $fmt,
                rule: Rule::$rule,
                hint: $hint,
            });
        };
    }
    row!(
        Coil,
        1,
        1,
        "VOUT enable",
        "",
        Bool,
        Bool,
        "0/1 or off/on. Requires a programmed setpoint; enabling also powers TRANS."
    );
    row!(
        Coil,
        2,
        1,
        "TRANS branch enable",
        "",
        Bool,
        Bool,
        "0/1 or off/on. Turning TRANS off also turns VOUT off."
    );
    row!(
        Coil,
        3,
        1,
        "Fault latch / clear",
        "",
        Bool,
        Bool,
        "Write 1 to clear latched faults; 0 does nothing."
    );
    row!(
        Coil,
        10,
        1,
        "Heartbeat enable",
        "",
        Bool,
        Bool,
        "0/1 or off/on; affects NORMAL pattern only."
    );
    for (a, n) in [
        (10001, "TRANS eFuse fault"),
        (10002, "MCU eFuse fault"),
        (10010, "Heartbeat output"),
        (10011, "TRANS output"),
    ] {
        row!(Discrete, a, 1, n, "", Bool, ReadOnly, "Read only");
    }
    row!(Input,30020,1,"Status flags","",Hex,ReadOnly,"Bits: 0 LED, 1 TRANS, 2 TRANS fault, 3 MCU fault, 4/5 latched faults, 6 setpoint mismatch.");
    row!(
        Input,
        30021,
        1,
        "Board temperature",
        "°C",
        Temp,
        ReadOnly,
        "0x8000 means unavailable"
    );
    row!(
        Input,
        30022,
        1,
        "Operating mode",
        "",
        Mode,
        ReadOnly,
        "Read only"
    );
    row!(
        Input,
        30023,
        1,
        "Commanded VOUT",
        "mV",
        Unsigned,
        ReadOnly,
        "0xFFFF means unset"
    );
    for (i, n) in [
        "VOUT voltage",
        "VOUT current",
        "+12 input voltage",
        "+12 input current",
        "+5_TRANS voltage",
        "+5_TRANS current",
        "+5 voltage",
        "+5 current",
    ]
    .into_iter()
    .enumerate()
    {
        row!(
            Input,
            30024 + i as u16,
            1,
            n,
            if i % 2 == 0 { "mV" } else { "mA" },
            Unsigned,
            ReadOnly,
            "0xFFFF means unavailable"
        );
    }
    row!(
        Input,
        30032,
        1,
        "Boot count",
        "",
        Unsigned,
        ReadOnly,
        "Read only"
    );
    row!(
        Input,
        30033,
        1,
        "Boot cause",
        "",
        Boot,
        ReadOnly,
        "Read only"
    );
    row!(
        Input,
        30034,
        1,
        "Watchdog source",
        "",
        Unsigned,
        ReadOnly,
        "1 console, 2 heartbeat, 3 Modbus, 4 power monitor, 5 temperature, 16 monitor"
    );
    for (i, n) in [
        "VOUT power",
        "+12 input power",
        "+5_TRANS power",
        "+5 power",
    ]
    .into_iter()
    .enumerate()
    {
        row!(
            Input,
            30035 + i as u16,
            1,
            n,
            "mW",
            Power,
            ReadOnly,
            "Raw units are 10 mW; 0xFFFF means unavailable"
        );
    }
    row!(
        Input,
        30039,
        2,
        "Clock",
        "Unix s UTC",
        Clock,
        ReadOnly,
        "0xFFFFFFFF means unset"
    );
    row!(
        Input,
        30041,
        1,
        "Event sequence",
        "",
        Unsigned,
        ReadOnly,
        "0 means no matching event"
    );
    row!(
        Input,
        30042,
        1,
        "Event severity",
        "",
        Severity,
        ReadOnly,
        "Read only"
    );
    row!(
        Input,
        30043,
        1,
        "Event code",
        "",
        Event,
        ReadOnly,
        "Read only"
    );
    row!(
        Input,
        30044,
        1,
        "Event mode",
        "",
        Mode,
        ReadOnly,
        "Read only"
    );
    row!(
        Input,
        30045,
        1,
        "Event value",
        "",
        Signed,
        ReadOnly,
        "Signed; units depend on event code"
    );
    row!(
        Input,
        30046,
        2,
        "Event timestamp",
        "Unix s UTC",
        Clock,
        ReadOnly,
        "Read only"
    );
    row!(
        Input,
        30048,
        1,
        "Clock source",
        "",
        ClockSource,
        ReadOnly,
        "RESTORED time is approximate until set"
    );
    row!(
        Input,
        30049,
        1,
        "Event clock source",
        "",
        ClockSource,
        ReadOnly,
        "Read only"
    );
    row!(
        Holding,
        40001,
        1,
        "VOUT setpoint",
        "mV",
        Unsigned,
        Vout,
        "800–20000 mV in 10 mV steps. Writing can power TRANS on."
    );
    row!(
        Holding,
        40020,
        2,
        "LED on period",
        "ms",
        Unsigned,
        Positive32,
        "1–4294967295 ms"
    );
    row!(
        Holding,
        40022,
        2,
        "LED off period",
        "ms",
        Unsigned,
        Positive32,
        "1–4294967295 ms"
    );
    row!(
        Holding,
        40030,
        1,
        "Mode request",
        "",
        Mode,
        Mode,
        "Write 0 NORMAL, 1 MANUAL, or 4 immediate retry"
    );
    row!(
        Holding,
        40031,
        1,
        "Temperature limit",
        "°C",
        Temp,
        Temp,
        "-40.0–125.0 °C in 0.1 °C steps; stored in EEPROM"
    );
    row!(
        Holding,
        40032,
        1,
        "VOUT current limit",
        "mA",
        Unsigned,
        Current,
        "100–5000 mA; stored in EEPROM"
    );
    row!(
        Holding,
        40033,
        1,
        "TRANS current limit",
        "mA",
        Unsigned,
        Current,
        "100–5000 mA; stored in EEPROM"
    );
    row!(
        Holding,
        40034,
        1,
        "Automatic retry",
        "",
        Bool,
        Bool,
        "0/1; stored in EEPROM"
    );
    row!(
        Holding,
        40035,
        1,
        "Retry interval",
        "s",
        Unsigned,
        Retry,
        "1–60 seconds; stored in EEPROM"
    );
    row!(
        Holding,
        40036,
        2,
        "RS485 baud configuration",
        "baud",
        Unsigned,
        Baud,
        "1200/2400/4800/9600/19200/38400/115200; applies after reboot. RS232 stays 115200."
    );
    row!(
        Holding,
        40040,
        2,
        "Set clock",
        "Unix s UTC",
        Clock,
        Clock,
        "0–4294967294 Unix seconds UTC"
    );
    row!(
        Holding,
        40050,
        1,
        "Select event",
        "sequence",
        Unsigned,
        Event,
        "0–65535; 0 selects newest event"
    );
    row!(
        Holding,
        40060,
        1,
        "Enter bootloader",
        "",
        Unsigned,
        Bootloader,
        "Write 0xB007: turns outputs off and resets after acknowledgment."
    );
    row!(
        Holding,
        40100,
        2,
        "Product code",
        "",
        Product,
        ReadOnly,
        "Expected PDU"
    );
    row!(
        Holding,
        40102,
        1,
        "Firmware version",
        "",
        Version,
        ReadOnly,
        "Major.minor"
    );
    row!(
        Holding,
        40103,
        1,
        "Firmware patch",
        "",
        Unsigned,
        ReadOnly,
        "Read only"
    );
    row!(
        Holding,
        40104,
        1,
        "CPU frequency",
        "MHz",
        Unsigned,
        ReadOnly,
        "Read only"
    );
    row!(
        Holding,
        40105,
        1,
        "Slave address",
        "",
        Unsigned,
        ReadOnly,
        "Configure through the firmware console; applies after reboot"
    );
    row!(
        Holding,
        40106,
        1,
        "RS485 baud low word",
        "",
        Unsigned,
        ReadOnly,
        "Baud = high word × 65536 + low word"
    );
    row!(
        Holding,
        40107,
        2,
        "Firmware build",
        "",
        Hex,
        ReadOnly,
        "Bit 28 dirty flag; bits 0–27 git commit"
    );
    row!(
        Holding,
        40109,
        1,
        "RS485 baud high word",
        "",
        Unsigned,
        ReadOnly,
        "See low word at 40106"
    );
    rows
}
