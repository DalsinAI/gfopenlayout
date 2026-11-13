#!/usr/bin/env python3
"""GFLayoutBench's runner and report (Workbench build plan, wave N, N4): the same benchmark on a plain 68k (the
AC090's interpreter alone, JIT_INTERP=1), with the AC090's JIT, and later native, run at each merge.

    gflbench_run.py parse OUT.txt...                     the BENCH lines of each output, as JSON
    gflbench_run.py report --sha SHA --mode off=A,B,C --mode jit=D,E,F [--mode native=...] [--load TEXT]
                          [--lab NAME] [--date YYYYMMDD] [--previous OLD.md] [--out PATH]
    gflbench_run.py lab --lab NAME --binary GFLayoutBench --sha SHA [--modes off,jit] [--runs 3] [--outdir DIR]
                          [--out PATH] [--previous OLD.md]

parse and report need nothing but Python. lab runs on the Cradle host (daletop): one lab at a time, a copy of
Instance-32 that already has the agent (Cradle's install_agent). For each mode it stops the lab, puts the program
in the lab's SYS:C (refused outside the labs folder: never an instance), starts the lab with that mode's
environment through scripts/lab_instance.py, runs the program RUNS times through the agent (SYS:ACAgent/run.script,
as Cradle's amiga_run does), keeps each output in OUTDIR, and stops the lab. The PC's load is read before each run.

Each result's figure is the median of the runs (each run's own figure is already the median of the program's three
timed runs). The report is Markdown, with the figures as JSON at its end for the next run's --previous.
MIT, Copyright (c) 2026 AmiCradle Team.
"""
import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

# The modes: how the lab's runtime is started for each, and the report's column for it
MODES = {
    "off": {"title": "68k plain (AC090 JIT off)", "env": {"JIT_INTERP": "1"}},
    "jit": {"title": "AC090 JIT", "env": {}},
    "native": {"title": "native", "env": None},       # filled from a host or card run's output files
}
BENCH = re.compile(r"^BENCH (\S+) (\S+) (\S+)\s*$")
INFO = re.compile(r"^BENCH-INFO (.*?)\s*$")
END = re.compile(r"^BENCH-END (\S+)")


def parse(text):
    """{'results': {name: (value or None, unit)}, 'info': [...], 'end': 'ok'|'fail'|None} from one output."""
    out = {"results": {}, "info": [], "end": None}
    for raw in text.splitlines():
        line = raw.strip("\r\n\t ")
        m = BENCH.match(line)
        if m:
            name, value, unit = m.groups()
            if value == "n/a":
                out["results"][name] = (None, unit)
            else:
                try:
                    out["results"][name] = (float(value), unit)
                except ValueError:
                    raise ValueError(f"bad value in {line!r}")
            continue
        m = INFO.match(line)
        if m:
            out["info"].append(m.group(1))
            continue
        m = END.match(line)
        if m:
            out["end"] = m.group(1)
    return out


def higher_is_better(unit):
    return unit != "ms"


def median_runs(runs):
    """{name: (median or None, unit, [each run's value])} over parsed runs; a result n/a in every run stays None."""
    names = []
    for r in runs:
        for n in r["results"]:
            if n not in names:
                names.append(n)
    out = {}
    for n in names:
        vals = [r["results"][n][0] for r in runs if n in r["results"] and r["results"][n][0] is not None]
        unit = next(r["results"][n][1] for r in runs if n in r["results"])
        out[n] = (statistics.median(vals) if vals else None, unit, vals)
    return out


def fmt(v):
    if v is None:
        return "n/a"
    if v >= 100:
        return f"{v:,.0f}"
    if v >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def speedup(a, b, unit):
    """How many times faster b is than a."""
    if a is None or b is None or a == 0 or b == 0:
        return None
    return b / a if higher_is_better(unit) else a / b


def previous_figures(path):
    """The JSON block at the end of an earlier report: {mode: {name: value}}."""
    text = Path(path).read_text(encoding="utf-8")
    m = re.search(r"```json\n(\{.*?\})\n```", text, re.S)
    if not m:
        raise ValueError(f"{path}: no figures block")
    return json.loads(m.group(1)).get("median", {})


def report(modes, sha, date, load="", lab="", previous=None, notes=()):
    """modes: [(mode, [parsed runs], [file names])]. Markdown."""
    meds = {mode: median_runs(runs) for mode, runs, _ in modes}
    names = []
    for mode, _, _ in modes:
        for n in meds[mode]:
            if n not in names:
                names.append(n)
    units = {}
    for mode, _, _ in modes:
        for n, (_, u, _) in meds[mode].items():
            units.setdefault(n, u)
    prev = previous_figures(previous) if previous else {}
    L = [f"# GFLayoutBench: gfopenlayout {sha[:8]}, {date}", ""]
    L.append("Workbench build plan wave N, N4: the same GFOpenLayout drawing benchmark (`bench/` in DalsinAI/gfopenlayout)"
             " on each target. Each figure is the median of the runs listed below; each run's own figure is the median"
             " of three timed runs of at least 500 ms.")
    L.append("")
    if lab:
        L.append(f"- Lab: `{lab}` (a copy of Instance-32; Instance-32 itself untouched), one lab at a time.")
    if load:
        L.append(f"- Host load: {load}")
    for mode, runs, files in modes:
        ends = ", ".join(r["end"] or "none" for r in runs)
        L.append(f"- {MODES.get(mode, {'title': mode})['title']}: {len(runs)} runs (BENCH-END {ends})"
                 + (f"; environment {' '.join(f'{k}={v}' for k, v in (MODES.get(mode, {}).get('env') or {}).items())}"
                    if MODES.get(mode, {}).get("env") else ""))
    for n in notes:
        L.append(f"- {n}")
    L.append("")
    cols = [m for m, _, _ in modes]
    head = ["Result", "Unit"] + [MODES.get(m, {"title": m})["title"] for m in cols]
    if "off" in cols and "jit" in cols:
        head.append("JIT speed-up")
    if prev:
        head.append("vs previous (JIT)")
    L.append("| " + " | ".join(head) + " |")
    L.append("|" + "|".join(["---"] * 2 + ["---:"] * (len(head) - 2)) + "|")
    for n in names:
        u = units[n]
        row = [f"`{n}`", u] + [fmt(meds[m].get(n, (None,))[0]) for m in cols]
        if "off" in cols and "jit" in cols:
            s = speedup(meds["off"].get(n, (None,))[0], meds["jit"].get(n, (None,))[0], u)
            row.append(f"{s:.1f}x" if s else "n/a")
        if prev:
            old = prev.get("jit", {}).get(n)
            new = meds.get("jit", {}).get(n, (None,))[0]
            s = speedup(old, new, u)
            row.append(f"{(s - 1) * 100:+.1f}%" if s else "n/a")
        L.append("| " + " | ".join(row) + " |")
    L.append("")
    L.append("Units: `/s` and `icons` are better higher, `ms` lower. `drawer_200_redraw` is one full redraw of a drawer"
             " of 200 icons (icon and label each, 1024x640); `drawer_200_icons_per_frame` is how many of its icons fit"
             " in a 60 Hz frame; `list_scroll_1000` scrolls a 20-row view through 1000 list rows a row a step, each"
             " step a damage-rect redraw (this benchmark deliberately does not use scroll-by-copy).")
    L.append("")
    L.append("## Each run")
    L.append("")
    for mode, runs, files in modes:
        L.append(f"### {MODES.get(mode, {'title': mode})['title']}")
        L.append("")
        for i, r in enumerate(runs):
            src = f" ({files[i]})" if i < len(files) and files[i] else ""
            L.append(f"- run {i + 1}{src}: " + "; ".join(r["info"]))
        L.append("")
        L.append("| Result | " + " | ".join(f"run {i + 1}" for i in range(len(runs))) + " |")
        L.append("|---|" + "---:|" * len(runs))
        for n in meds[mode]:
            L.append(f"| `{n}` | " + " | ".join(fmt(r["results"].get(n, (None,))[0]) for r in runs) + " |")
        L.append("")
    figures = {"sha": sha, "date": date, "load": load, "lab": lab,
               "median": {m: {n: v for n, (v, _, _) in meds[m].items()} for m in cols},
               "units": units}
    L.append("## Figures")
    L.append("")
    L.append("```json")
    L.append(json.dumps(figures, sort_keys=True))
    L.append("```")
    L.append("")
    return "\n".join(L)


def read_runs(spec):
    """'off=a.txt,b.txt' -> ('off', [parsed], [names])"""
    if "=" not in spec:
        raise SystemExit(f"--mode wants MODE=FILE,FILE...: {spec!r}")
    mode, files = spec.split("=", 1)
    names = [f for f in files.split(",") if f]
    runs = [parse(Path(f).read_text(encoding="latin-1")) for f in names]
    for f, r in zip(names, runs):
        if not r["results"]:
            raise SystemExit(f"{f}: no BENCH lines")
        require_complete_run(r, f)
    return mode, runs, [Path(f).name for f in names]


# ---- on the Cradle host

def require_complete_run(result, source="benchmark"):
    """Refuse a failed or truncated run before it can enter a baseline."""
    if result["end"] != "ok":
        state = result["end"] if result["end"] is not None else "missing"
        raise SystemExit(f"{source}: BENCH-END {state} (not a complete run)")


def within(path, root):
    """Path-aware containment, after following links."""
    path, root = Path(path).resolve(), Path(root).resolve()
    return path == root or path.is_relative_to(root)

def loadavg():
    try:
        return Path("/proc/loadavg").read_text().split()[:3]
    except OSError:
        return ["?", "?", "?"]


class Lab:
    """A lab through scripts/lab_instance.py and its agent's folder."""

    def __init__(self, name, app_root, labs_root, who="Team 5 Franklin", purpose="GFLayoutBench N4"):
        self.name, self.who, self.purpose = name, who, purpose
        self.app_root = Path(app_root).expanduser()
        self.labs_root = Path(labs_root).expanduser().resolve()
        self.folder = (self.labs_root / name).resolve()
        allowed = [self.labs_root]
        if os.environ.get("AMICRADLE_NEW_LABS"):
            allowed.append(Path(os.environ["AMICRADLE_NEW_LABS"]).expanduser().resolve())
        instances = Path(os.environ.get("AMICRADLE_LIVE_INSTANCES", "~/AmiCradleInstances")).expanduser().resolve()
        if not any(within(self.folder, root) for root in allowed) or within(self.folder, instances):
            raise SystemExit(f"{name}: not a lab")
        self.sys = self.folder / "devices" / "harddisks" / "DH0"
        if not (self.sys / "S" / "ACAgent").is_file():
            raise SystemExit(f"lab {name} has no agent (S:ACAgent): Cradle's install_agent first")
        self.agent = self.sys / "ACAgent"

    def tool(self, *args):
        cmd = [sys.executable, str(self.app_root / "scripts" / "lab_instance.py"), *args]
        subprocess.run(cmd, check=True)

    def stop(self):
        self.tool("stop", self.name)

    def start(self, env):
        args = ["start", self.name, "--who", self.who, "--purpose", self.purpose]
        for k, v in env.items():
            args += ["--env", f"{k}={v}"]
        self.tool(*args)

    def put(self, binary):
        dest = self.sys / "C" / "GFLayoutBench"
        if not within(dest.resolve().parent, self.folder):
            raise SystemExit("refusing to write outside the lab")
        shutil.copyfile(binary, dest)

    def run(self, script, timeout=900, boot_wait=240):
        """What the script printed, through the agent, as amiga_run does."""
        self.agent.mkdir(exist_ok=True)
        done = self.agent / "run.done"
        if done.exists():
            done.unlink()
        tmp = self.agent / "run.tmp"
        tmp.write_bytes(("Stack 262144\n" + script.rstrip("\n") + "\n").encode("latin-1"))
        os.replace(tmp, self.agent / "run.script")
        t0 = time.monotonic()
        while not done.exists():
            waited = time.monotonic() - t0
            if (self.agent / "run.script").exists() and waited > boot_wait:
                raise SystemExit("the agent didn't pick the script up (the lab still booting, or hung)")
            if waited > timeout:
                raise SystemExit("the benchmark ran too long")
            time.sleep(1)
        time.sleep(0.3)
        return (self.agent / "run.out").read_text(encoding="latin-1", errors="replace")


def run_lab(a):
    lab = Lab(a.lab, a.app_root, a.labs_root)
    outdir = Path(a.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    modes, loads = [], []
    for mode in a.modes.split(","):
        env = MODES[mode]["env"]
        if env is None:
            raise SystemExit(f"{mode} doesn't run in a lab")
        lab.stop()
        lab.put(a.binary)
        lab.start(env)
        runs, files = [], []
        try:
            for i in range(a.runs):
                la = loadavg()
                loads.append(float(la[0]) if la[0] != "?" else None)
                text = lab.run(f"SYS:C/GFLayoutBench LABEL={a.lab}-{mode}-{i + 1}")
                f = outdir / f"{mode}-{i + 1}.txt"
                f.write_text(f"# load {' '.join(la)}\n" + text, encoding="latin-1")
                parsed = parse(text)
                require_complete_run(parsed, f"{mode} run {i + 1}")
                runs.append(parsed)
                files.append(f.name)
                print(f"{mode} run {i + 1}: load {la[0]}, {len(runs[-1]['results'])} results", flush=True)
        finally:
            lab.stop()
        modes.append((mode, runs, files))
    known = [x for x in loads if x is not None]
    load = f"1-minute load {min(known):.1f} to {max(known):.1f} during the runs" if known else ""
    text = report(modes, a.sha, a.date, load=load, lab=a.lab, previous=a.previous)
    write(text, a.out, a.sha, a.date)


def write(text, out, sha, date):
    if not out:
        out = f"gflayout-{date}-{sha[:8]}.md"
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_text(text, encoding="utf-8")
    print(f"wrote {out}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("parse")
    p.add_argument("files", nargs="+")
    r = sub.add_parser("report")
    r.add_argument("--sha", required=True)
    r.add_argument("--mode", action="append", required=True, help="MODE=FILE,FILE... (off, jit, native)")
    r.add_argument("--date", default=time.strftime("%Y%m%d"))
    r.add_argument("--load", default="")
    r.add_argument("--lab", default="")
    r.add_argument("--note", action="append", default=[])
    r.add_argument("--previous")
    r.add_argument("--out")
    l = sub.add_parser("lab")
    l.add_argument("--lab", required=True)
    l.add_argument("--binary", required=True)
    l.add_argument("--sha", required=True)
    l.add_argument("--modes", default="off,jit")
    l.add_argument("--runs", type=int, default=3)
    l.add_argument("--date", default=time.strftime("%Y%m%d"))
    l.add_argument("--outdir", default="gflbench-out")
    l.add_argument("--previous")
    l.add_argument("--out")
    l.add_argument("--app-root", default=os.environ.get("AMICRADLE_APP", "~/AmiCradle"))
    l.add_argument("--labs-root", default=os.environ.get("AMICRADLE_LABS", "~/AmiCradle-dev/labs"))
    a = ap.parse_args(argv)
    if a.cmd == "parse":
        print(json.dumps({f: parse(Path(f).read_text(encoding="latin-1")) for f in a.files}, indent=1))
    elif a.cmd == "report":
        modes = [read_runs(s) for s in a.mode]
        write(report(modes, a.sha, a.date, load=a.load, lab=a.lab, previous=a.previous, notes=a.note),
              a.out, a.sha, a.date)
    else:
        run_lab(a)
    return 0


if __name__ == "__main__":
    sys.exit(main())
