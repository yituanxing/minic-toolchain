#!/usr/bin/env bash
set -Eeuo pipefail
test "$#" -eq 1
minic="$1"
test -x "$minic"
repo="$(cd "$(dirname "$0")/../../.." && pwd)"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
# Keep every original grep/awk rejection strict.  Report the first failing
# predicate and the already-emitted physical GNU assembly before cleanup.
a64_frame_diagnose_failure() {
    local status="$1" line="$2" command="$3" asm
    printf 'AARCH64_TYPED_FRAME_DIAG status=%s line=%s command=%s\n' \
        "$status" "$line" "$command" >&2
    for asm in "$scratch"/*.s; do
        [[ -f "$asm" ]] || continue
        printf 'AARCH64_TYPED_FRAME_DIAG assembly=%s\n' "$asm" >&2
        grep -nE 'fadd|fmul|fdiv|ldr|str|mov|add|sub|bl|\.size|^[[:alpha:]_].*:' \
            "$asm" | sed -n '1,240p' >&2 || :
    done
    exit "$status"
}
trap 'a64_frame_diagnose_failure "$?" "$LINENO" "$BASH_COMMAND"' ERR

# Native typed MachineIR tests verify width encoding, live scratch masking,
# stable x19 versus outgoing SP, incoming aligned frame SP, overflow rejection
# and verified GNU serialization. There is no second FILE* frame emitter.
frame_build=${BUILD_DIR:-"$repo/build/debug"}
case "$frame_build" in
    /*) ;;
    *) frame_build="$repo/$frame_build" ;;
esac
mkdir -p "$frame_build/tests/aarch64-frame-temporary"
TMPDIR="$frame_build/tests/aarch64-frame-temporary" \
    make -s --no-print-directory -C "$repo" BUILD_DIR="$frame_build" \
    check-a64-machine-mir-frame-select

# There must be one frame-address authority: the typed emitter. Reintroducing
# either late textual rewrite silently loses scratch-register liveness and
# can modify Linux's literal hardware ASM after contract qualification.
if test -e "$repo/src/target/aarch64/asm_legalize.c" ||
   test -e "$repo/src/target/aarch64/asm_legalize.h" ||
   grep -Eq 'asm_legalize|copy_dynamic_frame_asm|legalize_large_sp_memory' \
       "$repo/src/target/aarch64/codegen.c"; then
    echo 'AARCH64_TYPED_FRAME_SOURCE=FAIL legacy text postpass reachable' >&2
    exit 1
fi

# The entire direct Core-to-text integer/residual families were physically
# deleted. A reintroduced FILE* implementation is a regression, even if its
# eventual assembly happened to have valid frame offsets.
for source in integer_emit.c residual_emit.c; do
    if test -e "$repo/src/target/aarch64/$source"; then
        echo "AARCH64_TYPED_FRAME_SOURCE=FAIL legacy direct emitter restored: $source" >&2
        exit 1
    fi
done

# Real production paths: Core FP SSA and scalar memory operations.
"$minic" --target=aarch64-linux -S \
    "$repo/tests/compiler/c0/aarch64-typed-frame-production.i" \
    -o "$scratch/real.s"
grep -Fq 'fadd d9, d9, d10' "$scratch/real.s"
grep -Fq 'fmul d9, d9, d10' "$scratch/real.s"
grep -Fq 'fdiv d9, d9, d10' "$scratch/real.s"
grep -Eq 'ldr x10, \[(sp|x19)' "$scratch/real.s"
grep -Eq '^  str x10, \[x9(, #0)?\]$' "$scratch/real.s"

# Pointer arithmetic loads integer indices through a stable owned frame
# address. Narrow reads (ldrsb/ldrsh/ldrsw) can use the same region without
# pretending that all integer values occupy an 8-byte memory transfer.
cc -E -P -nostdinc -x c \
    "$repo/tests/compiler/c0/aarch64_64bit_pointer_stride.c" \
    -o "$scratch/pointer.i"
"$minic" --target=aarch64-linux -S \
    "$scratch/pointer.i" -o "$scratch/pointer.s"
grep -Eq '^  add x9, (sp|x19), #[0-9]+$' "$scratch/pointer.s"
grep -Eq '^  ldr x10, \[x9(, #0)?\]$' "$scratch/pointer.s"

# Real Core block/return/parameter paths, including a moving live SP and
# record-copy returns. These now share the same typed owned-slot/frame APIs
# as FP and memory rather than depending on a post-emission SP string pass.
"$minic" --target=aarch64-linux -S \
    "$repo/tests/compiler/c0/dynamic_stack_alloca.c" \
    -o "$scratch/dynamic.s"
grep -Fq '  mov x19, sp' "$scratch/dynamic.s"
grep -Eq '^  (ldr|str) [xw]9, \[x19' "$scratch/dynamic.s"
# Within an alloca frame, outgoing calls must read Core-owned arguments from
# the stable x19 frame and write only actual ABI spill slots at live SP.
awk '
    /^  sub sp, sp, #16$/ { call=1 }
    call && /^  ldr x9, \[x19, #[0-9]+\]$/ { owned=1 }
    call && /^  str x9, \[sp, #0\]$/ { outgoing0=1 }
    call && /^  str x9, \[sp, #8\]$/ { outgoing1=1 }
    call && /^  bl dynamic_stack_sum10$/ { invoked=1 }
    call && /^  add sp, sp, #16$/ { restored=1; exit }
    END { exit !(call && owned && outgoing0 && outgoing1 && invoked && restored) }
' "$scratch/dynamic.s"
cc -E -P -x c \
    "$repo/tests/compiler/c0/aarch64_indirect_record_return_minic.c" \
    -o "$scratch/record.i"
"$minic" --target=aarch64-linux -S \
    "$scratch/record.i" -o "$scratch/record.s"
grep -Eq '^  ldr x30, \[(x19|x17)' "$scratch/record.s"
# x8 is AAPCS64's incoming indirect-result pointer.  The physical
# selector preserves it in an owned frame slot across the nested indirect
# call.  The old x11/x19 textual-address spelling was not the ABI contract.
awk '
    $0 == "minic_indirect_three:" { inside=1; next }
    inside && /^\.size minic_indirect_three,/ { complete=1; inside=0; exit }
    !inside { next }
    /^  str x8, \[(sp|x19), #[0-9]+\]$/ {
        slot=$0; sub(/^.*\[/, "", slot); sub(/\]$/, "", slot)
        saved=1
        next
    }
    saved && /^  blr x16$/ { invoked=1; next }
    invoked && /^  ldr x10, \[(sp|x19), #[0-9]+\]$/ {
        reload=$0; sub(/^.*\[/, "", reload); sub(/\]$/, "", reload)
        if (reload == slot) stage=1
        next
    }
    stage == 1 {
        stage=($0 ~ /^  ldrb w11, \[x9, #0\]$/) ? 2 : 0
        next
    }
    stage == 2 {
        if ($0 ~ /^  strb w11, \[x10, #0\]$/) copied=1
        stage=0
    }
    END { exit !(complete && saved && invoked && copied) }
' "$scratch/record.s"
# Dynamic alloca moves physical SP.  The same caller-owned x8 must be
# saved and recovered through the stable x19 frame across gnu_make_three.
awk '
    $0 == "minic_dynamic_three:" { inside=1; next }
    inside && /^\.size minic_dynamic_three,/ { complete=1; inside=0; exit }
    !inside { next }
    /^  mov x19, sp$/ { anchored=1; next }
    anchored && /^  str x8, \[x19, #[0-9]+\]$/ {
        slot=$0; sub(/^.*\[/, "", slot); sub(/\]$/, "", slot)
        saved=1
        next
    }
    saved && /^  bl gnu_make_three$/ { invoked=1; next }
    invoked && /^  ldr x10, \[x19, #[0-9]+\]$/ {
        reload=$0; sub(/^.*\[/, "", reload); sub(/\]$/, "", reload)
        if (reload == slot) reloaded=1
        next
    }
    reloaded && /^  strb w11, \[x10, #0\]$/ { copied=1 }
    END { exit !(complete && anchored && saved && invoked && reloaded && copied) }
' "$scratch/record.s"
# A >4 KiB owned frame uses typed scratch materialization, never a silent
# truncation to an A64 immediate or an x12/x17 string postpass guess.
grep -Eq '^  movz x17, #[4-9][0-9]{3,}$' "$scratch/record.s"
grep -Fq '  str x8, [x17]' "$scratch/record.s"
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/real.s" \
        -o "$scratch/real.o"
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/dynamic.s" \
        -o "$scratch/dynamic.o"
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/record.s" \
        -o "$scratch/record.o"
    aarch64-linux-gnu-gcc -c -x assembler "$scratch/pointer.s" \
        -o "$scratch/pointer.o"
fi
echo 'AARCH64_TYPED_FRAME_PRODUCTION=PASS fp_memory_integer_residual_pointer_block_return_dynamic_owned_outgoing_large_record_gnu_asm_if_available'
