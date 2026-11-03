#!/usr/bin/env python3
"""Package the editable DAL OBC flight software for its Yocto recipe."""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parent
PROJECT = ROOT / "dal_obc_fsw"
ARCHIVE = (ROOT.parent / "sources/meta-src-dalobcv110/recipes-app/"
           "dal-obc-fsw/files/dal_obc_fsw.zip")

paths = [PROJECT / "Makefile"]
paths += sorted((PROJECT / "include").rglob("*.h"))
paths += sorted((PROJECT / "src").rglob("*.c"))
paths += sorted((PROJECT / "lib").rglob("*.h"))
paths += sorted((PROJECT / "lib").rglob("*.c"))
paths += sorted((PROJECT / "tool/pc_client").rglob("*.py"))
paths.append(PROJECT / "tool/pc_client/requirements.txt")

with ZipFile(ARCHIVE, "w", compression=ZIP_DEFLATED) as output:
    for path in paths:
        item = ZipInfo(f"dal_obc_fsw/{path.relative_to(PROJECT)}")
        item.date_time = (1980, 1, 1, 0, 0, 0)
        item.external_attr = 0o100644 << 16
        item.compress_type = ZIP_DEFLATED
        output.writestr(item, path.read_bytes())

print(ARCHIVE)
