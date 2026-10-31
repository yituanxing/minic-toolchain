#!/usr/bin/env bash
# Both failures were first diagnosed against frozen ORIGINAL GNU Linux 6.6.143
# preprocessed C: x86 arch/x86/entry/common.i and drivers/char/random.i.
# Check both source-grammar correctness and REAL linked execution. Never
# conceal a bad ISA operand behind C-parser acceptance.
set -Eeuo pipefail
minic=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/static.i" <<'EOF'
int value=41;
int static_pointer_attributes(void) {
    static int * __attribute__((__used__))
        __attribute__((__section__(".data.minic_attr"))) p = &value;
    return *p + 1;
}
int call_indirect_from_statement(int input) {
    return ({ static void * __attribute__((__used__))
            __attribute__((__section__(".discard.addressable")))
            __minic_addressable = (void *)&static_pointer_attributes;;
             (&static_pointer_attributes); })() + input;
}
int main(void) {
    return call_indirect_from_statement(0);
}
EOF
cc -O0 -no-pie -x c "$work/static.i" -o "$work/static.gnu"
set +e
"$work/static.gnu"
gcc_rc=$?
set -e
test "$gcc_rc" -eq 42 || { echo 'FAIL GNU original static statement attribute oracle'; exit 1; }
# End-to-end executable x86 control includes a GNU function-pointer result
# from a statement expression. Other targets have separate runtime ABI gates;
# this probe isolates the shared declaration grammar from those backends.
for target in x86_64-linux; do
    "$minic" --target="$target" -S "$work/static.i" \
        -o "$work/static.$target.s"
    grep -Fq '.data.minic_attr' "$work/static.$target.s"
done
cc -O0 -no-pie -x assembler "$work/static.x86_64-linux.s" -o "$work/static.minic"
set +e
"$work/static.minic"
minic_rc=$?
set -e
test "$minic_rc" -eq "$gcc_rc" || {
    echo "FAIL MiniC static local GNU oracle gnu=$gcc_rc minic=$minic_rc"; exit 1;
}
cat >"$work/gnu-register.i" <<'EOF'
unsigned long linux_register_bind_percent(void) {
    register unsigned long ret __asm__("%""rax")=41UL;
    return ret+1UL;
}
int main(void) {return linux_register_bind_percent() == 42UL ? 42 : 1;}
EOF
"$minic" --target=x86_64-linux -S "$work/gnu-register.i" \
    -o "$work/gnu-register.s"
cc -O0 -no-pie -x assembler "$work/gnu-register.s" -o "$work/gnu-register.minic"
set +e
"$work/gnu-register.minic"
register_rc=$?
set -e
test "$register_rc" -eq 42 || {
    echo "FAIL x86 GNU %rax local register spelling rc=$register_rc"; exit 1;
}
for name in '%rbx' '%rsp' '%' '%r17' '%cc'; do
    sed "s/%\"\"rax/$name/" "$work/gnu-register.i" >"$work/reject.i"
    if "$minic" --target=x86_64-linux -S "$work/reject.i" \
        -o "$work/reject.s" >"$work/reject.out" 2>"$work/reject.err"; then
        echo "FAIL unsafe x86 local binding admitted $name"; exit 1
    fi
done
cat >"$work/invalid-section.i" <<'EOF'
int value=0;
int f(void) {
    static int * __attribute__((__section__(".data.foo")))
        __attribute__((__section__(".data.bar"))) p=&value;
    return *p;
}
EOF
if "$minic" --target=x86_64-linux -S "$work/invalid-section.i" \
    -o "$work/invalid-section.s" >"$work/invalid-section.out" 2>&1; then
    echo 'FAIL conflicting GNU static local section admitted'; exit 1
fi
echo 'MINIC_LINUX_GNU_DECLARATION_GATES=PASS native_target=x86_64 native_static=42 x86_percent_rax=42 malformed_hardreg=5 conflicting_section_rejected=1'
