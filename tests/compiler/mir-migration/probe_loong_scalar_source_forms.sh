#!/usr/bin/env bash
# Independent Source -> Core -> typed MachineIR proof for the original
# LoongArch scalar hardware ASM family. All checks stop at -S: privileged
# hardware operations must not be executed in an unprivileged emulator.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${1:-${MINIC_CC:-"$root/build/debug/bin/minic-cc"}}
source_file="$root/tests/compiler/mir-migration/loong_scalar_source_forms.c"
# Accept an explicit workspace TMPDIR while avoiding the shared /tmp tmpfs,
# which is often full when multiple compiler agents run in parallel.
case ${TMPDIR:-} in
    "$root"/*) scratch_parent=$TMPDIR ;;
    build/*) scratch_parent="$root/$TMPDIR" ;;
    *) scratch_parent="$root/build" ;;
esac
mkdir -p "$scratch_parent"
work=$(mktemp -d "$scratch_parent/loong-scalar-source.XXXXXX")
export TMPDIR="$work"
trap 'rm -rf "$work"' EXIT

fail() { printf '%s\n' "LOONG_SCALAR_MIR=FAIL $*" >&2; exit 1; }
contains() { grep -Eq -- "$1" "$2" || fail "missing instruction: $1"; }
reject() {
    local name=$1
    if "$minic" --target=loongarch64-linux -S "$work/$name.i" \
        -o "$work/$name.s" >"$work/$name.out" 2>"$work/$name.err"; then
        fail "unsafe source form accepted: $name"
    fi
    test ! -e "$work/$name.s" || fail "partial output on reject: $name"
}

test -x "$minic" || fail "missing compiler: $minic"
cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source_file"
cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
    -DMINIC_LOONG_CLOCK_CPU_ID_HISTORICAL_NEGATIVE "$source_file"
cc -E -P -std=gnu11 -x c "$source_file" -o "$work/original.i"
"$minic" --target=loongarch64-linux -S "$work/original.i" \
    -o "$work/original.s" || fail 'valid source rejected'
test -s "$work/original.s" || fail 'empty assembly'

for entry in loong_scalar_cacop loong_scalar_cpucfg \
             loong_scalar_fpr_read loong_scalar_fpr_write \
             loong_scalar_clock loong_scalar_clock_named \
             loong_scalar_clock_vdso_base \
             loong_scalar_clock_counter_only loong_scalar_invtlb \
             loong_scalar_frametrace_csr; do
    contains "^${entry}:$" "$work/original.s"
done

# Assert instruction families, immediate values, register classes, and
# complete INVTLB variants. Do not assert old $t0/$t1/$t3 allocation,
# frame addressing or numeric assembler labels: they are not the ABI.
contains '^[[:space:]]+cacop 8, \$[a-z0-9]+, 0$' "$work/original.s"
contains '^  cpucfg \$[a-z0-9]+, \$[a-z0-9]+$' "$work/original.s"
contains '^  movfr2gr\.d \$[a-z0-9]+, \$f0$' "$work/original.s"
contains '^  movgr2fr\.d \$f0, \$[a-z0-9]+$' "$work/original.s"
contains '^  rdtime\.d \$[a-z0-9]+, \$[a-z0-9]+$' "$work/original.s"
contains '^[[:space:]]+csrrd \$[a-z0-9]+, (7|0x7)$' "$work/original.s"
for name in loong_scalar_clock loong_scalar_clock_named \
            loong_scalar_clock_vdso_base \
            loong_scalar_clock_counter_only; do
    sed -n "/^${name}:/,/^\\.size ${name},/p" "$work/original.s" \
        >"$work/$name.s"
    test -s "$work/$name.s" || fail "empty CLOCK variant $name"
done
contains '^  rdtime\.d \$[a-z0-9]+, \$[a-z0-9]+$' \
    "$work/loong_scalar_clock.s"
contains '^  rdtime\.d \$[a-z0-9]+, \$[a-z0-9]+$' \
    "$work/loong_scalar_clock_named.s"
contains '^  la\.pcrel \$[a-z0-9]+, _start$' \
    "$work/loong_scalar_clock_vdso_base.s"
contains '^  rdtime\.d \$[a-z0-9]+, \$zero$' \
    "$work/loong_scalar_clock_counter_only.s"
test "$(grep -Ec '^  rdtime\.d ' "$work/original.s")" -eq 3 ||
    fail 'CLOCK instruction count must cover numeric/named/counter_only'
test "$(grep -Ec '^  invtlb 0,' "$work/original.s")" -eq 4 ||
    fail 'missing INVTLB operand variant'
contains '^  invtlb 0, \$[a-z0-9]+, \$[a-z0-9]+$' "$work/original.s"
contains '^  invtlb 0, \$zero, \$[a-z0-9]+$' "$work/original.s"
contains '^  invtlb 0, \$[a-z0-9]+, \$zero$' "$work/original.s"
contains '^  invtlb 0, \$zero, \$zero$' "$work/original.s"

# Keep the source-origin ISA grammar closed. Each negative changes one
# established template or operand contract; no generic inline-asm fallback.
for name in cacop-template cacop-constraint cpucfg-template \
            cpucfg-constraint fpr-template clock-template \
            clock-named-constraint clock-vdso-volatile \
            clock-cpu-id-historical clock-counter-memory \
            invtlb-template invtlb-constraint csr-template; do
    case "$name" in
        cacop-template)
            sed 's/cacop %0, %1/cacop %0, %1; syscall 0/' \
                "$work/original.i" >"$work/$name.i" ;;
        cacop-constraint)
            sed 's/"ZC"/"r"/' "$work/original.i" >"$work/$name.i" ;;
        cpucfg-template)
            sed 's/cpucfg %0, %1/cpucfg %0, %1; syscall 0/' \
                "$work/original.i" >"$work/$name.i" ;;
        cpucfg-constraint)
            sed 's/"=&r"(result)/"=m"(result)/' \
                "$work/original.i" >"$work/$name.i" ;;
        fpr-template)
            sed 's/movfr2gr\.d %0, \$f0/movfr2gr.d %0, $f32/' \
                "$work/original.i" >"$work/$name.i" ;;
        clock-template)
            sed 's/rdtime\.d %0, %1/rdtime.d %0, %1; nop/' \
                "$work/original.i" >"$work/$name.i" ;;
        clock-named-constraint)
            sed 's/\[val\] "=&r"(named_value)/[val] "=r"(named_value)/' \
                "$work/original.i" >"$work/$name.i" ;;
        clock-vdso-volatile)
            sed 's/__asm__("la\.pcrel %0, _start"/__asm__ __volatile__("la.pcrel %0, _start"/' \
                "$work/original.i" >"$work/$name.i" ;;
        clock-cpu-id-historical)
            cc -E -P -std=gnu11 -x c \
                -DMINIC_LOONG_CLOCK_CPU_ID_HISTORICAL_NEGATIVE \
                "$source_file" -o "$work/$name.i" ;;
        clock-counter-memory)
            sed 's/"=r"(counter_only));/"=r"(counter_only) : : "memory");/' \
                "$work/original.i" >"$work/$name.i" ;;
        invtlb-template)
            sed 's/invtlb %0, %1, %2/invtlb %0, %1, %2; nop/' \
                "$work/original.i" >"$work/$name.i" ;;
        invtlb-constraint)
            sed 's/"i"(0), "r"(asid)/"r"(0), "r"(asid)/' \
                "$work/original.i" >"$work/$name.i" ;;
        csr-template)
            sed 's/csrrd %0, 0x7/csrrd %0, 0x8/' \
                "$work/original.i" >"$work/$name.i" ;;
    esac
    cmp -s "$work/original.i" "$work/$name.i" &&
        fail "missed negative mutation: $name"
    reject "$name"
done

printf '%s\n' \
  'LOONG_SCALAR_MIR=PASS CACOP=1 CPUCFG=1 FPR=2 CLOCK=4 HISTORICAL_CLOCK_CPU_ID_REJECT=1 INVTLB=4 FRAMETRACE_CSR=1 negative=13 GNU_AS=NOT_RUN QEMU=NOT_RUN'
