#!/usr/bin/env bash
# Host-only P45.1 AArch64 GNU ASM regression. The source bytes are taken
# directly from the existing GNU/GAS/QEMU oracle, never independently copied.
# A P44 compiler is optional; when supplied, its emitted .s must be byte-equal.
set -Eeuo pipefail

minic="${1:?usage: $0 MINI_CC [P44_MINIC_CC=...] }"
test -x "$minic"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
oracle="$root/tests/compiler/c0/probe-gnu-file-asm-multitarget.sh"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Literal shell fragments between these markers are also executed by the
# original, fully cross-compiled GNU/QEMU probe. The loop deliberately keeps
# all 16 named [tmp]/[ret] templates; no numeric %1 rewriting is permitted.
fixture() {
    local tag="$1" fragment
    local start="P45_FIXTURE_${tag}_BEGIN"
    local end="P45_FIXTURE_${tag}_END"
    test "$(grep -Fc "# $start" "$oracle")" -eq 1
    test "$(grep -Fc "# $end" "$oracle")" -eq 1
    fragment="$(awk -v s="$start" -v e="$end" '
        index($0, "# " s) { in_fixture=1; next }
        index($0, "# " e) { in_fixture=0; next }
        in_fixture { print }
    ' "$oracle")"
    test -n "$fragment"
    eval "$fragment"
}

fixture PERCPU_LL_SC
fixture PERCPU_ALTERNATIVE

run_compiler() {
    local compiler="$1" label="$2" fixture="$3"
    MINIC_ASM_OPERAND_PLAN_TRACE=1 "$compiler" --target=aarch64-linux -S \
        "$work/aarch64-${fixture}.i" -o "$work/${fixture}.${label}.s" \
        >"$work/${fixture}.${label}.stdout" \
        2>"$work/${fixture}.${label}.stderr"
    test -s "$work/${fixture}.${label}.s"
    if grep -Eq '^[[:space:]]*\.error[[:space:]]' \
        "$work/${fixture}.${label}.s"; then
        echo "FAIL ${fixture} compiler emitted Core poison .error ($label)" >&2
        exit 1
    fi
}

run_compiler "$minic" p451 percpu-llsc
run_compiler "$minic" p451 percpu-alternative

# Every one of the 16 original C functions must reach a distinct LL/SC
# instruction quartet in machine ASM. The return variants are NOT collapsed
# into a source-level spelling; they share operation IDs only by ISA meaning.
for bits in 8 16 32 64; do
    for op in add andnot or add_return; do
        grep -Fq "percpu_${bits}_${op}:" "$work/percpu-llsc.p451.s"
    done
done
test "$(grep -Ec '^[[:space:]]*ldxr(b|h)?[[:space:]]' \
    "$work/percpu-llsc.p451.s")" -eq 16
test "$(grep -Ec '^[[:space:]]*stxr(b|h)?[[:space:]]' \
    "$work/percpu-llsc.p451.s")" -eq 16
test "$(grep -Ec '^[[:space:]]*ldxrb[[:space:]]' \
    "$work/percpu-llsc.p451.s")" -eq 4
test "$(grep -Ec '^[[:space:]]*ldxrh[[:space:]]' \
    "$work/percpu-llsc.p451.s")" -eq 4
test "$(grep -Ec '^[[:space:]]*ldxr[[:space:]]' \
    "$work/percpu-llsc.p451.s")" -eq 8
for alu in add bic orr; do
    grep -Eq "^[[:space:]]*$alu[[:space:]]" "$work/percpu-llsc.p451.s"
done

# Core tracing is emitted after Source qualification and Core construction,
# before ISA machine emission. All 16 effects must carry nonzero FormId and
# numeric variant. The CPU-offset ALTERNATIVE is qualified independently.
for case in 'percpu-llsc:16' 'percpu-alternative:1'; do
    IFS=: read -r fixture expected <<< "$case"
    trace="$work/$fixture.p451.stderr"
    count="$(grep -Ec '^CORE_ASM_OPERAND_PLAN owner=1 .*machine_form=[1-9][0-9]* machine_variant=[0-9]+' "$trace" || true)"
    if (( count != expected )); then
        echo "FAIL $fixture expected $expected nonzero machine FormIds; got $count" >&2
        grep '^CORE_ASM_OPERAND_PLAN ' "$trace" >&2 || true
        exit 1
    fi
    if grep -Eq '^CORE_ASM_OPERAND_PLAN owner=1 .*machine_form=0( |$)' "$trace"; then
        echo "FAIL $fixture qualified Core ASM retained FormId=0" >&2
        exit 1
    fi
done
for opcode in 'mrs x9, tpidr_el1' 'mrs x9, tpidr_el2' \
              '.pushsection .altinstructions' '.hword 47' \
              '.org . - (664b-663b) + (662b-661b)'; do
    grep -Fq "$opcode" "$work/percpu-alternative.p451.s"
done

# The same nine source-contract negatives from the GNU/QEMU oracle must be
# rejected by MiniC alone. No cross assembler or emulator is necessary.
for bad in injected wrong_input wrong_mem; do
    tail=''; input=r; memory='+Q'
    case "$bad" in
        injected) tail='; svc #0' ;;
        wrong_input) input=i ;;
        wrong_mem) memory='=Q' ;;
    esac
    cat > "$work/percpu-bad-$bad.i" <<SRC
void bad(unsigned int *p,unsigned int n) {
    unsigned int loop, tmp;
    __asm__ __volatile__(
        "1: ldxr %w1, %2\n"
        " add %w1, %w1, %w3\n"
        " stxr %w0, %w1, %2\n"
        " cbnz %w0, 1b$tail"
        : "=&r"(loop), "=&r"(tmp), "$memory"(*p) : "$input"(n));
}
SRC
    if "$minic" --target=aarch64-linux -S "$work/percpu-bad-$bad.i" \
        -o "$work/percpu-bad-$bad.s" >"$work/percpu-bad-$bad.out" \
        2>"$work/percpu-bad-$bad.err"; then
        echo "FAIL malformed perCPU LL/SC admitted: $bad" >&2
        exit 1
    fi
done
for bad in feature replacement input_constraint output_constraint volatile patch_injection; do
    case "$bad" in
        feature)
            sed 's/"47"/"48"/' "$work/aarch64-percpu-alternative.i" ;;
        replacement)
            sed 's/mrs %0, tpidr_el2/mrs %0, tpidr_el3/' "$work/aarch64-percpu-alternative.i" ;;
        input_constraint)
            sed 's/: "Q" (\*(const unsigned long \*)current_stack_pointer)/: "r" (*(const unsigned long *)current_stack_pointer)/' "$work/aarch64-percpu-alternative.i" ;;
        output_constraint)
            sed 's/: "=r" (off)/: "=m" (off)/' "$work/aarch64-percpu-alternative.i" ;;
        volatile)
            sed 's/    asm("\.if/    asm volatile(".if/' "$work/aarch64-percpu-alternative.i" ;;
        patch_injection)
            sed 's/\.previous\\n" "\.endif/\.previous\\n" "svc #0\\n" "\.endif/' "$work/aarch64-percpu-alternative.i" ;;
    esac > "$work/alt-bad-$bad.i"
    if "$minic" --target=aarch64-linux -S "$work/alt-bad-$bad.i" \
        -o "$work/alt-bad-$bad.s" >"$work/alt-bad-$bad.out" \
        2>"$work/alt-bad-$bad.err"; then
        echo "FAIL malformed perCPU ALTERNATIVE admitted: $bad" >&2
        exit 1
    fi
done

# Optional offline P44 equivalence: compare .s as complete byte streams,
# including ABI scaffolding, machine instructions and ELF directives.
if [[ -n "${P44_MINIC_CC:-}" ]]; then
    test -x "$P44_MINIC_CC"
    for fixture in percpu-llsc percpu-alternative; do
        run_compiler "$P44_MINIC_CC" p44 "$fixture"
        if ! cmp -s "$work/$fixture.p44.s" "$work/$fixture.p451.s"; then
            echo "FAIL P44/P45.1 byte drift in $fixture" >&2
            diff -u "$work/$fixture.p44.s" "$work/$fixture.p451.s" >&2 || true
            exit 1
        fi
        echo "P451_A64_P44_BYTE_IDENTICAL=$fixture sha256=$(sha256sum "$work/$fixture.p451.s" | awk '{print $1}')"
    done
fi

echo 'P451_A64_PERCPU_NORMALIZED=PASS llsc_variants=16 widths=4 alternative=1 nonzero_form_ids=17 negatives=9 no_gnu=1 no_qemu=1'
