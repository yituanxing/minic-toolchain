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
#include "target/riscv/instruction_select.h"
#include "target/riscv32/rv32_abi_cursor.h"
#include "target/riscv64/function_symbol.h"
#include "target/riscv32/rv32_global_emit.h"
#include "target/riscv/record_copy_bytes.h"
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

/* GNU RISC-V inline asm is source-owned, not a MiniC machine-selected
 * opcode. Translate only the Core verified *scalar register* operand contract.
 * The original Kbuild GNU assembler keeps upstream A/CSR/privileged ISA flags.
 * For example Linux arch_local_save_flags has "=r" and "memory" clobber;
 * no physical register clobber is implicitly assumed or shared with Core. */
static bool rv32_emit_source_structured_asm(
    FILE *out, const MinicCoreFunction *f, const Rv32Frame *fr,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const char *regs[] = {"t0", "t1", "t2"};
    bool seen[3] = {false, false, false};
    size_t n, i;
    if (out == NULL || f == NULL || fr == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->value.structured_inline_asm.inline_asm_id >= f->inline_asm_count)
        return false;
    source = &f->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    n = ins->value.structured_inline_asm.operand_count;
    if (n == 0U || n > 3U || !source->is_volatile ||
        source->template_text == NULL || source->is_goto ||
        source->register_clobber_count != 0U)
        return false;
    for (i = 0U; i < n; ++i) {
        const MinicCoreStructuredInlineAsmOperand *b =
            &ins->value.structured_inline_asm.operands[i];
        unsigned bits;
        bool is_signed;
        MinicType pointee;
        if (b->operand_index >= n || seen[b->operand_index] ||
            b->has_fixed_register_binding || b->value >= f->value_count)
            return false;
        seen[b->operand_index] = true;
        if (b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) {
            if (!rv32_scalar(f, f->values[b->value].type, &bits, &is_signed) ||
                bits != 32U ||
                !rv32_slot(out, fr, b->value, regs[b->operand_index], true))
                return false;
        } else if (b->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) {
            if (!minic_type_pointee(f->values[b->value].type, &pointee) ||
                !rv32_scalar(f, pointee, &bits, &is_signed) || bits != 32U)
                return false;
        } else {
            /* Address-backed RMW, memory constraints and special registers
             * require a separate target ABI contract; fail closed. */
            return false;
        }
    }
    for (i = 0U; i < n; ++i)
        if (!seen[i]) return false;
    if (fprintf(out, "# MINIC_SOURCE_INLINE_ASM_BEGIN\n  ") < 0)
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
        /* GNU %zN means zero-register for a proven immediate zero;
         * no instruction in this contract currently needs it. */
        if (ch < '0' || ch > '2') return false;
        index = (size_t)(ch - '0');
        if (index >= n || fprintf(out, "%s", regs[index]) < 0)
            return false;
    }
    if (fprintf(out, "\n# MINIC_SOURCE_INLINE_ASM_END\n") < 0)
        return false;
    for (i = 0U; i < n; ++i) {
        const MinicCoreStructuredInlineAsmOperand *b =
            &ins->value.structured_inline_asm.operands[i];
        if (b->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT)
            continue;
        if (!rv32_slot(out, fr, b->value, "t3", true) ||
            fprintf(out, "  sw %s, 0(t3)\n", regs[b->operand_index]) < 0)
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
    /* Core opcode ownership follows the machine-operation family, not
     * a second target-specific copy of the language semantics. */
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
            !minic_target_info_fixed_register_supported(
                target, binding->register_name, binding->register_name_length) ||
            !minic_type_equal(binding->type, ins->type) ||
            !rv32_scalar(f, ins->type, &bits, &is_signed) || bits != 32U)
            return false;
        /* Architectural tp/sp are read-only C register variables. Use
         * RV32I mv (ADDI), then the existing ILP32 SSA value transport.
         * No RV64-specific frame, ABI or physical-register code enters Core. */
        return fprintf(out, "  mv t0, %s\n", binding->register_name) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return rv32_emit_integer_instruction(out, f, fr, ins, symbol);
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        return rv32_emit_fp_instruction(out, f, fr, ins);
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
                       MinicDiagnostic *diag) {
    Rv32Frame fr = {0};
    MinicRiscv64FunctionSymbol sym;
    bool ok = false;
    size_t i, j;
    char message[200];
    unsigned bits;
    if (!minic_type_is_void(f->return_type) &&
        !minic_type_is_record(f->return_type) &&
        !rv32_scalar_storage_words(f, f->return_type, &bits)) {
        rv32_diag(diag, "<rv32-abi>", "rv32i: return type exceeds one XLEN or is unqualified");
        return false;
    }
    for (i = 0U; i < f->parameter_count; ++i) {
        if (!minic_type_is_record(f->parameter_types[i]) &&
            !rv32_scalar_storage_words(f, f->parameter_types[i], &bits)) {
            rv32_diag(diag, "<rv32-abi>", "rv32i: non-scalar or multi-XLEN parameters require ABI lowering");
            return false;
        }
    }
    if (!rv32_frame_init(&fr, program, f, target, def->is_variadic)) {
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
    if (!minic_riscv64_function_symbol_from_function(def, &sym)) goto done;
    sym.function_sections = function_sections;
    if (!minic_riscv64_emit_function_symbol_begin(out, &sym) ||
        fprintf(out, "  li t0, %zu\n  sub sp, sp, t0\n", fr.frame_size) < 0 ||
        !rv32_stack(out, "sw", "ra", fr.outgoing_size) ||
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
    for (i = 0; i < f->block_count; ++i) {
        const MinicCoreBlock *block = &f->blocks[i];
        if (!block->has_terminator ||
            fprintf(out, ".L%s_rv32_bb%zu:\n", sym.symbol_name, i) < 0)
            goto done;
        if (!f->suppress_source_label_aliases &&
            block->source_label_id != SIZE_MAX &&
            fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0)
            goto done;
        for (j = 0U; j < block->instruction_count; ++j) {
            MinicCoreInstructionId id = block->instructions[j];
            if (id >= f->instruction_count ||
                !rv32_emit_ins(out, program, target, f, &fr,
                               &f->instructions[id], id, sym.symbol_name)) {
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
        ok = rv32_fixed_stack(out, &fr, "lw", "t1", fr.saved_base_offset) &&
             fprintf(out, "  li t0, %zu\n  add sp, s0, t0\n"
                          "  mv s0, t1\n  ret\n", fr.frame_size) >= 0;
    } else {
        ok = fprintf(out, "  li t0, %zu\n  add sp, sp, t0\n  ret\n",
                     fr.frame_size) >= 0;
    }
    ok = ok && minic_riscv64_emit_function_symbol_end(out, &sym);
done:
    rv32_frame_destroy(&fr);
    return ok;
}
bool minic_riscv32_write_core_program(const char *path,
                                      const MinicC0Program *program,
                                      const MinicTargetInfo *target,
                                      const MinicCoreFunction *functions,
                                      size_t function_count,
                                      bool function_sections,
                                      MinicDiagnostic *diagnostic) {
    FILE *file;
    bool ok = true;
    size_t i;
    if (path == NULL || program == NULL || target == NULL ||
        target->riscv_isa.xlen != 32U ||
        target->riscv_isa.abi != MINIC_RISCV_ABI_ILP32 ||
        function_count != program->function_count ||
        (function_count != 0U && functions == NULL)) {
        rv32_diag(diagnostic, "<rv32-abi>",
                  "rv32i: unsupported file-scope inline ASM or invalid Core function map");
        return false;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        rv32_diag(diagnostic, path, "rv32i: cannot open output file");
        return false;
    }
    if (fprintf(file, ".option norvc\n") < 0) ok = false;
    for (i = 0U; ok && i < program->global_object_count; ++i) {
        ok = minic_riscv32_emit_flat_global(
            file, program, target, &program->global_objects[i]);
        if (!ok) {
            char message[256];
            const MinicGlobalObject *obj = &program->global_objects[i];
            (void)snprintf(message, sizeof(message),
                "rv32i: unsupported global initializer or relocation"
                " object=%s values=%zu relocs=%zu zero=%u",
                obj->name == NULL ? "<unnamed>" : obj->name,
                obj->initializer_count, obj->relocation_count,
                obj->is_zero_initialized ? 1U : 0U);
            rv32_diag(diagnostic, "<rv32-global>", message);
        }
    }
    /* Preserve genuine GNU file-scope asm from the original source.
     * The compiler-owned machine emitter remains strict RV32I. The native
     * kernel is allowed to provide its own A/CSR/privileged operations, and
     * the existing Kbuild GNU assembler retains its ORIGINAL march flags.
     * Source-asm provenance markers prevent crediting those instructions
     * as ISA operations selected by MiniC. */
    if (ok && program->file_asm_count != 0U)
        ok = fprintf(file, ".text\n") >= 0;
    for (i = 0U; ok && i < program->file_asm_count; ++i) {
        const MinicFileAsm *source_asm = &program->file_asms[i];
        if (source_asm->text == NULL) {
            ok = false;
            break;
        }
        ok = fprintf(file, "# MINIC_SOURCE_FILE_ASM_BEGIN\n") >= 0 &&
             fwrite(source_asm->text, 1U, source_asm->length, file) ==
                 source_asm->length &&
             fprintf(file, "\n# MINIC_SOURCE_FILE_ASM_END\n") >= 0;
    }
    /* Undefined weak functions are real ELF symbols, not ordinary strong
     * references. A missing weak Zstd tracing hook must resolve to zero
     * during GNU static linking (it is tested by the original caller).
     * Defined weak functions are handled by the shared symbol emitter. */
    for (i = 0U; ok && i < program->function_count; ++i) {
        const MinicFunction *decl = &program->functions[i];
        const char *name;
        if (!decl->is_weak || decl->is_defined || decl->is_internal)
            continue;
        name = minic_c0_function_symbol_name(decl);
        if (name == NULL || *name == '\0' ||
            fprintf(file, ".weak %s\n", name) < 0)
            ok = false;
    }
    if (ok && fprintf(file, ".text\n") < 0) ok = false;
    for (i = 0U; ok && i < function_count; ++i) {
        const MinicFunction *decl = &program->functions[i];
        if (decl->is_defined) {
            if (decl->is_internal && !decl->is_referenced) continue;
            ok = rv32_func(file, program, &functions[i], decl,
                           function_sections, target, diagnostic);
        }
    }
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        (void)remove(path);
        if (diagnostic != NULL && diagnostic->message[0] == '\0')
            rv32_diag(diagnostic, path, "rv32i: emission failed");
    }
    return ok;
}
