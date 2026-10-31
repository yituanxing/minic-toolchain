#!/usr/bin/env bash
# GNU __builtin_{add,sub,mul}_overflow computes in unbounded precision.
# Linux overflows_type() checks u64 -> u32 by adding a destination-typed 0.
set -Eeuo pipefail
minic=$1; d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cat >"$d/mixed.i" <<'EOF'
struct overflow_input {
    unsigned long long a, b;
    long long s;
    unsigned int x;
    int y;
};
struct overflow_output {
    unsigned int narrow32, subtract32, multiply32;
    unsigned short narrow16;
    unsigned char narrow8;
    unsigned long long add64, subtract64;
    long long signed64;
    int signed32;
    unsigned int flags[9];
};
void mini_mixed_overflow(const struct overflow_input *v,
                         struct overflow_output *o) {
    o->flags[0] = __builtin_add_overflow(v->a, (unsigned int)0, &o->narrow32);
    o->flags[1] = __builtin_add_overflow(v->a, v->y, &o->subtract32);
    o->flags[2] = __builtin_mul_overflow(v->a, v->x, &o->multiply32);
    o->flags[3] = __builtin_mul_overflow(v->a, v->b, &o->signed64);
    o->flags[4] = __builtin_sub_overflow(v->s, v->a, &o->subtract64);
    o->flags[5] = __builtin_add_overflow(v->a, v->b, &o->add64);
    o->flags[6] = __builtin_add_overflow((unsigned char)v->a,
                                          (signed char)v->y, &o->narrow16);
    o->flags[7] = __builtin_sub_overflow((unsigned short)v->a,
                                          (signed short)v->y, &o->narrow8);
    o->flags[8] = __builtin_mul_overflow(v->s, (unsigned int)v->x,
                                          &o->signed32);
}
EOF
sed 's/mini_mixed_overflow/gnu_mixed_overflow/g' "$d/mixed.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
#include <string.h>
struct overflow_input {
    unsigned long long a,b;
    long long s;
    unsigned int x;
    int y;
};
struct overflow_output {
    unsigned int narrow32, subtract32, multiply32;
    unsigned short narrow16;
    unsigned char narrow8;
    unsigned long long add64, subtract64;
    long long signed64;
    int signed32;
    unsigned int flags[9];
};
extern void mini_mixed_overflow(const struct overflow_input *,
                                struct overflow_output *);
extern void gnu_mixed_overflow(const struct overflow_input *,
                               struct overflow_output *);
int main(void) {
    const unsigned long long vals[]={0,1,127,255,65535,0x7fffffffULL,
                                    0x80000000ULL,0xffffffffULL,
                                    0x100000000ULL,0x7fffffffffffffffULL,
                                    0x8000000000000000ULL,~0ULL};
    const long long signs[]={0,1,-1,127,-128,32767,-32768,
                             0x7fffffffLL,-2147483648LL,
                             0x7fffffffffffffffLL,(-9223372036854775807LL-1)};
    unsigned int i,j;
    for(i=0;i<sizeof(vals)/sizeof(vals[0]);++i)
      for(j=0;j<sizeof(signs)/sizeof(signs[0]);++j) {
        struct overflow_input v;
        struct overflow_output a={0},b={0};
        v.a=vals[i];v.b=vals[(i+3*j)%12];
        v.s=signs[j];v.x=(unsigned int)vals[(i+j)%12];
        v.y=(int)signs[j];
        mini_mixed_overflow(&v,&a);
        gnu_mixed_overflow(&v,&b);
        if(memcmp(&a,&b,sizeof(a))!=0)return 1;
      }
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$d/mixed.i" -o "$d/mixed.s"
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/mixed.s" -o "$d/diff"
set +e
"$d/diff"; rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL GNU mixed overflow differential rc=$rc"; exit 1; }
echo 'MINIC_X86_LINUX_MIXED_OVERFLOW=PASS operations=9 differential=132'
