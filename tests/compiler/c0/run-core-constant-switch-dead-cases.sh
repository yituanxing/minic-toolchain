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
int main(void) {
    return constant_size() == 42 && reentry(1) == 43 &&
           reentry(0) == 42 ? 0 : 1;
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
echo 'MINIC_CORE_CONSTANT_SWITCH_DEAD_SOURCE=PASS five_targets=5 external_goto=2 dynamic_negative=1'
