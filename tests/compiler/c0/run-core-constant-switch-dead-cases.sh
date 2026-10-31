#!/bin/sh
# M188: prune only unreachable, target-neutral constant-switch C segments.
# An externally re-entered label must keep its otherwise-dead source body.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
: "${MINIC_CC:?set MINIC_CC to strict built minic-cc}"
work=${BUILD_DIR:-"$root/build/debug"}/tests/minic-core-constant-switch
mkdir -p "$work"
cat > "$work/constant-switch.i" <<'SRC'
int constant_size(void) {
    int answer = 0;
    switch (sizeof(void *)) {
    case 1:
        __asm__ __volatile__("minic_unsupported_dead_hw" : : "r"(answer) : "memory");
        break;
    case 2:
        __asm__ __volatile__("minic_unsupported_dead_hw" : : "r"(answer) : "memory");
        break;
    case 4: answer = 42; break;
    case 8: answer = 42; break;
    default: answer = 99; break;
    }
    return answer;
}
int reentry(int early) {
    int result = 0;
    if (early) goto EARLY;
    switch (sizeof(void *)) {
    case 1: EARLY: result = 43; break;
    case 8: result = 42; break;
    default: result = 41; break;
    }
    return result;
}
int reentry_prefix(int early) {
    int result = 0;
    if (early) goto PREFIX;
    switch (sizeof(void *)) {
    PREFIX: result = 40;
    case 1: result += 1; break;
    case 4: result = 42; break;
    case 8: result = 42; break;
    default: result = 99; break;
    }
    return result;
}
int suffix_after_break(void) {
    int result = 42;
    switch (sizeof(char)) {
    case 1: break;
    case 2: __asm__ __volatile__("minic_unsupported_dead_hw" : : "r"(result)); break;
    case 4: __asm__ __volatile__("minic_unsupported_dead_hw" : : "r"(result)); break;
    default: result = 9; break;
    }
    return result;
}
int suffix_after_return(void) {
    switch (sizeof(char)) {
    case 1: return 42;
    case 2: __asm__ __volatile__("minic_unsupported_dead_hw"); return 43;
    default: return 44;
    }
}
int suffix_reentry(int early) {
    int result = 42;
    if (early) goto AFTER;
    switch (sizeof(char)) {
    case 1: break;
    case 2: AFTER: result = 43; break;
    default: result = 44; break;
    }
    return result;
}
int suffix_conditional(int early) {
    switch (sizeof(char)) {
    case 1: if (early) break;
    case 2: return 43;
    default: return 44;
    }
    return 42;
}
int main(void) {
    return constant_size() == 42 && reentry(1) == 43 &&
           reentry(0) == 42 && reentry_prefix(1) == 41 &&
           reentry_prefix(0) == 42 &&
           suffix_after_break() == 42 && suffix_after_return() == 42 &&
           suffix_reentry(0) == 42 && suffix_reentry(1) == 43 &&
           suffix_conditional(0) == 43 && suffix_conditional(1) == 42
               ? 0 : 1;
}
SRC
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$MINIC_CC" --target="$target" -S "$work/constant-switch.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
    if grep -Fq 'minic_unsupported_dead_hw' "$work/$target.s"; then
        echo "FAIL $target emitted unreachable machine template" >&2
        exit 1
    fi
done
# The direct LP64 emitters must consume the same source-label-aware Core
# reachable-block set as RV64. Counting actual emitted block labels guards
# against silently restoring unreachable machine bodies (including cloned
# function bodies), while source LABEL roots above retain external reentry.
rv64_blocks=$(grep -Ec '^\.L[^:]+_core_bb[0-9]+:' "$work/riscv64-linux.s")
aarch64_blocks=$(grep -Ec '^\.Lmini_arm_f[0-9]+_b[0-9]+:' "$work/aarch64-linux.s")
loongarch64_blocks=$(grep -Ec '^\.Lmini_loong_f[0-9]+_b[0-9]+:' "$work/loongarch64-linux.s")
if [ "$rv64_blocks" -eq 0 ] || [ "$aarch64_blocks" -ne "$rv64_blocks" ] ||
   [ "$loongarch64_blocks" -ne "$rv64_blocks" ]; then
    printf 'FAIL direct LP64 unreachable Core block emission rv64=%s aarch64=%s loongarch64=%s\n' \
      "$rv64_blocks" "$aarch64_blocks" "$loongarch64_blocks" >&2
    exit 1
fi
cc -O0 -no-pie -fno-pie -x c "$work/constant-switch.i" -o "$work/gnu"
cc -O0 -no-pie -fno-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic"
"$work/gnu"
"$work/minic"
# A nonconstant source selector must NEVER be mistaken for a dead case.
cat > "$work/dynamic-switch.i" <<'SRC'
int dynamic_switch(int value) {
    switch (value) {
    case 1: __asm__ __volatile__("minic_unsupported_dead_hw" : : "r"(value) : "memory"); break;
    default: return 42;
    }
    return 0;
}
int main(void) { return dynamic_switch(1); }
SRC
if "$MINIC_CC" --target=x86_64-linux -S "$work/dynamic-switch.i" \
    -o "$work/dynamic-switch.s" >"$work/dynamic-switch.out" \
    2>"$work/dynamic-switch.err"; then
    echo 'FAIL dynamic switch pruned executable unknown GNU asm' >&2
    exit 1
fi
echo "MINIC_CORE_CONSTANT_SWITCH_DEAD_SOURCE=PASS five_targets=5 rv64_aarch64_loongarch64_reachable_blocks=$rv64_blocks external_goto=6 suffix_break=1 suffix_return=1 conditional_fallthrough=1 dynamic_negative=1"
