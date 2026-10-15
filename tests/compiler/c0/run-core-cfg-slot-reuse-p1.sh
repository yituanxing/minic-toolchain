#!/usr/bin/env bash
# Opt-in GNU vs experimental MiniC RV64 semantic tests for cross-CFG stack slots.
set -Eeuo pipefail
: "$MINIC" "$WORK" "$RISCV_CC" "$QEMU_RISCV64"
mkdir -p "$WORK"
cat >"$WORK/subject.c" <<'C_SRC'
int cfg_branch_chain(int x) {
    int a=x+3,b=7-x;
    if(x&1) a=a*3; else a=a+17;
    if(x&2) b=b*5; else b=b-11;
    if(a>b) a=a-b; else b=a+b;
    return a+b;
}
int cfg_loop_reentry(int x) {
    int sum=0;
    for(int i=0;i<13;i=i+1) {
        if((i+x)%3==0) {
            int p=i+x,q=p*3; sum=sum+q;
        } else {
            int p=i-x,q=p*2; sum=sum+q;
        }
    }
    return sum;
}
int cfg_nested_ternary(int x) {
    int a=x>0?(x&3?x*3:x+7):(x< -20?-x:x-7);
    return (a&1)?a+5:a-6;
}
int cfg_early_goto(int x) {
    int y=x+7;
    if(x<0) goto negative;
    y=y*2;goto done;
negative:
    y=y-x*3;
done:
    if((x&4)==0) y=y+19;
    return y;
}
int cfg_side_effect(volatile int *a,int x) {
    int before=*a;
    if(x&1) *a=before+x; else *a=before-x;
    return *a+before;
}
int cfg_pointer_escape(int *p,int x) {
    int value=*p;
    int *ptr=&value;
    if(x&1) *ptr=value+x; else *ptr=value-x;
    *p=*ptr;return *p;
}
int cfg_switch_repeat(int x) {
    int y=x;
    for(int i=0;i<9;i=i+1) {
        switch((i+x+300)%4) {
        case 0: y=y+i;break;
        case 1: y=y-i;break;
        case 2: y=y+i*3;break;
        default:y=y-i*2;break;
        }
    }
    return y;
}
C_SRC
cat >"$WORK/harness.c" <<'C_HARNESS'
extern int cfg_branch_chain(int),cfg_branch_chain_ref(int);
extern int cfg_loop_reentry(int),cfg_loop_reentry_ref(int);
extern int cfg_nested_ternary(int),cfg_nested_ternary_ref(int);
extern int cfg_early_goto(int),cfg_early_goto_ref(int);
extern int cfg_side_effect(volatile int*,int),cfg_side_effect_ref(volatile int*,int);
extern int cfg_pointer_escape(int*,int),cfg_pointer_escape_ref(int*,int);
extern int cfg_switch_repeat(int),cfg_switch_repeat_ref(int);
extern int printf(const char*,...);
int main(void) {
    for(int x=-127;x<=127;x++) {
        int m[5]={cfg_branch_chain(x),cfg_loop_reentry(x),
            cfg_nested_ternary(x),cfg_early_goto(x),cfg_switch_repeat(x)};
        int g[5]={cfg_branch_chain_ref(x),cfg_loop_reentry_ref(x),
            cfg_nested_ternary_ref(x),cfg_early_goto_ref(x),cfg_switch_repeat_ref(x)};
        for(int i=0;i<5;i++) if(m[i]!=g[i]) {
            printf("P1_CFG_SEMANTIC=FAIL x=%d case=%d minic=%d gcc=%d\n",x,i,m[i],g[i]);return 2;
        }
        volatile int a=13,b=13;
        int p=cfg_side_effect(&a,x),q=cfg_side_effect_ref(&b,x);
        if(p!=q||a!=b) {printf("P1_CFG_SEMANTIC=VOLATILE_FAIL x=%d\n",x);return 3;}
        int c=23,d=23;
        p=cfg_pointer_escape(&c,x);q=cfg_pointer_escape_ref(&d,x);
        if(p!=q||c!=d) {printf("P1_CFG_SEMANTIC=POINTER_FAIL x=%d\n",x);return 4;}
    }
    printf("P1_CFG_SEMANTIC=PASS vectors=255 cases=7 gcc_reference=same_process\n");
    return 0;
}
C_HARNESS
"$RISCV_CC" -O0 -c \
  -Dcfg_branch_chain=cfg_branch_chain_ref \
  -Dcfg_loop_reentry=cfg_loop_reentry_ref \
  -Dcfg_nested_ternary=cfg_nested_ternary_ref \
  -Dcfg_early_goto=cfg_early_goto_ref \
  -Dcfg_side_effect=cfg_side_effect_ref \
  -Dcfg_pointer_escape=cfg_pointer_escape_ref \
  -Dcfg_switch_repeat=cfg_switch_repeat_ref \
  "$WORK/subject.c" -o "$WORK/gcc-reference.o"
MINIC_CORE_IR=strict "$MINIC" -S "$WORK/subject.c" -o "$WORK/experimental.s" \
  >"$WORK/minic.stdout" 2>"$WORK/minic.stderr"
"$RISCV_CC" -O0 -static "$WORK/harness.c" "$WORK/gcc-reference.o" \
  "$WORK/experimental.s" -o "$WORK/differential.elf"
timeout 25s "$QEMU_RISCV64" "$WORK/differential.elf" | tee "$WORK/result.log"
grep -Fq 'P1_CFG_SEMANTIC=PASS' "$WORK/result.log"
