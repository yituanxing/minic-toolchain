#!/usr/bin/env bash
# SysV x86 caller-PC and frame0: exact machine-state oracles, no guesses.
set -Eeuo pipefail
minic=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/minic.i" <<'EOF'
void nested_effect(void);
unsigned long mini_return_leaf(void) { return (unsigned long)__builtin_return_address(0); }
unsigned long mini_return_nonleaf(void) {
    nested_effect(); return (unsigned long)__builtin_return_address(0);
}
unsigned long mini_frame_leaf(void) { return (unsigned long)__builtin_frame_address(0); }
unsigned long mini_frame_nonleaf(void) {
    nested_effect(); return (unsigned long)__builtin_frame_address(0);
}
EOF
cat >"$work/gnu.c" <<'EOF'
void nested_effect(void);
unsigned long gnu_return_leaf(void) { return (unsigned long)__builtin_return_address(0); }
unsigned long gnu_return_nonleaf(void) {
    nested_effect(); return (unsigned long)__builtin_return_address(0);
}
unsigned long gnu_frame_leaf(void) { return (unsigned long)__builtin_frame_address(0); }
unsigned long gnu_frame_nonleaf(void) {
    nested_effect(); return (unsigned long)__builtin_frame_address(0);
}
EOF
cat >"$work/oracle.s" <<'EOF'
.text
.globl oracle_return_pc
.type oracle_return_pc,@function
oracle_return_pc:
  pushq %rbp
  movq %rsp, %rbp
  call *%rdi
1:
  leaq 1b(%rip), %r10
  xorq %r10, %rax
  leave
  ret
.size oracle_return_pc, .-oracle_return_pc
.globl oracle_frame
.type oracle_frame,@function
oracle_frame:
  pushq %rbp
  movq %rsp, %rbp
  call *%rdi
  leaq -16(%rbp), %r10
  xorq %r10, %rax
  leave
  ret
.size oracle_frame, .-oracle_frame
EOF
cat >"$work/driver.c" <<'EOF'
typedef unsigned long (*function)(void);
extern unsigned long mini_return_leaf(void), mini_return_nonleaf(void);
extern unsigned long mini_frame_leaf(void), mini_frame_nonleaf(void);
extern unsigned long gnu_return_leaf(void), gnu_return_nonleaf(void);
extern unsigned long gnu_frame_leaf(void), gnu_frame_nonleaf(void);
extern unsigned long oracle_return_pc(function), oracle_frame(function);
volatile unsigned long nested_counter;
void nested_effect(void) { ++nested_counter; }
int main(void) {
    if (oracle_return_pc(mini_return_leaf) ||
        oracle_return_pc(mini_return_nonleaf) ||
        oracle_frame(mini_frame_leaf) ||
        oracle_frame(mini_frame_nonleaf) ||
        oracle_return_pc(gnu_return_leaf) ||
        oracle_return_pc(gnu_return_nonleaf) ||
        oracle_frame(gnu_frame_leaf) ||
        oracle_frame(gnu_frame_nonleaf))
        return 1;
    return nested_counter == 4UL ? 42 : 2;
}
EOF
"$minic" --target=x86_64-linux -S "$work/minic.i" -o "$work/minic.s"
grep -F '  movq 8(%rbp), %rax' "$work/minic.s" >/dev/null
grep -F '  movq %rbp, %rax' "$work/minic.s" >/dev/null
cc -no-pie -O0 -fno-omit-frame-pointer "$work/driver.c" "$work/gnu.c" \
    -x assembler "$work/oracle.s" "$work/minic.s" -o "$work/check.elf"
set +e
"$work/check.elf"
rc=$?
set -e
test "$rc" -eq 42 || {
    echo "FAIL: x86 exact return-PC/frame GNU oracle exit=$rc" >&2; exit 1;
}
for builtin in return_address frame_address; do
    cat >"$work/unsupported.i" <<EOF
void *illegal(void) { return __builtin_$builtin(1); }
EOF
    if "$minic" --target=x86_64-linux -S "$work/unsupported.i" \
        -o "$work/unsupported.s" >"$work/unsupported.log" 2>&1; then
        echo "FAIL: x86 caller unwind depth>0 admitted for $builtin" >&2
        exit 1
    fi
    grep -F 'level is not supported by target' "$work/unsupported.log" >/dev/null
done
echo 'X86_SYSV_CALL_FRAME_GNU_NATIVE=PASS return_leaf=2 return_nonleaf=2 frame_leaf=2 frame_nonleaf=2 exact_PC=1 exact_RBP=1 depth1_rejected=2'
