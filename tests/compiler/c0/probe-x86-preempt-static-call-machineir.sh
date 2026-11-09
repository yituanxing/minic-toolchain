#!/bin/sh
# Exact Linux +r(RSP) static-call contract, strict C ingress and MachineIR.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic="$root/build/debug/bin/minic-cc"
if test -n "$(printenv MINIC_CC || :)"; then minic="$MINIC_CC"; fi
work="$root/build/debug/tests/x86-preempt-static-call"
if test -n "$(printenv BUILD_DIR || :)"; then work="$BUILD_DIR/tests/x86-preempt-static-call"; fi
gnu=cc
if test -n "$(printenv HOST_CC || :)"; then gnu="$HOST_CC"; fi
mkdir -p "$work"
cat >"$work/positive.c" <<'SOURCE'
register unsigned long current_stack_pointer asm("rsp");
unsigned int preempt_counter;
void __SCT__preempt_schedule(void) { preempt_counter += 3U; }
void __SCT__preempt_schedule_notrace(void) { preempt_counter += 5U; }
void checked_preempt(void) {
    asm volatile("call __SCT__preempt_schedule" : "+r"(current_stack_pointer));
}
void checked_preempt_notrace(void) {
    asm volatile("call __SCT__preempt_schedule_notrace" : "+r"(current_stack_pointer));
}
int main(void) {
    checked_preempt();
    checked_preempt_notrace();
    return preempt_counter == 8U ? 42 : 1;
}
SOURCE
"$gnu" -std=gnu11 -Werror -fsyntax-only "$work/positive.c"
"$gnu" -E -P -x c "$work/positive.c" -o "$work/positive.i"
"$minic" --target=x86_64-linux -S "$work/positive.i" -o "$work/mini.s"
"$gnu" -no-pie -x assembler "$work/mini.s" -o "$work/mini.elf"
"$gnu" -std=gnu11 -no-pie "$work/positive.c" -o "$work/gnu.elf"
for producer in mini gnu; do
    rc=0
    "$work/$producer.elf" || rc=$?
    test "$rc" -eq 42 || { echo "X86_PREEMPT_STATIC_CALL=FAIL producer=$producer rc=$rc" >&2; exit 1; }
done
for form in other target constraint; do
    case "$form" in
        other) sed 's/call __SCT__preempt_schedule"/call __SCT__preempt_other"/' "$work/positive.i" >"$work/negative.i" ;;
        target) sed 's/asm("rsp")/asm("rbp")/' "$work/positive.i" >"$work/negative.i" ;;
        constraint) sed 's/: "+r"(current_stack_pointer)/: "=r"(current_stack_pointer)/' "$work/positive.i" >"$work/negative.i" ;;
    esac
    rm -f "$work/negative.s"
    if "$minic" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" \
            >"$work/negative.out" 2>"$work/negative.err"; then
        echo "X86_PREEMPT_STATIC_CALL=FAIL admitted=$form" >&2; exit 1
    fi
    test ! -e "$work/negative.s"
done
printf '%s\n' 'X86_PREEMPT_STATIC_CALL=PASS forms=2 GNU_MiniC_ELF=2 exit=42 invalid=3'
