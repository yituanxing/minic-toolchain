#!/bin/sh
# RV32 target selection is real, but only the semantic model is qualified.
# Prevent accidentally running the RV64 emitter for ILP32 C code.
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
grep -Fq 'sw t0, 0(t3)' "$work/kernel-csr-structured.s"
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
echo 'RV32I_TARGET_GATE=PASS ILP32_registered=true incompatible_ISA_rejected=true basic_C_core_emitted=true'
