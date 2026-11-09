#!/usr/bin/env bash
# x86 GNU source RBX syntax is not physical machine clobber permission.
# ABI guard must fail-closed for live asm, while unused Linux static
# inline definitions may be parsed without interpreting their ISA effects.
set -Eeuo pipefail
cc=$1;test -x "$cc"
w=$(mktemp -d);trap 'rm -rf "$w"' EXIT
cat >"$w/dead.i" <<'SRC'
static inline void unused_x86(void){
    __asm__ __volatile__("movl $1, %%ebx" : : : "ebx");
}
static inline void unused_x86_64(void){
    __asm__ __volatile__("movq $1, %%rbx" : : : "rbx");
}
int main(void){return 42;}
SRC
"$cc" --target=x86_64-linux -S "$w/dead.i" -o "$w/dead.s"
cc -no-pie -x assembler "$w/dead.s" -o "$w/dead.elf"
rc=0;"$w/dead.elf" || rc=$?
test "$rc" -eq 42
cat >"$w/live.i" <<'SRC'
void actually_clobbers_saved_register(void){
    __asm__ __volatile__("movl $1, %%ebx" : : : "ebx");
}
SRC
if "$cc" --target=x86_64-linux -S "$w/live.i" -o "$w/live.s" >/dev/null 2>&1;then
 echo "FAIL: live EBX clobber silently accepted without save/restore" >&2;exit 1
fi
test ! -e "$w/live.s"
echo 'MINIC_X86_SAVED_CLOBBER_SYNTAX=PASS unused=2 executed_rejected=1'
