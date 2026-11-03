use crate::camera_data::{crc32, Metadata};
use std::path::{Path, PathBuf};

#[cfg(windows)]
fn dialog(script: &str, initial: &Path) -> Result<Option<PathBuf>, String> {
    use std::os::windows::process::CommandExt;
    let output = std::process::Command::new("powershell.exe")
        .args(["-NoProfile", "-STA", "-Command", &format!("[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; Add-Type -AssemblyName System.Windows.Forms; {script}")])
        .env("DAL_DIALOG_INITIAL", initial).creation_flags(0x08000000)
        .output().map_err(|e| e.to_string())?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).into());
    }
    let path = String::from_utf8_lossy(&output.stdout).trim().to_owned();
    Ok(if path.is_empty() {
        None
    } else {
        Some(PathBuf::from(path))
    })
}
#[cfg(not(windows))]
fn dialog(_: &str, initial: &Path) -> Result<Option<PathBuf>, String> {
    if initial.as_os_str().is_empty() {
        return Err("Enter a file path in the camera panel".into());
    }
    Ok(Some(initial.to_path_buf()))
}
pub fn folder(initial: &Path) -> Result<Option<PathBuf>, String> {
    dialog("$d=New-Object System.Windows.Forms.FolderBrowserDialog; $d.Description='Select destination for camera image and metadata'; $d.SelectedPath=$env:DAL_DIALOG_INITIAL; if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.SelectedPath)}", initial)
}
#[cfg(windows)]
pub fn binary() -> Result<Option<PathBuf>, String> {
    dialog("$d=New-Object System.Windows.Forms.OpenFileDialog; $d.Title='Select thermal image binary'; $d.Filter='Thermal binary (*.bin)|*.bin|All files (*.*)|*.*'; if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.FileName)}", Path::new(""))
}
#[cfg(not(windows))]
pub fn binary_path(path: &Path) -> Result<Option<PathBuf>, String> {
    dialog("", path)
}
pub fn bitmap(initial: &Path) -> Result<Option<PathBuf>, String> {
    dialog("$d=New-Object System.Windows.Forms.SaveFileDialog; $d.Title='Save thermal BMP'; $d.Filter='Bitmap (*.bmp)|*.bmp'; $d.DefaultExt='bmp'; $d.AddExtension=$true; $d.OverwritePrompt=$true; $d.FileName=$env:DAL_DIALOG_INITIAL; if($d.ShowDialog() -eq 'OK'){[Console]::Write($d.FileName)}", initial)
}
pub fn load(path: &Path, camera: usize) -> Result<(Metadata, Vec<f32>), String> {
    let size = std::fs::metadata(path).map_err(|e| e.to_string())?.len();
    if size != 1536 && size != 3072 {
        return Err("Expected 1536 or 3072 bytes; trim XMODEM padding first".into());
    }
    let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
    let sidecar = path.with_extension("txt");
    let meta = if sidecar.exists() {
        let text = std::fs::read_to_string(sidecar).map_err(|e| e.to_string())?;
        Metadata::parse(&text).ok_or("Invalid image metadata sidecar")?
    } else {
        let encoding=match bytes.len() {1536=>1,3072=>2,_=>return Err("Expected a 32x24 int16 (1536 bytes) or float32 (3072 bytes) image; trim XMODEM padding first".into())};
        Metadata {
            boot: 0,
            object: 0,
            kind: 3,
            source: camera as u32,
            encoding,
            width: 32,
            height: 24,
            length: bytes.len(),
            crc: crc32(&bytes),
            run: 0,
            sequence: 0,
            timestamp_ms: 0,
        }
    };
    let values = meta.temperatures(&bytes)?;
    Ok((meta, values))
}
pub fn bmp(values: &[f32]) -> Result<Vec<u8>, String> {
    if values.len() != 768 || values.iter().any(|v| !v.is_finite()) {
        return Err("Invalid 32x24 temperature image".into());
    }
    let min = values.iter().copied().fold(f32::INFINITY, f32::min);
    let max = values.iter().copied().fold(f32::NEG_INFINITY, f32::max);
    let mut out = vec![0u8; 54 + 32 * 24 * 3];
    out[..2].copy_from_slice(b"BM");
    let size = out.len() as u32;
    out[2..6].copy_from_slice(&size.to_le_bytes());
    out[10..14].copy_from_slice(&54u32.to_le_bytes());
    out[14..18].copy_from_slice(&40u32.to_le_bytes());
    out[18..22].copy_from_slice(&32i32.to_le_bytes());
    out[22..26].copy_from_slice(&24i32.to_le_bytes());
    out[26..28].copy_from_slice(&1u16.to_le_bytes());
    out[28..30].copy_from_slice(&24u16.to_le_bytes());
    for y in 0..24 {
        for x in 0..32 {
            let v = values[y * 32 + x];
            let color = crate::camera_panel::color(if max > min {
                (v - min) / (max - min)
            } else {
                0.5
            });
            let at = 54 + ((23 - y) * 32 + x) * 3;
            out[at..at + 3].copy_from_slice(&[color.b(), color.g(), color.r()]);
        }
    }
    Ok(out)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn bitmap_header_orientation_and_invalid_values() {
        let mut values = vec![20.0; 768];
        values[0] = 10.0;
        values[767] = 30.0;
        let bytes = bmp(&values).unwrap();
        assert_eq!(&bytes[..2], b"BM");
        assert_eq!(bytes.len(), 2358);
        assert_ne!(&bytes[54..57], &bytes[54 + 23 * 96..57 + 23 * 96]);
        values[0] = f32::NAN;
        assert!(bmp(&values).is_err());
        assert!(bmp(&[]).is_err());
    }
    #[test]
    fn import_infers_encoding_and_checks_sidecar_crc() {
        let folder = PathBuf::from("../../unit_test/out")
            .join(format!("camera_import_{}", std::process::id()));
        std::fs::create_dir_all(&folder).unwrap();
        let path = folder.join("image.bin");
        let bytes = 25.0f32.to_le_bytes().repeat(768);
        std::fs::write(&path, &bytes).unwrap();
        let (meta, values) = load(&path, 1).unwrap();
        assert_eq!(meta.encoding, 2);
        assert_eq!(values[0], 25.0);
        std::fs::write(path.with_extension("txt"),"DATA boot=1 object=4 type=3 source=1 encoding=2 width=32 height=24 length=3072 crc32=0x0").unwrap();
        assert!(load(&path, 1).is_err());
        std::fs::remove_file(path.with_extension("txt")).unwrap();
        std::fs::write(&path, 1250i16.to_le_bytes().repeat(768)).unwrap();
        assert_eq!(load(&path, 0).unwrap().1[0], 25.0);
        std::fs::remove_file(&path).unwrap();
        std::fs::remove_dir(&folder).unwrap();
    }
}
