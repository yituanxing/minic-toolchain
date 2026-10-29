#!/usr/bin/env bash
# Linux CONFIG_PREEMPT_DYNAMIC: physical RSP +r is only a liveness anchor;
# each statement must execute its exact static-call trampoline once.
set -Eeuo pipefail
minic=$1
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat >"$d/mini.i" <<'EOF'
register unsigned long current_stack_pointer asm("rsp");
void mini_preempt(void) {
    asm volatile("call __SCT__preempt_schedule" : "+r"(current_stack_pointer));
}
void mini_preempt_notrace(void) {
    asm volatile("call __SCT__preempt_schedule_notrace" : "+r"(current_stack_pointer));
}
EOF
sed 's/mini_preempt/gnu_preempt/g' "$d/mini.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
volatile unsigned int preempt_counter;
volatile unsigned int notrace_counter;
void __SCT__preempt_schedule(void) { preempt_counter += 3U; }
void __SCT__preempt_schedule_notrace(void) { notrace_counter += 5U; }
extern void mini_preempt(void), mini_preempt_notrace(void);
extern void gnu_preempt(void), gnu_preempt_notrace(void);
int main(void) {
    mini_preempt();
    mini_preempt_notrace();
    gnu_preempt();
    gnu_preempt_notrace();
    return preempt_counter == 6U && notrace_counter == 10U ? 42 : 1;
}
EOF
"$minic" --target=x86_64-linux -S "$d/mini.i" -o "$d/mini.s"
test "$(grep -Fc '  call __SCT__preempt_schedule' "$d/mini.s")" -eq 2
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mini.s" -o "$d/oracle"
set +e
"$d/oracle"; result=$?
set -e
test "$result" -eq 42 || {
    echo "FAIL x86 Linux preempt static-call result=$result" >&2
    exit 1
}

# None of these is Linux's qualified static-call ABI. Reject altered target,
# register binding, output access and GNU volatility independently.
sed 's/__SCT__preempt_schedule_notrace/__SCT__preempt_schedule_notrace_unknown/g' \
    "$d/mini.i" >"$d/unknown.i"
sed 's/current_stack_pointer asm("rsp")/current_stack_pointer asm("r12")/g' \
    "$d/mini.i" >"$d/wrong_register.i"
sed 's/"+r"(current_stack_pointer)/"=r"(current_stack_pointer)/g' \
    "$d/mini.i" >"$d/wrong_constraint.i"
sed 's/asm volatile(/asm(/g' "$d/mini.i" >"$d/not_volatile.i"
for shape in unknown wrong_register wrong_constraint not_volatile; do
    if "$minic" --target=x86_64-linux -S "$d/$shape.i" \
        -o "$d/$shape.s" >"$d/$shape.out" 2>"$d/$shape.err"; then
        echo "FAIL preempt static-call malformed GNU shape accepted: $shape" >&2
        exit 1
    fi
done
echo 'MINIC_X86_LINUX_PREEMPT_STATIC_CALL=PASS schedule=1 notrace=1 fixed_rsp=1 GCC_native=2 reject=4'
