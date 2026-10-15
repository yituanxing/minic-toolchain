#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/function-pointer-field-array
mkdir -p "$work"
cat > "$work/input.c" <<'SRC'
typedef int (*filter_t)(int);
struct hook_filter {
    filter_t filters[4];
    unsigned int count;
};
static struct hook_filter state;
int main(void)
{
    return sizeof(struct hook_filter) == 40 && sizeof(state.filters) == 32 ? 0 : 1;
}
SRC
"$host_cc" -E -P -std=gnu11 -x c "$work/input.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -F '.size state, 40' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/function-pointer-field-array element=function-pointer count=4 layout=generic'
