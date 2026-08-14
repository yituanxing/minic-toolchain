#!/usr/bin/env bash
set -Eeuo pipefail

caller_formal_base=b85e5318c5f82ae1ab64de87770d4f778aa0af24
local_placement_formal=b47e67c45405c0e71dd0ce9a6e01aef68f65f718
record_datalayout_formal=c1b8f1498f28fcf54682ca7c454c9f80662f0477
discovery_base=4a4893dc1f306cb082ceb7ea086bf93c4f11790b
formal_function_body=c60828f883639a06409b33e1890ef08b15bad688
formal_abi_head=dcc47c80bf8b00caf1b1098654015ccf1d86cca3
local_placement_staging=18d052ab

# Preserve this slice's materializers and build its exact clean architecture patch.
cp tools/dev/materialize-global-object-datalayout-query-v1.py /tmp/global-object-query.py
cp tools/dev/run-materialize-global-object-datalayout-query-v1.py /tmp/global-object-query-runner.py
cp tools/dev/materialize-global-object-datalayout-cache-removal.py /tmp/global-object-cache-removal.py
cp tools/dev/materialize-global-object-datalayout-hybrid.py /tmp/global-object-hybrid.py
python3 tools/dev/run-materialize-global-object-datalayout-query-v1.py
python3 tools/dev/materialize-global-object-datalayout-cache-removal.py
CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh write
git diff --check
# The discovery codegen_function contains extra zero-sized-record semantics. Apply
# the non-conflicting product files directly and bridge only that emitter later.
git diff --binary "$record_datalayout_formal" -- \
  src/frontend/ast.h \
  src/target/data_layout.c \
  src/target/data_layout.h \
  src/target/riscv64/layout.c \
  > /tmp/global-object-datalayout-clean.patch
test -s /tmp/global-object-datalayout-clean.patch
git reset --hard HEAD
git clean -fd

# Fixed, already-proven architecture patches from earlier formal slices.
git diff --binary "$caller_formal_base" "$local_placement_formal" -- \
  src/frontend/ast.c \
  src/frontend/ast.h \
  src/frontend/parser_function.c \
  src/frontend/parser_statement.c \
  src/target/riscv64/layout.c \
  src/target/riscv64/layout.h \
  src/target/riscv64/codegen_inline_asm.c \
  > /tmp/local-placement-clean.patch
test -s /tmp/local-placement-clean.patch

git diff --binary "$local_placement_formal" "$record_datalayout_formal" -- \
  src/frontend/ast.c \
  src/frontend/ast.h \
  src/target/riscv64/layout.c \
  src/target/riscv64/codegen_expression.c \
  src/target/riscv64/codegen_function.c \
  > /tmp/record-datalayout.patch
test -s /tmp/record-datalayout.patch

# Preserve the proven discovery adapters.
git fetch --no-tags origin \
  agent/rv64-caller-abi-linux-v1 \
  agent/rv64-callee-abi-linux-v1 \
  refactor/rv64-local-placement-side-state-v1

git show origin/agent/rv64-caller-abi-linux-v1:tools/dev/materialize-rv64-caller-abi-location-hybrid.py \
  > /tmp/rv64-caller-abi-location-hybrid.py
git show origin/agent/rv64-callee-abi-linux-v1:tools/dev/materialize-rv64-abi-location-compat.py \
  > /tmp/rv64-abi-location-compat.py
git show origin/agent/rv64-callee-abi-linux-v1:tools/dev/materialize-rv64-callee-abi-location-linux.py \
  > /tmp/rv64-callee-abi-location-linux.py
git show "$local_placement_staging":tools/dev/materialize-rv64-local-placement-hybrid.py \
  > /tmp/rv64-local-placement-hybrid.py

# Rebuild the authoritative discovery semantic workspace.
git checkout --detach "$discovery_base"
merge_base=$(git merge-base "$discovery_base" "$formal_function_body")
git diff --binary "$merge_base" "$formal_function_body" -- . ':(exclude).github/workflows/*' \
  > /tmp/formal-function-body.patch
git apply --3way --index /tmp/formal-function-body.patch
git diff --cached --check
cat > tools/dev/materialize-cleanup-cast-remap.py <<'PY'
#!/usr/bin/env python3
from pathlib import Path
traversal = Path("src/frontend/ast_traversal.h").read_text()
normalization = Path("src/frontend/cast_normalization.c").read_text()
if "minic_c0_program_remap_external_expression_ids" not in traversal:
    raise SystemExit("canonical external remap API missing")
if "minic_c0_program_remap_external_expression_ids" not in normalization:
    raise SystemExit("cast normalization is not using canonical external remap")
print("SKIP legacy cleanup remap materializer")
PY

python3 tools/dev/materialize-tail-ownership.py
set +e
python3 tools/dev/materialize-subxlen-aggregate.py 2>build-subxlen.err
materialize_status=$?
set -e
if test "$materialize_status" -ne 0; then
  grep -Fx 'unexpected aggregate call staging anchor' build-subxlen.err >/dev/null
fi
python3 tools/dev/materialize-subxlen-aggregate-finish.py
python3 tools/dev/fix-subxlen-materialization.py
python3 tools/dev/materialize-aggregate-rvalue-arg.py
python3 tools/dev/materialize-inline-asm-symbolic-immediate.py
python3 tools/dev/materialize-inline-asm-rj-z.py
python3 tools/dev/fix-inline-asm-rj-materialization.py
python3 tools/dev/materialize-inline-asm-rk.py
python3 tools/dev/materialize-codegen-span-trace.py
git diff --check

# Reapply the formal ABI seam while retaining discovery-only caller/callee semantics.
python3 /tmp/rv64-abi-location-compat.py
FORMAL_ABI_HEAD="$formal_abi_head" python3 - <<'PY'
import os
import subprocess
from pathlib import Path

abi_head = os.environ["FORMAL_ABI_HEAD"]
path = Path("src/target/riscv64/codegen_support.c")
target = path.read_text()
formal = subprocess.check_output(
    ["git", "show", f"{abi_head}:src/target/riscv64/codegen_support.c"], text=True
)

def span(text, name):
    marker_index = text.index(name + "(")
    start = text.rfind("\n", 0, marker_index) + 1
    brace = text.index("{", marker_index)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return start, index + 1
    raise RuntimeError(name)

for name in ("minic_riscv64_integer_aggregate_abi", "minic_riscv64_frame_layout"):
    ts, te = span(target, name)
    fs, fe = span(formal, name)
    target = target[:ts] + formal[fs:fe] + target[te:]

include = '#include "target/riscv64/abi.h"\n'
if include not in target:
    anchor = '#include "target/riscv64/codegen_internal.h"\n'
    target = target.replace(anchor, anchor + "\n" + include, 1)
path.write_text(target)
print("ABI_FORMAL_CONSUMERS_OVERLAID=2")
PY
python3 /tmp/rv64-callee-abi-location-linux.py
python3 /tmp/rv64-caller-abi-location-hybrid.py
CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh write
git diff --check

# Reapply proven local-placement ownership.
git add -A
git apply --3way --index /tmp/local-placement-clean.patch
python3 /tmp/rv64-local-placement-hybrid.py
CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh write
git add -A
git diff --cached --check
if grep -R -n --include='*.c' --include='*.h' '\<local_storage_size\>' src/frontend; then
  echo 'hybrid frontend local_storage_size mirror remains' >&2
  exit 1
fi

# Reapply the already-proven record DataLayout ownership patch.
git apply --3way --index /tmp/record-datalayout.patch
CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh write
git add -A
git diff --cached --check
if grep -R -n --include='*.c' --include='*.h' \
    'field->storage_offset\|field->bit_offset\|record->storage_size\|record->alignment' src; then
  echo 'record layout cache remains in global-object hybrid base' >&2
  exit 1
fi

echo 'PROVEN_RECORD_DATALAYOUT_BASE=1'

# Apply the conflict-free object-query owner files, then adapt only discovery's
# global emitter so zero-sized-record behavior is preserved unchanged.
git apply --3way --index /tmp/global-object-datalayout-clean.patch
python3 /tmp/global-object-hybrid.py
CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh write
git add -A
git diff --cached --check
if grep -R -n --include='*.c' --include='*.h' 'object->storage_size\|object->alignment' src; then
  echo 'global-object layout cache remains in Linux hybrid' >&2
  exit 1
fi
grep -F 'minic_data_layout_global_object' src/target/riscv64/codegen_function.c >/dev/null
grep -F 'zero_size_record_definition' src/target/riscv64/codegen_function.c >/dev/null
echo 'GLOBAL_OBJECT_DATALAYOUT_HYBRID=1'

make -j4 MODE=release BUILD_DIR=build/global-object-datalayout-hybrid-linux
mkdir -p build/linux-artifact
gh run download 31689171683 -n linux-frontier-31689171683 -D build/linux-artifact
test -s build/linux-artifact/kbuild/init/main.i
lines=$(wc -l < build/linux-artifact/kbuild/init/main.i)
test "$lines" -eq 90928
set +e
build/global-object-datalayout-hybrid-linux/bin/minic -S \
  build/linux-artifact/kbuild/init/main.i \
  -o build/linux-artifact/main.s \
  >build/linux-artifact/minic.stdout \
  2>build/linux-artifact/minic.stderr
status=$?
set -e
echo "cached_tu_status=$status"
if test -s build/linux-artifact/minic.stderr; then
  tail -n 100 build/linux-artifact/minic.stderr
fi
test "$status" -eq 0
test -s build/linux-artifact/main.s
echo "FULL_TU_PASS lines=$lines"
