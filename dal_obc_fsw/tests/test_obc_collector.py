"""Check that flight software replaces the OBC part of hk_service."""

import json
import time

from fsw_auto.fixtures import Fsw


def main():
    fsw = Fsw(with_snapshot=False)
    run_dir = fsw.tmp / "obc_run"
    fsw.extra_env = {
        "DAL_OBC_HK_COLLECTOR_ENABLE": "1",
        "DAL_OBC_HK_RUN_DIR": str(run_dir),
        "DAL_OBC_HK_SNAPSHOT_PATH": str(run_dir / "obc_data.json"),
    }
    try:
        fsw.start()
        snapshot = run_dir / "obc_data.json"
        deadline = time.monotonic() + 5
        while not snapshot.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert snapshot.exists(), fsw.log_tail(20)
        data = json.loads(snapshot.read_text())
        assert data["board"] == "obc" and data["schema_version"] == 1
        assert (run_dir / "obc_status").read_text() == "online\n"
        csvs = list((fsw.data_dir / "logs" / "hk").glob("HK0_*.csv"))
        assert len(csvs) == 1 and "pac_valid" in csvs[0].read_text()
        print("OBC collector snapshot and HK0 CSV: OK")
    finally:
        fsw.stop()


if __name__ == "__main__":
    main()
