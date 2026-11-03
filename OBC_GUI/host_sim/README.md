# OBC GUI host simulation

Run the same terminal interface used on the OBC with simulated sensor data and
controls:

```sh
make -C service/OBC_GUI/host_sim run
```

Or display the one-shot simulated status:

```sh
make -C service/OBC_GUI/host_sim status
```

This mode needs a terminal, not X11. It does not read board snapshots, access
device nodes, launch board utilities, send CAN traffic, or write camera files.
The header says `HOST SIM` and the Home page labels system values simulated.

The OBC, EXP and PDU snapshots refresh every second with small changing sensor
values. AI and FPGA eFuse actions update their simulated GPIO states. Camera selection
reports the simulated SEL1/SEL0 bits and active-low OE state. Firmware command
results are marked simulated. Press `f` to cycle between healthy data, an
invalid BME688 sample, an unavailable PAC U2, PDU offline, and EXP offline.
RTC and CAMERA_LED settings also update only simulated state. `q` quits.
The MIPI page simulates both board power lines, sensor 0–3 routing and RAW
capture without writing an image file.

`make -C service/OBC_GUI/host_sim check` runs the Rust tests and drives the
actual TUI in a pseudo-terminal with deliberately nonexistent board tool paths.
