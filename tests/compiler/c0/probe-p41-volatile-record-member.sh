#!/usr/bin/env bash
# P41: C record member cv qualifiers must survive parsing, AST verification,
# nested anonymous/direct member addressing, Core load/store and all targets.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$repo_root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
test -x "$minic_cc"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

source_file="$repo_root/tests/compiler/c0/p41_volatile_record_member.c"
"$host_cc" -E -P -std=gnu11 -DP41_NATIVE_MAIN -x c "$source_file" \
    -o "$work/input.i"

targets=(riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux)
for target in "${targets[@]}"; do
    "$minic_cc" --target="$target" -S "$work/input.i" \
        -o "$work/minic.$target.s"
done

# These five _Generic controlling expressions are purely C semantic tests:
# volatile scalar, nested field, pointer field, array element and const-volatile.
# Before P41 the first one incorrectly resolved to 0, whereas GNU C yields 1.
"$host_cc" -O2 -std=gnu11 -DP41_NATIVE_MAIN "$source_file" \
    -o "$work/gnu-native"
"$host_cc" -O2 -fno-pie -std=gnu11 -S "$source_file" -o "$work/gnu.s"
"$host_cc" -no-pie "$work/minic.x86_64-linux.s" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

# Neither backend may merge the two sequenced observable reads from one
# volatile member. Keep this focused on the source fixture's x86 load form,
# rather than requiring identical GCC/MiniC instruction selection.
count_pointer_reads() {
    local file=$1 base_register=$2
    awk -v reg="$base_register" '
        /^p41_read_pointer_twice:$/ { active=1; next }
        active && $1 == ".size" && $2 ~ /^p41_read_pointer_twice,/ { exit }
        active && $1 == "movl" && index($2, "(" reg ")") != 0 { count++ }
        END { print count + 0 }
    ' "$file"
}
if [[ $(count_pointer_reads "$work/gnu.s" '%rdi') != 2 ||
      $(count_pointer_reads "$work/minic.x86_64-linux.s" '%r10') != 2 ]]; then
    echo 'P41_VOLATILE_MEMBER_LOAD_COUNT_MISMATCH' >&2
    exit 1
fi

echo 'P41_VOLATILE_RECORD_MEMBER=PASS five_isa=5 generic=5 native_gnu=PASS native_minic=PASS volatile_two_reads=PASS volatile_bitfield=PASS'
