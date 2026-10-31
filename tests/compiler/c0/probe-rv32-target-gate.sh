#!/bin/sh
# RV32 ILP32 is qualified through Core, native GNU/MiniAS ELF32 and QEMU.
# This focused gate rejects accidentally selecting the RV64 emitter.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf '%s\n' 'int f(int x) { return x+1; }' > "$work/unit.i"
"$MINIC_CC" --target=riscv64-linux -S "$work/unit.i" -o "$work/rv64.s"
test -s "$work/rv64.s"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
     -S "$work/unit.i" -o "$work/rv32.s"
test -s "$work/rv32.s"
grep -q '^f:' "$work/rv32.s"
if grep -Eq '^[[:space:]]+(ld|sd|addiw|slliw|srliw|sraiw|addw|subw|mul)[[:space:]]' \
   "$work/rv32.s"; then
    echo 'RV32I_TARGET_GATE=FAIL emitted_illegal_opcode' >&2; exit 1
fi
cat > "$work/structured-eight.i" <<'SRC'
int rv32_eight_operands(int a, int b, int c, int d,
                        int e, int f, int g, int h) {
    __asm__ volatile("nop" : : "r"(a), "r"(b), "r"(c), "r"(d),
                              "r"(e), "r"(f), "r"(g), "r"(h) : "memory");
    return a;
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/structured-eight.i" -o "$work/structured-eight.s"
grep -Fq '# MINIC_SOURCE_INLINE_ASM_BEGIN' "$work/structured-eight.s"
cat > "$work/structured-nine.i" <<'SRC'
int rv32_nine_operands(int a, int b, int c, int d, int e,
                       int f, int g, int h, int j) {
    __asm__ volatile("nop" : : "r"(a), "r"(b), "r"(c), "r"(d),
                              "r"(e), "r"(f), "r"(g), "r"(h),
                              "r"(j) : "memory");
    return a;
}
SRC
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/structured-nine.i" -o "$work/structured-nine.s" \
    2>"$work/structured-nine.log"; then
    echo 'RV32I_ASM_CAPACITY=FAIL accepted_nine_generic_register_roles' >&2
    exit 1
fi
test ! -e "$work/structured-nine.s"
echo 'RV32I_ASM_CAPACITY=PASS accepted_8_roles=1 rejected_9_roles=1'
if "$MINIC_CC" --target=riscv32-linux -march=rv64i -mabi=lp64 \
     -S "$work/unit.i" -o "$work/mixed.s" 2>"$work/reject.log"; then
    echo 'RV32I_TARGET_GATE=FAIL accepted_RV64_profile' >&2; exit 1
fi
grep -F 'invalid RISC-V configuration' "$work/reject.log"
test ! -e "$work/mixed.s"
cat > "$work/real-kernel-fixed.i" <<'SRC'
struct task_struct;
register struct task_struct *riscv_current_is_tp __asm__("tp");
register unsigned long current_stack_pointer __asm__("sp");
struct task_struct *rv32_current(void) { return riscv_current_is_tp; }
unsigned long rv32_stack_pointer(void) { return current_stack_pointer; }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/real-kernel-fixed.i" -o "$work/real-kernel-fixed.s"
grep -Fq 'mv t0, tp' "$work/real-kernel-fixed.s"
grep -Fq 'mv t0, sp' "$work/real-kernel-fixed.s"
cat > "$work/unsafe-fixed.i" <<'SRC'
register unsigned long unsafe_argument_register __asm__("a0");
unsigned long unsafe(void) { return unsafe_argument_register; }
SRC
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
   -S "$work/unsafe-fixed.i" -o "$work/unsafe-fixed.s" 2>"$work/unsafe.log"; then
    echo 'RV32I_FIXED_REGISTER=FAIL accepted_live_argument_register' >&2
    exit 1
fi
test ! -e "$work/unsafe-fixed.s"
echo 'RV32I_FIXED_REGISTER=PASS tp_sp=Core_Target_RV32I bad_a0_rejected=1'
cat > "$work/kernel-frame.i" <<'SRC'
void *__raw_spin_lock_bh_origin(void) { return __builtin_return_address(0); }
void *linux_frame_origin(void) { return __builtin_frame_address(0); }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-frame.i" -o "$work/kernel-frame.s"
grep -Eq 'lw t0, [0-9]+\((s0|sp)\)' "$work/kernel-frame.s"
grep -Eq 'mv t0, (s0|sp)' "$work/kernel-frame.s"
cat > "$work/kernel-invalid-frame.i" <<'SRC'
void *unqualified_caller(void) { return __builtin_return_address(1); }
SRC
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
   -S "$work/kernel-invalid-frame.i" -o "$work/kernel-invalid-frame.s" \
   2>"$work/kernel-invalid-frame.log"; then
    echo 'RV32I_CALL_FRAME=FAIL accepted_nonzero_return_level' >&2
    exit 1
fi
test ! -e "$work/kernel-invalid-frame.s"
echo 'RV32I_CALL_FRAME=PASS return_address0=true frame_address0=true level1_rejected=true'
cat > "$work/kernel-wchar.i" <<'SRC'
unsigned long kernel_wide_literal_bytes(void) { return sizeof(L"A"); }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/kernel-wchar.i" -o "$work/kernel-wchar-default.s"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 -fshort-wchar \
    -S "$work/kernel-wchar.i" -o "$work/kernel-wchar-short.s"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -fshort-wchar -fno-short-wchar -S "$work/kernel-wchar.i" \
    -o "$work/kernel-wchar-restored.s"
grep -Fq 'li t0, 8' "$work/kernel-wchar-default.s"
grep -Fq 'li t0, 4' "$work/kernel-wchar-short.s"
grep -Fq 'li t0, 8' "$work/kernel-wchar-restored.s"
echo 'RV32I_WCHAR_OPTION=PASS default=32 short_wchar=16 reset=32'
cat > "$work/kernel-file-asm.i" <<'SRC'
__asm__(".globl rv32_kernel_asm_origin\nrv32_kernel_asm_origin:\n  ebreak");
unsigned int ordinary_c_machine_code(void) { return 42U; }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/kernel-file-asm.i" -o "$work/kernel-file-asm.s"
grep -Fq '# MINIC_SOURCE_FILE_ASM_BEGIN' "$work/kernel-file-asm.s"
grep -Fq '.globl rv32_kernel_asm_origin' "$work/kernel-file-asm.s"
grep -Fq '# MINIC_SOURCE_FILE_ASM_END' "$work/kernel-file-asm.s"
grep -Fq 'li t0, 42' "$work/kernel-file-asm.s"
echo 'RV32I_FILE_ASM=PASS preserved_original_asm=true compiler_C_strict_I=true'
cat > "$work/kernel-discard-section.i" <<'SRC'
int kernel_system_state;
static int * __attribute__((used, section(".discard.addressable")))
    kernel_addressable = &kernel_system_state;
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/kernel-discard-section.i" -o "$work/kernel-discard-section.s"
grep -Fq '.section .discard.addressable' "$work/kernel-discard-section.s"
grep -Fq '.4byte kernel_system_state' "$work/kernel-discard-section.s"
echo 'RV32I_GLOBAL_SECTION_RELOC=PASS shared_DataLayout=1 section_addressable=1 ELF32_width=4'
cat > "$work/kernel-init-setup.i" <<'SRC'
const char fixture_reset_name[] = "reset_devices";
int fixture_reset_handler(char *arg) { return arg != 0; }
struct obs_kernel_param { const char *name; int (*run)(char *); int early; };
static struct obs_kernel_param fixture_setup
  __attribute__((used, section(".init.setup"))) = {
    fixture_reset_name, fixture_reset_handler, 0
};
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/kernel-init-setup.i" -o "$work/kernel-init-setup.s"
grep -Fq '.section .init.setup' "$work/kernel-init-setup.s"
grep -Fq '.4byte fixture_reset_name' "$work/kernel-init-setup.s"
grep -Fq '.4byte fixture_reset_handler' "$work/kernel-init-setup.s"
echo 'RV32I_GLOBAL_SETUP_RECORD=PASS explicit_section=1 two_relocations=1 shared_record_layout=1'
cat > "$work/kernel-initcall-array.i" <<'SRC'
int initcall_start, initcall_mid, initcall_end;
static int *initcall_levels[] __attribute__((section(".init.data"))) = {
    &initcall_start, &initcall_mid, &initcall_end
};
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/kernel-initcall-array.i" -o "$work/kernel-initcall-array.s"
grep -Fq '.section .init.data' "$work/kernel-initcall-array.s"
grep -Fq '.4byte initcall_start' "$work/kernel-initcall-array.s"
grep -Fq '.4byte initcall_mid' "$work/kernel-initcall-array.s"
grep -Fq '.4byte initcall_end' "$work/kernel-initcall-array.s"
echo 'RV32I_GLOBAL_SECTION_ARRAY=PASS three_pointer_relocations=1 shared_array_layout=1'
cat > "$work/kernel-csr-structured.i" <<'SRC'
static inline unsigned long original_save_flags(void) {
    unsigned long value;
    __asm__ __volatile__("csrr %0, 0x100" : "=r"(value) : : "memory");
    return value;
}
static inline unsigned long original_exchange(unsigned long value) {
    unsigned long old;
    __asm__ __volatile__("csrrc %0, 0x100, %1"
                         : "=r"(old) : "r"(value) : "memory");
    return old;
}
unsigned long original_kernel_flags(void) {
    return original_exchange(original_save_flags());
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/kernel-csr-structured.i" -o "$work/kernel-csr-structured.s"
grep -Fq 'csrr t0, 0x100' "$work/kernel-csr-structured.s"
grep -Fq 'csrrc t0, 0x100, t1' "$work/kernel-csr-structured.s"
grep -Fq 'sw t0, 0(a2)' "$work/kernel-csr-structured.s"
grep -Fq '# MINIC_SOURCE_INLINE_ASM_BEGIN' "$work/kernel-csr-structured.s"
echo 'RV32I_STRUCTURED_CSR=PASS output_input=1 pointer_writeback=1 kernel_asm_provenance=1'
cat > "$work/kernel-opaque-asm.i" <<'SRC'
void rv32_warn_bugtable(void) {
    __asm__ __volatile__ (
        "1:\n\t"
        "ebreak\n"
        ".pushsection __bug_table,\"aw\"\n\t"
        "2:\n\t"
        ".word 1b\n\t"
        ".word 1262\n\t"
        ".half 1\n\t"
        ".half 0\n\t"
        ".org 2b + 12\n\t"
        ".popsection");
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-opaque-asm.i" -o "$work/kernel-opaque-asm.s"
grep -Fq '.pushsection __bug_table' "$work/kernel-opaque-asm.s"
grep -Fq 'ebreak' "$work/kernel-opaque-asm.s"
grep -Fq '# MINIC_SOURCE_INLINE_ASM_BEGIN' "$work/kernel-opaque-asm.s"
echo 'RV32I_OPAQUE_BUG_TABLE=PASS source_asm=1 MiniC_RV32I_unmodified=1'
cat > "$work/kernel-clzll.i" <<'SRC'
int linux_clzll(unsigned long long value) {
    return __builtin_clzll(value);
}
int linux_ctzll(unsigned long long value) {
    return __builtin_ctzll(value);
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-clzll.i" -o "$work/kernel-clzll.s"
grep -Fq '_rv32_clz64_high_' "$work/kernel-clzll.s"
grep -Fq '_rv32_ctz64_start_' "$work/kernel-clzll.s"
grep -Fq 'add t1, t1, t4' "$work/kernel-clzll.s"
if grep -Eq '^[[:space:]]+(clz|ctz|clzw|ctzw|mul|mulw|div)[[:space:]]' "$work/kernel-clzll.s"; then
  echo 'RV32I_CLZLL=FAIL extension_opcodes_leaked' >&2
  exit 1
fi
echo 'RV32I_CLZLL=PASS two_word_64bit_I_only=1 zero_half_offset=1'
cat > "$work/kernel-structured-atomic.i" <<'SRC'
unsigned int linux_amo_fetch_add(volatile unsigned int *ptr, unsigned int delta) {
    unsigned int old;
    __asm__ __volatile__("amoadd.w %1, %2, %0"
                         : "+A"(*ptr), "=r"(old) : "r"(delta) : "memory");
    return old;
}
void linux_amo_add(volatile unsigned int *ptr, unsigned int delta) {
    __asm__ __volatile__("amoadd.w zero, %1, %0"
                         : "+A"(*ptr) : "r"(delta) : "memory");
}
unsigned int linux_lrsc(volatile unsigned int *ptr, unsigned int desired) {
    unsigned int old, scratch;
    __asm__ __volatile__("0: lr.w %0, %2\n\t"
                         "bne %0, %z3, 1f\n\t"
                         "sc.w %1, %4, %2\n\t"
                         "bnez %1, 0b\n1:"
                         : "=&r"(old), "=&r"(scratch), "+A"(*ptr)
                         : "rJ"(0), "r"(desired) : "memory");
    return old;
}
unsigned int linux_register_rmw(unsigned int value) {
    __asm__ __volatile__("addi %0, %0, 1" : "+r"(value) : : "memory");
    return value;
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-structured-atomic.i" -o "$work/kernel-structured-atomic.s"
grep -Fq 'amoadd.w t1, t2, (t0)' "$work/kernel-structured-atomic.s"
grep -Fq 'amoadd.w zero, t1, (t0)' "$work/kernel-structured-atomic.s"
grep -Fq 'lr.w t0, (t2)' "$work/kernel-structured-atomic.s"
grep -Fq 'bne t0, t3, 1f' "$work/kernel-structured-atomic.s"
grep -Fq 'sc.w t1, t4, (t2)' "$work/kernel-structured-atomic.s"
grep -Fq 'addi t0, t0, 1' "$work/kernel-structured-atomic.s"
grep -Fq 'sw t1, 0(a2)' "$work/kernel-structured-atomic.s"
grep -Fq '# MINIC_SOURCE_INLINE_ASM_BEGIN' "$work/kernel-structured-atomic.s"
echo 'RV32I_STRUCTURED_ATOMIC=PASS memory_RW=1 early_clobber=1 operand_z=1 register_RMW=1'
cat > "$work/kernel-legacy-and-wide-asm.i" <<'SRC'
static inline unsigned long rv32_irq_save_legacy(void) {
    unsigned long flags = 2;
    __asm__ __volatile__("csrrc %0, 0x100, %1"
                         : "=r"(flags) : "rK"(flags) : "memory");
    return flags;
}
unsigned long rv32_irq_save_entry(void) { return rv32_irq_save_legacy(); }
int rv32_put_user_pair(unsigned int *ptr, unsigned long long value) {
    unsigned long err = 0;
    __asm__ __volatile__("sw %z3, %1\n\tsw %z4, %2"
                         : "+r"(err), "=m"(ptr[0]), "=m"(ptr[1])
                         : "rJ"(value), "rJ"(value >> 32) : "memory");
    return (int)err;
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-legacy-and-wide-asm.i" -o "$work/kernel-legacy-and-wide-asm.s"
grep -Fq 'csrrc t0, 0x100,' "$work/kernel-legacy-and-wide-asm.s"
grep -Fq 'sw t3, (t1)' "$work/kernel-legacy-and-wide-asm.s"
grep -Fq 'sw t4, (t2)' "$work/kernel-legacy-and-wide-asm.s"
grep -Fq 'sw t0, 0(a2)' "$work/kernel-legacy-and-wide-asm.s"
grep -Fq '# MINIC_SOURCE_INLINE_ASM_BEGIN' "$work/kernel-legacy-and-wide-asm.s"
echo 'RV32I_LEGACY_WIDE_ASM=PASS CSR=1 two_u64_low_words=1 memory_outputs=2 register_RMW=1'
cat > "$work/kernel-section-zero-aggregate.i" <<'SRC'
struct boot_pageset_fixture { unsigned int count; unsigned long buckets[7]; };
struct boot_zonestats_fixture { unsigned long counters[0]; };
static struct boot_pageset_fixture test_boot_pageset
    __attribute__((section(".data")));
static struct boot_zonestats_fixture test_boot_zonestats
    __attribute__((section(".data")));
void *rv32_boot_pageset_address(void) { return &test_boot_pageset; }
void *rv32_boot_zonestats_address(void) { return &test_boot_zonestats; }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-section-zero-aggregate.i" -o "$work/kernel-section-zero-aggregate.s"
grep -Fq 'test_boot_pageset:' "$work/kernel-section-zero-aggregate.s"
grep -Fq '.zero 32' "$work/kernel-section-zero-aggregate.s"
grep -Fq 'test_boot_zonestats:' "$work/kernel-section-zero-aggregate.s"
grep -Fq '.size test_boot_zonestats, 0' "$work/kernel-section-zero-aggregate.s"
echo 'RV32I_SECTION_ZERO_AGGREGATE=PASS DataLayout_zero_record=1 zero_sized_record=1 original_section=1'
cat > "$work/kernel-asm-goto.i" <<'SRC'
int rv32_jump_owner(int test) {
    if (test)
        __asm__ __volatile__ goto (
            ".option push\n\t.option norelax\n"
            "1: nop\n\t"
            ".pushsection __jump_table,\"aw\"\n\t"
            ".long 1b - ., %l[enabled] - .\n\t"
            ".word %[key]\n\t"
            ".popsection\n\t.option pop"
            : : [key] "i"(11) : : enabled);
    return 0;
enabled:
    return 1;
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-asm-goto.i" -o "$work/kernel-asm-goto.s"
grep -Fq '.pushsection __jump_table' "$work/kernel-asm-goto.s"
grep -Eq '\.long 1b - \., \.Lrv32_jump_owner_rv32_bb[0-9]+ - \.' "$work/kernel-asm-goto.s"
grep -Fq '# MINIC_SOURCE_INLINE_ASM_BEGIN' "$work/kernel-asm-goto.s"
echo 'RV32I_ASM_GOTO=PASS Core_target_block=1 source_jump_table=1 original_template=1'
cat > "$work/kernel-sizeof-switch.i" <<'SRC'
int rv32_source_asm_size_switch(void) {
    switch (sizeof(unsigned long)) {
    case 4:
        __asm__ __volatile__("fence rw, rw");
        return 4;
    case 8:
        __asm__ __volatile__("lr.d t0, (t1)");
        return 8;
    default:
        return 0;
    }
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-sizeof-switch.i" -o "$work/kernel-sizeof-switch.s"
grep -Fq 'fence rw, rw' "$work/kernel-sizeof-switch.s"
if grep -Fq 'lr.d' "$work/kernel-sizeof-switch.s"; then
    echo 'RV32I_CORE_CFG=FAIL unreachable_RV64_asm_emitted' >&2
    exit 1
fi
echo 'RV32I_CORE_CFG=PASS shared_reachability=1 dead_RV64_source_asm_pruned=1'
cat > "$work/kernel-weak-alias.i" <<'SRC'
int blake2s_compress_generic(int value) { return value + 1; }
int blake2s_compress(int value)
    __attribute__((weak, alias("blake2s_compress_generic")));
int rv32_alias_user(int value) { return blake2s_compress(value); }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/kernel-weak-alias.i" -o "$work/kernel-weak-alias.s"
grep -Fxq '.weak blake2s_compress' "$work/kernel-weak-alias.s"
grep -Fxq '.type blake2s_compress, @function' "$work/kernel-weak-alias.s"
grep -Fxq '.set blake2s_compress, blake2s_compress_generic' "$work/kernel-weak-alias.s"
echo 'RV32I_FUNCTION_ALIAS=PASS GNU_weak_alias=1 common_riscv_contract=1'
cat > "$work/kernel-dead-callgraph.i" <<'SRC'
extern int unresolved_kernel_backend(void);
static int dead_after_goto(void) { return unresolved_kernel_backend(); }
int live_kernel_entry(void) {
    goto done;
    return dead_after_goto();
done:
    return 11;
}
SRC
for arch in riscv32 riscv64; do
  if [ "$arch" = riscv32 ]; then
    "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
      -S "$work/kernel-dead-callgraph.i" -o "$work/kernel-dead-callgraph-32.s"
    output="$work/kernel-dead-callgraph-32.s"
  else
    "$MINIC_CC" --target=riscv64-linux \
      -S "$work/kernel-dead-callgraph.i" -o "$work/kernel-dead-callgraph-64.s"
    output="$work/kernel-dead-callgraph-64.s"
  fi
  grep -Fq 'live_kernel_entry:' "$output"
  if grep -Eq 'dead_after_goto:|call unresolved_kernel_backend' "$output"; then
    echo "RV32I_CORE_CALLGRAPH=FAIL dead_function_emitted target=$arch" >&2
    exit 1
  fi
done
echo 'RV32I_CORE_CALLGRAPH=PASS rv32_rv64_emission_roots=real_Core_CFG'
# Stress the same Core SSA last-use graph previously proven on RV64.
# One long basic block must reuse dead ILP32 4/8-byte result slots; this is
# a compiler regression, not a Linux-source workaround. A wide 64-bit value
# must still occupy TWO XLEN words in every live spill.
awk 'BEGIN {
  printf "unsigned rv32_spill_chain(unsigned x) { return "
  for (i=0; i<64; ++i) printf "("
  printf "x"
  for (i=0; i<64; ++i) printf " + %uu)",i+1
  printf "; }\n"
  printf "unsigned long long rv32_spill_wide(unsigned long long x) { return "
  for (i=0; i<48; ++i) printf "("
  printf "x"
  for (i=0; i<48; ++i) printf " + %uULL)",i+1
  printf "; }\n"
}' > "$work/rv32-shared-spill.i"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/rv32-shared-spill.i" -o "$work/rv32-shared-spill.s"
for entry in rv32_spill_chain rv32_spill_wide; do
  bytes=$(awk -v name="$entry:" '
    $0==name {in_function=1;next}
    in_function && $1=="li" && $2=="t0," {print $3;exit}
  ' "$work/rv32-shared-spill.s")
  case "$bytes" in ''|*[!0-9]*) echo "RV32I_CORE_SPILL=FAIL missing_frame entry=$entry bytes=$bytes" >&2; exit 1;; esac
  # Wide 64-bit lowering also reserves CoreObjects for arithmetic staging.
  # It occupies more stack than narrow ILP32 SSA even after safe reuse.
  limit=384
  if [ "$entry" = rv32_spill_wide ]; then limit=512; fi
  if [ "$bytes" -gt "$limit" ]; then
    echo "RV32I_CORE_SPILL=FAIL excessive_frame entry=$entry bytes=$bytes limit=$limit" >&2
    exit 1
  fi
  echo "RV32I_CORE_SPILL=PASS entry=$entry frame_bytes=$bytes reuse=block_last_use"
done
# RV64 already realigns stacks for over-aligned local Core objects. On RV32,
# object offsets alone are insufficient: entry sp is only 16-byte-aligned.
# Exercise the real ILP32 prologue/epilogue and stack-passed ninth argument.
cat > "$work/rv32-overaligned-frame.i" <<'SRC'
int rv32_overaligned_frame(int p0, int p1, int p2, int p3,
                           int p4, int p5, int p6, int p7,
                           int p8, int p9) {
    char huge[7] __attribute__((aligned(64)));
    huge[0] = (char)p9;
    return (int)huge[0] + (int)((unsigned long)huge & 63u) + p8;
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/rv32-overaligned-frame.i" -o "$work/rv32-overaligned-frame.s"
grep -q '^rv32_overaligned_frame:' "$work/rv32-overaligned-frame.s"
grep -Fq 'li t1, -64' "$work/rv32-overaligned-frame.s"
grep -Fq 'and sp, sp, t1' "$work/rv32-overaligned-frame.s"
grep -Fq 'mv sp, t0' "$work/rv32-overaligned-frame.s"
grep -Fq 'lw t6,' "$work/rv32-overaligned-frame.s"
echo 'RV32I_OVERALIGNED_FRAME=PASS alignment=64 entry_sp_saved=true incoming_stack_args=true'
echo 'RV32I_TARGET_GATE=PASS ILP32_registered=true incompatible_ISA_rejected=true basic_C_core_emitted=true'
