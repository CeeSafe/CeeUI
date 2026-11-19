#!/usr/bin/env python3
"""ClayWind's benchmark harness (roadmap P0): measures the demo on this machine and checks each number against its
budget for this system in bench/budgets.json.

What it measures, all through the headless host (claywind-hosts/headless, the software renderer) and the desktop
host's binary:

  build_s              a clean build of the headless host (out/ removed first), in seconds
  headless_mb          the headless host's binary, stripped, in MB
  desktop_mb           the desktop host's binary (the demo in a window), stripped, in MB
  frame_ms_*           a frame of a demo tab laid out and drawn whole, in ms, the median of three runs of
                       BENCH_FRAMES frames: dashboard at 1x and 2x (Retina), effects at 1x
  first_frame_ms       the process started, fonts loaded, the first frame laid out, drawn and written, in ms (the
                       median of five starts)
  peak_mb              the most memory the headless host held while running the dashboard at 2x, in MB (not on
                       Windows)
  idle_redraws         frames that redrew anything, of IDLE_FRAMES, on the controls tab with no input (Damage): 1, the
                       first frame, when nothing moves
  incremental_differs  tabs whose frame drawn through Damage (only what changed, every frame) is not the frame drawn
                       whole, byte for byte: 0

A budget is the most a number may be; a number over its budget fails the run (exit 1). A system with no budgets
only records. The fonts are Arial, Arial Bold and Courier New (macOS: the system's; Windows: arial.ttf, arialbd.ttf,
cour.ttf; Linux: Liberation Sans and Mono, which have Arial's and Courier New's metrics).

usage: python3 tools/bench.py [--budgets bench/budgets.json] [--json OUT] [--markdown OUT] [--skip-build]
       python3 tools/bench.py --fonts DIR   (only put the three fonts in DIR, for the other hosts' checks)
"""

import argparse
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADLESS = os.path.join(ROOT, "claywind-hosts", "headless")
DESKTOP = os.path.join(ROOT, "claywind-hosts", "desktop")
EXE = ".exe" if os.name == "nt" else ""
BENCH_FRAMES = 200
IDLE_FRAMES = 60
TABS = 6  # claywind-demo's NAV_COUNT
DASHBOARD, CONTROLS, EFFECTS = 0, 1, 2


def system():
    return {"Darwin": "macos", "Linux": "linux", "Windows": "windows"}.get(platform.system(), platform.system().lower())


def fonts(work):
    """A folder holding Arial.ttf, Arial Bold.ttf and Courier New.ttf, as the headless host loads them."""
    if system() == "macos":
        return "/System/Library/Fonts/Supplemental"
    if system() == "windows":
        windir = os.environ.get("WINDIR", r"C:\Windows")
        pick = {"Arial.ttf": "arial.ttf", "Arial Bold.ttf": "arialbd.ttf", "Courier New.ttf": "cour.ttf"}
        source = os.path.join(windir, "Fonts")
    else:
        pick = {"Arial.ttf": "LiberationSans-Regular.ttf", "Arial Bold.ttf": "LiberationSans-Bold.ttf",
                "Courier New.ttf": "LiberationMono-Regular.ttf"}
        source = None
        for d in ("/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/liberation2",
                  "/usr/share/fonts/liberation", "/usr/share/fonts/liberation-sans"):
            if os.path.exists(os.path.join(d, pick["Arial.ttf"])):
                source = d
                break
        if source is None:
            sys.exit("bench: no Liberation fonts (Ubuntu: apt-get install fonts-liberation)")
    folder = os.path.join(work, "fonts")
    os.makedirs(folder, exist_ok=True)
    for name, file in pick.items():
        shutil.copyfile(os.path.join(source, file), os.path.join(folder, name))
    return folder


def stripped_mb(binary, work):
    """The binary's size in MB once its symbols are stripped (as a release ships it)."""
    copy = os.path.join(work, os.path.basename(binary) + ".stripped")
    shutil.copyfile(binary, copy)
    if os.name != "nt" and shutil.which("strip"):
        subprocess.run(["strip", copy], check=True, capture_output=True)
    return os.path.getsize(copy) / 1e6


def build(package, clean):
    if clean:
        shutil.rmtree(os.path.join(package, "out"), ignore_errors=True)
    started = time.perf_counter()
    done = subprocess.run(["ceesafec", "build"], cwd=package, capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(f"bench: {os.path.relpath(package, ROOT)} does not build:\n{done.stdout}{done.stderr}")
    return time.perf_counter() - started


def headless(env, **extra):
    """Runs the headless host with `env` and `extra` CLAYWIND_ variables; its output."""
    run = dict(env)
    for key, value in extra.items():
        run["CLAYWIND_" + key] = str(value)
    done = subprocess.run([os.path.join(HEADLESS, "out", "headless" + EXE)], env=run, capture_output=True,
                          text=True, timeout=600)
    if done.returncode != 0:
        sys.exit(f"bench: the headless host failed ({extra}):\n{done.stdout}{done.stderr}")
    return done.stdout


def frame_ms(env, tab, ratio):
    runs = []
    for _ in range(3):
        out = headless(env, BENCH=BENCH_FRAMES, SCREENSHOT_TAB=tab, PIXEL_RATIO=ratio)
        runs.append(float(out.split("ms-per-frame")[1].split()[0]))
    return statistics.median(runs)


def first_frame_ms(env, work):
    runs = []
    for k in range(5):
        shot = os.path.join(work, f"first{k}.bmp")
        started = time.perf_counter()
        headless(env, SCREENSHOT=shot, SHOT_FRAME=0)
        runs.append((time.perf_counter() - started) * 1000.0)
    return statistics.median(runs)


def peak_mb(env):
    """The headless host's peak resident memory, measured in a process of its own so that the builds' compilers
    are not counted."""
    if os.name == "nt":
        return None
    probe = ("import os, resource, subprocess, sys\n"
             "subprocess.run(sys.argv[1:], check=True, capture_output=True)\n"
             "peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss\n"
             "print(peak / 1e6 if sys.platform == 'darwin' else peak / 1e3)\n")
    run = dict(env, CLAYWIND_BENCH=str(BENCH_FRAMES), CLAYWIND_PIXEL_RATIO="2")
    done = subprocess.run([sys.executable, "-c", probe, os.path.join(HEADLESS, "out", "headless" + EXE)], env=run,
                          capture_output=True, text=True, check=True)
    return float(done.stdout.strip())


def idle_redraws(env, work):
    out = headless(env, INCREMENTAL=1, SCREENSHOT_TAB=CONTROLS, SHOT_FRAME=IDLE_FRAMES - 1,
                   SCREENSHOT=os.path.join(work, "idle.bmp"))
    # "frames redrawn R of N"
    return int(out.split("frames redrawn")[1].split()[0])


def incremental_differs(env, work):
    """The tabs whose last frame differs between a whole redraw every frame and Damage's partial redraws."""
    differ = []
    for tab in range(TABS):
        whole = os.path.join(work, f"whole{tab}.bmp")
        partial = os.path.join(work, f"partial{tab}.bmp")
        headless(env, SCREENSHOT_TAB=tab, SCREENSHOT=whole)
        headless(env, SCREENSHOT_TAB=tab, SCREENSHOT=partial, INCREMENTAL=1)
        with open(whole, "rb") as a, open(partial, "rb") as b:
            if a.read() != b.read():
                differ.append(tab)
    return differ


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--budgets", default=os.path.join(ROOT, "bench", "budgets.json"))
    parser.add_argument("--json", help="write the numbers here")
    parser.add_argument("--markdown", help="append a table of the numbers here (CI: $GITHUB_STEP_SUMMARY)")
    parser.add_argument("--skip-build", action="store_true", help="use the hosts as built (no build_s)")
    parser.add_argument("--fonts", help="only copy the three fonts into this folder and print it")
    args = parser.parse_args()
    if args.fonts:
        os.makedirs(args.fonts, exist_ok=True)
        folder = fonts(args.fonts)
        if folder != os.path.join(args.fonts, "fonts"):
            for name in ("Arial.ttf", "Arial Bold.ttf", "Courier New.ttf"):
                shutil.copyfile(os.path.join(folder, name), os.path.join(args.fonts, name))
        else:
            for name in os.listdir(folder):
                shutil.move(os.path.join(folder, name), os.path.join(args.fonts, name))
            os.rmdir(folder)
        print(args.fonts)
        return 0
    os_name = system()
    numbers = {}
    with tempfile.TemporaryDirectory() as work:
        env = dict(os.environ, CLAYWIND_FONT_DIR=fonts(work))
        for name in list(env):
            if name.startswith("CLAYWIND_") and name != "CLAYWIND_FONT_DIR":
                del env[name]
        if not args.skip_build:
            numbers["build_s"] = build(HEADLESS, clean=True)
            build(DESKTOP, clean=False)
        numbers["headless_mb"] = stripped_mb(os.path.join(HEADLESS, "out", "headless" + EXE), work)
        desktop = os.path.join(DESKTOP, "out", "desktop" + EXE)
        if os.path.exists(desktop):
            numbers["desktop_mb"] = stripped_mb(desktop, work)
        numbers["frame_ms_dashboard_1x"] = frame_ms(env, DASHBOARD, 1)
        numbers["frame_ms_dashboard_2x"] = frame_ms(env, DASHBOARD, 2)
        numbers["frame_ms_effects_1x"] = frame_ms(env, EFFECTS, 1)
        numbers["first_frame_ms"] = first_frame_ms(env, work)
        peak = peak_mb(env)
        if peak is not None:
            numbers["peak_mb"] = peak
        numbers["idle_redraws"] = idle_redraws(env, work)
        differ = incremental_differs(env, work)
        numbers["incremental_differs"] = len(differ)

    budgets = {}
    if os.path.exists(args.budgets):
        with open(args.budgets) as f:
            budgets = json.load(f).get(os_name, {})
    over = []
    rows = [f"| {'metric':24} | {'value':>10} | {'budget':>10} |", f"|{'-' * 26}|{'-' * 12}|{'-' * 12}|"]
    for name, value in numbers.items():
        budget = budgets.get(name)
        mark = ""
        if budget is not None and value > budget:
            over.append(name)
            mark = " over"
        shown = f"{value:.2f}" if isinstance(value, float) else str(value)
        rows.append(f"| {name:24} | {shown:>10} | {('-' if budget is None else str(budget)):>10} |{mark}")
    title = f"ClayWind benchmarks on {os_name} ({platform.machine()})"
    print(title)
    print("\n".join(rows))
    if differ:
        print(f"bench: tabs {differ} drawn through Damage differ from the same frame drawn whole")
    if not budgets:
        print(f"bench: no budgets for {os_name}: numbers recorded only")
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"system": os_name, "machine": platform.machine(), "numbers": numbers}, f, indent=2)
    if args.markdown:
        with open(args.markdown, "a") as f:
            f.write(f"### {title}\n\n" + "\n".join(rows) + "\n\n")
            if over:
                f.write(f"Over budget: {', '.join(over)}\n\n")
    for name in over:
        print(f"FAIL {name}: {numbers[name]} is over its budget of {budgets[name]}")
    return 1 if over else 0


if __name__ == "__main__":
    sys.exit(main())
