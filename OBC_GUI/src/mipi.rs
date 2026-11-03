//! Input validation for the two DAL MIPI camera boards and four sensor slots.

pub fn select_gpio(board: u8) -> Result<(), String> {
    if (1..=2).contains(&board) { Ok(()) }
    else { Err("MIPI camera must be 1 or 2".into()) }
}

pub fn check_sensor(sensor: u8) -> Result<(), String> {
    if sensor <= 3 { Ok(()) }
    else { Err("MIPI sensor must be 0..3".into()) }
}
