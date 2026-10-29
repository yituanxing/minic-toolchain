#!/usr/bin/env bash
set -Eeuo pipefail
minic="$1"
test -x "$minic"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/lse.i" <<'C'
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
static u8 v8;
static u16 v16;
static u32 v32;
static u64 v64;
void lse_store32(u32 i) {
    asm volatile(".arch_extension lse\n\tstset\t%w[i], %[v]\n"
                 : [v] "+Q" (v32) : [i] "r" (i));
}
void lse_store64(u64 i) {
    asm volatile(".arch_extension lse\n\tstclr\t%[i], %[v]\n"
                 : [v] "+Q" (v64) : [i] "r" (i));
}
u32 lse_fetch32(u32 i) {
    u32 old;
    asm volatile(".arch_extension lse\n\tldaddal\t%w[i], %w[old], %[v]"
                 : [v] "+Q" (v32), [old] "=r" (old)
                 : [i] "r" (i) : "memory");
    return old;
}
u64 lse_fetch64(u64 i) {
    u64 old;
    asm volatile(".arch_extension lse\n\tldsetl\t%[i], %[old], %[v]"
                 : [v] "+Q" (v64), [old] "=r" (old)
                 : [i] "r" (i) : "memory");
    return old;
}
u8 lse_cas8(u8 old, u8 updated) {
    asm volatile(".arch_extension lse\n\tcasb\t%w[old], %w[new], %[v]\n"
                 : [v] "+Q" (v8), [old] "+r" (old)
                 : [new] "rZ" (updated));
    return old;
}
u16 lse_cas16(u16 old, u16 updated) {
    asm volatile(".arch_extension lse\n\tcasah\t%w[old], %w[new], %[v]\n"
                 : [v] "+Q" (v16), [old] "+r" (old)
                 : [new] "rZ" (updated) : "memory");
    return old;
}
u32 lse_cas32(u32 old, u32 updated) {
    asm volatile(".arch_extension lse\n\tcasal\t%w[old], %w[new], %[v]\n"
                 : [v] "+Q" (v32), [old] "+r" (old)
                 : [new] "rZ" (updated) : "memory");
    return old;
}
u64 lse_cas64(u64 old, u64 updated) {
    asm volatile(".arch_extension lse\n\tcasl\t%[old], %[new], %[v]\n"
                 : [v] "+Q" (v64), [old] "+r" (old)
                 : [new] "rZ" (updated) : "memory");
    return old;
}
C
"$minic" --target=aarch64-linux -S "$d/lse.i" -o "$d/lse.s"
for needle in 'stset w11, [x10]' 'stclr x11, [x10]' \
              'ldaddal w11, w9, [x10]' 'ldsetl x11, x9, [x10]' \
              'casb w9, w11, [x10]' 'casah w9, w11, [x10]' \
              'casal w9, w11, [x10]' 'casl x9, x11, [x10]'; do
    grep -Fq "$needle" "$d/lse.s" || {
        echo "AARCH64_LSE_MISSING=$needle" >&2
        exit 1
    }
done
sed 's/stset\\t%w\[i\]/unrecognized\\t%w[i]/' "$d/lse.i" > "$d/negative.i"
if "$minic" --target=aarch64-linux -S "$d/negative.i" -o "$d/negative.s" \
    >/dev/null 2>&1; then
    echo 'AArch64 qualified LSE accepted alien opcode' >&2
    exit 1
fi
test ! -e "$d/negative.s"
echo 'AARCH64_LSE_ATOMICS=PASS store=2 fetch=2 cas=4 alien_opcode_rejected=1'
