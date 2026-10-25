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
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return rv32_emit_memory_instruction(out, program, target, f, fr, ins);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
        size_t offset;
        if (!fr->is_variadic || fr->abi.named_gpr_words > 8U ||
            fr->abi.named_stack_words > (SIZE_MAX - fr->frame_size) / 4U)
            return false;
        offset = fr->varargs_size != 0U ?
            fr->varargs_offset :
            fr->frame_size + fr->abi.named_stack_words * 4U;
        return rv32_address(out, "t0", offset) &&
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
        if (fr->abi.record_return_size != 0U) {
            MinicCoreObjectId id = term->return_object;
            size_t word;
            if (id >= fr->object_count)
                return false;
            if (fr->abi.indirect_return) {
                return rv32_stack(out, "lw", "t1", fr->sret_offset) &&
                       rv32_address(out, "t0", fr->object_offsets[id]) &&
                       minic_riscv_emit_record_copy_bytes(
                           out, "t1", "t0", fr->abi.record_return_size) &&
                       rv32_stack(out, "lw", "a0", fr->sret_offset) &&
                       fprintf(out, "  lla t6, .L%s_rv32_return\n"
                                    "  jalr zero, 0(t6)\n", symbol) >= 0;
            }
            for (word = 0U; word < (fr->abi.record_return_size + 3U) / 4U; ++word) {
                if (!rv32_emit_object_word(
                        out, fr, id, fr->abi.record_return_size, (unsigned)word,
                        word == 0U ? "a0" : "a1"))
                    return false;
            }
            return fprintf(out,
                           "  lla t6, .L%s_rv32_return\n"
                           "  jalr zero, 0(t6)\n", symbol) >= 0;
        }
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
         !rv32_stack(out, "sw", "a0", fr.sret_offset))) goto done;
    if (fr.is_variadic && fr.varargs_size != 0U) {
        const size_t first = fr.abi.named_gpr_words;
        for (i = first; i < 8U; ++i) {
            char reg[4];
            (void)snprintf(reg, sizeof(reg), "a%zu", i);
            if (!rv32_stack(out, "sw", reg,
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
    ok = fprintf(out,
        ".L%s_rv32_return:\n"
        "  li t6, %zu\n  add t6, sp, t6\n  lw ra, 0(t6)\n"
        "  li t0, %zu\n  add sp, sp, t0\n  ret\n",
        sym.symbol_name, fr.outgoing_size, fr.frame_size) >= 0 &&
        minic_riscv64_emit_function_symbol_end(out, &sym);
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
        (function_count != 0U && functions == NULL) ||
        program->file_asm_count != 0U) {
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
