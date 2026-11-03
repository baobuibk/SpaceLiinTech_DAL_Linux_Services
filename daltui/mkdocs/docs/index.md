# daltui

`daltui` is a terminal UI (Rust, ratatui) for the DAL OBC, EXP and PDU subsystems. It has three data sources:

| Source | What it talks to | Use it for |
| --- | --- | --- |
| **SIM** | Built-in simulator, nothing is sent anywhere | Learning the UI, layout checks |
| **CONSOLE** | `dal_obc_fsw` console socket, text commands (`json <cmd>`) | Normal operation, on the board or against the sandbox rig |
| **RS-422** | Binary `A5 … CRC16` frames, via the fsw frame socket or a serial device | Protocol checks |

Tabs: HOME, OBC, EXP, PDU, LOG, CFG.

* [Operation](operation.md): build, run the simulation, run the UI on the real board.
* Source and key reference: `README.md` and `CLAUDE.md` in the project root.

## Build this documentation

```bash
pip install mkdocs
cd ~/Linux_DAL/service/daltui/mkdocs
mkdocs serve            # http://127.0.0.1:8000
mkdocs build            # static site in mkdocs/site
```
