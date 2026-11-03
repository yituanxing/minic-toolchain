#!/bin/sh
# P32: one Core CFG + Module symbol reachability acceptance entry. The legacy
# focused probes remain independent fixtures; this gate joins their proof
# surfaces without constructing a second compiler-side reachability path.
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

if [ ! -x "$minic" ]; then
    echo "CORE_MODULE_REACHABILITY=FAIL compiler_not_executable=$minic" >&2
    exit 1
fi

# One canonical closure must preserve the older graph edge classes too:
# used/section/file-asm object roots, local static owner, function/object
# alias roots and dead callback pruning. Never infer an ASM symbol by text
# matching; check the compiler's resulting ELF symbol edges downstream.
MINIC_CC="$minic" sh "$root/tests/compiler/c0/probe-gnu-dead-file-static-object-graph.sh"
MINIC="$minic" BUILD_DIR="$work" bash "$root/tests/compiler/c0/run-static-local-owner-reachability.sh"
MINIC="$minic" BUILD_DIR="$work" sh "$root/tests/compiler/c0/run-gnu-object-unused-alias.sh"
MINIC="$minic" BUILD_DIR="$work" sh "$root/tests/compiler/c0/run-gnu-function-copy-alias.sh"
echo 'CORE_MODULE_ALIAS_OWNER=PASS used_section_file_asm static_local dead_callback function_alias object_alias'

# A source-level "referenced" mark in an if(0) branch is not a live Core
# reference. Section and GNU asm-name metadata on static functions may name
# a real ELF symbol only after Core reachability has kept its owner. Assert
# that neither metadata variant revives an undefined external call. A static
# GNU visibility attribute is intentionally absent: GCC ignores it and MiniC
# rejects it because visibility requires external linkage.
dead_source="$root/tests/compiler/c0/core_module_dead_root_after_prune.c"
"$host_cc" -std=gnu11 -O2 -Wall -Wextra -Werror -no-pie \
    "$dead_source" -o "$work/dead-root.gnu"
"$work/dead-root.gnu"
"$host_cc" -std=gnu11 -E -P -x c "$dead_source" -o "$work/dead-root.i"
for target in x86_64 riscv32 riscv64 aarch64 loongarch64; do
    "$minic" --target="$target-linux" -S "$work/dead-root.i" \
        -o "$work/dead-root.$target.s"
    if grep -Eq 'mc_dead_(section|asmname)_unresolved|mc_dead_(section_helper|asmname_symbol)|\.text\.mc_dead_section' \
            "$work/dead-root.$target.s"; then
        echo "CORE_MODULE_REACHABILITY=FAIL target=$target dead_static_metadata_resurrected" >&2
        exit 1
    fi
done
"$host_cc" -c -x assembler "$work/dead-root.x86_64.s" \
    -o "$work/dead-root.minic.o"
if nm -u "$work/dead-root.minic.o" |
    grep -Eq 'mc_dead_(section|asmname)_unresolved'; then
    echo 'CORE_MODULE_REACHABILITY=FAIL native_ELF_dead_static_undefined' >&2
    exit 1
fi
"$host_cc" -no-pie "$work/dead-root.minic.o" -o "$work/dead-root.minic"
"$work/dead-root.minic"
echo 'CORE_MODULE_DEAD_SOURCE_METADATA=PASS five_ISA=5 GNU_native=0 MiniC_native=0 dead_section_and_asmname=pruned'

# MC-064: x86-specific %c0 would require a different backend ASM grammar;
# use the original RV64 GNU-valid %0 symbolic immediate in all variants.
# With only -S available, insist the referenced local definition is present;
# if RV64 GNU/QEMU tools are available, the existing ELF proof below is also
# mandatory (real relocation, static GNU LD and runtime, not just GNU AS).
source="$root/tests/compiler/audit/mc064_rv64_asm_static_symbol.c"
for variant in asm_only direct_call static_object; do
    case "$variant" in
        asm_only) define='' ; symbol=asm_only_target ;;
        direct_call) define='-DMC064_DIRECT_CALL' ; symbol=asm_only_target ;;
        static_object) define='-DMC064_STATIC_OBJECT' ; symbol=asm_only_object ;;
    esac
    "$host_cc" -std=gnu11 -E -P -x c $define "$source" \
        -o "$work/mc064-$variant.i"
    "$minic" --target=riscv64-linux -march=rv64i -mabi=lp64 \
        -S "$work/mc064-$variant.i" -o "$work/mc064-$variant.s"
    if ! grep -Eq "^[[:space:]]*\.dword[[:space:]]+$symbol\$" \
            "$work/mc064-$variant.s" ||
       ! grep -Fxq "$symbol:" "$work/mc064-$variant.s"; then
        echo "CORE_MODULE_REACHABILITY=FAIL mc064=$variant symbol=$symbol asm_relocation_or_definition" >&2
        exit 1
    fi
done
echo 'MC064_ISA=PASS asm_only_function=retained direct_call=retained static_object=retained relocations=3'

rv64_tools='riscv64-linux-gnu-gcc riscv64-linux-gnu-as riscv64-linux-gnu-ld riscv64-linux-gnu-readelf riscv64-linux-gnu-nm qemu-riscv64'
have_rv64=1
for executable in $rv64_tools; do
    command -v "$executable" >/dev/null 2>&1 || have_rv64=0
done
if [ "$have_rv64" -eq 1 ]; then
    MINIC_CC="$minic" sh "$root/tests/compiler/audit/probe-mc064-rv64-elf-link.sh"
elif [ "${MINIC_REACHABILITY_REQUIRE_CROSS:-0}" = 1 ]; then
    echo 'CORE_MODULE_REACHABILITY=FAIL MC064 RV64 GNU/QEMU toolchain unavailable' >&2
    exit 1
else
    echo 'MC064_ELF_REACHABILITY=SKIP cross_RV64_GNU_QEMU_unavailable (ISA symbol pairing PASS)'
fi

# MC-072: references in a static &&label table must pair with definitions,
# even after integer specialization clones its inline owner. The same fixture
# includes a dead inline owner: its table must be pruned, not made a root.
MINIC_CC="$minic" MINIC_REACHABILITY_REQUIRE_CROSS="${MINIC_REACHABILITY_REQUIRE_CROSS:-0}" \
    sh "$root/tests/compiler/audit/probe-mc072-inline-computed-label-owner.sh"

# MC-073: an address-taken Core block without a C local_label_symbol is still
# reachable, but an address producer in an unreachable block is not a root.
bash "$root/tests/core/run-core-ir-shapes.sh" "$host_cc"

# The pre-existing computed-goto runtime probes cover direct, dynamic and
# static tables. Their GNU/QEMU differential is mandatory when installed.
computed_targets=''
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 &&
   command -v qemu-aarch64 >/dev/null 2>&1; then
    computed_targets=aarch64
fi
if command -v loongarch64-linux-gnu-gcc-13 >/dev/null 2>&1 &&
   command -v qemu-loongarch64 >/dev/null 2>&1; then
    computed_targets="$computed_targets loongarch64"
fi
if [ -n "$computed_targets" ]; then
    MINIC_CC="$minic" MINIC_COMPUTED_GOTO_TARGET="$computed_targets" \
        sh "$root/tests/compiler/c0/probe-four-backend-computed-goto.sh"
elif [ "${MINIC_REACHABILITY_REQUIRE_CROSS:-0}" = 1 ]; then
    echo 'CORE_MODULE_REACHABILITY=FAIL computed-goto GNU/QEMU unavailable' >&2
    exit 1
else
    echo 'CORE_COMPUTED_GOTO_RUNTIME=SKIP GNU_QEMU_unavailable (MC072 four-ISA symbol pairing PASS)'
fi

echo 'CORE_MODULE_REACHABILITY=PASS Core_CFG=MC073 Module_alias_and_dead_roots=PASS MC064_RV64_symbol_pair=PASS MC072_four_ISA_label_pairs=PASS cross_GNU_QEMU=conditional'
