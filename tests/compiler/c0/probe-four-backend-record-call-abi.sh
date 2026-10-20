#!/bin/sh
# LP64 integer-class by-value records: GNU/MiniC bidirectional linking.
# Both sides must agree on a Token-sized 2-GP record, a small 1-GP
# record, GP register exhaustion, and mixed FP argument banks.
set -eu
: "${MINIC_CC:?set MINIC_CC to built minic-cc}"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT HUP INT TERM
cat >"$w/functions.i" <<'SRC'
struct Small { int left; int right; };
struct Token { const char *ptr; unsigned int count; };
int small_value(struct Small s) {
    return s.left * 10 + s.right;
}
int token_value(const char *prefix, int step, struct Token tok) {
    return prefix[0] == 'p' && step == 5 &&
           tok.ptr[0] == 'q' && tok.count == 19U;
}
int spill_value(long a, long b, long c, long d, long e,
                long f, long g, long h, struct Token tok) {
    return a+b+c+d+e+f+g+h == 36 &&
           tok.ptr[0] == 'q' && tok.count == 19U;
}
int near_spill(long a, long b, long c, long d, long e,
               long f, long g, struct Token tok) {
    return a+b+c+d+e+f+g == 28 &&
           tok.ptr[0] == 'q' && tok.count == 19U;
}
int mixed_fp(double d, struct Token tok) {
    return d == 1.25 && tok.ptr[0] == 'q' && tok.count == 19U;
}
int scalar_after_record(struct Token tok, long value, const char *str) {
    return tok.ptr[0] == 'q' && tok.count == 19U &&
           value == 37 && str[0] == 'z';
}
int pointer_after_mid_record(int prefix, struct Token tok, const char *str) {
    return prefix == 3 && tok.ptr[0] == 'q' &&
           tok.count == 19U && str[0] == 'z';
}
int double_after_record(struct Token tok, double value, const char *str) {
    return tok.ptr[0] == 'q' && tok.count == 19U &&
           value == 2.25 && str[0] == 'z';
}
int two_records_then_scalar(struct Token first, struct Token second, long value) {
    return first.ptr[0] == 'q' && second.ptr[0] == 'q' &&
           first.count == 19U && second.count == 19U && value == 73;
}
int partial_record_then_scalar(long a,long b,long c,long d,
        long e,long f,long g,struct Token tok,const char *str) {
    return a+b+c+d+e+f+g == 28 &&
           tok.ptr[0] == 'q' && tok.count == 19U && str[0] == 'z';
}

long many16(long a,long b,long c,long d,long e,long f,long g,long h,
            long i,long j,long k,long l,long m,long n,long o,long p) {
    return a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p;
}
int many_record11(long a,long b,long c,long d,long e,long f,long g,
                  long h,long i,long j,struct Token tok) {
    return a+b+c+d+e+f+g+h+i+j == 55 &&
           tok.count == 19U && tok.ptr[0] == 'q';
}
SRC
cat >"$w/driver.i" <<'SRC'
struct Small { int left; int right; };
struct Token { const char *ptr; unsigned int count; };
int small_value(struct Small);
int token_value(const char *, int, struct Token);
int spill_value(long,long,long,long,long,long,long,long,struct Token);
int near_spill(long,long,long,long,long,long,long,struct Token);
int mixed_fp(double,struct Token);
int scalar_after_record(struct Token,long,const char *);
int pointer_after_mid_record(int,struct Token,const char *);
int double_after_record(struct Token,double,const char *);
int two_records_then_scalar(struct Token,struct Token,long);
int partial_record_then_scalar(long,long,long,long,long,long,long,struct Token,
                               const char *);
long many16(long,long,long,long,long,long,long,long,
            long,long,long,long,long,long,long,long);
int many_record11(long,long,long,long,long,long,long,long,long,long,
                  struct Token);
extern int puts(const char *);
int main(void) {
    struct Small s = {4, 2};
    struct Token t = {"qpayload", 19U};
    int (*fp)(const char *, int, struct Token) = token_value;
    if (s.left != 4 || s.right != 2) return 17;
    if (t.ptr[0] != 'q' || t.count != 19U) return 18;
    puts("test-small");
    if (small_value(s) != 42) return 11;
    puts("test-token");
    if (!token_value("prefix",5,t)) return 12;
    puts("test-indirect");
    if (!fp("prefix",5,t)) return 13;
    puts("test-overflow");
    if (!spill_value(1,2,3,4,5,6,7,8,t)) return 14;
    puts("test-one-gp-left");
    if (!near_spill(1,2,3,4,5,6,7,t)) return 15;
    puts("test-mixed-fp");
    if (!mixed_fp(1.25,t)) return 16;
    puts("test-scalar-after-record");
    if (!scalar_after_record(t, 37, "z")) return 21;
    if (!pointer_after_mid_record(3,t,"z")) return 22;
    if (!double_after_record(t,2.25,"z")) return 23;
    if (!two_records_then_scalar(t,t,73)) return 24;
    if (!partial_record_then_scalar(1,2,3,4,5,6,7,t,"z")) return 25;
    puts("test-16-parameters");
    if (many16(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16) != 136)
        return 19;
    puts("test-11-parameter-record");
    if (!many_record11(1,2,3,4,5,6,7,8,9,10,t)) return 20;
    return 42;
}
SRC
for target in aarch64 loongarch64; do
    case "$target" in
      aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
      loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    for part in functions driver; do
        "$gcc" -O0 -std=gnu11 -c -x c "$w/$part.i" -o "$w/$target.$part.gnu.o"
        if ! MINIC_M1_TRACE=1 "$MINIC_CC" --target="$target-linux" \
            -S "$w/$part.i" -o "$w/$target.$part.s" \
            >"$w/$target.$part.out" 2>"$w/$target.$part.err"; then
            echo "RECORD_CALL_ABI=FAIL target=$target part=$part stage=MiniC" >&2
            sed -n '1,24p' "$w/$target.$part.err" >&2
            exit 1
        fi
        "$gcc" -c -x assembler "$w/$target.$part.s" \
            -o "$w/$target.$part.mini.o"
    done
    "$gcc" -static "$w/$target.driver.gnu.o" \
        "$w/$target.functions.gnu.o" -o "$w/$target.gnu"
    "$gcc" -static "$w/$target.driver.gnu.o" \
        "$w/$target.functions.mini.o" -o "$w/$target.gnu_calls_mini"
    "$gcc" -static "$w/$target.driver.mini.o" \
        "$w/$target.functions.gnu.o" -o "$w/$target.mini_calls_gnu"
    "$gcc" -static "$w/$target.driver.mini.o" \
        "$w/$target.functions.mini.o" -o "$w/$target.mini"
    for cc in gnu gnu_calls_mini mini_calls_gnu mini; do
        set +e
        timeout 45s "$qemu" "$w/$target.$cc" \
            >"$w/$target.$cc.out" 2>"$w/$target.$cc.err"
        status=$?
        set -e
        if [ "$status" -ne 42 ]; then
            echo "RECORD_CALL_ABI=FAIL target=$target variant=$cc stage=QEMU status=$status" >&2
            echo "RECORD_CALL_ABI_RUNTIME_STAGES target=$target compiler=$cc" >&2
            cat "$w/$target.$cc.out" >&2
            sed -n '1,20p' "$w/$target.$cc.err" >&2
            exit 1
        fi
        cmp "$w/$target.gnu.out" "$w/$target.$cc.out"
        cmp "$w/$target.gnu.err" "$w/$target.$cc.err"
    done
    echo "RECORD_CALL_ABI=PASS target=$target small=1 token16=1 stack=1 partial_gp=1 fp=1 indirect=1 sixteen_params=1 record_11th=1 scalar_after_record=1 mixed_fp=1 partial_stack=1 GNU_cross=2"
done
echo "RECORD_CALL_ABI_ACCEPT=PASS targets=2"
