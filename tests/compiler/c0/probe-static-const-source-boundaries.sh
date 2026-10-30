#!/bin/sh
# Strict GNU preprocess -> MiniC C -> GNU assembler/linker/native runtime.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/static_const_source_boundaries.c"

printf 'int external_guard_owner(int value) { return value * 2; }\n' > "$work/owner.c"
"$host" -E -P -std=gnu11 "$source_file" -o "$work/original.gnu.i"
"$mini" --target=x86_64-linux -S "$work/original.gnu.i" -o "$work/minic.s"
"$host" -c -x assembler "$work/minic.s" -o "$work/minic.o"
"$host" -c "$work/owner.c" -o "$work/owner.o"
"$host" -no-pie "$work/minic.o" "$work/owner.o" -o "$work/minic"
"$host" -O2 -no-pie "$source_file" "$work/owner.o" -o "$work/gnu"

# A clone is genuinely used, but the source still has address/dynamic/recursive
# references. Source ASM and GNU computed-goto labels must not be cloned.
grep -Fq '  call __minic_inline_spec_' "$work/minic.s"
for symbol in address_guard mixed_guard recursive_guard asm_guard label_guard; do
    grep -Fq "$symbol:" "$work/minic.s"
done
grep -Fq '  call asm_guard' "$work/minic.s"
grep -Fq '  call label_guard' "$work/minic.s"
if grep -Eq '^__minic_inline_spec_[0-9]+_[0-9]+:.*label' "$work/minic.s"; then
    echo 'STATIC_CONST_SOURCE=FAIL label cloned' >&2
    exit 1
fi
for image in gnu minic; do
    set +e
    "$work/$image"
    status=$?
    set -e
    if [ "$status" -ne 42 ]; then
        echo "STATIC_CONST_SOURCE=FAIL image=$image exit=$status" >&2
        exit 1
    fi
done

# The extra non-inline budget must fall back to the original static function
# without failing TU compilation or expanding into unbounded clones.
printf 'extern int external_guard_owner(int);\n' > "$work/budget.c"
printf 'static inline __attribute__((always_inline)) int helper(int x) { if(x > 5) return external_guard_owner(x); return x + 1; }\n' >> "$work/budget.c"
i=0
while [ "$i" -lt 66 ]; do
    printf 'static int guard_%s(int x) { return helper(x); }\n' "$i" >> "$work/budget.c"
    i=$((i + 1))
done
printf 'int main(void) { int result=0;\n' >> "$work/budget.c"
i=0
while [ "$i" -lt 66 ]; do
    printf 'result += guard_%s(2);\n' "$i" >> "$work/budget.c"
    i=$((i + 1))
done
printf 'return result == 198 ? 42 : 1; }\n' >> "$work/budget.c"
"$host" -E -P -std=gnu11 "$work/budget.c" -o "$work/budget.i"
"$mini" --target=x86_64-linux -S "$work/budget.i" -o "$work/budget.s"
grep -Fq '  call guard_64' "$work/budget.s"
grep -Fq '  call guard_65' "$work/budget.s"
"$host" -c -x assembler "$work/budget.s" -o "$work/budget.o"
"$host" -no-pie "$work/budget.o" "$work/owner.o" -o "$work/budget"
set +e
"$work/budget"
status=$?
set -e
if [ "$status" -ne 42 ]; then
    echo "STATIC_CONST_SOURCE=FAIL budget_exit=$status" >&2
    exit 1
fi
printf 'STATIC_CONST_SOURCE=PASS GNU_preprocessed=1 GNU_AS_LD=1 GNU_MiniC_exit=42 address_dynamic_recursive=retained asm_labels=retained budget64=fallback\n'
