#!/usr/bin/env bash
set -Eeuo pipefail
cc=${1:?MiniC binary};test -x "$cc"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/zero.i" <<'SRC'
struct Pending {unsigned long epoch;void* next;unsigned int flags;};
static struct Pending cache;
int main(void){return !cache.epoch && !cache.next && !cache.flags?42:1;}
SRC
"$cc" --target=x86_64-linux -S "$d/zero.i" -o "$d/mini.s"
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]];then
 gcc -static -x c "$d/zero.i" -o "$d/gnu"
 gcc -static -x assembler "$d/mini.s" -o "$d/mini"
 for exe in mini gnu;do rc=0;"$d/$exe" || rc=$?;test "$rc" -eq 42;done
fi
echo 'X86_ORIGINAL_STATIC_RECORD_ZERO=PASS GNU_diff=1'
