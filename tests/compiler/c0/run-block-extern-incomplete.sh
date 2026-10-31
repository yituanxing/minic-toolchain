#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-extern-incomplete
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/block-extern-incomplete.c" \
    -o "$work/block-extern-incomplete.i"
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/block-extern-incomplete.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
    grep -F 'seq_user_ns:' "$work/$target.s" >/dev/null
done
cat > "$work/driver.c" <<'SRC'
struct user_namespace { int value; };
struct user_namespace init_user_ns = {42};
extern struct user_namespace *seq_user_ns(void);
int main(void) { return seq_user_ns()->value; }
SRC
"$host_cc" -std=gnu11 -O0 -no-pie -x assembler "$work/x86_64-linux.s" \
    -x c "$work/driver.c" -o "$work/minic-x86"
set +e
"$work/minic-x86"
mini_result=$?
set -e
test "$mini_result" -eq 42
"$host_cc" -std=gnu11 -O0 -no-pie \
    "$root/tests/compiler/c0/block-extern-incomplete.c" \
    "$work/driver.c" -o "$work/gnu-x86"
set +e
"$work/gnu-x86"
gnu_result=$?
set -e
test "$gnu_result" -eq 42
cat > "$work/incomplete-storage.i" <<'SRC'
struct unknown;
void erroneous_definition(void) {
    struct unknown bad;
}
SRC
if "$minic" --target=aarch64-linux -S "$work/incomplete-storage.i" \
    -o "$work/incomplete-storage.s" >"$work/negative.stdout" 2>"$work/negative.stderr"; then
    echo 'BLOCK_EXTERN_INCOMPLETE=FAIL incomplete local object accepted' >&2
    exit 1
fi
grep -F 'local object type is incomplete' "$work/negative.stderr" >/dev/null
printf '%s\n' 'BLOCK_EXTERN_INCOMPLETE=PASS five_targets=1 GNU_native=42 MiniC_native=42 bad_storage_rejected=1'
