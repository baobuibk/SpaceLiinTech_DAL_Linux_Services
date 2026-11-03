#!/usr/bin/env python3
"""
bee_dal_repl.py -- entry point
Setup:
    pip install -r requirements.txt

Usage:
    python bee_dal_repl.py COM5                 (Windows)
    python bee_dal_repl.py /dev/ttyUSB0         (Linux)
    python bee_dal_repl.py COM5 --verbose       (full TX/RX frame + timing dump)

Then at the "bee-dal>" prompt: ping, sys_info, sys_rtc, pdu_vout,
pwr_en, pwr_stat, tm_get, tm_obc_get, shell, dat_lst, dat_sel, dat_get,
dat_des, dat_del, raw, verbose, timeout, help, quit.
"""
from bee_dal_client.repl import main

if __name__ == "__main__":
    main()
