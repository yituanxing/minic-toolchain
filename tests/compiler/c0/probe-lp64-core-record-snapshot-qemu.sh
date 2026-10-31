#!/usr/bin/env bash
# Same Core record ABI and object semantics across independent GNU LP64 ISAs.
set -Eeuo pipefail
target=${1:?aarch64 or loongarch64}; minic=${2:?MiniC driver}
test -x "$minic"
case "$target" in
    aarch64) gnu=aarch64-linux-gnu-gcc; emulator=qemu-aarch64 ;;
    loongarch64) gnu=loongarch64-linux-gnu-gcc-13; emulator=qemu-loongarch64 ;;
    *) echo 'unsupported LP64 target' >&2; exit 2 ;;
esac
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/records.i" <<'SRC'
typedef unsigned long u64;
typedef struct { u64 a,b,c,d,e; } Large;
typedef struct { u64 x,y; } Pair;
static u64 check_large(const Large value) {
    return value.a+value.b+value.c+value.d+value.e;
}
static Pair read_pair(const volatile Pair *p) { return *p; }
static Pair read_mutable(Pair *p) { return *p; }
static Pair read_local(const Pair source) { return source; }
static Pair read_nested(const Pair *source) {
    const Pair *p=source; return *p;
}
int main(void) {
    Large big={1,2,3,4,5};
    Pair p={17,25};
    if(check_large(big)!=15)return 1;
    Pair v=read_pair(&p);if(v.x!=17||v.y!=25)return 2;
    Pair w=read_mutable(&p);if(w.x!=17||w.y!=25)return 3;
    Pair z=read_local(w);if(z.x!=17||z.y!=25)return 4;
    Pair a=read_nested(&z);if(a.x!=17||a.y!=25)return 5;
    return 42;
}
SRC
"$minic" --target="$target-linux" -S "$d/records.i" -o "$d/mini.s"
test -s "$d/mini.s"
if [[ "$target" == "aarch64" ]];then
    grep -Eq 'ldrb w9|ldrb[[:space:]]' "$d/mini.s"
else
    grep -Eq 'ld\.bu[[:space:]]' "$d/mini.s"
fi
if [[ "${MINIC_SOURCE_ONLY:-0}" == 1 ]];then
    echo "LP64_CORE_RECORD_VALUE=PASS target=$target source=1"
    exit 0
fi
"$gnu" -static -O0 -x c "$d/records.i" -o "$d/gnu.elf"
"$gnu" -static -x assembler "$d/mini.s" -o "$d/mini.elf"
for c in mini gnu;do
    code=0
    timeout 35s "$emulator" "$d/$c.elf" || code=$?
    if [[ "$code" != 42 ]];then
        echo "LP64 record $target/$c failed return=$code" >&2
        exit 1
    fi
done
echo "LP64_CORE_RECORD_VALUE=PASS target=$target indirect_const=1 record_load_volatile=1 nested_record=1 GNU_AS=1 GNU_QEMU=42"
