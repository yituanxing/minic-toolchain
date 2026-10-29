#!/usr/bin/env bash
# Original i915 *_REG register helpers return a (const i915_reg_t){ .reg=... }
# compound literal by value. The expression object is const-qualified while
# the function's SysV INTEGER-class record result is not.
set -Eeuo pipefail
minic=$1; d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
cat >"$d/record.i" <<'EOF'
typedef struct { unsigned int reg; } i915_reg_t;
i915_reg_t mini_intel_vga_cntrl_reg(unsigned int version) {
    if (version < 5U) return (const i915_reg_t){ .reg=0x1234U };
    if (version >= 12U) return (const i915_reg_t){ .reg=0x9876U };
    return (const i915_reg_t){ .reg=version + 0xA000U };
}
typedef struct { unsigned long long value; } i915_reg64_t;
i915_reg64_t mini_intel_gt_perf_limit_reasons_reg(unsigned long long v) {
    return (const i915_reg64_t){ .value=v + 17ULL };
}
EOF
sed 's/mini_/gnu_/g' "$d/record.i" >"$d/gnu.c"
cat >"$d/main.c" <<'EOF'
typedef struct { unsigned int reg; } i915_reg_t;
typedef struct { unsigned long long value; } i915_reg64_t;
extern i915_reg_t mini_intel_vga_cntrl_reg(unsigned int);
extern i915_reg_t gnu_intel_vga_cntrl_reg(unsigned int);
extern i915_reg64_t mini_intel_gt_perf_limit_reasons_reg(unsigned long long);
extern i915_reg64_t gnu_intel_gt_perf_limit_reasons_reg(unsigned long long);
int main(void) {
    const unsigned int version[]={0,4,5,7,11,12,255};
    unsigned int i;
    for (i=0; i<7; ++i)
        if (mini_intel_vga_cntrl_reg(version[i]).reg !=
            gnu_intel_vga_cntrl_reg(version[i]).reg) return 1;
    for (i=0; i<7; ++i)
        if (mini_intel_gt_perf_limit_reasons_reg(version[i]).value !=
            gnu_intel_gt_perf_limit_reasons_reg(version[i]).value) return 2;
    return 42;
}
EOF
"$minic" --target=x86_64-linux -S "$d/record.i" -o "$d/record.s"
cc -O0 -no-pie "$d/main.c" "$d/gnu.c" -x assembler "$d/record.s" -o "$d/diff"
set +e
"$d/diff"; rc=$?
set -e
test "$rc" -eq 42 || { echo "FAIL const-compound-literal SysV record return rc=$rc"; exit 1; }
echo 'MINIC_X86_LINUX_CONST_RECORD_RETURN=PASS one_gp_4=1 one_gp_8=1 differential=14'
