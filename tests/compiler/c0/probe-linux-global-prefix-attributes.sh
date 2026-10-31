#!/usr/bin/env bash
# Original GNU Linux 6.6.143 x86 idt.i and kernel/sched/build_policy.i:
# 'static const __attribute__((section/unused)) <type>' used to fail parse.
set -Eeuo pipefail
minic=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/global.i" <<'EOF'
struct rec { unsigned long value; };
static const __attribute__((__section__(".init.rodata")))
    struct rec initial[] = { {41UL} };
static volatile __attribute__((__used__)) unsigned long counter=1UL;
static const __attribute__((__used__))
    __attribute__((__section__(".init.rodata"))) unsigned long base=40UL;
int main(void) {
    return (initial[0].value + counter == 42UL &&
            base==40UL) ? 42 : 1;
}
EOF
cc -O0 -no-pie -x c "$work/global.i" -o "$work/global.gnu"
"$minic" --target=x86_64-linux -S "$work/global.i" -o "$work/global.minic.s"
grep -Fq '.init.rodata' "$work/global.minic.s"
cc -O0 -no-pie -x assembler "$work/global.minic.s" -o "$work/global.minic"
for variant in gnu minic; do
    set +e
    "$work/global.$variant"
    rc=$?
    set -e
    test "$rc" -eq 42 || {
        echo "FAIL Linux global prefix $variant rc=$rc"; exit 1;
    }
done
cat >"$work/bad-global.i" <<'EOF'
static const __attribute__((__section__(".one")))
    __attribute__((__section__(".two"))) int value=1;
int main(void) { return value; }
EOF
if "$minic" --target=x86_64-linux -S "$work/bad-global.i" \
    -o "$work/bad-global.s" >"$work/bad-global.log" 2>&1; then
    echo 'FAIL conflicting GNU section declaration head admitted'; exit 1
fi
echo 'MINIC_LINUX_GNU_GLOBAL_PREFIX=PASS GNU_native=42 MiniC_native=42 section=1 qualifier_preserved=1 conflicting_section_rejected=1'
