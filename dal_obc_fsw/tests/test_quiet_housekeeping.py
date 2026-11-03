"""Verbose housekeeping filter hides routine reads, not commands or errors."""

import time

from fsw_auto.fixtures import Fsw


def wait_for_log(fsw, text):
    deadline = time.monotonic() + 2
    while text not in "\n".join(fsw.log) and time.monotonic() < deadline:
        time.sleep(0.02)
    assert text in "\n".join(fsw.log), fsw.log_tail(30)


def main():
    quiet = Fsw(fsw_args=("-v", "--quiet-housekeeping"))
    try:
        quiet.start()
        quiet.link.txrx(4, 0).expect_ack(4, 0)
        quiet.link.txrx(4, 2).expect_ack(4, 2)
        quiet.link.txrx(1, 4).expect_err(0)
        quiet.link.txrx(0, 0).expect_ack(0, 0)
        wait_for_log(quiet, "TYPE=0x00")
        quiet.console.run("tm_get")
        quiet.console.run("sys_info")
        quiet.console.run("not_a_command")
        wait_for_log(quiet, "[CONSOLE] RX: sys_info")
        wait_for_log(quiet, "[CONSOLE] TX")
        log = "\n".join(quiet.log)
        assert "TYPE=0x04" not in log and "ACK/ERR=0x04" not in log, log
        assert "TYPE=0x01  ADDR=0x04" not in log, log
        assert "ACK/ERR=0xFF (ERR)" in log, log
        assert "[CONSOLE] RX: tm_get" not in log, log
        assert "[CONSOLE] RX: not_a_command" in log, log
        assert "[CONSOLE] TX" in log and "PDU" in log, log
    finally:
        quiet.stop()

    normal = Fsw(fsw_args=("-v",))
    try:
        normal.start()
        normal.link.txrx(4, 2).expect_ack(4, 2)
        wait_for_log(normal, "TYPE=0x04  ADDR=0x02")
        wait_for_log(normal, "ACK/ERR=0x04")
        normal.console.run("tm_get")
        wait_for_log(normal, "[CONSOLE] RX: tm_get")
        assert "[CONSOLE] TX" in "\n".join(normal.log)
        shell = normal.console.run("gui_shell", "2", "printf", "hello")
        assert '"text":"hello"' in shell, shell
        wait_for_log(normal, "[CONSOLE] RX: gui_shell 2 printf hello")
        assert '"text":"hello"' in "\n".join(normal.log)
    finally:
        normal.stop()

    daemon_env = Fsw(extra_env={"DAL_OBC_VERBOSE": "1",
                                "DAL_OBC_QUIET_HOUSEKEEPING": "1"})
    try:
        daemon_env.start()
        daemon_env.link.txrx(4, 2).expect_ack(4, 2)
        daemon_env.link.txrx(0, 0).expect_ack(0, 0)
        wait_for_log(daemon_env, "TYPE=0x00")
        assert "TYPE=0x04" not in "\n".join(daemon_env.log)
    finally:
        daemon_env.stop()
    print("Verbose housekeeping filter and default trace: OK")


if __name__ == "__main__":
    main()
