#!/usr/bin/env python3
"""Run the flight-software automation.  Usage: run_all.py [--tier sim] [--junit F] [--report F] [-k PAT]

Only tier `sim` exists so far. Tiers `target` and `hil` are specified in
docs/flight_sw_test_plan.md and are not implemented yet.
"""
import argparse
import pathlib
import sys
import time
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))


class Result(unittest.TextTestResult):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.rows = []
        self._t0 = 0.0

    def startTest(self, test):
        self._t0 = time.perf_counter()
        super().startTest(test)

    def _row(self, test, status, note=""):
        self.rows.append((test.id().split("cases.")[-1], status,
                          (time.perf_counter() - self._t0) * 1000, note.strip().splitlines()[-1] if note.strip() else ""))

    def addSuccess(self, test):
        super().addSuccess(test)
        self._row(test, "PASS")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._row(test, "FAIL", str(err[1]))

    def addError(self, test, err):
        super().addError(test, err)
        self._row(test, "ERROR", str(err[1]))

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._row(test, "SKIP", reason)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tier", default="sim", choices=["sim"])
    ap.add_argument("--report", help="write a Markdown result table")
    ap.add_argument("--junit", help="write JUnit XML (needs no extra packages)")
    ap.add_argument("-k", dest="pattern", help="only run tests whose id contains this text")
    args = ap.parse_args()

    suite = unittest.defaultTestLoader.discover(str(HERE / "cases"), top_level_dir=str(HERE.parent))

    def flatten(s):
        for t in s:
            yield from flatten(t) if isinstance(t, unittest.TestSuite) else [t]
    tests = [t for t in flatten(suite) if not args.pattern or args.pattern in t.id()]
    runner = unittest.TextTestRunner(resultclass=Result, verbosity=2)
    result = runner.run(unittest.TestSuite(tests))

    if args.report:
        lines = ["| Test | Status | ms | Note |", "| --- | --- | ---: | --- |"]
        lines += [f"| {n} | {s} | {ms:.0f} | {note.replace('|', '/')[:100]} |" for n, s, ms, note in result.rows]
        pathlib.Path(args.report).write_text("\n".join(lines) + "\n")
    if args.junit:
        import xml.etree.ElementTree as ET
        root = ET.Element("testsuite", name="fsw_auto", tests=str(len(result.rows)),
                          failures=str(sum(r[1] in ("FAIL", "ERROR") for r in result.rows)))
        for name, status, ms, note in result.rows:
            case = ET.SubElement(root, "testcase", name=name, time=f"{ms / 1000:.3f}")
            if status in ("FAIL", "ERROR"):
                ET.SubElement(case, "failure", message=note)
            elif status == "SKIP":
                ET.SubElement(case, "skipped", message=note)
        ET.ElementTree(root).write(args.junit)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
