#!/usr/bin/env bash
set -Eeuo pipefail
minic=$1
target=$2
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
if [[ "$target" == x86_64 ]]; then
cat >"$dir/source.i" <<'EOF'
unsigned long test_a(unsigned long v) {
    register unsigned long x __asm__("r10") = v + 1UL;
    return x;
}
unsigned long test_b(unsigned long v) {
    register unsigned long x __asm__("rcx") = v + 2UL;
    return x;
}
int main(void) { return test_a(19UL) == 20UL &&
                        test_b(20UL) == 22UL ? 42 : 1; }
EOF
"$minic" --target=x86_64-linux -S "$dir/source.i" -o "$dir/minic.s"
cc -no-pie -x assembler "$dir/minic.s" -o "$dir/minic.elf"
cc -no-pie -x c "$dir/source.i" -o "$dir/gnu.elf"
for name in minic gnu; do
    set +e
    "$dir/$name.elf"
    rc=$?
    set -e
    test "$rc" -eq 42 || { echo "x86 local GNU ABI $name failed: $rc" >&2; exit 1; }
done
cat >"$dir/negative.i" <<'EOF'
unsigned long unsupported(unsigned long v) {
    register unsigned long x __asm__("rbx") = v;
    return x;
}
EOF
if "$minic" --target=x86_64-linux -S "$dir/negative.i" \
    -o "$dir/negative.s" >"$dir/negative.log" 2>&1; then
    echo "unsupported callee-saved x86 local register admitted" >&2
    exit 1
fi
grep -F 'local fixed register binding is not supported' "$dir/negative.log" >/dev/null
echo 'X86_LOCAL_HARDREG_SOURCE_GNU_NATIVE=PASS two_caller_names=1 saved_negative=1'
elif [[ "$target" == aarch64 ]]; then
cat >"$dir/source.i" <<'EOF'
unsigned long test_a(unsigned long v) {
    register unsigned long x __asm__("x0") = v + 1UL;
    return x;
}
unsigned long test_b(unsigned long v) {
    register unsigned long x __asm__("x8") = v + 2UL;
    return x;
}
int main(void) { return test_a(19UL) == 20UL &&
                        test_b(20UL) == 22UL ? 42 : 1; }
EOF
cat >"$dir/start.s" <<'EOF'
.text
.globl _start
_start:
  bl main
  mov x8, #93
  svc #0
EOF
"$minic" --target=aarch64-linux -S "$dir/source.i" -o "$dir/minic.s"
aarch64-linux-gnu-gcc -ffreestanding -fno-builtin \
  -c -x c "$dir/source.i" -o "$dir/gnu.o"
aarch64-linux-gnu-gcc -static -nostdlib -no-pie -Wl,-e,_start \
  -x assembler "$dir/start.s" "$dir/minic.s" -o "$dir/minic.elf"
aarch64-linux-gnu-gcc -static -nostdlib -no-pie -Wl,-e,_start \
  -x assembler "$dir/start.s" -x none "$dir/gnu.o" -o "$dir/gnu.elf"
for name in minic gnu; do
    set +e
    qemu-aarch64 "$dir/$name.elf"
    rc=$?
    set -e
    test "$rc" -eq 42 || { echo "AArch64 local GNU ABI $name failed: $rc" >&2; exit 1; }
done
cat >"$dir/negative.i" <<'EOF'
unsigned long bad(unsigned long v) {
    register unsigned long x __asm__("x18") = v;
    return x;
}
EOF
if "$minic" --target=aarch64-linux -S "$dir/negative.i" \
    -o "$dir/negative.s" >"$dir/negative.log" 2>&1; then
    echo 'AArch64 platform-reserved x18 local binding admitted' >&2
    exit 1
fi
grep -F 'local fixed register binding is not supported' "$dir/negative.log" >/dev/null
echo 'AARCH64_LOCAL_HARDREG_SOURCE_GNU_QEMU=PASS two_caller_names=1 reserved_negative=1'
else
    exit 1
fi
