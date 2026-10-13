#!/usr/bin/env bash
# Three-way real RV64 differential of the *opt-in* P1 Core slot allocator.
# This is a runtime semantic gate, never proof of global CFG correctness.
set -Eeuo pipefail
: "${MINIC:?MINIC points to the already-patched Runtime MiniC}"
: "${WORK:?WORK is a focused disposable output directory}"
RISCV_CC=${RISCV_CC:-riscv64-linux-gnu-gcc}
QEMU_RISCV64=${QEMU_RISCV64:-qemu-riscv64}
mkdir -p "$WORK"

cat >"$WORK/adversarial.c" <<'C_SOURCE'
/* Explicit writes before reads, plus dynamic control-flow backedges. */
int cfg_first_write_branches(int x) {
    int total = x & 32767;
    for (int i=0; i<19; ++i) {
        if ((i+x)&1) {
            int local = (i+11)*3;
            total = (total+local) & 32767;
        } else {
            int local = (i+17)*5;
            total = (total-local) & 32767;
        }
    }
    return total;
}
/* The old value is needed across a loop backedge: never assume dead at exit. */
int cfg_read_first_backedge(int x) {
    int live=(x+3000) & 32767;
    for (int i=0; i<17; ++i) {
        if ((i+x)&2) live = (live + i*13 + x) & 32767;
        else live = (live * 3 + i*7 + 19) & 32767;
    }
    return live;
}
/* Switch fallthrough/continue and a value surviving the join. */
int cfg_switch_carried(int x) {
    int carry=(x+1024)&32767;
    for (int i=0; i<21; ++i) {
        switch ((i+(x&15))%5) {
          case 0: carry=(carry+i*3)&32767; break;
          case 1: carry=(carry-i*5)&32767;
          case 2: carry=(carry+17)&32767; break;
          case 3: carry=(carry^((i+1)*19))&32767; continue;
          default: carry=(carry+11)&32767; break;
        }
        carry=(carry+1)&32767;
    }
    return carry;
}
/* A goto predecessor and loop backedge must not discard the live value. */
int cfg_goto_join_loop(int x) {
    int i=0, acc=(x+201)&32767;
again:
    if (i==15) goto done;
    if ((i+x)&1) goto odd;
    acc=(acc + 3*i + 11)&32767;
    goto advance;
odd:
    acc=(acc * 3 + 7)&32767;
advance:
    ++i;
    goto again;
done:
    return acc;
}
/* &local is stored and read in a later block.  No cross-block reuse here. */
int cfg_pointer_survives_join(int x) {
    int local=x+33;
    int *p=&local;
    for (int i=0; i<9; ++i) {
        if ((x+i)&1) local += 3*i;
        else local -= i+2;
    }
    return *p + local;
}
/* An unknown external call can mutate &local (real address escape). */
extern int cfg_external_mutate(int *p, int delta);
int cfg_external_escape(int x) {
    int local=x+7;
    if (x&1) cfg_external_mutate(&local,9);
    else cfg_external_mutate(&local,-11);
    if (local&4) local += 5;
    return local;
}
/* Volatile memory effects must survive across predecessor and backedge. */
int cfg_volatile_backedge(int x) {
    volatile int local=(x+500)&32767;
    for (int i=0; i<9; ++i) {
        if ((i+x)&1) local=(local+i*7)&32767;
        else local=(local+11)&32767;
    }
    return local;
}
/* Pair the separately-tested pure-expression elimination with CFG coloring. */
int cfg_discarded_addresses(int x) {
    int accum=0;
    for (int i=0; i<11; ++i) {
        if ((i+x)&1) {
            int a=i+3,b=x+19;
            (void)(&a==&b);
            accum=(accum+a+b)&32767;
        } else {
            int a=i+17,b=x-7;
            (void)(&a!=&b);
            accum=(accum+a-b)&32767;
        }
    }
    return accum;
}
C_SOURCE
names=(cfg_first_write_branches cfg_read_first_backedge cfg_switch_carried
       cfg_goto_join_loop cfg_pointer_survives_join cfg_external_escape
       cfg_volatile_backedge cfg_discarded_addresses)
refdefs=()
ref2defs=()
basedefs=()
for n in "${names[@]}"; do
    refdefs+=("-D${n}=${n}_ref")
    ref2defs+=("-D${n}=${n}_ref2")
    basedefs+=("-D${n}=${n}_base")
done
"$RISCV_CC" -E -P -x c "$WORK/adversarial.c" -o "$WORK/optimized.i"
"$RISCV_CC" -E -P -x c "${basedefs[@]}" "$WORK/adversarial.c" -o "$WORK/baseline.i"
# GNU reference source semantics and ABI must be independent of MiniC.
"$RISCV_CC" -O0 -c "${refdefs[@]}" "$WORK/adversarial.c" -o "$WORK/gcc-ref.o"
"$RISCV_CC" -O2 -c "${ref2defs[@]}" "$WORK/adversarial.c" -o "$WORK/gcc-ref2.o"
# The same profile MiniC binary in both paths.  The baseline disables the
# cross-block heuristic BY FUNCTION, not by switching compiler binaries.
env -u MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ \
  MINIC_P1_CROSSBLOCK_FUNCTION=__no_matching_function__ MINIC_CORE_IR=strict \
  "$MINIC" -S "$WORK/baseline.i" -o "$WORK/baseline.s" \
  >"$WORK/baseline.stdout" 2>"$WORK/baseline.stderr"
env -u MINIC_P1_CROSSBLOCK_FUNCTION MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
  MINIC_CORE_IR=strict "$MINIC" -S "$WORK/optimized.i" -o "$WORK/optimized.s" \
  >"$WORK/optimized.stdout" 2>"$WORK/optimized.stderr"
test -s "$WORK/optimized.s" && test -s "$WORK/baseline.s"
cat >"$WORK/harness.c" <<'C_HARNESS'
extern int printf(const char*,...);
int cfg_external_mutate(int *p,int delta) {
    *p = *p + delta;
    return *p;
}
#define DECL(n) extern int n(int), n##_base(int), n##_ref(int), n##_ref2(int)
DECL(cfg_first_write_branches);
DECL(cfg_read_first_backedge);
DECL(cfg_switch_carried);
DECL(cfg_goto_join_loop);
DECL(cfg_pointer_survives_join);
DECL(cfg_external_escape);
DECL(cfg_volatile_backedge);
DECL(cfg_discarded_addresses);
#define TEST(n,index) do { \
    int opt=n(x), base=n##_base(x), gcc=n##_ref(x), gcc2=n##_ref2(x); \
    if (opt!=gcc || base!=gcc || gcc2!=gcc) { \
       printf("P1_CFG_ADVERSARIAL=FAIL x=%d case=%d opt=%d base=%d gccO0=%d gccO2=%d\\n", \
              x,index,opt,base,gcc,gcc2);return 3; \
    } \
} while(0)
int main(void) {
    for (int x=-511; x<=511; ++x) {
        TEST(cfg_first_write_branches,0);
        TEST(cfg_read_first_backedge,1);
        TEST(cfg_switch_carried,2);
        TEST(cfg_goto_join_loop,3);
        TEST(cfg_pointer_survives_join,4);
        TEST(cfg_external_escape,5);
        TEST(cfg_volatile_backedge,6);
        TEST(cfg_discarded_addresses,7);
    }
    printf("P1_CFG_ADVERSARIAL=PASS vectors=1023 cases=8 oracles=4 optimized=real_qemu baseline=real_qemu gcc_O0_O2=real_qemu\\n");
    return 0;
}
C_HARNESS
"$RISCV_CC" -O0 -static "$WORK/harness.c" "$WORK/gcc-ref.o" "$WORK/gcc-ref2.o" \
    "$WORK/baseline.s" "$WORK/optimized.s" -o "$WORK/differential.elf"
timeout 35s "$QEMU_RISCV64" "$WORK/differential.elf" | tee "$WORK/result.log"
grep -Fq "P1_CFG_ADVERSARIAL=PASS vectors=1023 cases=8" "$WORK/result.log"
echo "P1_CFG_ADVERSARIAL=VERIFIED all_oracles_agree=true alias_loop_goto_switch_volatile=true source_scope=opt_in_only"
