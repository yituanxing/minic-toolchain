#!/usr/bin/env bash
# The location counter in `unresolved - .` is a synthetic, typed SymbolID
# when a target requires ADD/SUB relocations. Its ELF visibility must come
# from that SymbolID, not from a post-hoc fixup to private symbol spellings.
# Compare GNU and MiniAS relocation targets, including st_other visibility.
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/dot.s" <<'ASM'
.section .debug_info,"",@progbits
  .long unresolved_target - .
  .quad unresolved_target - .
ASM

check_target() {
    local arch=$1 gnu=$2
    shift 2
    local -a mini_args=("$@")
    if ! command -v "$gnu" >/dev/null; then
        if [[ ${REQUIRE_ALL:-0} == 1 ]]; then
            echo "MINIAS_DOT_ANCHOR=FAIL arch=$arch missing=$gnu" >&2
            return 1
        fi
        echo "MINIAS_DOT_ANCHOR=SKIP arch=$arch missing=$gnu"
        return 0
    fi
    case "$arch" in
        riscv64) "$gnu" -march=rv64gc -mabi=lp64d \
                    -o "$work/gnu.o" "$work/dot.s" ;;
        loongarch64) "$gnu" -mabi=lp64d \
                         -o "$work/gnu.o" "$work/dot.s" ;;
        *) "$gnu" -o "$work/gnu.o" "$work/dot.s" ;;
    esac
    "$MINIAS" --target="$arch" "${mini_args[@]}" \
        -o "$work/mini.o" "$work/dot.s"
    python3 "$repo/tests/assembler/elf_semantic_compare.py" --strict \
        "$work/gnu.o" "$work/mini.o" >"$work/$arch-strict.json" || true
    python3 - "$work/$arch-strict.json" "$arch" <<'PY'
import json
import pathlib
import sys

result = json.loads(pathlib.Path(sys.argv[1]).read_text())
diff = result.get('differences', {})
# GNU GAS may generate an implicit .debug_line from the bare input filename
# where MiniAS emits only explicit `.loc` rows. That unrelated policy must
# not dilute the exact relocation tuple/visibility check in this probe.
for dimension in ('relocation_order', 'relocations'):
    assert dimension not in diff, (sys.argv[2], dimension, diff[dimension])
print(f'MINIAS_DOT_ANCHOR=PASS arch={sys.argv[2]} GNU_relocation_tuple_and_visibility=exact')
PY
}

check_target x86_64 as
check_target riscv64 riscv64-linux-gnu-as -march=rv64gc -mabi=lp64d
check_target aarch64 aarch64-linux-gnu-as
check_target loongarch64 loongarch64-linux-gnu-as -march=loongarch64 -mabi=lp64d
