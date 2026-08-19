#!/usr/bin/env bash
set -Eeuo pipefail

git fetch origin diagnostic/effective-convergence-snapshot-v1
git show origin/diagnostic/effective-convergence-snapshot-v1:tools/dev/materialize-backward-static-union-zero-overlay-v1.py > tools/dev/materialize-backward-static-union-zero-overlay-v1.py
python3 tools/dev/materialize-backward-static-union-zero-overlay-v1.py
rm tools/dev/materialize-backward-static-union-zero-overlay-v1.py

# Designator parsing owns only the member path. Whether a selected union member
# has a representable flattened scalar slot is a later storage-ownership
# question. Keep the normal scalar-slot resolver strict, but allow a
# noncanonical union member to reach the dedicated zero-overlay owner.
python3 - <<'PY'
from pathlib import Path
path = Path('src/frontend/parser_global.c')
text = path.read_text()
early_guard = '''        if (current_record->is_union && field_index != 0U) {
            minic_parser_error(
                parser, "nested static union designator requires the representable first member");
            return false;
        }
'''
if text.count(early_guard) != 1:
    raise SystemExit(f'early noncanonical union path guard count={text.count(early_guard)}')
text = text.replace(early_guard, '', 1)
path.write_text(text)
PY

cat > tests/compiler/c0/static_union_zero_overlay.c <<'EOF'
union reader_special {
    long l;
    int s;
};

struct task_state {
    long before;
    union reader_special special;
    long after;
};

static struct task_state init_task = {
    .before = 3,
    .after = 5,
    .special.s = 0,
};

int main(void) {
    return (init_task.before == 3 && init_task.special.s == 0 && init_task.after == 5) ? 0 : 1;
}
EOF

cat > tests/compiler/c0/static_union_nonzero_overlay_invalid.c <<'EOF'
union reader_special {
    long l;
    int s;
};
struct task_state {
    long before;
    union reader_special special;
    long after;
};
static struct task_state init_task = {
    .before = 3,
    .after = 5,
    .special.s = 1,
};
int main(void) { return init_task.special.s; }
EOF

cat > tests/compiler/c0/run-static-union-zero-overlay.sh <<'EOF'
#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-union-zero-overlay
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_zero_overlay.c" -o "$work/valid.i"
"$minic" -S "$work/valid.i" -o "$work/valid.s"
test -s "$work/valid.s"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_nonzero_overlay_invalid.c" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
  echo "expected nonzero noncanonical union overlay rejection" >&2
  exit 1
fi
cat "$work/invalid.err"
grep -Fq 'backward noncanonical static union member requires a zero initializer' "$work/invalid.err"
echo 'PASS compiler/c0/static-union-zero-overlay zero-noncanonical=accepted nonzero=fail-closed'
EOF
chmod +x tests/compiler/c0/run-static-union-zero-overlay.sh

python3 - <<'PY'
from pathlib import Path
path = Path('tests/compiler/c0/run.sh')
source = path.read_text()
invocation = '''\nMINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-static-union-zero-overlay.sh"\n'''
if 'run-static-union-zero-overlay.sh' not in source:
    path.write_text(source + invocation)
PY

clang-format-18 -i src/frontend/parser_global.c tests/compiler/c0/static_union_zero_overlay.c tests/compiler/c0/static_union_nonzero_overlay_invalid.c
CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/product-static-union-zero
MINIC="$GITHUB_WORKSPACE/build/product-static-union-zero/bin/minic" \
BUILD_DIR="$GITHUB_WORKSPACE/build/product-static-union-zero" \
  sh tests/compiler/c0/run-static-union-zero-overlay.sh
make -j4 check-fast MODE=release BUILD_DIR=build/product-static-union-zero-fast

rm -f diagnostics/final-static-union-zero-trigger.txt
git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com
git add src/frontend/parser_global.c \
        tests/compiler/c0/static_union_zero_overlay.c \
        tests/compiler/c0/static_union_nonzero_overlay_invalid.c \
        tests/compiler/c0/run-static-union-zero-overlay.sh \
        tests/compiler/c0/run.sh \
        diagnostics/final-static-union-zero-trigger.txt
git commit -m 'frontend: accept zero overlays for noncanonical static union members'
git push origin HEAD:product/final-static-union-zero-v1
