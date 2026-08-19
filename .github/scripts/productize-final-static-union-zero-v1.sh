#!/usr/bin/env bash
set -Eeuo pipefail

git fetch origin diagnostic/effective-convergence-snapshot-v1
git show origin/diagnostic/effective-convergence-snapshot-v1:src/frontend/parser_global.c > /tmp/convergence-parser-global.c
python3 - <<'PY'
from pathlib import Path

path = Path('src/frontend/parser_global.c')
text = path.read_text()
staging = Path('/tmp/convergence-parser-global.c').read_text()

helper_name = 'try_overwrite_static_zero_noncanonical_union_designator'
if f'static bool {helper_name}(' not in text:
    helper_start = staging.index(f'static bool {helper_name}(')
    helper_end = staging.index('\nstatic bool append_static_record_designator_value(', helper_start)
    helper = staging[helper_start:helper_end] + '\n\n'
    insert_at = text.index('static bool append_static_record_designator_value(')
    text = text[:insert_at] + helper + text[insert_at:]

parse_start = text.index('static bool parse_static_record_constant(')
parse_end = text.index('\nstatic bool ', parse_start + 1)
parse_body = text[parse_start:parse_end]
if f'{helper_name}(' not in parse_body:
    branch_start = text.index('        if (has_designator && designator.depth > 1U) {', parse_start)
    overwrite_start = text.index('            if (overwrite_materialized_field) {', branch_start)
    forward_else = text.index(
        '\n            } else {\n                const MinicRecord *nested_record;', overwrite_start
    )
    replacement = '''            if (overwrite_materialized_field) {
                size_t relative_slot;
                size_t slot_index;
                MinicType slot_type;
                bool handled_union_zero;

                if (!try_overwrite_static_zero_noncanonical_union_designator(
                        parser,
                        object_id,
                        record,
                        &designator,
                        record_base_slot,
                        &handled_union_zero)) {
                    return false;
                }
                if (!handled_union_zero) {
                    if (!static_record_designator_scalar_slot(
                            parser->program, record, &designator, &relative_slot, &slot_type) ||
                        record_base_slot > SIZE_MAX - relative_slot) {
                        minic_parser_error(parser,
                                           "backward nested static record designator currently "
                                           "requires a scalar leaf");
                        return false;
                    }
                    slot_index = record_base_slot + relative_slot;
                    if (!parse_static_scalar_constant_at(
                            parser, object_id, slot_type, true, slot_index)) {
                        return false;
                    }
                }
'''
    text = text[:overwrite_start] + replacement + text[forward_else:]

path.write_text(text)
PY
rm -f /tmp/convergence-parser-global.c

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
