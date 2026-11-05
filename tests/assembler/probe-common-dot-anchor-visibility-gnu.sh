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
    # GNU triplets name the 64-bit RISC-V assembler `riscv64`, while MiniAS
    # has one `riscv` target with XLEN/ABI selected by the profile flags.
    local mini_target=$arch
    if [[ $arch == riscv64 ]]; then mini_target=riscv; fi
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
    "$MINIAS" --target="$mini_target" "${mini_args[@]}" \
        -o "$work/mini.o" "$work/dot.s"
    python3 - "$work/gnu.o" "$work/mini.o" "$arch" "$repo" <<'PY'
import sys
from pathlib import Path

sys.path.insert(0, str(Path(sys.argv[4]) / 'tests' / 'assembler'))
from elf_semantic_compare import load_semantics

reference = load_semantics(sys.argv[1])
candidate = load_semantics(sys.argv[2])
arch = sys.argv[3]

# The source defines precisely two dot-anchored expressions in .debug_info.
# Compare the COMPLETE normalized relocation tuples there (offset, kind,
# symbol identity including LOCAL/STV_HIDDEN, addend, multiplicity), not a
# truncated sample of global differences. GNU LoongArch may also produce an
# implicit .rela.debug_line for the source filename. Its unrelated contents
# belong to the independent DWARF oracle, not this synthetic-dot contract.
def info_relocations(semantics):
    return [row for row in semantics['relocations']
            if row['key'][0] == '.debug_info']

def info_relocation_order(semantics):
    return {name: entries for name, entries in
            semantics['relocation_order'].items()
            if name in ('.rel.debug_info', '.rela.debug_info')}

gnu_relocs = info_relocations(reference)
mini_relocs = info_relocations(candidate)
assert gnu_relocs, (arch, 'missing-GNU-dot-relocations')
assert mini_relocs == gnu_relocs, (arch, 'debug-info-relocations',
                                 mini_relocs, gnu_relocs)
gnu_order = info_relocation_order(reference)
mini_order = info_relocation_order(candidate)
assert mini_order == gnu_order, (arch, 'debug-info-relocation-order',
                                mini_order, gnu_order)

if arch == 'loongarch64':
    # LoongArch emits ADD/SUB pairs for both source expressions. Guard
    # against a false green if both objects silently drop the pair, or if
    # the synthetic SUB anchors lose GNU's LOCAL/HIDDEN visibility.
    assert len(gnu_relocs) == len(mini_relocs) == 4, (
        arch, 'expected-four-dot-relocations', gnu_relocs, mini_relocs)
    assert gnu_order and mini_order, (arch, 'missing-ordered-ADD-SUB-pairs')
    for who, relocations in (('GNU', gnu_relocs), ('MiniAS', mini_relocs)):
        hidden = [row for row in relocations
                  if row['key'][3][0] == 'LOCAL']
        assert sorted(row['key'][2] for row in hidden) == [55, 56], (
            arch, who, 'missing-LOCAL-SUB-anchors', hidden)
        assert all(row['key'][3][2] == 'STV_HIDDEN' for row in hidden), (
            arch, who, 'wrong-synthetic-anchor-visibility', hidden)

print(f'MINIAS_DOT_ANCHOR=PASS arch={arch} '
      'GNU_debug_info_relocation_tuple_order_and_visibility=exact')
PY
}

check_target x86_64 as
check_target riscv64 riscv64-linux-gnu-as -march=rv64gc -mabi=lp64d
check_target aarch64 aarch64-linux-gnu-as
check_target loongarch64 loongarch64-linux-gnu-as -march=loongarch64 -mabi=lp64d
