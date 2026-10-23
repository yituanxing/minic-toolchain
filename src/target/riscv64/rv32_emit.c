/* RISC-V XLEN=32 ILP32 initial Core consumer.
 * This is NOT an independent copy of the RV64 backend: Core, target data
 * layout, function symbol policy, architecture registry and the assembler
 * are all shared. ILP32's scalar ABI and frame arithmetic are deliberately
 * separated from the current LP64-only 8-byte frame owner until both widths
 * can be parameterized without regressing Linux's accepted frame allocation.
 *
 * First contract: 32-bit scalar values, local integer objects, memory,
 * Core CFG, direct calls (up to eight scalar arguments), and M-less multiply.
 * Reject wider scalars, records, varargs and unsupported opcodes fail-closed.
 */
#include "target/riscv64/rv32_emit.h"
#include "target/riscv64/function_symbol.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Rv32Frame {
    size_t frame_size;
    size_t *object_offsets;
    size_t object_count;
    size_t value_count;
} Rv32Frame;

static void rv32_diag(MinicDiagnostic *d, const char *path, const char *text) {
    if (d == NULL) return;
    d->path = path;
    d->line = 1U;
    d->column = 1U;
    (void)snprintf(d->message, sizeof(d->message), "%s", text);
}

static bool rv32_align(size_t value, size_t by, size_t *out) {
    if (by == 0U || (by & (by - 1U)) != 0U ||
        value > SIZE_MAX - (by - 1U)) return false;
    *out = (value + (by - 1U)) & ~(by - 1U);
    return true;
}

static bool rv32_frame_init(Rv32Frame *frame, const MinicC0Program *program,
                            const MinicCoreFunction *f, const MinicTargetInfo *t) {
    size_t next, i;
    if (frame == NULL || program == NULL || f == NULL || t == NULL ||
        f->value_count > 100000U || f->object_count > 100000U)
        return false;
    frame->object_offsets = NULL;
    frame->object_count = f->object_count;
    frame->value_count = f->value_count;
    if (f->object_count != 0U) {
        frame->object_offsets = calloc(f->object_count, sizeof(size_t));
        if (frame->object_offsets == NULL) return false;
    }
    /* Slot zero saves ra even in a leaf; a stable ABI-oriented frame makes
     * lowering of calls and arithmetic runtime helpers deterministic. */
    next = 4U + f->value_count * 4U;
    for (i = 0; i < f->object_count; ++i) {
        const MinicCoreObject *obj = &f->objects[i];
        size_t size, align;
        if (!minic_data_layout_type(t->data_layout, program,
                                    obj->type, &size, &align) ||
            obj->element_count > 1U &&
              size > SIZE_MAX / obj->element_count) return false;
        if (obj->element_count > 1U) size *= obj->element_count;
        if (obj->explicit_alignment > align) align = obj->explicit_alignment;
        if (align < 4U) align = 4U;
        if (!rv32_align(next, align, &next) || size > SIZE_MAX - next)
            return false;
        frame->object_offsets[i] = next;
        next += size != 0U ? size : 1U;
    }
    return rv32_align(next, 16U, &frame->frame_size) &&
           frame->frame_size <= (size_t)INT32_MAX;
}
static void rv32_frame_destroy(Rv32Frame *f) {
    if (f != NULL) {
        free(f->object_offsets);
        f->object_offsets = NULL;
    }
}
/* t6 is reserved as the large-displacement scratch register. */
static bool rv32_stack(FILE *out, const char *op, const char *reg, size_t off) {
    if (off <= 2047U)
        return fprintf(out, "  %s %s, %zu(sp)\n", op, reg, off) >= 0;
    return fprintf(out, "  li t6, %zu\n  add t6, sp, t6\n"
                        "  %s %s, 0(t6)\n", off, op, reg) >= 0;
}
static bool rv32_slot(FILE *out, const Rv32Frame *f,
                       MinicCoreValueId id, const char *reg, bool load) {
    return id != MINIC_CORE_VALUE_INVALID && (size_t)id < f->value_count &&
           rv32_stack(out, load ? "lw" : "sw", reg, 4U + (size_t)id * 4U);
}
static bool rv32_address(FILE *out, const char *reg, size_t off) {
    return off <= 2047U ?
        fprintf(out, "  addi %s, sp, %zu\n", reg, off) >= 0 :
        fprintf(out, "  li %s, %zu\n  add %s, sp, %s\n",
                reg, off, reg, reg) >= 0;
}
static bool rv32_scalar(const MinicCoreFunction *f, MinicType t,
                         unsigned *bits, bool *sign) {
    MinicType effective = t;
    if (minic_type_is_pointer(t)) {
        *bits = 32U;
        *sign = false;
        return true;
    }
    if (!minic_type_is_integer(t) ||
        (minic_type_is_enum(t) &&
          !minic_core_function_effective_integer_type(f, t, &effective)))
        return false;
    switch (effective.integer_rank) {
    case MINIC_INTEGER_RANK_BOOL: *bits = 1U; break;
    case MINIC_INTEGER_RANK_CHAR: *bits = 8U; break;
    case MINIC_INTEGER_RANK_SHORT: *bits = 16U; break;
    case MINIC_INTEGER_RANK_INT:
    case MINIC_INTEGER_RANK_LONG: *bits = 32U; break;
    default: return false;
    }
    *sign = effective.integer_sign == MINIC_INTEGER_SIGN_SIGNED &&
            !effective.is_plain_char;
    return true;
}
static bool rv32_canon(FILE *out, const MinicCoreFunction *f,
                        MinicType t, const char *reg) {
    unsigned bits;
    bool is_signed;
    if (!rv32_scalar(f, t, &bits, &is_signed)) return false;
    if (bits == 32U) return true;
    if (bits == 1U)
        return fprintf(out, "  snez %s, %s\n", reg, reg) >= 0;
    return fprintf(out, "  slli %s, %s, %u\n  %s %s, %s, %u\n",
                   reg, reg, 32U - bits,
                   is_signed ? "srai" : "srli",
                   reg, reg, 32U - bits) >= 0;
}
static bool rv32_load_mem(FILE *out, const MinicCoreFunction *f,
                           MinicType t, const char *dst) {
    unsigned bits;
    bool is_signed;
    const char *op;
    if (!rv32_scalar(f, t, &bits, &is_signed)) return false;
    op = bits == 8U ? (is_signed ? "lb" : "lbu") :
         bits == 16U ? (is_signed ? "lh" : "lhu") :
         bits == 1U ? "lbu" : "lw";
    return fprintf(out, "  %s %s, 0(t0)\n", op, dst) >= 0;
}
static bool rv32_store_mem(FILE *out, const MinicCoreFunction *f,
                            MinicType t) {
    unsigned bits;
    bool is_signed;
    if (!rv32_scalar(f, t, &bits, &is_signed)) return false;
    return fprintf(out, "  %s t1, 0(t0)\n",
                   bits <= 8U ? "sb" : bits <= 16U ? "sh" : "sw") >= 0;
}
static bool rv32_load2(FILE *out, const Rv32Frame *fr,
                        const MinicCoreInstruction *ins) {
    return rv32_slot(out, fr, ins->value.binary.left, "t0", true) &&
           rv32_slot(out, fr, ins->value.binary.right, "t1", true);
}
static bool rv32_emit_ins(FILE *out, const MinicCoreFunction *f,
                          const Rv32Frame *fr,
                          const MinicCoreInstruction *ins) {
    const char *op = NULL;
    MinicType lhs;
    unsigned bits;
    bool is_signed;
    size_t idx;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return rv32_scalar(f, ins->type, &bits, &is_signed) &&
               fprintf(out, "  li t0, %" PRId64 "\n", ins->value.integer_value) >= 0 &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        idx = ins->value.parameter_index;
        return idx < 8U && idx < f->parameter_count &&
               rv32_scalar(f, ins->type, &bits, &is_signed) &&
               fprintf(out, "  mv t0, a%zu\n", idx) >= 0 &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD: op = "add"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT: op = "sub"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND: op = "and"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR: op = "or"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR: op = "xor"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT: op = "sll"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        if (ins->value.binary.left >= f->value_count) return false;
        lhs = f->values[ins->value.binary.left].type;
        if (!rv32_scalar(f, lhs, &bits, &is_signed)) return false;
        op = is_signed ? "sra" : "srl"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (ins->value.binary.left >= f->value_count) return false;
        lhs = f->values[ins->value.binary.left].type;
        if (!rv32_scalar(f, lhs, &bits, &is_signed)) return false;
        op = ins->kind == MINIC_CORE_INSTRUCTION_POINTER_LESS ||
             !is_signed ? "sltu" : "slt";
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return rv32_load2(out, fr, ins) &&
               fprintf(out, "  xor t0, t0, t1\n  seqz t0, t0\n") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        /* RV32I has no M. Shift-add computes the low 32-bit product using
         * only I opcodes; all C signed/unsigned low bits are identical. */
        return rv32_load2(out, fr, ins) &&
               fprintf(out, "  li t2, 0\n"
                            ".Lrv32_mul_%u:\n"
                            "  andi t3, t1, 1\n"
                            "  beqz t3, .Lrv32_mul_skip_%u\n"
                            "  add t2, t2, t0\n"
                            ".Lrv32_mul_skip_%u:\n"
                            "  slli t0, t0, 1\n"
                            "  srli t1, t1, 1\n"
                            "  bnez t1, .Lrv32_mul_%u\n"
                            "  mv t0, t2\n",
                       ins->result, ins->result, ins->result,
                       ins->result) >= 0 &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return rv32_slot(out, fr, ins->value.operand, "t0", true) &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return rv32_slot(out, fr, ins->value.operand, "t0", true) &&
               fprintf(out, ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ?
                       "  neg t0, t0\n" :
                       ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT ?
                       "  not t0, t0\n" : "  seqz t0, t0\n") >= 0 &&
               rv32_canon(out, f, ins->type, "t0") &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        return (size_t)ins->value.object_id < fr->object_count &&
               rv32_address(out, "t0",
                            fr->object_offsets[ins->value.object_id]) &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return rv32_slot(out, fr, ins->value.field_address.base, "t0", true) &&
               fprintf(out, "  li t1, %zu\n  add t0, t0, t1\n",
                       ins->value.field_address.byte_offset) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        size_t stride = ins->value.pointer_offset.element_size;
        if (stride == 0U || stride > UINT32_MAX) return false;
        return rv32_slot(out, fr, ins->value.pointer_offset.base, "t0", true) &&
               rv32_slot(out, fr, ins->value.pointer_offset.index, "t1", true) &&
               fprintf(out, "  li t2, %zu\n  li t3, 0\n"
                            ".Lrv32_idx_%u:\n"
                            "  andi t4, t1, 1\n"
                            "  beqz t4, .Lrv32_idx_skip_%u\n"
                            "  add t3, t3, t2\n"
                            ".Lrv32_idx_skip_%u:\n"
                            "  slli t2, t2, 1\n"
                            "  srli t1, t1, 1\n"
                            "  bnez t1, .Lrv32_idx_%u\n"
                            "  %s t0, t0, t3\n",
                       ins->result, ins->result, ins->result, ins->result,
                       ins->value.pointer_offset.subtract ? "sub" : "add") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_LOAD:
        return rv32_slot(out, fr, ins->value.load.address, "t0", true) &&
               rv32_load_mem(out, f, ins->type, "t1") &&
               rv32_slot(out, fr, ins->result, "t1", false);
    case MINIC_CORE_INSTRUCTION_STORE:
        return ins->value.store.stored_value < f->value_count &&
               rv32_slot(out, fr, ins->value.store.address, "t0", true) &&
               rv32_slot(out, fr, ins->value.store.stored_value, "t1", true) &&
               rv32_store_mem(out, f, f->values[ins->value.store.stored_value].type);
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        return ins->value.global_id < f->global_count &&
               fprintf(out, "  lla t0, %s\n",
                       f->globals[ins->value.global_id].name) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return ins->value.function_symbol_id < f->function_symbol_count &&
               fprintf(out, "  lla t0, %s\n",
                       f->function_symbols[ins->value.function_symbol_id].name) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t begin, count, i;
        if (ins->value.call.callee_id >= f->callee_count ||
            ins->value.call.argument_count > 8U ||
            ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID)
            return false;
        callee = &f->callees[ins->value.call.callee_id];
        count = ins->value.call.argument_count;
        begin = ins->value.call.argument_begin;
        if (callee->is_variadic || begin > f->call_argument_count ||
            count > f->call_argument_count - begin) return false;
        for (i = 0U; i < count; ++i) {
            const MinicCoreCallArgument *arg = &f->call_arguments[begin + i];
            char areg[8];
            if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                arg->value.value_id >= f->value_count ||
                snprintf(areg, sizeof(areg), "a%zu", i) < 0 ||
                !rv32_scalar(f, f->values[arg->value.value_id].type,
                             &bits, &is_signed) ||
                !rv32_slot(out, fr, arg->value.value_id, areg, true))
                return false;
        }
        return fprintf(out, "  call %s\n", callee->name) >= 0 &&
               (ins->result == MINIC_CORE_VALUE_INVALID ||
                (rv32_canon(out, f, ins->type, "a0") &&
                 rv32_slot(out, fr, ins->result, "a0", false)));
    }
    default:
        return false;
    }
    return rv32_load2(out, fr, ins) &&
           fprintf(out, "  %s t0, t0, t1\n", op) >= 0 &&
           rv32_canon(out, f, ins->type, "t0") &&
           rv32_slot(out, fr, ins->result, "t0", false);
}
static bool rv32_term(FILE *out, const MinicCoreFunction *f,
                       const Rv32Frame *fr,
                       const MinicCoreTerminator *term,
                       const char *symbol) {
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (!minic_type_is_void(f->return_type)) {
            if (!rv32_slot(out, fr, term->return_value, "a0", true)) return false;
        }
        return fprintf(out, "  j .L%s_rv32_return\n", symbol) >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return term->branch_target < f->block_count &&
               fprintf(out, "  j .L%s_rv32_bb%u\n",
                       symbol, (unsigned)term->branch_target) >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return term->conditional.when_true < f->block_count &&
               term->conditional.when_false < f->block_count &&
               rv32_slot(out, fr, term->conditional.condition, "t0", true) &&
               fprintf(out, "  bnez t0, .L%s_rv32_bb%u\n"
                            "  j .L%s_rv32_bb%u\n",
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
    bool sign;
    if (!minic_type_is_void(f->return_type) &&
        !rv32_scalar(f, f->return_type, &bits, &sign)) {
        rv32_diag(diag, "<rv32-abi>", "rv32i: return type exceeds one XLEN or is unqualified");
        return false;
    }
    if (f->parameter_count > 8U) {
        rv32_diag(diag, "<rv32-abi>", "rv32i: stack and split parameter ABI not yet qualified");
        return false;
    }
    for (i = 0U; i < f->parameter_count; ++i) {
        if (!rv32_scalar(f, f->parameter_types[i], &bits, &sign)) {
            rv32_diag(diag, "<rv32-abi>", "rv32i: non-scalar or multi-XLEN parameters require ABI lowering");
            return false;
        }
    }
    if (!rv32_frame_init(&fr, program, f, target)) {
        rv32_diag(diag, "<rv32-frame>", "rv32i: cannot allocate an ILP32 Core frame");
        rv32_frame_destroy(&fr);
        return false;
    }
    if (!minic_riscv64_function_symbol_from_function(def, &sym)) goto done;
    sym.function_sections = function_sections;
    if (!minic_riscv64_emit_function_symbol_begin(out, &sym) ||
        fprintf(out, "  li t0, %zu\n  sub sp, sp, t0\n", fr.frame_size) < 0 ||
        !rv32_stack(out, "sw", "ra", 0U)) goto done;
    for (i = 0; i < f->block_count; ++i) {
        const MinicCoreBlock *block = &f->blocks[i];
        if (!block->has_terminator ||
            fprintf(out, ".L%s_rv32_bb%zu:\n", sym.symbol_name, i) < 0)
            goto done;
        for (j = 0U; j < block->instruction_count; ++j) {
            MinicCoreInstructionId id = block->instructions[j];
            if (id >= f->instruction_count ||
                !rv32_emit_ins(out, f, &fr, &f->instructions[id])) {
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
        ".L%s_rv32_return:\n  lw ra, 0(sp)\n"
        "  li t0, %zu\n  add sp, sp, t0\n  ret\n",
        sym.symbol_name, fr.frame_size) >= 0 &&
        minic_riscv64_emit_function_symbol_end(out, &sym);
done:
    rv32_frame_destroy(&fr);
    return ok;
}
bool minic_riscv32_write_core_program(const char *path,
                                      const MinicC0Program *program,
                                      const MinicCoreFunction *functions,
                                      size_t function_count,
                                      bool function_sections,
                                      const MinicTargetInfo *target,
                                      MinicDiagnostic *diagnostic) {
    FILE *file;
    bool ok = true;
    size_t i;
    if (path == NULL || program == NULL || target == NULL ||
        target->riscv_isa.xlen != 32U ||
        target->riscv_isa.abi != MINIC_RISCV_ABI_ILP32 ||
        function_count != program->function_count ||
        (function_count != 0U && functions == NULL) ||
        program->global_object_count != 0U || program->file_asm_count != 0U) {
        rv32_diag(diagnostic, "<rv32-abi>",
                  "rv32i: scalar Core functions only; globals/inline ASM not yet qualified");
        return false;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        rv32_diag(diagnostic, path, "rv32i: cannot open output file");
        return false;
    }
    if (fprintf(file, ".option norvc\n.text\n") < 0) ok = false;
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
