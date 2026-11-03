#!/usr/bin/env python3
"""Refresh the Yocto source archives from the editable service projects."""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parent
RECIPE_ROOT = ROOT.parent / "sources/meta-src-dalobcv110/recipes-app"
PROJECTS = {
    "hk_service": ("hk-service", "hk_service_project"),
    "temp_service": ("temp-service", "temp_service_project"),
    "bme_service": ("bme-service", "bme_service_project"),
}


def package(name: str, recipe: str, archive_root: str) -> Path:
    project = ROOT / name
    archive = RECIPE_ROOT / recipe / "files" / f"{archive_root}.zip"
    files = [project / "Makefile"]
    if name == "bme_service":
        files.append(project / "LICENSE-BOSCH")
    files.extend(sorted((project / "include").rglob("*.h")))
    files.extend(sorted((project / "src").rglob("*.c")))
    with ZipFile(archive, "w", compression=ZIP_DEFLATED) as output:
        for path in files:
            item = ZipInfo(f"{archive_root}/{path.relative_to(project)}")
            item.date_time = (1980, 1, 1, 0, 0, 0)
            item.external_attr = 0o100644 << 16
            item.compress_type = ZIP_DEFLATED
            output.writestr(item, path.read_bytes())
    return archive


if __name__ == "__main__":
    for project_name, (recipe_name, root_name) in PROJECTS.items():
        print(package(project_name, recipe_name, root_name))
