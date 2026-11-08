#!/usr/bin/env python3
"""Package a cross-built Windows executable; stdlib only."""
import hashlib
from pathlib import Path
import shutil
import sys
import zipfile

root = Path(__file__).resolve().parent.parent
source = Path(sys.argv[1]).resolve()
if not source.is_file() or source.read_bytes()[:2] != b"MZ":
    raise SystemExit(f"Not a Windows executable: {source}")
folder = root / "target/windows-x64/daltui-windows-x64"
folder.mkdir(parents=True, exist_ok=True)
shutil.copy2(source, folder / "daltui.exe")
shutil.copy2(root / "tools/windows-README.txt", folder / "README.txt")
(folder / "SHA256SUMS").write_text("".join(
    f"{hashlib.sha256((folder / name).read_bytes()).hexdigest()}  {name}\n"
    for name in ("daltui.exe", "README.txt")
), encoding="utf-8")
archive = folder.parent / "daltui-windows-x64.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
    for name in ("daltui.exe", "README.txt", "SHA256SUMS"):
        output.write(folder / name, f"{folder.name}/{name}")
print(f"EXE: {folder / 'daltui.exe'}")
print(f"ZIP: {archive}")
