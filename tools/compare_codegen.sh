#!/bin/sh
# CEESAFE-FINDINGS.md Q4: the headless host's generated C from two ceesafec builds, each main.c compiled alone with
# ceesafec's release flags, and its code size; and the newer one again with __builtin_unreachable() at function ends
# turned off (CEE_UNREACHABLE_END, new in 0.84). Run from the repository's root:
#
#   tools/compare_codegen.sh OLD_CEESAFEC NEW_CEESAFEC WORK_DIR
#
# CC chooses the C compiler (default cc).
set -e
old=$1
new=$2
work=$3
cc=${CC:-cc}
flags="-O2 -std=c11 -ffp-contract=off -fno-math-errno -fno-stack-protector -fno-asynchronous-unwind-tables
       -fno-unwind-tables -w"
"$cc" --version | head -1
mkdir -p "$work"
text() {
    if [ "$(uname -s)" = Darwin ]; then size "$1" | tail -1 | awk '{print $1}'; else size "$1" | tail -1 | awk '{print $1}'; fi
}
compile() { # label, directory
    started=$(date +%s)
    # shellcheck disable=SC2086 # several flags
    (cd "$2" && "$cc" $flags -I. -c main.c -o "$work/$1.o")
    echo "$1: main.o code $(text "$work/$1.o") bytes, $(( $(date +%s) - started ))s"
}
for which in old new; do
    compiler=$old
    [ "$which" = new ] && compiler=$new
    rm -rf "$work/$which"
    (cd claywind-hosts/headless && "$compiler" build --emit c -o "$work/$which" >/dev/null)
    compile "$which" "$work/$which"
done
cp -R "$work/new" "$work/new-reachable"
sed -i.bak 's/#define CEE_UNREACHABLE_END() __builtin_unreachable()/#define CEE_UNREACHABLE_END() ((void)0)/' \
    "$work/new-reachable/ceesafe/rt.h"
grep -c 'CEE_UNREACHABLE_END() ((void)0)' "$work/new-reachable/ceesafe/rt.h" >/dev/null
compile new-reachable "$work/new-reachable"
# The newer one without the inliner's own choices (functions marked inline still are): whether the growth is inlining.
flags="$flags -fno-inline-functions"
compile new-no-inlining "$work/new"
flags="${flags% -fno-inline-functions}"
# The newer one with the rule ADR 0199 proposes, applied by hand: every function that takes or returns a Style (5.7 KB)
# by value kept out of line.
cp -R "$work/new" "$work/new-out-of-line"
python3 - "$work/new-out-of-line/main.c" <<'PY'
import re, sys
path = sys.argv[1]
lines = open(path).read().split("\n")
count = 0
for k, line in enumerate(lines):
    if line.startswith("static ") and line.rstrip().endswith("{") and "(" in line:
        head, params = line.split("(", 1)
        if "ceeT_8claywind_Style " in head or re.search(r"\bceeT_8claywind_Style v_", params):
            lines[k] = "static __attribute__((noinline)) " + line[len("static "):]
            count += 1
open(path, "w").write("\n".join(lines))
print(f"{count} functions kept out of line")
PY
compile new-out-of-line "$work/new-out-of-line"
