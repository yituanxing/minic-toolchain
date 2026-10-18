#!/bin/sh
# x86-64 ABI and ELF correctness: differential alignment and symbol visibility.
set -eu
: "${MINIC_CC:?set MINIC_CC to native minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in x86_64|amd64) ;; *) echo "SKIP x86_64 layout/ELF: non-x86 host"; exit 0;; esac
command -v readelf >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

cat > "$work/local-align.c" <<'EOF'
__attribute__((noinline))
static int verify(int seed) {
    char small[3] __attribute__((aligned(16)));
    char wide[5] __attribute__((aligned(32)));
    char huge[7] __attribute__((aligned(64)));
    small[0] = (char)seed;
    wide[0] = (char)(seed + 1);
    huge[0] = (char)(seed + 2);
    if (((unsigned long)small & 15UL) != 0) return 1;
    if (((unsigned long)wide & 31UL) != 0) return 2;
    if (((unsigned long)huge & 63UL) != 0) return 3;
    return small[0] + wide[0] + huge[0];
}
int main(void) { return verify(13); }
EOF
# Use the actual host target preprocessor; never reuse an RV64-generated .i.
"$HOST_CC" -E -P -x c "$work/local-align.c" -o "$work/local-align.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/local-align.i" -o "$work/local-align.s"
"$HOST_CC" -no-pie -x assembler "$work/local-align.s" -o "$work/local-align.bin"
"$HOST_CC" -O0 -no-pie "$work/local-align.c" -o "$work/local-align-gcc.bin"
set +e
"$work/local-align.bin"
mini_status=$?
"$work/local-align-gcc.bin"
gcc_status=$?
set -e
if [ "$gcc_status" -ne 42 ] || [ "$mini_status" -ne "$gcc_status" ]; then
    echo "FAIL: x86-64 local over-alignment mini=$mini_status gcc=$gcc_status" >&2
    exit 1
fi
echo "PASS: x86-64 local 16/32/64-byte alignments GNU/MiniC exit=$mini_status"

cat > "$work/visibility.c" <<'EOF'
__attribute__((visibility("hidden"))) int hidden_object = 1;
__attribute__((visibility("protected"))) int protected_object = 2;
int default_object = 3;
__attribute__((visibility("hidden"))) int hidden_function(void) { return hidden_object; }
EOF
"$HOST_CC" -E -P -x c "$work/visibility.c" -o "$work/visibility.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/visibility.i" -o "$work/visibility.s"
"$HOST_CC" -c -x assembler "$work/visibility.s" -o "$work/visibility.o"
"$HOST_CC" -c -x c "$work/visibility.c" -o "$work/visibility-gcc.o"
readelf -W --symbols "$work/visibility.o" >"$work/mini.sym"
readelf -W --symbols "$work/visibility-gcc.o" >"$work/gcc.sym"

check_visibility() {
    symbol="$1"
    expected="$2"
    mini=$(awk -v n="$symbol" '$NF == n {print $(NF-2)}' "$work/mini.sym" | tail -n 1)
    gcc=$(awk -v n="$symbol" '$NF == n {print $(NF-2)}' "$work/gcc.sym" | tail -n 1)
    if [ "$mini" != "$expected" ] || [ "$gcc" != "$expected" ]; then
        echo "FAIL: ELF visibility $symbol mini=$mini gcc=$gcc expected=$expected" >&2
        exit 1
    fi
    echo "PASS: ELF $symbol visibility=$mini matches GCC"
}
check_visibility hidden_object HIDDEN
check_visibility protected_object PROTECTED
check_visibility default_object DEFAULT
check_visibility hidden_function HIDDEN
