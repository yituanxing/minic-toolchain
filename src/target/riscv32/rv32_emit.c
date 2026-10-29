/* RV32I/ILP32 Core consumer. Core, DataLayout, symbol policy, RISC-V
 * opcode selection and assembler are shared with RV64. ILP32 owns its
 * two-word scalar ABI and frame layout; those are not interchangeable with
 * RV64 LP64. Qualified: records, direct/indirect and variadic call sites,
 * IEEE binary64 via pinned compiler-rt, and GNU differential ELF32/QEMU.
 * Variadic callee definitions use the ILP32 psABI register-save area.
 * Unqualified two-word va_arg operands continue to fail closed. */
#include "target/riscv32/rv32_emit.h"
#include "target/riscv32/rv32_internal.h"
#include "target/common/core_call_view.h"
#include "target/common/core_block_walk.h"
#include "target/common/core_instruction_family.h"
#include "target/riscv/instruction_select.h"
#include "target/riscv/register_identity.h"
#include "target/riscv32/rv32_abi_cursor.h"
#include "target/riscv/function_symbol.h"
#include "target/riscv32/rv32_global_emit.h"
#include "target/riscv/record_copy_bytes.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include "target/common/elf_alias_emit.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void rv32_diag(MinicDiagnostic *d, const char *path, const char *text) {
    if (d == NULL) return;
    d->path = path;
    d->line = 1U;
    d->column = 1U;
    (void)snprintf(d->message, sizeof(d->message), "%s", text);
}

/* Preserve explicit source-asm a-register clobbers without manufacturing
 * machine instructions. Core SSA lives in frame slots across asm; the
 * structured target allocator must separately avoid clobbered operands. */
static bool rv32_source_arg_clobbers_safe(const MinicCoreInlineAsm *source,
                                         size_t structured_operands) {
    size_t i;
    if (source == NULL ||
        (source->register_clobber_count != 0U &&
         source->register_clobbers == NULL)) return false;
    for (i = 0U; i < source->register_clobber_count; ++i) {
        const MinicCoreInlineAsmRegisterClobber *entry =
            &source->register_clobbers[i];
        unsigned physical_id;
        if (!minic_riscv_gpr_id(entry->name, entry->name_length,
                               &physical_id)) return false;
        if ((physical_id >= 5U && physical_id <= 7U) ||
            (physical_id >= 28U && physical_id <= 31U)) {
            /* SIZE_MAX is the explicit callee-saved-input allocator policy.
             * The normal t-register operand allocator cannot use this. */
            if (structured_operands != SIZE_MAX) return false;
            continue;
        }
        if (physical_id < 10U || physical_id > 17U) return false;
        /* Structured operands 6 and 7 are allocated to a0 and a1.
         * Legacy operands never use argument registers. */
        if (structured_operands != SIZE_MAX && physical_id < 12U &&
            structured_operands > (size_t)(6U + physical_id - 10U))
            return false;
    }
    return true;
}

/* GNU RISC-V inline asm is source-owned, not a MiniC machine-selected
 * opcode. Translate the Core-verified structured operand roles into ILP32
 * register and address transport; the source template owns its instructions.
 * The original Kbuild GNU assembler keeps upstream A/CSR/privileged ISA flags.
 * For example Linux arch_local_save_flags has "=r" and "memory" clobber;
 * no physical register clobber is implicitly assumed or shared with Core. */
/* Generic Core opaque-asm instruction: its GNU immediate operands have
 * already been specialized before machine lowering. The backend must not
 * interpret CSR/A/privileged source instructions as MiniC-produced RV32I.
 * Follow the existing RV64 opaque-asm literal/escaped-percent semantics,
 * while refusing asm-goto which requires a separate CFG binding. */
static bool rv32_emit_source_opaque_asm(
    FILE *out, const MinicCoreFunction *f, const char *symbol,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    size_t i;
    bool deferred_immediate;
    if (out == NULL || f == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        ins->value.inline_asm_id >= f->inline_asm_count)
        return false;
    source = &f->inline_asms[ins->value.inline_asm_id];
    if (!source->is_volatile || source->template_text == NULL ||
        !rv32_source_arg_clobbers_safe(source, 0U))
        return false;
    if (source->is_goto &&
        (symbol == NULL || symbol[0] == '\0' ||
         source->goto_target >= f->block_count))
        return false;
    deferred_immediate = source->is_goto &&
                         source->source_inline_asm_id != SIZE_MAX;
    if (deferred_immediate &&
        fprintf(out, "  .extern __minic_deferred_asm_immediate_%zu_0\n",
                source->source_inline_asm_id) < 0)
        return false;
    if (fprintf(out, "# MINIC_SOURCE_INLINE_ASM_BEGIN\n  ") < 0)
        return false;
    for (i = 0U; i < source->template_length; ++i) {
        unsigned char ch = (unsigned char)source->template_text[i];
        if (ch != '%') {
            if (fputc(ch, out) == EOF) return false;
        } else if (i + 1U < source->template_length &&
                   source->template_text[i + 1U] == '%') {
            if (fputc('%', out) == EOF) return false;
            ++i;
        } else if (deferred_immediate &&
                   i + 1U < source->template_length &&
                   source->template_text[i + 1U] == '0') {
            if (fprintf(out, "__minic_deferred_asm_immediate_%zu_0",
                        source->source_inline_asm_id) < 0) return false;
            ++i;
        } else if (source->is_goto && i + 2U < source->template_length &&
                   source->template_text[i + 1U] == 'l' &&
                   source->template_text[i + 2U] == '[') {
            size_t end = i + 3U;
            while (end < source->template_length &&
                   source->template_text[end] != ']') ++end;
            if (end >= source->template_length || end == i + 3U ||
                fprintf(out, ".L%s_rv32_bb%u", symbol,
                        (unsigned)source->goto_target) < 0)
                return false;
            i = end;
        } else {
            /* No unresolved percent substitutions may reach GNU assembler. */
            return false;
        }
    }
    return fprintf(out, "\n# MINIC_SOURCE_INLINE_ASM_END\n") >= 0;
}

/* The four legacy Core asm forms and the newer structured form use the
 * same source-template substitution. Register assignment is a Target concern;
 * Core owns the operand roles and the source owns every asm instruction.
 * Missing/unknown operands must never leak into the GNU assembler. */
static bool rv32_emit_source_template(
    FILE *out, const MinicCoreInlineAsm *source,
    const char *const bindings[10], const bool memory[10],
    bool saved_input_registers) {
    size_t i;
    if (out == NULL || source == NULL || source->template_text == NULL ||
        !source->is_volatile || source->is_goto ||
        !rv32_source_arg_clobbers_safe(source,
                                      saved_input_registers ? SIZE_MAX : 0U) ||
        fprintf(out, "# MINIC_SOURCE_INLINE_ASM_BEGIN\n  ") < 0)
        return false;
    for (i = 0U; i < source->template_length; ++i) {
        unsigned char ch = (unsigned char)source->template_text[i];
        size_t index;
        if (ch != '%') {
            if (fputc(ch, out) == EOF) return false;
            continue;
        }
        if (++i >= source->template_length) return false;
        ch = (unsigned char)source->template_text[i];
        if (ch == '%') {
            if (fputc('%', out) == EOF) return false;
            continue;
        }
        /* GNU %zN accepts a register for both nonzero and zero inputs.
         * The caller always materializes the input before this point. */
        if (ch == 'z') {
            if (++i >= source->template_length) return false;
            ch = (unsigned char)source->template_text[i];
        }
        if (ch < '0' || ch > '9') return false;
        index = (size_t)(ch - '0');
        if (bindings[index] == NULL ||
            (memory[index] ? fprintf(out, "(%s)", bindings[index]) :
                             fprintf(out, "%s", bindings[index])) < 0)
            return false;
    }
    return fprintf(out, "\n# MINIC_SOURCE_INLINE_ASM_END\n") >= 0;
}

/* Core's early single-output / single-input forms survive integer-constant
 * specialization (e.g. the real Linux arch_local_irq_save). All output and
 * input values here must occupy exactly one ILP32 register. */
static bool rv32_emit_source_legacy_asm(
    FILE *out, const MinicCoreFunction *f, const Rv32Frame *fr,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const char *bindings[10] = {NULL};
    bool memory[10] = {false};
    MinicCoreInlineAsmId asm_id;
    MinicCoreValueId input = MINIC_CORE_VALUE_INVALID;
    MinicCoreValueId address = MINIC_CORE_VALUE_INVALID;
    const char *output_reg = NULL;
    unsigned bits;
    bool is_signed;
    size_t mem_index, output_index, scalar_index;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL)
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        asm_id = ins->value.inline_asm_id;
        bindings[0] = "t0";
        output_reg = "t0";
        break;
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        asm_id = ins->value.register_output_input_inline_asm.inline_asm_id;
        input = ins->value.register_output_input_inline_asm.operand;
        bindings[0] = "t0";
        bindings[1] = "t1";
        output_reg = "t0";
        break;
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        asm_id = ins->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id;
        address = ins->value.memory_readwrite_scalar_input_inline_asm.memory_address;
        input = ins->value.memory_readwrite_scalar_input_inline_asm.operand;
        mem_index = ins->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
        output_index = ins->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
        scalar_index = ins->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
        if (mem_index >= 10U || scalar_index >= 10U ||
            mem_index == scalar_index ||
            (output_index != SIZE_MAX &&
             (output_index >= 10U || output_index == mem_index ||
              output_index == scalar_index))) return false;
        memory[mem_index] = true;
        bindings[mem_index] = "t0";
        bindings[scalar_index] = "t2";
        if (output_index != SIZE_MAX) {
            bindings[output_index] = "t1";
            output_reg = "t1";
        }
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        asm_id = ins->value.scalar_input_inline_asm.inline_asm_id;
        input = ins->value.scalar_input_inline_asm.operand;
        bindings[0] = "t0";
        break;
    default:
        return false;
    }
    if (asm_id >= f->inline_asm_count ||
        (input != MINIC_CORE_VALUE_INVALID &&
         (input >= f->value_count ||
          (!minic_type_is_integer(f->values[input].type) &&
           !minic_type_is_pointer(f->values[input].type)) ||
          !rv32_scalar(f, f->values[input].type, &bits, &is_signed))) ||
        (address != MINIC_CORE_VALUE_INVALID &&
         (address >= f->value_count ||
          !minic_type_is_pointer(f->values[address].type))) ||
        (output_reg != NULL &&
         (ins->result >= f->value_count ||
          (!minic_type_is_integer(ins->type) &&
           !minic_type_is_pointer(ins->type)) ||
          !rv32_scalar(f, ins->type, &bits, &is_signed))) ||
        (output_reg == NULL &&
         (ins->result != MINIC_CORE_VALUE_INVALID ||
          !minic_type_is_void(ins->type))))
        return false;
    source = &f->inline_asms[asm_id];
    if ((address != MINIC_CORE_VALUE_INVALID &&
         (!source->has_memory_clobber ||
          !rv32_slot(out, fr, address, "t0", true))) ||
        (input != MINIC_CORE_VALUE_INVALID &&
         !rv32_slot(out, fr, input,
                    address != MINIC_CORE_VALUE_INVALID ? "t2" :
                    output_reg != NULL ? "t1" : "t0", true)) ||
        !rv32_emit_source_template(out, source, bindings, memory, false))
        return false;
    return output_reg == NULL ||
           (rv32_canon(out, f, ins->type, output_reg) &&
            rv32_slot(out, fr, ins->result, output_reg, false));
}

/* Each Core operand owns one distinct caller-saved machine register.
 * t6 is reserved for large frame offsets by rv32_slot; a2 is reserved
 * for address-backed register operands and output writeback. Consequently
 * neither frame accesses nor early-clobber outputs alias a live asm operand.
 * Fixed-register bindings and explicit register clobbers need a separate
 * target allocation plan, and remain rejected until one is available. */
static bool rv32_emit_source_structured_asm(
    FILE *out, const MinicCoreFunction *f, const Rv32Frame *fr,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    static const char *const regs[] = {
        "t0", "t1", "t2", "t3", "t4", "t5", "a0", "a1"
    };
    bool seen[MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT] = {false};
    bool memory[10] = {false};
    const char *bindings[10] = {NULL};
    size_t n, i;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->value.structured_inline_asm.inline_asm_id >= f->inline_asm_count)
        return false;
    source = &f->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    n = ins->value.structured_inline_asm.operand_count;
    /* An asm that declares all scratch/argument registers clobbered (Linux
     * IRQ stack-switch trampolines do this) requires stable input registers.
     * s1/s2 are callee-saved by the invoked code; save the compiler's old
     * values on its own 16-byte-aligned stack before executing source asm.
     * The source promises to restore sp, exactly as required by GNU asm. */
    {
        bool has_t_clobber = false;
        for (i = 0U; i < source->register_clobber_count; ++i) {
            const MinicCoreInlineAsmRegisterClobber *c =
                &source->register_clobbers[i];
            unsigned id;
            if (minic_riscv_gpr_id(c->name, c->name_length, &id) &&
                ((id >= 5U && id <= 7U) ||
                 (id >= 28U && id <= 31U))) has_t_clobber = true;
        }
        if (has_t_clobber) {
            const char *const saved_regs[] = {"s1", "s2"};
            const char *const scratch_regs[] = {"t0", "t1"};
            if (n == 0U || n > 2U || source->is_goto ||
                !source->is_volatile || source->template_text == NULL ||
                !rv32_source_arg_clobbers_safe(source, SIZE_MAX))
                return false;
            for (i = 0U; i < n; ++i) {
                const MinicCoreStructuredInlineAsmOperand *b =
                    &ins->value.structured_inline_asm.operands[i];
                unsigned bits;
                bool is_signed;
                if (b->operand_index >= n || seen[b->operand_index] ||
                    b->has_fixed_register_binding ||
                    b->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                    b->value >= f->value_count ||
                    !rv32_scalar(f, f->values[b->value].type, &bits, &is_signed) ||
                    bits != 32U ||
                    !rv32_slot(out, fr, b->value,
                               scratch_regs[b->operand_index], true))
                    return false;
                seen[b->operand_index] = true;
                bindings[b->operand_index] = saved_regs[b->operand_index];
            }
            if (fprintf(out,
                "  addi sp, sp, -16\n  sw s1, 0(sp)\n  sw s2, 4(sp)\n") < 0)
                return false;
            for (i = 0U; i < n; ++i)
                if (fprintf(out, "  mv %s, %s\n", saved_regs[i], scratch_regs[i]) < 0)
                    return false;
            return rv32_emit_source_template(out, source, bindings,
                                            memory, true) &&
                   fprintf(out,
                       "  lw s1, 0(sp)\n  lw s2, 4(sp)\n  addi sp, sp, 16\n") >= 0;
        }
    }
    /* GNU local register variables are a declaration-to-inline-asm ABI,
     * not long-lived allocations of a0..a7.  The shared Core binding ID
     * identifies the required physical register, while each ordinary C
     * value still lives in its owned ILP32 stack slot between asm sites.
     * RISC-V SBI uses two read/write a-registers and six inputs. */
    {
        bool has_fixed = false;
        bool register_used[8] = {false};
        static const char *const abi_registers[8] = {
            "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
        };
        for (i = 0U; i < n; ++i)
            if (ins->value.structured_inline_asm.operands[i].has_fixed_register_binding)
                has_fixed = true;
        if (has_fixed) {
            if (n == 0U || n > 8U || source->is_goto ||
                !source->is_volatile || source->template_text == NULL ||
                !rv32_source_arg_clobbers_safe(source, n))
                return false;
            for (i = 0U; i < n; ++i) {
                const MinicCoreStructuredInlineAsmOperand *b =
                    &ins->value.structured_inline_asm.operands[i];
                const MinicCoreFixedRegisterBinding *fixed;
                unsigned reg_index, bits;
                bool is_signed;
                MinicType pointee;
                if (!b->has_fixed_register_binding ||
                    b->fixed_register_binding_id >= f->fixed_register_binding_count ||
                    b->operand_index >= n || seen[b->operand_index] ||
                    b->value >= f->value_count)
                    return false;
                fixed = &f->fixed_register_bindings[b->fixed_register_binding_id];
                if (!fixed->is_local ||
                    !minic_riscv_gpr_id(fixed->register_name,
                                       fixed->register_name_length, &reg_index) ||
                    reg_index < 10U || reg_index > 17U)
                    return false;
                reg_index -= 10U;
                if (register_used[reg_index]) return false;
                register_used[reg_index] = true;
                seen[b->operand_index] = true;
                bindings[b->operand_index] = abi_registers[reg_index];
                if (b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) {
                    if (!minic_type_equal(f->values[b->value].type, fixed->type) ||
                        !rv32_scalar(f, fixed->type, &bits, &is_signed) ||
                        bits != 32U ||
                        !rv32_slot(out, fr, b->value, abi_registers[reg_index], true))
                        return false;
                } else if (b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
                           b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) {
                    if (!minic_type_pointee(f->values[b->value].type, &pointee) ||
                        !minic_type_equal(pointee, fixed->type) ||
                        !rv32_scalar(f, pointee, &bits, &is_signed) || bits != 32U)
                        return false;
                    if (b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE &&
                        (!rv32_slot(out, fr, b->value, "t0", true) ||
                         fprintf(out, "  lw %s, 0(t0)\n",
                                 abi_registers[reg_index]) < 0))
                        return false;
                } else {
                    return false;
                }
            }
            if (!rv32_emit_source_template(out, source, bindings, memory, false))
                return false;
            for (i = 0U; i < n; ++i) {
                const MinicCoreStructuredInlineAsmOperand *b =
                    &ins->value.structured_inline_asm.operands[i];
                if (b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
                    b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) {
                    if (!rv32_slot(out, fr, b->value, "t0", true) ||
                        fprintf(out, "  sw %s, 0(t0)\n",
                                bindings[b->operand_index]) < 0)
                        return false;
                }
            }
            return true;
        }
    }
    if (n == 0U || n > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        !source->is_volatile || source->template_text == NULL || source->is_goto ||
        !rv32_source_arg_clobbers_safe(source, n))
        return false;
    for (i = 0U; i < n; ++i) {
        const MinicCoreStructuredInlineAsmOperand *b =
            &ins->value.structured_inline_asm.operands[i];
        unsigned bits = 0U;
        bool is_signed;
        MinicType pointee;
        if (b->operand_index >= n || seen[b->operand_index] ||
            b->has_fixed_register_binding || b->value >= f->value_count)
            return false;
        seen[b->operand_index] = true;
        bindings[b->operand_index] = regs[b->operand_index];
        switch (b->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!rv32_scalar(f, f->values[b->value].type, &bits, &is_signed)) {
                unsigned words;
                /* A GNU RV32 "rJ" input may be a 64-bit integer while the
                 * source asm names only its low register (Linux's two 32-bit
                 * user-access stores use %zN for each separately shifted
                 * half). The ILP32 frame is low-word first; rv32_slot loads
                 * exactly that register. Wide output/RMW stays rejected. */
                if (!minic_type_is_integer(f->values[b->value].type) ||
                    !rv32_scalar_storage_words(f, f->values[b->value].type, &words) ||
                    words != 2U)
                    return false;
            } else if (bits == 0U || bits > 32U)
                return false;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!minic_type_pointee(f->values[b->value].type, &pointee) ||
                (!rv32_scalar(f, pointee, &bits, &is_signed) &&
                 !(b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
                   rv32_is_wide_integer(f, pointee))) ||
                ((bits != 8U && bits != 16U && bits != 32U) &&
                 !(b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
                   rv32_is_wide_integer(f, pointee))))
                return false;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            if (!minic_type_pointee(f->values[b->value].type, &pointee))
                return false;
            memory[b->operand_index] = true;
            break;
        default:
            return false;
        }
    }
    for (i = 0U; i < n; ++i)
        if (!seen[i]) return false;
    /* Materialize all inputs before any source asm executes. Memory operands
     * carry addresses, not snapshots of their contents; GNU +A atomic writes
     * therefore remain source-owned and need no synthesized store. */
    for (i = 0U; i < n; ++i) {
        const MinicCoreStructuredInlineAsmOperand *b =
            &ins->value.structured_inline_asm.operands[i];
        const char *reg = regs[b->operand_index];
        if (b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT)
            continue;
        if (!rv32_slot(out, fr, b->value,
                       b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE
                           ? "a2" : reg, true))
            return false;
        if (b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
            unsigned bits;
            bool is_signed;
            MinicType pointee;
            const char *load;
            if (!minic_type_pointee(f->values[b->value].type, &pointee) ||
                !rv32_scalar(f, pointee, &bits, &is_signed))
                return false;
            load = bits == 8U ? (is_signed ? "lb" : "lbu") :
                   bits == 16U ? (is_signed ? "lh" : "lhu") : "lw";
            if (fprintf(out, "  %s %s, 0(a2)\n", load, reg) < 0)
                return false;
        }
    }
    if (!rv32_emit_source_template(out, source, bindings, memory, false))
        return false;
    for (i = 0U; i < n; ++i) {
        const MinicCoreStructuredInlineAsmOperand *b =
            &ins->value.structured_inline_asm.operands[i];
        unsigned bits = 0U;
        bool is_signed;
        MinicType pointee;
        const char *store;
        if (b->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            b->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)
            continue;
        if (!minic_type_pointee(f->values[b->value].type, &pointee) ||
            (!rv32_scalar(f, pointee, &bits, &is_signed) &&
             !rv32_is_wide_integer(f, pointee)))
            return false;
        store = bits == 8U ? "sb" : bits == 16U ? "sh" : "sw";
        if (!rv32_slot(out, fr, b->value, "a2", true) ||
            fprintf(out, "  %s %s, 0(a2)\n", store, regs[b->operand_index]) < 0)
            return false;
        /* GNU ILP32 permits a long-long register output; the source template
         * can designate its low register via %N.  A template which only
         * writes that low word leaves the upper register unspecified; choose
         * zero as a stable value for the otherwise uninitialized half. */
        if (rv32_is_wide_integer(f, pointee) &&
            fprintf(out, "  sw zero, 4(a2)\n") < 0)
            return false;
    }
    return true;
}

static bool rv32_emit_ins(FILE *out, const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicCoreFunction *f,
                          const Rv32Frame *fr,
                          const MinicCoreInstruction *ins,
                          MinicCoreInstructionId id,
                          const char *symbol) {
    if (out == NULL || program == NULL || target == NULL ||
        f == NULL || fr == NULL || ins == NULL) return false;
    /* Shared semantic dispatch, but the 32-bit emitter still decides
     * helper calls, ABI limb splitting and legal Base-I instructions. */
    const MinicCoreSemanticFamily family =
        minic_core_instruction_semantic_family(ins->kind);
    if (family == MINIC_CORE_FAMILY_INTEGER_SCALAR ||
        family == MINIC_CORE_FAMILY_OVERFLOW ||
        family == MINIC_CORE_FAMILY_BITCAST)
        return rv32_emit_integer_instruction(out, f, fr, ins, symbol);
    if (family == MINIC_CORE_FAMILY_FLOATING_SCALAR)
        return rv32_emit_fp_instruction(out, f, fr, ins);
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return rv32_emit_parameter(out, f, fr, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return rv32_emit_parameter_object(out, f, fr, ins);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;
        unsigned bits;
        bool is_signed;
        size_t index = ins->value.fixed_register_binding_id;
        if (index >= f->fixed_register_binding_count) return false;
        binding = &f->fixed_register_bindings[index];
        if (binding->register_name == NULL ||
            !(minic_target_info_fixed_register_supported(
                  target, binding->register_name, binding->register_name_length) ||
              (binding->is_local &&
               minic_target_info_local_fixed_register_supported(
                   target, binding->register_name, binding->register_name_length))) ||
            !minic_type_equal(binding->type, ins->type) ||
            !rv32_scalar(f, ins->type, &bits, &is_signed) || bits != 32U)
            return false;
        /* Architectural tp/sp are read-only C register variables. Use
         * RV32I mv (ADDI), then the existing ILP32 SSA value transport.
         * No RV64-specific frame, ABI or physical-register code enters Core. */
        return fprintf(out, "  mv t0, %s\n", binding->register_name) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return ins->value.block_id < f->block_count &&
               fprintf(out, "  lla t0, .L%s_rv32_bb%u\n",
                       symbol, (unsigned)ins->value.block_id) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return rv32_emit_memory_instruction(out, program, target, f, fr, ins);
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return rv32_emit_source_opaque_asm(out, f, symbol, ins);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return rv32_emit_source_legacy_asm(out, f, fr, ins);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return rv32_emit_source_structured_asm(out, f, fr, ins);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS: {
        /* RV64 and RV32 consume the same typed Core builtin.  RV32 owns
         * the saved 32-bit ra slot, moving stack and frame-pointer policy.
         * This is a base-I lw/mv operation, not an architecture-wide unwind. */
        if (ins->value.call_frame_address.level != 0U ||
            !minic_type_is_pointer(ins->type)) return false;
        switch (ins->value.call_frame_address.kind) {
        case MINIC_CORE_CALL_FRAME_ADDRESS_RETURN:
            return rv32_fixed_stack(out, fr, "lw", "t0", fr->outgoing_size) &&
                   rv32_slot(out, fr, ins->result, "t0", false);
        case MINIC_CORE_CALL_FRAME_ADDRESS_FRAME:
            return fprintf(out, "  mv t0, %s\n",
                           fr->has_dynamic_stack ? "s0" : "sp") >= 0 &&
                   rv32_slot(out, fr, ins->result, "t0", false);
        default:
            return false;
        }
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
        size_t offset;
        if (!fr->is_variadic || fr->abi.named_gpr_words > 8U ||
            fr->abi.named_stack_words > (SIZE_MAX - fr->frame_size) / 4U)
            return false;
        offset = fr->varargs_size != 0U ?
            fr->varargs_offset :
            fr->frame_size + fr->abi.named_stack_words * 4U;
        return rv32_address(out, fr, "t0", offset) &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_CALL:
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return rv32_emit_call(out, f, fr, ins, id);
    default:
        return false;
    }
}
static bool rv32_term(FILE *out, const MinicCoreFunction *f,
                       const Rv32Frame *fr,
                       const MinicCoreTerminator *term,
                       const char *symbol) {
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (fr->abi.record_return_size != 0U)
            return rv32_emit_record_return(out, fr, term, symbol);
        if (!minic_type_is_void(f->return_type)) {
            unsigned words;
            if (!rv32_scalar_storage_words(f, f->return_type, &words))
                return false;
            if (words == 2U) {
                if (!rv32_pair_slot(out, fr, term->return_value,
                                    "a0", "a1", true)) return false;
            } else if (!rv32_slot(out, fr, term->return_value, "a0", true))
                return false;
        }
        /* A real Lua VM frame can exceed the single-instruction JAL
         * range. Use the normal PC-relative far-branch sequence for all
         * function exits rather than requiring assembler relaxation. */
        return fprintf(out,
                       "  lla t6, .L%s_rv32_return\n"
                       "  jalr zero, 0(t6)\n", symbol) >= 0;
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return rv32_slot(out, fr, term->indirect_target, "t0", true) &&
               fprintf(out, "  jalr zero, 0(t0)\n") >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return term->branch_target < f->block_count &&
               fprintf(out,
                       "  lla t6, .L%s_rv32_bb%u\n"
                       "  jalr zero, 0(t6)\n",
                       symbol, (unsigned)term->branch_target) >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return term->conditional.when_true < f->block_count &&
               term->conditional.when_false < f->block_count &&
               rv32_slot(out, fr, term->conditional.condition, "t0", true) &&
               fprintf(out,
                       "  beqz t0, 1f\n"
                       "  lla t6, .L%s_rv32_bb%u\n"
                       "  jalr zero, 0(t6)\n"
                       "1:\n"
                       "  lla t6, .L%s_rv32_bb%u\n"
                       "  jalr zero, 0(t6)\n",
                       symbol, (unsigned)term->conditional.when_true,
                       symbol, (unsigned)term->conditional.when_false) >= 0;
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return fprintf(out, "  ebreak\n") >= 0;
    default:
        return false;
    }
}
static bool rv32_func(FILE *out, const MinicC0Program *program,
                       const MinicCoreFunction *f, const MinicFunction *def,
                       bool function_sections, const MinicTargetInfo *target,
                       const MinicCoreFunctionFacts *facts,
                       const MinicCoreObjectStoragePlan *storage,
                       MinicDiagnostic *diag) {
    Rv32Frame fr = {0};
    bool *reachable = NULL;
    MinicCoreBlockWalk walk = {0};
    const MinicCoreBlock *block = NULL;
    MinicRiscvFunctionSymbol sym;
    bool ok = false;
    size_t i, j;
    char message[200];
    /* RV32FunctionAbiPlan checks both return and parameter types once,
     * before constructing the frame. Its placements are the sole ILP32
     * authority for call, prologue and return emission. */
    if (!rv32_frame_init_with_storage(
            &fr, program, f, target, def->is_variadic, facts, storage)) {
        if (getenv("MINIC_RV32_FRAME_TRACE") != NULL)
            (void)fprintf(stderr,
                "RV32_FRAME_OWNER function=%s parameters=%zu values=%zu objects=%zu instructions=%zu\n",
                def->name != NULL ? def->name : "<unnamed>",
                f->parameter_count, f->value_count, f->object_count,
                f->instruction_count);
        rv32_diag(diag, "<rv32-frame>", "rv32i: cannot allocate an ILP32 Core frame");
        rv32_frame_destroy(&fr);
        return false;
    }
    reachable = (bool *)calloc(f->block_count, sizeof(*reachable));
    if (reachable == NULL ||
        !minic_core_function_mark_reachable_blocks(f, reachable, f->block_count)) {
        rv32_diag(diag, "<rv32-core>", "rv32i: cannot classify Core CFG reachability");
        goto done;
    }
    if (!minic_riscv_function_symbol_from_function(def, &sym)) goto done;
    sym.function_sections = function_sections;
    if (!minic_riscv_emit_function_symbol_begin(out, &sym)) goto done;
    if (fr.has_dynamic_stack_alignment) {
        /* Alignment beyond the psABI's 16 bytes must preserve entry sp.
         * The frame itself remains 16-byte aligned at every call. */
        if (fprintf(out,
              "  mv t2, sp\n"
              "  li t0, %zu\n  sub sp, sp, t0\n"
              "  li t1, -%zu\n  and sp, sp, t1\n",
              fr.frame_size, fr.stack_alignment) < 0 ||
            !rv32_stack(out, "sw", "t2", fr.entry_sp_offset))
            goto done;
    } else if (fprintf(out, "  li t0, %zu\n  sub sp, sp, t0\n",
                       fr.frame_size) < 0) goto done;
    if (!rv32_stack(out, "sw", "ra", fr.outgoing_size) ||
        (fr.abi.indirect_return &&
         !rv32_stack(out, "sw", "a0", fr.sret_offset)) ||
        (fr.has_dynamic_stack &&
         (!rv32_stack(out, "sw", "s0", fr.saved_base_offset) ||
          fprintf(out, "  mv s0, sp\n") < 0))) goto done;
    if (fr.is_variadic && fr.varargs_size != 0U) {
        const size_t first = fr.abi.named_gpr_words;
        for (i = first; i < 8U; ++i) {
            char reg[4];
            (void)snprintf(reg, sizeof(reg), "a%zu", i);
            if (!rv32_fixed_stack(out, &fr, "sw", reg,
                                   fr.varargs_offset + (i - first) * 4U))
                goto done;
        }
    }
    minic_core_block_walk_begin(&walk, f, reachable);
    while (minic_core_block_walk_next(&walk, &i, &block)) {
        if (!block->has_terminator ||
            fprintf(out, ".L%s_rv32_bb%zu:\n", sym.symbol_name, i) < 0)
            goto done;
        if (!f->suppress_source_label_aliases &&
            block->source_label_id != SIZE_MAX &&
            fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0)
            goto done;
        for (j = 0U; j < block->instruction_count; ++j) {
            MinicCoreInstructionId id = 0U;
            const MinicCoreInstruction *ins = NULL;
            if (!minic_core_block_instruction_at(f, block, j, &id, &ins) ||
                !rv32_emit_ins(out, program, target, f, &fr,
                               ins, id, sym.symbol_name)) {
                (void)snprintf(message, sizeof(message),
                               "rv32i: unqualified Core instruction function=%.75s block=%zu id=%u kind=%u",
                               sym.symbol_name, i, (unsigned)id,
                               id < f->instruction_count ?
                               (unsigned)f->instructions[id].kind : UINT_MAX);
                rv32_diag(diag, "<rv32-core>", message);
                goto done;
            }
        }
        if (!rv32_term(out, f, &fr, &block->terminator, sym.symbol_name)) {
            rv32_diag(diag, "<rv32-core>", "rv32i: unsupported terminator or CFG edge");
            goto done;
        }
    }
    if (fprintf(out, ".L%s_rv32_return:\n", sym.symbol_name) < 0 ||
        !rv32_fixed_stack(out, &fr, "lw", "ra", fr.outgoing_size))
        goto done;
    if (fr.has_dynamic_stack) {
        ok = rv32_fixed_stack(out, &fr, "lw", "t1", fr.saved_base_offset);
        if (ok && fr.has_dynamic_stack_alignment) {
            ok = rv32_fixed_stack(out, &fr, "lw", "t2", fr.entry_sp_offset) &&
                 fprintf(out, "  mv sp, t2\n  mv s0, t1\n  ret\n") >= 0;
        } else if (ok) {
            ok = fprintf(out, "  li t0, %zu\n  add sp, s0, t0\n"
                               "  mv s0, t1\n  ret\n", fr.frame_size) >= 0;
        }
    } else if (fr.has_dynamic_stack_alignment) {
        ok = rv32_fixed_stack(out, &fr, "lw", "t0", fr.entry_sp_offset) &&
             fprintf(out, "  mv sp, t0\n  ret\n") >= 0;
    } else {
        ok = fprintf(out, "  li t0, %zu\n  add sp, sp, t0\n  ret\n",
                     fr.frame_size) >= 0;
    }
    ok = ok && minic_riscv_emit_function_symbol_end(out, &sym);
done:
    free(reachable);
    rv32_frame_destroy(&fr);
    return ok;
}
/* Generic TU traversal lives in Target/common; ELF32 globals, declaration
 * aliases, source asm and the physical ILP32 function ABI remain RV32-owned. */
typedef struct Rv32ProgramContext {
    MinicDiagnostic *diagnostic;
} Rv32ProgramContext;

static bool rv32_visit_global(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicGlobalObject *obj, size_t index, void *opaque) {
    Rv32ProgramContext *ctx = opaque;
    (void)index;
    if (obj->is_extern && obj->alias_target != MINIC_GLOBAL_OBJECT_INVALID)
        return minic_multi_elf_emit_object_alias(
            out, program, obj, "@object");
    if (minic_riscv32_emit_flat_global(out, program, target, obj)) return true;
    {
        char message[256];
        (void)snprintf(message, sizeof(message),
            "rv32i: unsupported global initializer or relocation"
            " object=%s values=%zu relocs=%zu zero=%u",
            obj->name == NULL ? "<unnamed>" : obj->name,
            obj->initializer_count, obj->relocation_count,
            obj->is_zero_initialized ? 1U : 0U);
        rv32_diag(ctx->diagnostic, "<rv32-global>", message);
    }
    return false;
}

static bool rv32_visit_declaration(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    size_t index, bool function_sections, void *opaque) {
    const char *name;
    (void)target;
    (void)fn;
    (void)index;
    (void)function_sections;
    (void)opaque;
    if (decl->alias_target != MINIC_FUNCTION_INVALID)
        return minic_riscv_emit_function_alias(out, program, decl);
    if (!decl->is_weak || decl->is_internal) return true;
    name = minic_c0_function_symbol_name(decl);
    return name != NULL && *name != '\0' &&
           fprintf(out, ".weak %s\n", name) >= 0;
}

static bool rv32_visit_function(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t index,
    bool function_sections, void *opaque) {
    Rv32ProgramContext *ctx = opaque;
    (void)index;
    return rv32_func(out, program, fn, decl, function_sections,
                     target, facts, storage, ctx->diagnostic);
}

static bool rv32_prefix(FILE *out, const MinicC0Program *program,
                        const MinicTargetInfo *target,
                        const MinicCoreFunction *functions, size_t count,
                        bool function_sections, void *opaque) {
    (void)program;
    (void)target;
    (void)functions;
    (void)count;
    (void)function_sections;
    (void)opaque;
    return fprintf(out, ".option norvc\n") >= 0;
}

static bool rv32_between(FILE *out, const MinicC0Program *program,
                         const MinicTargetInfo *target,
                         const MinicCoreFunction *functions, size_t count,
                         bool function_sections, void *opaque) {
    size_t i;
    if (program->file_asm_count != 0U && fprintf(out, ".text\n") < 0)
        return false;
    for (i = 0U; i < program->file_asm_count; ++i) {
        const MinicFileAsm *source_asm = &program->file_asms[i];
        if (source_asm->text == NULL ||
            fprintf(out, "# MINIC_SOURCE_FILE_ASM_BEGIN\n") < 0 ||
            fwrite(source_asm->text, 1U, source_asm->length, out) !=
                source_asm->length ||
            fprintf(out, "\n# MINIC_SOURCE_FILE_ASM_END\n") < 0)
            return false;
    }
    /* ELF32 alias/undefined-weak definitions precede live functions. */
    return minic_program_visit_functions(
               out, program, target, functions, count, function_sections,
               MINIC_PROGRAM_FUNCTION_DECLARATIONS, false,
               rv32_visit_declaration, opaque) &&
           fprintf(out, ".text\n") >= 0;
}

bool minic_riscv32_write_core_program(const char *path,
                                      const MinicC0Program *program,
                                      const MinicTargetInfo *target,
                                      const MinicCoreFunction *functions,
                                      size_t function_count,
                                      bool function_sections,
                                      MinicDiagnostic *diagnostic) {
    static const MinicProgramEmitConfig config = {
        rv32_visit_global, rv32_visit_function, rv32_prefix, rv32_between,
        NULL, NULL, MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false
    };
    bool ok;
    Rv32ProgramContext context = { diagnostic };
    if (path == NULL || program == NULL || target == NULL ||
        target->riscv_isa.xlen != 32U ||
        target->riscv_isa.abi != MINIC_RISCV_ABI_ILP32 ||
        function_count != program->function_count ||
        (function_count != 0U && functions == NULL)) {
        rv32_diag(diagnostic, "<rv32-abi>",
                  "rv32i: unsupported file-scope inline ASM or invalid Core function map");
        return false;
    }
    ok = minic_program_emit_write(
        path, program, target, functions, function_count,
        function_sections, &config, &context);
    if (!ok && diagnostic != NULL && diagnostic->message[0] == '\0')
        rv32_diag(diagnostic, path, "rv32i: emission failed");
    return ok;
}
