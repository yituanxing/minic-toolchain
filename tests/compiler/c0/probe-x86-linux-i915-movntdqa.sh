#!/usr/bin/env bash
# Linux 6.6 i915_memcpy.c: both 1x/4x MOVNTDQA + MOVAPS/MOVUPS blocks.
set -Eeuo pipefail
minic=$1; d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cat >"$d/copy.i" <<'EOF'
void mini_copy_aligned_1(void *dst,const void *src) {
    asm("movntdqa (%0), %%xmm0\n"
        "movaps %%xmm0, (%1)\n"
        ::"r"(src),"r"(dst):"memory");
}
void mini_copy_unaligned_1(void *dst,const void *src) {
    asm("movntdqa (%0), %%xmm0\n"
        "movups %%xmm0, (%1)\n"
        ::"r"(src),"r"(dst):"memory");
}
void mini_copy_aligned_4(void *dst,const void *src) {
    asm("movntdqa   (%0), %%xmm0\n"
        "movntdqa 16(%0), %%xmm1\n"
        "movntdqa 32(%0), %%xmm2\n"
        "movntdqa 48(%0), %%xmm3\n"
        "movaps %%xmm0,   (%1)\n"
        "movaps %%xmm1, 16(%1)\n"
        "movaps %%xmm2, 32(%1)\n"
        "movaps %%xmm3, 48(%1)\n"
        ::"r"(src),"r"(dst):"memory");
}
void mini_copy_unaligned_4(void *dst,const void *src) {
    asm("movntdqa   (%0), %%xmm0\n"
        "movntdqa 16(%0), %%xmm1\n"
        "movntdqa 32(%0), %%xmm2\n"
        "movntdqa 48(%0), %%xmm3\n"
        "movups %%xmm0,   (%1)\n"
        "movups %%xmm1, 16(%1)\n"
        "movups %%xmm2, 32(%1)\n"
        "movups %%xmm3, 48(%1)\n"
        ::"r"(src),"r"(dst):"memory");
}
EOF
sed 's/mini_copy/gnu_copy/g' "$d/copy.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
#include <string.h>
extern void mini_copy_aligned_1(void*,const void*);
extern void mini_copy_unaligned_1(void*,const void*);
extern void mini_copy_aligned_4(void*,const void*);
extern void mini_copy_unaligned_4(void*,const void*);
extern void gnu_copy_aligned_1(void*,const void*);
extern void gnu_copy_unaligned_1(void*,const void*);
extern void gnu_copy_aligned_4(void*,const void*);
extern void gnu_copy_unaligned_4(void*,const void*);
int main(void) {
    static unsigned char src[128] __attribute__((aligned(16)));
    static unsigned char a[128] __attribute__((aligned(16)));
    static unsigned char b[128] __attribute__((aligned(16)));
    unsigned int i;
    if (!__builtin_cpu_supports("sse4.1")) return 42;
    for(i=0;i<128;++i) src[i]=(unsigned char)(i*7+13);
    mini_copy_aligned_1(a,src); gnu_copy_aligned_1(b,src);
    if(memcmp(a,b,128))return 1;
    mini_copy_unaligned_1(a+1,src); gnu_copy_unaligned_1(b+1,src);
    if(memcmp(a,b,128))return 2;
    mini_copy_aligned_4(a,src); gnu_copy_aligned_4(b,src);
    if(memcmp(a,b,128))return 3;
    mini_copy_unaligned_4(a+1,src); gnu_copy_unaligned_4(b+1,src);
    if(memcmp(a,b,128))return 4;
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$d/copy.i" -o "$d/copy.s"
test "$(grep -c '  movntdqa ' "$d/copy.s")" -eq 10
test "$(grep -c '  movaps ' "$d/copy.s")" -eq 5
test "$(grep -c '  movups ' "$d/copy.s")" -eq 5
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/copy.s" -o "$d/diff"
set +e
"$d/diff"; rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL i915 MOVNTDQA GNU differential rc=$rc"; exit 1; }
cat >"$d/unsafe.i" <<'EOF'
void bad(void *d,const void *s) {
    asm("movntdqa (%0), %%xmm0\nmovaps %%xmm0, (%1)\ncli"
        ::"r"(s),"r"(d):"memory");
}
EOF
if "$minic" --target=x86_64-linux -S "$d/unsafe.i" \
    -o "$d/unsafe.s" >"$d/unsafe.log" 2>&1; then
    echo 'FAIL arbitrary privileged suffix admitted after MOVNTDQA'; exit 1
fi
echo 'MINIC_X86_LINUX_I915_MOVNTDQA=PASS 4forms=1 sse41_guard=1 negative=1'
