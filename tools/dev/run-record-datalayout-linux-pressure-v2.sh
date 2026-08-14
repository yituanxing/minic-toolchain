#!/usr/bin/env bash
set -Eeuo pipefail

python3 - <<'PY'
from pathlib import Path

path = Path("tools/dev/run-record-datalayout-linux-pressure.sh")
source = path.read_text(encoding="utf-8")

old = "foundation_head=b47e67c45405c0e71dd0ce9a6e01aef68f65f718\n"
new = old + "caller_formal_base=b85e5318c5f82ae1ab64de87770d4f778aa0af24\n"
if source.count(old) != 1:
    raise SystemExit("foundation head anchor changed")
source = source.replace(old, new, 1)

old = """test -s /tmp/record-datalayout.patch
git reset --hard HEAD
git clean -fd
"""
new = """test -s /tmp/record-datalayout.patch
# Reuse the same clean frontend/layout portion of the already-proven
# FunctionLayout/local-placement migration that preceded its discovery adapter.
git diff --binary "$caller_formal_base" "$foundation_head" -- \
  src/frontend/ast.c \
  src/frontend/ast.h \
  src/frontend/parser_function.c \
  src/frontend/parser_statement.c \
  src/target/riscv64/layout.c \
  src/target/riscv64/layout.h \
  src/target/riscv64/codegen_inline_asm.c \
  > /tmp/local-placement-clean.patch
test -s /tmp/local-placement-clean.patch
git reset --hard HEAD
git clean -fd
"""
if source.count(old) != 1:
    raise SystemExit("record patch reset anchor changed")
source = source.replace(old, new, 1)

old = """# Reapply the already-proven FunctionLayout/local placement ownership bridge.
python3 /tmp/rv64-local-placement-hybrid.py
"""
new = """# Reapply the already-proven FunctionLayout/local placement ownership bridge.
git add -A
git apply --3way --index /tmp/local-placement-clean.patch
python3 /tmp/rv64-local-placement-hybrid.py
"""
if source.count(old) != 1:
    raise SystemExit("local placement bridge anchor changed")
source = source.replace(old, new, 1)

out = Path("/tmp/run-record-datalayout-linux-pressure-v2.sh")
out.write_text(source, encoding="utf-8")
PY

exec bash /tmp/run-record-datalayout-linux-pressure-v2.sh
