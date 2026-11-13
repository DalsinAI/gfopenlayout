#!/usr/bin/env python3
"""bench/gflbench_run.py's parser, medians and report. MIT, Copyright (c) 2026 AmiCradle Team."""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "bench"))
import gflbench_run as g  # noqa: E402

OUT1 = """BENCH-INFO screen 1920x1080 depth 32
BENCH-INFO version GFLayoutBench 1.0 oldraw 1
BENCH fill_128 1000.00 fills/s
BENCH icon_blit_diskobject n/a no-diskobject
BENCH drawer_200_redraw 40.00 ms\r
some other line a Shell printed
BENCH-END ok 1 n/a
"""


def out(fill, drawer):
    return f"BENCH fill_128 {fill:.2f} fills/s\nBENCH drawer_200_redraw {drawer:.2f} ms\nBENCH-END ok 0 n/a\n"


class Parse(unittest.TestCase):
    def test_lines(self):
        r = g.parse(OUT1)
        self.assertEqual(r["results"]["fill_128"], (1000.0, "fills/s"))
        self.assertEqual(r["results"]["icon_blit_diskobject"], (None, "no-diskobject"))
        self.assertEqual(r["results"]["drawer_200_redraw"], (40.0, "ms"))     # a CR on the end
        self.assertEqual(r["info"][0], "screen 1920x1080 depth 32")
        self.assertEqual(r["end"], "ok")
        self.assertEqual(len(r["results"]), 3)

    def test_not_bench(self):
        r = g.parse("BENCHMARK 1 2 3\nBENCH a 1\nBENCH a 1 b c\n")
        self.assertEqual(r["results"], {})
        self.assertIsNone(r["end"])

    def test_bad_value(self):
        with self.assertRaises(ValueError):
            g.parse("BENCH a fast b/s\n")

    def test_fail(self):
        self.assertEqual(g.parse("BENCH-INFO error no window\nBENCH-END fail\n")["end"], "fail")


class Median(unittest.TestCase):
    def test_median_of_three(self):
        m = g.median_runs([g.parse(out(100, 50)), g.parse(out(300, 10)), g.parse(out(200, 30))])
        self.assertEqual(m["fill_128"][0], 200.0)
        self.assertEqual(m["drawer_200_redraw"][0], 30.0)
        self.assertEqual(m["fill_128"][1], "fills/s")

    def test_na_dropped(self):
        runs = [g.parse("BENCH x n/a why\n"), g.parse("BENCH x 4.00 a/s\n"), g.parse("BENCH x 2.00 a/s\n")]
        self.assertEqual(g.median_runs(runs)["x"][0], 3.0)
        self.assertIsNone(g.median_runs([g.parse("BENCH x n/a why\n")])["x"][0])

    def test_speedup(self):
        self.assertEqual(g.speedup(100, 400, "fills/s"), 4.0)
        self.assertEqual(g.speedup(40, 10, "ms"), 4.0)
        self.assertIsNone(g.speedup(None, 3, "ms"))
        self.assertIsNone(g.speedup(0, 3, "a/s"))

    def test_failed_run_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "failed.txt"
            p.write_text("BENCH x 1 x/s\nBENCH-END fail\n")
            with self.assertRaisesRegex(SystemExit, "BENCH-END fail"):
                g.read_runs(f"jit={p}")

    def test_incomplete_run_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "incomplete.txt"
            p.write_text("BENCH x 1 x/s\n")
            with self.assertRaisesRegex(SystemExit, "BENCH-END missing"):
                g.read_runs(f"jit={p}")


class Report(unittest.TestCase):
    def modes(self):
        off = [g.parse(out(100, 400)), g.parse(out(110, 380)), g.parse(out(90, 420))]
        jit = [g.parse(out(1000, 40)), g.parse(out(1100, 38)), g.parse(out(900, 42))]
        return [("off", off, ["off-1.txt", "off-2.txt", "off-3.txt"]), ("jit", jit, ["a", "b", "c"])]

    def test_table(self):
        md = g.report(self.modes(), "0123456789abcdef", "20261113", load="9.1", lab="t5-x")
        self.assertIn("# GFLayoutBench: gfopenlayout 01234567, 20261113", md)
        self.assertIn("| `fill_128` | fills/s | 100 | 1,000 | 10.0x |", md)
        self.assertIn("| `drawer_200_redraw` | ms | 400 | 40.0 | 10.0x |", md)
        self.assertIn("JIT_INTERP=1", md)
        self.assertIn("Host load: 9.1", md)

    def test_previous(self):
        md = g.report(self.modes(), "0123456789abcdef", "20261113")
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "old.md"
            p.write_text(md.replace('"fill_128": 1000.0', '"fill_128": 800.0'), encoding="utf-8")
            self.assertEqual(g.previous_figures(p)["jit"]["fill_128"], 800.0)
            md2 = g.report(self.modes(), "fedcba9876543210", "20261114", previous=p)
        self.assertIn("vs previous (JIT)", md2)
        self.assertIn("| `fill_128` | fills/s | 100 | 1,000 | 10.0x | +25.0% |", md2)
        self.assertIn("| `drawer_200_redraw` | ms | 400 | 40.0 | 10.0x | +0.0% |", md2)

    def test_figures_json(self):
        md = g.report(self.modes(), "0123456789abcdef", "20261113")
        block = md.split("```json\n")[1].split("\n```")[0]
        f = json.loads(block)
        self.assertEqual(f["median"]["off"]["drawer_200_redraw"], 400.0)
        self.assertEqual(f["units"]["fill_128"], "fills/s")

    def test_cli(self):
        with tempfile.TemporaryDirectory() as d:
            files = []
            for i, (a, b) in enumerate([(1, 2), (3, 4), (5, 6)]):
                p = Path(d) / f"r{i}.txt"
                p.write_text(out(a, b))
                files.append(str(p))
            dest = Path(d) / "bench" / "x.md"
            g.main(["report", "--sha", "abcdef0123", "--date", "20261113", "--mode", "jit=" + ",".join(files),
                    "--out", str(dest)])
            text = dest.read_text()
            self.assertIn("| `fill_128` | fills/s | 3.00 |", text)
            self.assertNotIn("JIT speed-up", text)


class LabGuard(unittest.TestCase):
    @staticmethod
    def agent(root):
        s = Path(root) / "devices" / "harddisks" / "DH0" / "S"
        s.mkdir(parents=True)
        (s / "ACAgent").write_text("x")

    def test_needs_agent(self):
        with tempfile.TemporaryDirectory() as d:
            (Path(d) / "labs" / "l1" / "devices" / "harddisks" / "DH0" / "S").mkdir(parents=True)
            with self.assertRaises(SystemExit):
                g.Lab("l1", d, Path(d) / "labs")
            (Path(d) / "labs" / "l1" / "devices" / "harddisks" / "DH0" / "S" / "ACAgent").write_text("x")
            lab = g.Lab("l1", d, Path(d) / "labs")
            self.assertTrue(str(lab.agent).endswith(os.path.join("DH0", "ACAgent")))

    def test_prefix_sibling_is_not_a_lab(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "labs").mkdir()
            self.agent(root / "labs-x" / "inst")
            with self.assertRaisesRegex(SystemExit, "not a lab"):
                g.Lab("../labs-x/inst", d, root / "labs")

    def test_symlink_to_live_instance_is_not_a_lab(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            labs, instances = root / "labs", root / "instances"
            labs.mkdir()
            self.agent(instances / "Instance-32")
            (labs / "evil").symlink_to(instances / "Instance-32", target_is_directory=True)
            old = os.environ.get("AMICRADLE_LIVE_INSTANCES")
            os.environ["AMICRADLE_LIVE_INSTANCES"] = str(instances)
            try:
                with self.assertRaisesRegex(SystemExit, "not a lab"):
                    g.Lab("evil", d, labs)
            finally:
                if old is None:
                    os.environ.pop("AMICRADLE_LIVE_INSTANCES", None)
                else:
                    os.environ["AMICRADLE_LIVE_INSTANCES"] = old

    def test_symlink_to_new_labs_root_is_allowed(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            labs, new_labs = root / "labs", root / "new-labs"
            labs.mkdir()
            self.agent(new_labs / "bench")
            (labs / "bench").symlink_to(new_labs / "bench", target_is_directory=True)
            old = os.environ.get("AMICRADLE_NEW_LABS")
            os.environ["AMICRADLE_NEW_LABS"] = str(new_labs)
            try:
                self.assertEqual(g.Lab("bench", d, labs).folder, (new_labs / "bench").resolve())
            finally:
                if old is None:
                    os.environ.pop("AMICRADLE_NEW_LABS", None)
                else:
                    os.environ["AMICRADLE_NEW_LABS"] = old


if __name__ == "__main__":
    unittest.main()
