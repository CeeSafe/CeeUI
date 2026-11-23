#!/usr/bin/env python3
"""Compiles every CeeSafe example of the documentation site (site/guide/*.md) against ClayWind.

An example is a fenced block whose language is `ceesafe`. What follows the language on the fence line says how it is
checked:

  ```ceesafe            a whole source file: it is checked as it stands (it imports claywind itself)
  ```ceesafe frame      the body of a frame: statements checked inside
                            import claywind;
                            public fn example<M: TextMetrics>(ui: borrow mut Ui, metrics: borrow M) { ... }
  ```ceesafe skip       not checked (a fragment that cannot stand alone); the count of these is reported

Each example is a package of its own, a library that depends on ../claywind by path, checked with `ceesafec check`.
A failure prints the page, the line the example begins at, and the compiler's errors with the page's own line numbers.

usage: python3 tools/guide_check.py [--ceesafec ceesafec] [--jobs N] [page.md ...]
"""

import argparse
import concurrent.futures
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUIDE = os.path.join(ROOT, "site", "guide")
FRAME_HEAD = "import claywind;\n\npublic fn example<M: TextMetrics>(ui: borrow mut Ui, metrics: borrow M) {\n"
FRAME_TAIL = "}\n"


def examples(path):
    """The examples of a page: (line of its first line of code, kind, code)."""
    found = []
    kind = None
    code = []
    start = 0
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            fence = re.match(r"^```(\S*)\s*(.*?)\s*$", line)
            if kind is None:
                if fence and fence.group(1) == "ceesafe":
                    kind = fence.group(2) or "file"
                    code = []
                    start = number + 1
            elif fence and fence.group(1) == "":
                found.append((start, kind, "".join(code)))
                kind = None
            else:
                code.append(line)
    return found


def check(ceesafec, work, page, start, kind, code):
    """One example: (page, line, kind, the compiler's errors or "")."""
    if kind == "skip":
        return (page, start, kind, "")
    if kind not in ("file", "frame"):
        return (page, start, kind, f"unknown kind of example: {kind!r}")
    name = f"{os.path.splitext(os.path.basename(page))[0]}_{start}"
    package = os.path.join(work, name)
    os.makedirs(os.path.join(package, "src"))
    with open(os.path.join(package, "ceesafe.toml"), "w", encoding="utf-8") as f:
        f.write('[package]\nname = "example"\nversion = "0.1.0"\nkind = "library"\n\n[dependencies]\n'
                f'claywind = {{ path = "{os.path.join(ROOT, "claywind")}" }}\n')
    head_lines = 0
    source = code
    if kind == "frame":
        head_lines = FRAME_HEAD.count("\n")
        source = FRAME_HEAD + code + FRAME_TAIL
    with open(os.path.join(package, "src", "example.cee"), "w", encoding="utf-8") as f:
        f.write(source)
    # A path dependency still wants its lock: made here, as `ceesafec fetch` writes it.
    fetched = subprocess.run([ceesafec, "fetch"], cwd=package, capture_output=True, text=True)
    if fetched.returncode != 0:
        return (page, start, kind, fetched.stdout + fetched.stderr)
    done = subprocess.run([ceesafec, "check"], cwd=package, capture_output=True, text=True)
    output = done.stdout + done.stderr
    if done.returncode == 0 and "error" not in output:
        return (page, start, kind, "")
    # The compiler's line numbers are the example file's: say the page's.
    def to_page(match):
        return f"{os.path.relpath(page, ROOT)}:{int(match.group(1)) - head_lines + start - 1}:"

    errors = [line for line in output.splitlines() if "warning[" not in line]
    return (page, start, kind, re.sub(r"src/example\.cee:(\d+):", to_page, "\n".join(errors)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ceesafec", default="ceesafec")
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    parser.add_argument("pages", nargs="*")
    args = parser.parse_args()
    pages = args.pages or sorted(glob.glob(os.path.join(GUIDE, "*.md")))
    todo = [(os.path.abspath(page), start, kind, code) for page in pages for start, kind, code in examples(page)]
    work = tempfile.mkdtemp(prefix="claywind-guide-")
    failed = 0
    skipped = 0
    try:
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results = list(pool.map(lambda e: check(args.ceesafec, work, *e), todo))
        for page, start, kind, errors in results:
            if kind == "skip":
                skipped += 1
            elif errors:
                failed += 1
                print(f"FAIL {os.path.relpath(page, ROOT)}:{start} ({kind})\n{errors}\n")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f"guide_check: {len(pages)} pages, {len(todo)} examples ({skipped} skipped): {failed} problems")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
