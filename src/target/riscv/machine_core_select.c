#include "target/riscv/machine_core_select.h"
#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv/machine_opcode.h"
#include "target/riscv/machine_target_state.h"
#include "target/riscv/instruction_select.h"
#include "target/common/machine_core_coverage.h"
#include "target/common/elf_module_symbol_info.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { RV_ZERO = 0U, RV_SP = 2U, RV_T0 = 5U, RV_T1 = 6U,
       RV_T2 = 7U, RV_S0 = 8U, RV_A0 = 10U, RV_A7 = 17U,
       RV_T3 = 28U, RV_T6 = 31U };
typedef struct RvCoreSelection {
    MinicMachineBuilder builder;
    const MinicRiscvCoreMachinePlan *plan;
    unsigned xlen;
    bool *reachable;
    size_t *core_blocks;
    size_t *far_entries;
    size_t *conditional_fallthroughs;
    size_t *conditional_others;
    size_t epilogue_block;
} RvCoreSelection;

static MinicMachineOperand rv_reg(unsigned xlen, unsigned id,
                                  unsigned access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){id,
        (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.physical.access = access;
    return op;
}
static MinicMachineOperand rv_imm(int64_t value) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_INTEGER;
    op.value.integer = value;
    return op;
}
static MinicMachineOperand rv_target(size_t block, bool address) {
    MinicMachineOperand op = {0};
    op.kind = address ? MINIC_MACHINE_OPERAND_BLOCK_ADDRESS :
        MINIC_MACHINE_OPERAND_BLOCK;
    op.value.block_id = block;
    return op;
}
static MinicMachineOperand rv_addr(unsigned xlen, unsigned base) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    op.value.address.target_mode =
        MINIC_RISCV_MACHINE_ADDRESS_BASE_DISPLACEMENT;
    op.value.address.has_base = true;
    op.value.address.base = (MinicMachineRegister){base,
        (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.address.scale = 1U;
    return op;
}
static bool rv_emit(RvCoreSelection *sel, uint32_t opcode,
                    const MinicMachineOperand *ops, size_t count) {
    return minic_riscv_machine_emit_selected(&sel->builder, opcode,
                                              ops, count);
}
static bool rv_emit_volatile(RvCoreSelection *sel, uint32_t opcode,
    bool is_volatile, const MinicMachineOperand *ops, size_t count) {
    return minic_riscv_machine_emit_selected_effects(&sel->builder, opcode,
        is_volatile ? MINIC_MACHINE_EFFECT_VOLATILE : 0U, ops, count);
}
static bool rv_li(RvCoreSelection *sel, unsigned dst, int64_t value) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE), rv_imm(value)
    };
    return rv_emit(sel, MINIC_RISCV_MACHINE_LI, ops, 2U);
}
static bool rv_rr(RvCoreSelection *sel, uint32_t opcode,
                  unsigned dst, unsigned src) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE),
        rv_reg(sel->xlen, src, MINIC_MACHINE_REGISTER_READ)
    };
    return rv_emit(sel, opcode, ops, 2U);
}
static bool rv_rrr(RvCoreSelection *sel, uint32_t opcode,
    unsigned dst, unsigned left, unsigned right) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE),
        rv_reg(sel->xlen, left, MINIC_MACHINE_REGISTER_READ),
        rv_reg(sel->xlen, right, MINIC_MACHINE_REGISTER_READ)
    };
    return rv_emit(sel, opcode, ops, 3U);
}
static bool rv_rri(RvCoreSelection *sel, uint32_t opcode,
    unsigned dst, unsigned src, int64_t imm) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE),
        rv_reg(sel->xlen, src, MINIC_MACHINE_REGISTER_READ), rv_imm(imm)
    };
    return rv_emit(sel, opcode, ops, 3U);
}
static bool rv_j(RvCoreSelection *sel, size_t to) {
    const size_t current = sel->builder.current_block;
    if (to == current + 1U) {
        const MinicMachineOperand ops[] = {rv_target(to, false)};
        return rv_emit(sel, MINIC_RISCV_MACHINE_J_BLOCK, ops, 1U) &&
            minic_machine_builder_add_successor(&sel->builder, to);
    }
    /* A far Core edge first lands in an auxiliary addressable dispatch block,
     * adjacent to the primary Core block. Thus Core indirect-entry metadata
     * remains truthful even when a direct edge needs a far jump. */
    {
        size_t far = to;
        size_t i;
        for (i = 0U; i < sel->plan->core->block_count; ++i)
            if (sel->core_blocks[i] == to) {
                far = sel->far_entries[i];
                break;
            }
        const MinicMachineOperand la[] = {
            rv_reg(sel->xlen, RV_T6, MINIC_MACHINE_REGISTER_WRITE),
            rv_target(far, true)
        };
        const MinicMachineOperand jump[] = {
            rv_reg(sel->xlen, RV_T6, MINIC_MACHINE_REGISTER_READ),
            rv_target(far, false)
        };
        return rv_emit(sel, MINIC_RISCV_MACHINE_LA_BLOCK, la, 2U) &&
            rv_emit(sel, MINIC_RISCV_MACHINE_JALR_BLOCK, jump, 2U) &&
            minic_machine_builder_add_successor(&sel->builder, far);
    }
}
static bool rv_branch_zero(RvCoreSelection *sel, bool nonzero,
    unsigned reg, size_t to) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, reg, MINIC_MACHINE_REGISTER_READ),
        rv_target(to, false)
    };
    return rv_emit(sel, nonzero ? MINIC_RISCV_MACHINE_BNEZ :
        MINIC_RISCV_MACHINE_BEQZ, ops, 2U) &&
        minic_machine_builder_add_successor(&sel->builder, to);
}
static bool rv_type(const RvCoreSelection *sel, MinicCoreTypeId id,
    unsigned *bytes, bool *sign, bool *pointer) {
    const MinicCoreTypeContext *types = sel->plan->core->type_context;
    const MinicCoreType *type;
    if (types == NULL || bytes == NULL || sign == NULL || pointer == NULL)
        return false;
    type = minic_core_type_get(types, minic_core_type_unqualified(types, id));
    if (type == NULL || !type->has_layout || type->size == 0U ||
        type->size > sel->xlen / 8U) return false;
    *pointer = type->kind == MINIC_CORE_TYPE_POINTER;
    if (*pointer) {
        if (type->size != sel->xlen / 8U) return false;
        *bytes = sel->xlen / 8U;
        *sign = false;
        return true;
    }
    if (type->kind != MINIC_CORE_TYPE_INTEGER ||
        (type->size != 1U && type->size != 2U &&
         type->size != 4U && type->size != 8U) ||
        type->value.integer.bit_width == 0U ||
        type->value.integer.bit_width > type->size * 8U) return false;
    *bytes = (unsigned)type->size;
    *sign = type->value.integer.is_signed;
    return true;
}
static bool rv_value_slot(const RvCoreSelection *sel, MinicCoreValueId value,
    size_t *offset) {
    const MinicRiscvCoreMachinePlan *plan = sel->plan;
    const size_t word = sel->xlen / 8U;
    if (value == MINIC_CORE_VALUE_INVALID || value >= plan->value_offset_count ||
        value >= plan->core->value_count || plan->value_offsets == NULL ||
        plan->value_offsets[value] == SIZE_MAX ||
        plan->value_offsets[value] % word != 0U ||
        plan->value_offsets[value] > plan->frame->frame_size ||
        word > plan->frame->frame_size - plan->value_offsets[value])
        return false;
    *offset = plan->value_offsets[value];
    return true;
}
static bool rv_load(RvCoreSelection *sel, MinicCoreValueId value,
                    unsigned reg) {
    size_t offset;
    return rv_value_slot(sel, value, &offset) &&
        minic_riscv_machine_select_frame_transfer(&sel->builder, reg,
            sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP,
            offset, true, sel->xlen / 8U);
}
static bool rv_store(RvCoreSelection *sel, MinicCoreValueId value,
                     unsigned reg) {
    size_t offset;
    return rv_value_slot(sel, value, &offset) &&
        minic_riscv_machine_select_frame_transfer(&sel->builder, reg,
            sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP,
            offset, false, sel->xlen / 8U);
}

/* These offsets are owned by the OLD finalized frame. The adapter only
 * transports words; it must not allocate ABI slots or recalculate layout. */
static bool rv_frame_address(RvCoreSelection *sel, unsigned reg,
                             size_t offset) {
    const unsigned base = sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP;
    if (offset > (size_t)INT32_MAX) return false;
    if (offset <= 2047U)
        return rv_rri(sel, MINIC_RISCV_MACHINE_ADDI,
            reg, base, (int64_t)offset);
    return rv_li(sel, reg, (int64_t)offset) &&
        rv_rrr(sel, MINIC_RISCV_MACHINE_ADD, reg, base, reg);
}
static bool rv_value_chunk(RvCoreSelection *sel, MinicCoreValueId id,
                           size_t chunk, unsigned reg, bool load) {
    size_t offset;
    const size_t word = sel->xlen / 8U;
    if (chunk > 1U || !rv_value_slot(sel, id, &offset) ||
        chunk > (SIZE_MAX - offset) / word) return false;
    offset += chunk * word;
    if (offset > sel->plan->frame->frame_size ||
        word > sel->plan->frame->frame_size - offset) return false;
    return minic_riscv_machine_select_frame_transfer(&sel->builder, reg,
        sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP,
        offset, load, (unsigned)word);
}
static bool rv_object_offset(RvCoreSelection *sel, MinicCoreObjectId id,
                             size_t size, size_t *offset) {
    const MinicRiscvCoreMachinePlan *plan = sel->plan;
    size_t start;
    if (id >= plan->core->object_count || id >= plan->object_offset_count ||
        plan->object_offsets == NULL || offset == NULL ||
        (start = plan->object_offsets[id]) == SIZE_MAX ||
        start > plan->frame->frame_size ||
        size > plan->frame->frame_size - start) return false;
    *offset = start;
    return true;
}
static bool rv_byte_transfer(RvCoreSelection *sel, bool load,
    unsigned reg, unsigned base, size_t displacement, bool is_volatile) {
    MinicMachineOperand ops[2];
    if (displacement > 2047U) return false;
    ops[0] = rv_reg(sel->xlen, reg, load ?
        MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
    ops[1] = rv_addr(sel->xlen, base);
    ops[1].value.address.displacement = (int64_t)displacement;
    return rv_emit_volatile(sel, load ? MINIC_RISCV_MACHINE_LBU :
        MINIC_RISCV_MACHINE_SB, is_volatile, ops, 2U);
}
/* Packed aggregates and trailing partial words must not read or overwrite
 * bytes past the Core object extent. This is the existing old byte-pack
 * transport lowered into typed LBU/SB/shift operations, not a new ABI. */
static bool rv_object_chunk(RvCoreSelection *sel, MinicCoreObjectId id,
    size_t object_size, size_t chunk, unsigned reg, bool load) {
    const size_t word = sel->xlen / 8U;
    size_t start, offset, count, byte;
    if (chunk > 1U || chunk > (SIZE_MAX / word) ||
        chunk * word >= object_size ||
        !rv_object_offset(sel, id, object_size, &start) ||
        start > SIZE_MAX - chunk * word) return false;
    offset = start + chunk * word;
    count = object_size - chunk * word;
    if (count > word) count = word;
    if (count == word && offset % word == 0U)
        return minic_riscv_machine_select_frame_transfer(&sel->builder,
            reg, sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP,
            offset, load, (unsigned)word);
    if (reg == RV_T2 || reg == RV_T3 ||
        !rv_frame_address(sel, RV_T2, offset) ||
        (load && !rv_li(sel, reg, 0))) return false;
    for (byte = 0U; byte < count; ++byte) {
        if (load) {
            if (!rv_byte_transfer(sel, true, RV_T3, RV_T2, byte, false) ||
                (byte != 0U && !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                    RV_T3, RV_T3, (int64_t)(byte * 8U))) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                    reg, reg, RV_T3)) return false;
        } else if ((byte == 0U
                ? !rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T3, reg)
                : !rv_rri(sel, MINIC_RISCV_MACHINE_SRLI,
                    RV_T3, reg, (int64_t)(byte * 8U))) ||
                !rv_byte_transfer(sel, false, RV_T3, RV_T2, byte, false))
            return false;
    }
    return true;
}
static bool rv_incoming_word(RvCoreSelection *sel, size_t stack_slot,
                             unsigned reg) {
    const MinicRiscvPhysicalFrame *frame = sel->plan->frame;
    const size_t word = sel->xlen / 8U;
    size_t offset;
    unsigned base = frame->has_dynamic_stack ? RV_S0 : RV_SP;
    if (stack_slot > SIZE_MAX / word) return false;
    offset = stack_slot * word;
    if (frame->has_dynamic_stack_alignment) {
        /* The old over-aligned frame saves the pre-adjustment SP. */
        if (reg == RV_T2 ||
            !minic_riscv_machine_select_frame_transfer(&sel->builder,
                RV_T2, base, frame->entry_sp_offset, true,
                (unsigned)word)) return false;
        base = RV_T2;
    } else {
        if (offset > SIZE_MAX - frame->frame_size) return false;
        offset += frame->frame_size;
    }
    return minic_riscv_machine_select_frame_transfer(&sel->builder,
        reg, base, offset, true, (unsigned)word);
}
static bool rv_outgoing_word(RvCoreSelection *sel, size_t stack_slot,
                             unsigned reg) {
    const MinicRiscvPhysicalFrame *frame = sel->plan->frame;
    const size_t word = sel->xlen / 8U;
    if (stack_slot >= frame->outgoing_size / word ||
        frame->dynamic_call_area_size != 0U ||
        stack_slot > SIZE_MAX / word) return false;
    return minic_riscv_machine_select_frame_transfer(&sel->builder,
        reg, RV_SP, stack_slot * word, false, (unsigned)word);
}
static bool rv_normalize(RvCoreSelection *sel, unsigned reg,
                         unsigned bytes, bool sign) {
    const unsigned shift = sel->xlen - bytes * 8U;
    if (bytes == 0U || bytes > sel->xlen / 8U) return false;
    if (shift == 0U) return true;
    return rv_rri(sel, MINIC_RISCV_MACHINE_SLLI, reg, reg, (int64_t)shift) &&
        rv_rri(sel, sign ? MINIC_RISCV_MACHINE_SRAI :
            MINIC_RISCV_MACHINE_SRLI, reg, reg, (int64_t)shift);
}

typedef struct RvFinalPlacement {
    size_t register_begin, register_count;
    size_t stack_begin, stack_count;
    size_t words, bytes;
    bool record, indirect, ignored;
} RvFinalPlacement;

/* Only convert the legacy immutable plan's representation into a common
 * transport view. No calling-convention classification occurs here. */
static bool rv_formal_placement(const RvCoreSelection *sel, size_t index,
                                RvFinalPlacement *out) {
    if (out == NULL) return false;
    *out = (RvFinalPlacement){0};
    if (sel->xlen == 32U) {
        const Rv32FunctionAbiPlan *abi = sel->plan->abi32;
        const Rv32AbiWordLocation *loc;
        if (abi == NULL || abi->parameters == NULL ||
            index >= abi->parameter_count) return false;
        loc = &abi->parameters[index];
        out->register_begin = loc->location.register_begin;
        out->register_count = loc->location.register_count;
        out->stack_begin = loc->location.stack_begin;
        out->stack_count = loc->location.stack_count;
        out->words = loc->words;
        out->record = loc->record_size != 0U;
        out->bytes = loc->record_size;
        out->indirect = loc->indirect_record;
        out->ignored = loc->words == 0U && loc->record_size == 0U;
    } else {
        const MinicRiscv64FunctionAbiView *abi = sel->plan->abi64;
        const MinicRiscv64AbiArgumentLocation *loc;
        if (abi == NULL || abi->parameter_locations == NULL ||
            index >= abi->parameter_location_count) return false;
        loc = &abi->parameter_locations[index];
        out->register_begin = loc->integer_register_begin;
        out->register_count = loc->integer_register_count;
        out->stack_begin = loc->stack_slot_begin;
        out->stack_count = loc->stack_slot_count;
        out->words = loc->value.slot_count;
        out->bytes = loc->value.storage_size;
        out->record = loc->value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE;
        out->indirect = loc->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
        out->ignored = loc->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE;
        if (loc->floating_register_count != 0U ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_INVALID ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_VOID) return false;
    }
    return out->words <= 2U &&
        out->register_count + out->stack_count == out->words &&
        out->register_begin <= 8U &&
        out->register_count <= 8U - out->register_begin;
}

/* Payload eligibility only: storage width and Core value identity are
 * already sealed. The ABI placement count comes exclusively from the old
 * target planner above. RV32 soft-float and RV64 LP64 pass FP bit patterns
 * through GPR/stack without generating any F/D instructions. */
static bool rv_transport_type(const RvCoreSelection *sel,
    MinicCoreTypeId id, size_t *size, bool *is_integer,
    bool *is_signed) {
    const MinicCoreTypeContext *types = sel->plan->core->type_context;
    const MinicCoreType *type = types == NULL ? NULL :
        minic_core_type_get(types, minic_core_type_unqualified(types, id));
    const size_t word = sel->xlen / 8U;
    if (type == NULL || !type->has_layout || type->size == 0U ||
        type->size > 2U * word || size == NULL ||
        is_integer == NULL || is_signed == NULL) return false;
    *is_integer = type->kind == MINIC_CORE_TYPE_INTEGER;
    *is_signed = *is_integer && type->value.integer.is_signed;
    if (*is_integer) {
        if (type->value.integer.bit_width == 0U ||
            type->value.integer.bit_width > type->size * 8U) return false;
    } else if (type->kind == MINIC_CORE_TYPE_POINTER) {
        if (type->size != word) return false;
    } else if (type->kind != MINIC_CORE_TYPE_FLOAT ||
               (type->size != 4U && type->size != 8U &&
                type->size != 16U)) return false;
    *size = type->size;
    return true;
}
static bool rv_core_constant(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    unsigned bytes;
    bool sign, pointer;
    return rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) &&
        rv_li(sel, RV_T0, ins->value.integer_value) &&
        rv_normalize(sel, RV_T0, bytes, sign) &&
        rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_unary(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    unsigned bytes;
    bool sign, pointer;
    uint32_t opcode;
    if (!rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) ||
        !rv_load(sel, ins->value.operand, RV_T0)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        opcode = MINIC_RISCV_MACHINE_NEG; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        opcode = MINIC_RISCV_MACHINE_NOT; break;
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        opcode = MINIC_RISCV_MACHINE_SEQZ; break;
    default: return false;
    }
    return rv_rr(sel, opcode, RV_T0, RV_T0) &&
        rv_normalize(sel, RV_T0, bytes, sign) &&
        rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_binary(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    unsigned bytes, left_bytes, right_bytes;
    bool sign, pointer, left_sign, right_sign, left_ptr, right_ptr;
    uint32_t op;
    if (ins->value.binary.left >= fn->value_count ||
        ins->value.binary.right >= fn->value_count ||
        !rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) ||
        !rv_type(sel, fn->values[ins->value.binary.left].core_type_id,
            &left_bytes, &left_sign, &left_ptr) ||
        !rv_type(sel, fn->values[ins->value.binary.right].core_type_id,
            &right_bytes, &right_sign, &right_ptr) ||
        !rv_load(sel, ins->value.binary.left, RV_T0) ||
        !rv_load(sel, ins->value.binary.right, RV_T1)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        op = MINIC_RISCV_MACHINE_ADD; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        op = MINIC_RISCV_MACHINE_SUB; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        op = MINIC_RISCV_MACHINE_AND; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        op = MINIC_RISCV_MACHINE_XOR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        op = MINIC_RISCV_MACHINE_OR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        op = MINIC_RISCV_MACHINE_SLL; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        op = left_sign ? MINIC_RISCV_MACHINE_SRA :
            MINIC_RISCV_MACHINE_SRL; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        op = left_sign ? MINIC_RISCV_MACHINE_SLT :
            MINIC_RISCV_MACHINE_SLTU; break;
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (!left_ptr || !right_ptr) return false;
        op = MINIC_RISCV_MACHINE_SLTU; break;
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                RV_T0, RV_T0, RV_T1)) return false;
        return rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T0, RV_T0) &&
            rv_store(sel, ins->result, RV_T0);
    default: return false;
    }
    if (!rv_rrr(sel, op, RV_T0, RV_T0, RV_T1) ||
        !rv_normalize(sel, RV_T0, bytes, sign)) return false;
    return rv_store(sel, ins->result, RV_T0);
}

static bool rv_core_parameter(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTypeContext *types = fn->type_context;
    const size_t index = ins->value.parameter_index;
    RvFinalPlacement loc;
    size_t size, chunks, word;
    bool integer, sign;
    if (index >= minic_core_function_parameter_count(fn) ||
        minic_core_function_parameter_types(fn) == NULL ||
        minic_core_type_unqualified(types, ins->core_type_id) !=
            minic_core_type_unqualified(types,
                minic_core_function_parameter_types(fn)[index]) ||
        !rv_formal_placement(sel, index, &loc) || loc.record ||
        loc.indirect || loc.ignored ||
        !rv_transport_type(sel, ins->core_type_id,
            &size, &integer, &sign)) return false;
    word = sel->xlen / 8U;
    chunks = (size + word - 1U) / word;
    if (loc.words != chunks) return false;
    for (size_t i = 0U; i < chunks; ++i) {
        size_t incoming;
        if (i < loc.register_count) {
            incoming = loc.register_begin + i;
            if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV,
                RV_T0, RV_A0 + (unsigned)incoming)) return false;
        } else {
            incoming = loc.stack_begin + i - loc.register_count;
            if (!rv_incoming_word(sel, incoming, RV_T0)) return false;
        }
        if (integer && chunks == 1U &&
            !rv_normalize(sel, RV_T0, (unsigned)size, sign)) return false;
        if (!rv_value_chunk(sel, ins->result, i, RV_T0, false)) return false;
    }
    return true;
}
static bool rv_copy_bytes(RvCoreSelection *sel, size_t bytes,
    unsigned destination, unsigned source,
    bool destination_volatile, bool source_volatile) {
    size_t copied = 0U;
    if (destination == source || destination == RV_T2 ||
        destination == RV_T3 || source == RV_T2 || source == RV_T3)
        return false;
    while (copied < bytes) {
        size_t i, step = bytes - copied;
        if (step > 2048U) step = 2048U;
        for (i = 0U; i < step; ++i)
            if (!rv_byte_transfer(sel, true, RV_T2, source, i,
                    source_volatile) ||
                !rv_byte_transfer(sel, false, RV_T2, destination, i,
                    destination_volatile))
                return false;
        copied += step;
        if (copied != bytes &&
            (!rv_li(sel, RV_T3, (int64_t)step) ||
             !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                destination, destination, RV_T3) ||
             !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                source, source, RV_T3))) return false;
    }
    return true;
}

/* Consume the same sealed Core record extent as the direct RV32/RV64
 * emitters. A source pointer may designate an unaligned packed member, so
 * neither MachineIR nor its GNU writer is allowed to widen these copies to
 * a naturally aligned word transfer. The existing typed byte-copy lowering
 * owns both RECORD_COPY and RECORD_LOAD on both XLENs. */
static bool rv_record_extent(const RvCoreSelection *sel,
    MinicCoreTypeId id, size_t *extent) {
    const MinicCoreTypeContext *types = sel->plan->core->type_context;
    const MinicCoreType *type;
    if (types == NULL || extent == NULL) return false;
    type = minic_core_type_get(types, minic_core_type_unqualified(types, id));
    if (type == NULL || type->kind != MINIC_CORE_TYPE_RECORD ||
        !type->has_layout || type->alignment == 0U) return false;
    *extent = type->size;
    return true;
}

static bool rv_core_record(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    size_t bytes, offset;
    if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
        if (!rv_record_extent(sel, ins->core_type_id, &bytes) ||
            bytes > (size_t)INT32_MAX) return false;
        if (bytes == 0U) return true;
        return rv_load(sel, ins->value.record_copy.destination_address,
                   RV_T0) &&
            rv_load(sel, ins->value.record_copy.source_address, RV_T1) &&
            rv_copy_bytes(sel, bytes, RV_T0, RV_T1,
                ins->value.record_copy.destination_volatile,
                ins->value.record_copy.source_volatile);
    }
    if (ins->kind != MINIC_CORE_INSTRUCTION_RECORD_LOAD ||
        ins->value.record_load.destination_object >= fn->object_count ||
        !rv_record_extent(sel,
            fn->objects[ins->value.record_load.destination_object].core_type_id,
            &bytes) || bytes > (size_t)INT32_MAX) return false;
    if (bytes == 0U) return true;
    return rv_object_offset(sel, ins->value.record_load.destination_object,
               bytes, &offset) &&
        rv_load(sel, ins->value.record_load.source_address, RV_T0) &&
        rv_frame_address(sel, RV_T1, offset) &&
        rv_copy_bytes(sel, bytes, RV_T1, RV_T0, false,
            ins->value.record_load.is_volatile);
}

/* __builtin_frame_address(0) / __builtin_return_address(0) keep the
 * existing target-owned ABI frame meanings. Other levels are intentionally
 * not manufactured from a guessed frame-chain convention. */
static bool rv_core_call_frame_address(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicRiscvPhysicalFrame *frame = sel->plan->frame;
    unsigned bytes;
    bool sign, pointer;
    if (ins->value.call_frame_address.level != 0U ||
        !rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) ||
        !pointer) return false;
    switch (ins->value.call_frame_address.kind) {
    case MINIC_CORE_CALL_FRAME_ADDRESS_RETURN:
        return frame->saves_return_address &&
            minic_riscv_machine_select_frame_transfer(&sel->builder,
                RV_T0, frame->has_dynamic_stack ? RV_S0 : RV_SP,
                frame->return_address_offset, true, sel->xlen / 8U) &&
            rv_store(sel, ins->result, RV_T0);
    case MINIC_CORE_CALL_FRAME_ADDRESS_FRAME:
        return rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T0,
                   frame->has_dynamic_stack ? RV_S0 : RV_SP) &&
            rv_store(sel, ins->result, RV_T0);
    default:
        return false;
    }
}

/* An RV va_list cursor is a pointer into the psABI argument stream. Keep
 * the saved register interval and the original entry-SP anchor separate:
 * over-aligned functions must never derive varargs from realigned SP. */
static bool rv_core_variadic_address(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicRiscvPhysicalFrame *frame = sel->plan->frame;
    const size_t word = sel->xlen / 8U;
    size_t displacement;
    unsigned bytes;
    bool sign, pointer;
    if (!frame->variadic ||
        !rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) ||
        !pointer || frame->named_stack_argument_slots > SIZE_MAX / word)
        return false;
    if (frame->varargs_size != 0U &&
        !frame->has_dynamic_stack_alignment)
        return rv_frame_address(sel, RV_T0, frame->varargs_offset) &&
            rv_store(sel, ins->result, RV_T0);
    displacement = frame->varargs_size != 0U ? frame->varargs_size :
        frame->named_stack_argument_slots * word;
    if (displacement > (size_t)INT32_MAX) return false;
    if (frame->has_dynamic_stack_alignment) {
        if (!minic_riscv_machine_select_frame_transfer(&sel->builder,
                RV_T0, frame->has_dynamic_stack ? RV_S0 : RV_SP,
                frame->entry_sp_offset, true, (unsigned)word)) return false;
    } else if (!rv_rri(sel, MINIC_RISCV_MACHINE_ADDI, RV_T0,
                   frame->has_dynamic_stack ? RV_S0 : RV_SP, 0)) return false;
    if (frame->varargs_size == 0U) {
        if (!frame->has_dynamic_stack_alignment) {
            if (displacement > SIZE_MAX - frame->frame_size) return false;
            displacement += frame->frame_size;
        }
    }
    if (displacement != 0U) {
        const int64_t delta = frame->varargs_size != 0U ?
            -(int64_t)displacement : (int64_t)displacement;
        if (delta >= -2048 && delta <= 2047) {
            if (!rv_rri(sel, MINIC_RISCV_MACHINE_ADDI,
                    RV_T0, RV_T0, delta)) return false;
        } else if (!rv_li(sel, RV_T1, delta) ||
                   !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                       RV_T0, RV_T0, RV_T1)) return false;
    }
    return rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_parameter_object(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTypeContext *types = fn->type_context;
    const size_t idx = ins->value.parameter_object.parameter_index;
    const MinicCoreObjectId object = ins->value.parameter_object.object_id;
    RvFinalPlacement loc;
    size_t offset, word = sel->xlen / 8U, i;
    if (idx >= minic_core_function_parameter_count(fn) ||
        minic_core_function_parameter_types(fn) == NULL ||
        object >= fn->object_count ||
        minic_core_type_unqualified(types,
            fn->objects[object].core_type_id) !=
            minic_core_type_unqualified(types,
                minic_core_function_parameter_types(fn)[idx]) ||
        !rv_formal_placement(sel, idx, &loc) || !loc.record ||
        !rv_object_offset(sel, object, loc.bytes, &offset)) return false;
    if (loc.ignored) return true;
    if (loc.indirect) {
        if (loc.words != 1U) return false;
        if (loc.register_count == 1U) {
            if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T1,
                RV_A0 + (unsigned)loc.register_begin)) return false;
        } else if (loc.stack_count == 1U) {
            if (!rv_incoming_word(sel, loc.stack_begin, RV_T1)) return false;
        } else return false;
        return rv_frame_address(sel, RV_T0, offset) &&
            rv_copy_bytes(sel, loc.bytes, RV_T0, RV_T1, false, false);
    }
    if (loc.words != (loc.bytes + word - 1U) / word) return false;
    for (i = 0U; i < loc.words; ++i) {
        if (i < loc.register_count) {
            if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV,
                RV_T0, RV_A0 + (unsigned)(loc.register_begin + i)))
                return false;
        } else if (!rv_incoming_word(sel,
            loc.stack_begin + i - loc.register_count, RV_T0)) return false;
        if (!rv_object_chunk(sel, object, loc.bytes,
            i, RV_T0, false)) return false;
    }
    return true;
}
static uint32_t rv_memory_opcode(const RvCoreSelection *sel,
    bool load, unsigned bytes, bool sign) {
    if (load) {
        if (bytes == 1U) return sign ? MINIC_RISCV_MACHINE_LB :
            MINIC_RISCV_MACHINE_LBU;
        if (bytes == 2U) return sign ? MINIC_RISCV_MACHINE_LH :
            MINIC_RISCV_MACHINE_LHU;
        if (bytes == 4U) return sel->xlen == 64U && !sign ?
            MINIC_RISCV_MACHINE_LWU : MINIC_RISCV_MACHINE_LW;
        if (bytes == 8U && sel->xlen == 64U)
            return MINIC_RISCV_MACHINE_LD;
    } else {
        if (bytes == 1U) return MINIC_RISCV_MACHINE_SB;
        if (bytes == 2U) return MINIC_RISCV_MACHINE_SH;
        if (bytes == 4U) return MINIC_RISCV_MACHINE_SW;
        if (bytes == 8U && sel->xlen == 64U)
            return MINIC_RISCV_MACHINE_SD;
    }
    return MINIC_RISCV_MACHINE_INVALID;
}
static bool rv_core_memory(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const bool load = ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
    const MinicCoreValueId address = atomic ?
        (load ? ins->value.atomic_load.address : ins->value.atomic_store.address) :
        (load ? ins->value.load.address : ins->value.store.address);
    const MinicCoreValueId source = load ? MINIC_CORE_VALUE_INVALID :
        (atomic ? ins->value.atomic_store.stored_value :
            ins->value.store.stored_value);
    const bool is_volatile = !atomic && (load ?
        ins->value.load.is_volatile : ins->value.store.is_volatile);
    const MinicCoreTypeId type_id = load ? ins->core_type_id :
        (source < fn->value_count ? fn->values[source].core_type_id :
         MINIC_CORE_TYPE_INVALID);
    unsigned bytes;
    bool sign, pointer;
    uint32_t opcode;
    MinicMachineOperand ops[2];
    const MinicCoreType *memory_type;
    /* Core admits memory_order=0 (relaxed) only. Naturally aligned RV32I /
     * RV64I scalar load/store are atomic at those widths; stronger orders
     * would require a separate typed FENCE policy, never silent downgrading. */
    if ((atomic && (load ? ins->value.atomic_load.memory_order :
            ins->value.atomic_store.memory_order) != 0U) ||
        !rv_type(sel, type_id, &bytes, &sign, &pointer) ||
        (atomic && (fn->type_context == NULL ||
            (memory_type = minic_core_type_get(fn->type_context,
                minic_core_type_unqualified(fn->type_context, type_id))) == NULL ||
            !memory_type->has_layout || memory_type->alignment < bytes)) ||
        (opcode = rv_memory_opcode(sel, load, bytes, sign)) == 0U ||
        !rv_load(sel, address, RV_T0) ||
        (!load && !rv_load(sel, source, RV_T1))) return false;
    ops[0] = rv_reg(sel->xlen, RV_T1, load ?
        MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
    ops[1] = rv_addr(sel->xlen, RV_T0);
    if (!rv_emit_volatile(sel, opcode, is_volatile, ops, 2U)) return false;
    return !load || rv_store(sel, ins->result, RV_T1);
}
static bool rv_core_field_address(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const size_t offset = ins->value.field_address.byte_offset;
    if (offset > (size_t)INT64_MAX ||
        (sel->xlen == 32U && offset > (size_t)UINT32_MAX) ||
        !rv_load(sel, ins->value.field_address.base, RV_T0)) return false;
    if (offset <= 2047U) {
        if (!rv_rri(sel, MINIC_RISCV_MACHINE_ADDI,
            RV_T0, RV_T0, (int64_t)offset)) return false;
    } else if (!rv_li(sel, RV_T1, (int64_t)offset) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
            RV_T0, RV_T0, RV_T1)) return false;
    return rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_pointer_offset(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    MinicRiscvPointerScalePlan scale;
    const MinicCoreFunction *fn = sel->plan->core;
    const size_t stride = ins->value.pointer_offset.element_size;
    size_t bits;
    unsigned bit = 0U;
    unsigned index_bytes;
    bool index_signed, index_pointer;
    if (!minic_riscv_plan_pointer_scale(stride, &scale) ||
        (sel->xlen == 32U && stride > (size_t)UINT32_MAX) ||
        ins->value.pointer_offset.index >= fn->value_count ||
        !rv_type(sel,
            fn->values[ins->value.pointer_offset.index].core_type_id,
            &index_bytes, &index_signed, &index_pointer) || index_pointer ||
        !rv_load(sel, ins->value.pointer_offset.base, RV_T0)) return false;
    if (scale.kind == MINIC_RISCV_POINTER_SCALE_ZERO)
        return rv_store(sel, ins->result, RV_T0);
    if (!rv_load(sel, ins->value.pointer_offset.index, RV_T1)) return false;
    switch (scale.kind) {
    case MINIC_RISCV_POINTER_SCALE_UNIT:
        break;
    case MINIC_RISCV_POINTER_SCALE_SHIFT:
        if (scale.shift >= sel->xlen ||
            !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                RV_T1, RV_T1, (int64_t)scale.shift)) return false;
        break;
    case MINIC_RISCV_POINTER_SCALE_MULTIPLY:
        /* Literal stride is legalized to I-only shifts and adds. This is a
         * target-owned machine expansion; no runtime __muldi3/M extension. */
        if (!rv_li(sel, RV_T2, 0)) return false;
        for (bits = stride; bits != 0U; bits >>= 1U, ++bit) {
            if ((bits & 1U) == 0U) continue;
            if (bit == 0U) {
                if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV,
                    RV_T3, RV_T1)) return false;
            } else if (!rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                           RV_T3, RV_T1, (int64_t)bit)) return false;
            if (!rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                RV_T2, RV_T2, RV_T3)) return false;
        }
        if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T1, RV_T2)) return false;
        break;
    default:
        return false;
    }
    return rv_rrr(sel, ins->value.pointer_offset.subtract ?
        MINIC_RISCV_MACHINE_SUB : MINIC_RISCV_MACHINE_ADD,
        RV_T0, RV_T0, RV_T1) && rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_conversion(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    unsigned bytes;
    bool sign, pointer;
    return rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) &&
        rv_load(sel, ins->value.operand, RV_T0) &&
        rv_normalize(sel, RV_T0, bytes, sign) &&
        rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_object_address(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicRiscvCoreMachinePlan *plan = sel->plan;
    const size_t object = ins->value.object_id;
    const unsigned base = plan->frame->has_dynamic_stack ? RV_S0 : RV_SP;
    size_t offset;
    if (object >= plan->object_offset_count ||
        plan->object_offsets == NULL ||
        (offset = plan->object_offsets[object]) == SIZE_MAX ||
        offset > plan->frame->frame_size ||
        offset > INT32_MAX) return false;
    if (offset <= 2047U) {
        if (!rv_rri(sel, MINIC_RISCV_MACHINE_ADDI,
            RV_T0, base, (int64_t)offset)) return false;
    } else if (!rv_li(sel, RV_T0, (int64_t)offset) ||
               !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                        RV_T0, base, RV_T0)) return false;
    return rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_symbol(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const char *symbol = NULL;
    MinicElfModuleSymbolKind kind;
    uint32_t reloc;
    MinicMachineOperand ops[2] = {{0}};
    if (ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS) {
        if (ins->value.global_id >= fn->global_count) return false;
        symbol = fn->globals[ins->value.global_id].name;
        kind = MINIC_ELF_MODULE_SYMBOL_OBJECT;
    } else if (ins->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
        if (ins->value.function_symbol_id >=
            fn->function_symbol_count) return false;
        symbol = fn->function_symbols[ins->value.function_symbol_id].name;
        kind = MINIC_ELF_MODULE_SYMBOL_FUNCTION;
    } else return false;
    if (symbol == NULL) return false;
    /* The old emitters already use this exact ModuleIR ELF fact. An absent
     * standalone Module must conservatively use GOT for PIC unknowns. */
    reloc = sel->plan->target->position_independent &&
        !minic_multi_elf_module_symbol_facts(
            sel->plan->module, symbol, kind).nonpreemptible ?
        MINIC_RISCV_MACHINE_RELOC_GOT :
        MINIC_RISCV_MACHINE_RELOC_PCREL_LOCAL;
    ops[0] = rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_WRITE);
    ops[1].kind = MINIC_MACHINE_OPERAND_SYMBOL;
    ops[1].value.symbol.name = (char *)symbol;
    ops[1].value.symbol.relocation = reloc;
    return rv_emit(sel, reloc == MINIC_RISCV_MACHINE_RELOC_GOT ?
        MINIC_RISCV_MACHINE_LA_GOT : MINIC_RISCV_MACHINE_LA_PCREL,
        ops, 2U) && rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_block_address(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    MinicMachineOperand ops[2];
    if (ins->value.block_id >= sel->plan->core->block_count) return false;
    ops[0] = rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_WRITE);
    ops[1] = rv_target(sel->far_entries[ins->value.block_id], true);
    return rv_emit(sel, MINIC_RISCV_MACHINE_LA_BLOCK, ops, 2U) &&
        rv_store(sel, ins->result, RV_T0);
}
/* This is a consumer of the EXACT final psABI placements built for the old
 * RV32/RV64 emitter. It deliberately does not count, align, or assign a0-a7.
 * A not-yet-migrated placement rejects the entire MachineIR function. */
static bool rv_call_placement(const RvCoreSelection *sel,
    const Rv32CallAbiPlan *call32,
    const MinicRiscv64PreparedCall *call64,
    size_t index, RvFinalPlacement *out) {
    if (out == NULL) return false;
    *out = (RvFinalPlacement){0};
    if (sel->xlen == 32U) {
        const Rv32AbiWordLocation *loc;
        if (call32 == NULL || call32->arguments == NULL ||
            index >= call32->view.argument_count) return false;
        loc = &call32->arguments[index];
        out->register_begin = loc->location.register_begin;
        out->register_count = loc->location.register_count;
        out->stack_begin = loc->location.stack_begin;
        out->stack_count = loc->location.stack_count;
        out->words = loc->words;
        out->bytes = loc->record_size;
        out->record = loc->record_size != 0U;
        out->indirect = loc->indirect_record;
        out->ignored = loc->words == 0U && loc->record_size == 0U;
    } else {
        const MinicRiscv64AbiArgumentLocation *loc;
        if (call64 == NULL || index >= call64->argument_count) return false;
        loc = &call64->arguments[index];
        out->register_begin = loc->integer_register_begin;
        out->register_count = loc->integer_register_count;
        out->stack_begin = loc->stack_slot_begin;
        out->stack_count = loc->stack_slot_count;
        out->words = loc->value.slot_count;
        out->bytes = loc->value.storage_size;
        out->record = loc->value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE;
        out->indirect = loc->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
        out->ignored = loc->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE;
        if (loc->floating_register_count != 0U ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_INVALID ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_VOID) return false;
    }
    return out->words <= 2U &&
        out->register_count + out->stack_count == out->words &&
        out->register_begin <= 8U &&
        out->register_count <= 8U - out->register_begin;
}

static bool rv_core_call_arguments(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id, size_t start, size_t count,
    MinicCoreValueId scalar_result, MinicCoreObjectId record_result) {
    const MinicCoreFunction *fn = sel->plan->core;
    const Rv32CallAbiPlan *call32 = NULL;
    const MinicRiscv64PreparedCall *call64 = NULL;
    const MinicCoreCallView *view;
    size_t ordinal;
    size_t i, pass;
    const bool has_scalar_result = scalar_result != MINIC_CORE_VALUE_INVALID;
    const bool has_record_result = record_result != MINIC_CORE_OBJECT_INVALID;
    if (start > fn->call_argument_count ||
        count > fn->call_argument_count - start ||
        !sel->plan->frame->saves_return_address) return false;
    if (sel->xlen == 32U) {
        call32 = rv32_function_abi_call(sel->plan->abi32, instruction_id);
        if (call32 == NULL || call32->view.argument_begin != start ||
            call32->view.argument_count != count ||
            ((call32->view.return_form == MINIC_CORE_CALL_RETURN_OBJECT) !=
                has_record_result) ||
            (has_scalar_result &&
             call32->view.return_form != MINIC_CORE_CALL_RETURN_VALUE) ||
            (has_record_result && call32->record_return_size == 0U) ||
            (count != 0U && call32->arguments == NULL)) return false;
    } else {
        if (sel->plan->semantic_calls64 == NULL ||
            sel->plan->abi64 == NULL ||
            (view = minic_core_call_index_find(sel->plan->semantic_calls64,
                instruction_id, &ordinal)) == NULL ||
            view->argument_begin != start ||
            view->argument_count != count ||
            ((view->return_form == MINIC_CORE_CALL_RETURN_OBJECT) !=
                has_record_result) ||
            (has_scalar_result &&
             view->return_form != MINIC_CORE_CALL_RETURN_VALUE) ||
            ordinal >= sel->plan->abi64->outgoing_calls->count ||
            sel->plan->abi64->outgoing_calls->sites == NULL ||
            (call64 = sel->plan->abi64->outgoing_calls->sites[ordinal]) == NULL ||
            call64->instruction_id != instruction_id ||
            call64->argument_count != count ||
            (!has_record_result &&
             call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
             call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
             call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT &&
             call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) ||
            (has_record_result &&
             call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
             call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_INDIRECT) ||
            (has_scalar_result &&
             (call64->return_value.kind == MINIC_RISCV64_ABI_VALUE_VOID ||
              call64->return_value.slot_count == 0U ||
              call64->return_value.slot_count > 2U))) return false;
    }
    /* Same two-pass staging order as old RV32 CALL: stack words first,
     * then argument registers. No argument transfer can overwrite a later
     * stack source, and a split two-word argument follows its plan exactly. */
    for (pass = 0U; pass < 2U; ++pass) {
        for (i = 0U; i < count; ++i) {
            const MinicCoreCallArgument *arg = &fn->call_arguments[start + i];
            RvFinalPlacement loc;
            size_t bytes = 0U, j, object_offset = 0U;
            bool integer = false, sign = false;
            if (!rv_call_placement(sel, call32, call64, i, &loc))
                return false;
            if (loc.ignored) {
                if (arg->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) return false;
                continue;
            }
            if ((pass == 0U && loc.stack_count == 0U) ||
                (pass == 1U && loc.register_count == 0U)) continue;
            if (arg->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (loc.record || arg->value.value_id >= fn->value_count ||
                    !rv_transport_type(sel,
                        fn->values[arg->value.value_id].core_type_id,
                        &bytes, &integer, &sign) ||
                    loc.words != (bytes + sel->xlen / 8U - 1U) /
                                  (sel->xlen / 8U)) return false;
            } else if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (!loc.record ||
                    !rv_object_offset(sel, arg->value.object_id,
                        loc.bytes, &object_offset)) return false;
            } else return false;
            for (j = 0U; j < loc.words; ++j) {
                const bool in_register = j < loc.register_count;
                size_t dest;
                if ((pass == 0U && in_register) ||
                    (pass == 1U && !in_register)) continue;
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    if (!rv_value_chunk(sel, arg->value.value_id,
                        j, RV_T0, true)) return false;
                } else if (loc.indirect) {
                    if (j != 0U || !rv_frame_address(sel,
                        RV_T0, object_offset)) return false;
                } else if (!rv_object_chunk(sel, arg->value.object_id,
                    loc.bytes, j, RV_T0, true)) return false;
                if (in_register) {
                    dest = loc.register_begin + j;
                    if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV,
                        RV_A0 + (unsigned)dest, RV_T0)) return false;
                } else {
                    dest = loc.stack_begin + j - loc.register_count;
                    if (!rv_outgoing_word(sel, dest, RV_T0)) return false;
                }
            }
        }
    }
    if (has_record_result) {
        size_t offset, size;
        bool indirect;
        if (call32 != NULL) {
            size = call32->record_return_size;
            indirect = call32->indirect_return;
        } else {
            size = call64->return_value.storage_size;
            indirect = call64->return_value.kind ==
                MINIC_RISCV64_ABI_VALUE_INDIRECT;
        }
        if (size == 0U ||
            !rv_object_offset(sel, record_result, size, &offset)) return false;
        if (indirect && !rv_frame_address(sel, RV_A0, offset)) return false;
    }
    return true;
}

static bool rv_core_call_result(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins, MinicCoreObjectId record_result) {
    const size_t word = sel->xlen / 8U;
    size_t size, chunks, i;
    bool integer, sign;
    if (record_result != MINIC_CORE_OBJECT_INVALID) {
        bool indirect;
        if (ins->result != MINIC_CORE_VALUE_INVALID) return false;
        if (sel->xlen == 32U) {
            const Rv32CallAbiPlan *call =
                rv32_function_abi_call(sel->plan->abi32, instruction_id);
            if (call == NULL || call->record_return_size == 0U)
                return false;
            size = call->record_return_size;
            indirect = call->indirect_return;
        } else {
            const MinicRiscv64PreparedCall *call;
            size_t ordinal;
            if (sel->plan->semantic_calls64 == NULL ||
                minic_core_call_index_find(sel->plan->semantic_calls64,
                    instruction_id, &ordinal) == NULL ||
                ordinal >= sel->plan->abi64->outgoing_calls->count ||
                sel->plan->abi64->outgoing_calls->sites == NULL ||
                (call = sel->plan->abi64->outgoing_calls->sites[ordinal]) == NULL)
                return false;
            size = call->return_value.storage_size;
            indirect = call->return_value.kind ==
                MINIC_RISCV64_ABI_VALUE_INDIRECT;
        }
        if (indirect) return true; /* callee writes to caller snapshot */
        chunks = (size + word - 1U) / word;
        if (chunks == 0U || chunks > 2U) return false;
        for (i = 0U; i < chunks; ++i)
            if (!rv_object_chunk(sel, record_result, size, i,
                RV_A0 + (unsigned)i, false)) return false;
        return true;
    }
    if (ins->result == MINIC_CORE_VALUE_INVALID) return true;
    if (!rv_transport_type(sel, ins->core_type_id,
        &size, &integer, &sign)) return false;
    chunks = (size + word - 1U) / word;
    if (chunks == 0U || chunks > 2U) return false;
    for (i = 0U; i < chunks; ++i) {
        const unsigned source = RV_A0 + (unsigned)i;
        if (integer && chunks == 1U &&
            !rv_normalize(sel, source, (unsigned)size, sign)) return false;
        if (!rv_value_chunk(sel, ins->result, i, source, false)) return false;
    }
    return true;
}

static bool rv_core_call(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreCallee *callee;
    MinicMachineOperand op = {0};
    if (ins->value.call.extended_float_operation !=
            MINIC_CORE_EXT_FLOAT_NONE ||
        ins->value.call.callee_id >= fn->callee_count) return false;
    callee = &fn->callees[ins->value.call.callee_id];
    if (callee->name == NULL ||
        !rv_core_call_arguments(sel, instruction_id,
            ins->value.call.argument_begin, ins->value.call.argument_count,
            ins->result, ins->value.call.result_object)) return false;
    op.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    op.value.symbol.name = callee->name;
    op.value.symbol.relocation = MINIC_RISCV_MACHINE_RELOC_CALL;
    op.value.symbol.addend = 0;
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_CALL_DIRECT, &op, 1U)) return false;
    return rv_core_call_result(sel, instruction_id, ins,
        ins->value.call.result_object);
}
static bool rv_core_indirect_call(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    MinicMachineOperand callee;
    if (ins->value.indirect_call.signature_id >= fn->call_signature_count ||
        !rv_core_call_arguments(sel, instruction_id,
            ins->value.indirect_call.argument_begin,
            ins->value.indirect_call.argument_count,
            ins->result, ins->value.indirect_call.result_object) ||
        !rv_load(sel, ins->value.indirect_call.callee, RV_T0)) return false;
    callee = rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_READ);
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_CALL_INDIRECT, &callee, 1U))
        return false;
    return rv_core_call_result(sel, instruction_id, ins,
        ins->value.indirect_call.result_object);
}
static bool rv_core_instruction(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins) {
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return rv_core_constant(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return rv_core_parameter(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return rv_core_parameter_object(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return rv_core_binary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return rv_core_unary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return rv_core_conversion(sel, ins);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        return rv_core_object_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return rv_core_field_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        return rv_core_pointer_offset(sel, ins);
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return rv_core_symbol(sel, ins);
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return rv_core_block_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        return rv_core_memory(sel, ins);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return rv_core_record(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        return rv_core_call_frame_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return rv_core_variadic_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL:
        return rv_core_call(sel, instruction_id, ins);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return rv_core_indirect_call(sel, instruction_id, ins);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return rv_emit(sel, MINIC_RISCV_MACHINE_BARRIER, NULL, 0U);
    default:
        return false;
    }
}

static bool rv_core_terminator(RvCoreSelection *sel, size_t core_id) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTerminator *term = &fn->blocks[core_id].terminator;
    const size_t aux = sel->conditional_fallthroughs[core_id];
    const size_t other = sel->conditional_others[core_id];
    size_t i;
    if (!fn->blocks[core_id].has_terminator) return false;
    minic_machine_builder_clear_origin(&sel->builder);
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (term->return_object != MINIC_CORE_OBJECT_INVALID) {
            size_t size, offset, chunks;
            bool indirect;
            const size_t word = sel->xlen / 8U;
            if (sel->xlen == 32U) {
                size = sel->plan->abi32->record_return_size;
                indirect = sel->plan->abi32->indirect_return;
            } else {
                const MinicRiscv64AbiValue *ret =
                    &sel->plan->abi64->return_value;
                size = ret->storage_size;
                indirect = ret->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
                if (!indirect && ret->kind !=
                    MINIC_RISCV64_ABI_VALUE_AGGREGATE) return false;
            }
            if (size == 0U || !rv_object_offset(sel,
                    term->return_object, size, &offset)) return false;
            if (indirect) {
                if (!minic_riscv_machine_select_frame_transfer(
                        &sel->builder, RV_T1,
                        sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP,
                        sel->plan->frame->hidden_result_pointer_offset,
                        true, (unsigned)word) ||
                    !rv_frame_address(sel, RV_T0, offset) ||
                    !rv_copy_bytes(sel, size, RV_T1, RV_T0, false, false) ||
                    !minic_riscv_machine_select_frame_transfer(
                        &sel->builder, RV_A0,
                        sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP,
                        sel->plan->frame->hidden_result_pointer_offset,
                        true, (unsigned)word)) return false;
            } else {
                chunks = (size + word - 1U) / word;
                if (chunks == 0U || chunks > 2U) return false;
                for (i = 0U; i < chunks; ++i)
                    if (!rv_object_chunk(sel, term->return_object, size,
                        i, RV_A0 + (unsigned)i, true)) return false;
            }
        } else if (term->return_value != MINIC_CORE_VALUE_INVALID) {
            const MinicCoreValueId value = term->return_value;
            const size_t word = sel->xlen / 8U;
            size_t size, chunks;
            bool integer, sign;
            if (value >= fn->value_count ||
                !rv_transport_type(sel, fn->values[value].core_type_id,
                    &size, &integer, &sign)) return false;
            chunks = (size + word - 1U) / word;
            if (chunks == 0U || chunks > 2U) return false;
            for (i = 0U; i < chunks; ++i)
                if (!rv_value_chunk(sel, value, i,
                    RV_A0 + (unsigned)i, true)) return false;
        }
        return rv_j(sel, sel->epilogue_block);
    case MINIC_CORE_TERMINATOR_BRANCH:
        return term->branch_target < fn->block_count &&
            rv_j(sel, sel->core_blocks[term->branch_target]);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
        const size_t yes = term->conditional.when_true;
        const size_t no = term->conditional.when_false;
        const MinicCoreValueId cond = term->conditional.condition;
        size_t bytes, chunks;
        bool integer, sign;
        if (aux == SIZE_MAX || other == SIZE_MAX ||
            yes >= fn->block_count ||
            no >= fn->block_count ||
            cond >= fn->value_count ||
            !rv_transport_type(sel, fn->values[cond].core_type_id,
                &bytes, &integer, &sign) ||
            (chunks = (bytes + sel->xlen / 8U - 1U) /
                (sel->xlen / 8U)) == 0U || chunks > 2U ||
            !rv_value_chunk(sel, cond, 0U, RV_T0, true) ||
            (chunks == 2U &&
             (!integer || !rv_value_chunk(sel, cond, 1U, RV_T1, true) ||
              !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                  RV_T0, RV_T0, RV_T1))) ||
            !rv_branch_zero(sel, false, RV_T0, other) ||
            !minic_machine_builder_add_successor(&sel->builder, aux) ||
            !minic_machine_builder_set_fallthrough(&sel->builder, aux) ||
            !minic_machine_builder_position_at(&sel->builder, aux))
            return false;
        return rv_j(sel, sel->core_blocks[yes]) &&
            minic_machine_builder_position_at(&sel->builder, other) &&
            rv_j(sel, sel->core_blocks[no]);
    }
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH: {
        const MinicMachineOperand ops[] = {
            rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_READ),
            rv_imm(0)
        };
        if (!rv_load(sel, term->indirect_target, RV_T0) ||
            !rv_emit(sel, MINIC_RISCV_MACHINE_JALR_BRANCH, ops, 2U))
            return false;
        for (i = 0U; i < fn->block_count; ++i)
            if (fn->blocks[i].indirect_entry &&
                !minic_machine_builder_add_successor(&sel->builder,
                    sel->core_blocks[i])) return false;
        return true;
    }
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return rv_emit(sel, MINIC_RISCV_MACHINE_UNREACHABLE, NULL, 0U);
    default:
        return false;
    }
}

bool minic_riscv_machine_select_core_subset(MinicMachineFunction *machine,
    const MinicRiscvCoreMachinePlan *plan) {
    RvCoreSelection sel = {0};
    MinicRiscvIsa isa = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const MinicCoreFunction *core;
    const MinicTargetInfo *target;
    const MinicRiscvPhysicalFrame *frame;
    size_t i, id;
    bool success = false;
    if (machine == NULL || plan == NULL || plan->core == NULL ||
        plan->target == NULL || plan->frame == NULL ||
        (target = plan->target)->data_layout == NULL ||
        (core = plan->core)->name == NULL || core->block_count == 0U ||
        core->entry_block >= core->block_count || core->blocks == NULL ||
        !minic_core_function_verify(core) ||
        !minic_riscv_target_isa_load(target, &isa) ||
        (isa.xlen != 32U && isa.xlen != 64U) ||
        (isa.xlen == 32U
            ? (plan->abi32 == NULL || plan->abi64 != NULL ||
               plan->semantic_calls64 != NULL ||
               plan->abi32->semantic_calls.index == NULL ||
               plan->abi32->parameter_count !=
                   minic_core_function_parameter_count(core) ||
               plan->frame->has_hidden_result_pointer !=
                   plan->abi32->indirect_return)
            : (plan->abi64 == NULL || plan->abi32 != NULL ||
               !plan->abi64->validated ||
               plan->abi64->outgoing_calls == NULL ||
               !plan->abi64->named_parameter_cursor.lp64_soft_float ||
               plan->abi64->parameter_location_count !=
                   minic_core_function_parameter_count(core) ||
               plan->frame->has_hidden_result_pointer !=
                   (plan->abi64->return_value.kind ==
                    MINIC_RISCV64_ABI_VALUE_INDIRECT) ||
               (plan->semantic_calls64 == NULL
                   ? plan->abi64->outgoing_calls->count != 0U
                   : plan->semantic_calls64->count !=
                     plan->abi64->outgoing_calls->count))) ||
        target->data_layout->pointer_size != isa.xlen / 8U ||
        (frame = plan->frame)->frame_size > (size_t)INT32_MAX ||
        frame->frame_size % 16U != 0U ||
        plan->value_offset_count != core->value_count ||
        plan->object_offset_count != core->object_count ||
        (core->value_count != 0U && plan->value_offsets == NULL) ||
        (core->object_count != 0U && plan->object_offsets == NULL) ||
        !minic_machine_function_initialize(machine, target, core->name,
            minic_core_function_calling_convention(core)))
        return false;
    machine->frame_size = frame->frame_size;
    machine->frame_alignment = 16U;
    machine->has_dynamic_stack = frame->has_dynamic_stack;
    sel.plan = plan;
    sel.xlen = isa.xlen;
    if (!minic_machine_builder_initialize(&sel.builder, machine)) goto done;
    if (core->block_count > SIZE_MAX / sizeof(*sel.core_blocks)) goto done;
    sel.core_blocks = calloc(core->block_count, sizeof(*sel.core_blocks));
    sel.far_entries = calloc(core->block_count, sizeof(*sel.far_entries));
    sel.reachable = calloc(core->block_count, sizeof(*sel.reachable));
    sel.conditional_fallthroughs = malloc(
        core->block_count * sizeof(*sel.conditional_fallthroughs));
    sel.conditional_others = malloc(
        core->block_count * sizeof(*sel.conditional_others));
    if (sel.core_blocks == NULL || sel.far_entries == NULL ||
        sel.reachable == NULL ||
        sel.conditional_fallthroughs == NULL ||
        sel.conditional_others == NULL)
        goto done;
    if (!minic_core_function_mark_reachable_blocks(
            core, sel.reachable, core->block_count)) goto done;
    if (!minic_machine_builder_create_block(&sel.builder,
        NULL, false, true, &id) || id != 0U) goto done;
    /* Core entry first (the physical ABI prologue falls through to block1),
     * then the remaining reachable blocks in deterministic Core id order. */
    for (i = 0U; i < core->block_count; ++i) {
        const size_t core_id = i == 0U ? core->entry_block :
            (i <= core->entry_block ? i - 1U : i);
        const MinicCoreBlock *block = &core->blocks[core_id];
        if (!sel.reachable[core_id]) continue;
        if (!minic_machine_builder_create_block(&sel.builder,
            NULL, true, true, &sel.far_entries[core_id])) goto done;
        if (!minic_machine_builder_create_block(&sel.builder,
            block->local_label_symbol, block->indirect_entry, false,
            &sel.core_blocks[core_id]) ||
            !minic_machine_builder_mark_core_block(&sel.builder, core_id)) goto done;
        if (block->terminator.kind ==
            MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH) {
            if (!minic_machine_builder_create_block(&sel.builder,
                NULL, false, true, &sel.conditional_fallthroughs[core_id]) ||
                !minic_machine_builder_create_block(&sel.builder,
                NULL, false, true, &sel.conditional_others[core_id]))
                goto done;
        }
    }
    if (sel.far_entries[core->entry_block] != 1U ||
        !minic_machine_builder_create_block(&sel.builder,
            NULL, true, true, &sel.epilogue_block) ||
        !minic_riscv_machine_select_prologue(&sel.builder, frame, 1U))
        goto done;
    for (i = 0U; i < core->block_count; ++i) {
        const MinicCoreBlock *block = &core->blocks[i];
        size_t j;
        if (!sel.reachable[i]) continue;
        if (!minic_machine_builder_position_at(&sel.builder,
                sel.far_entries[i]) ||
            !rv_j(&sel, sel.core_blocks[i])) goto done;
        if (!minic_machine_builder_position_at(&sel.builder,
            sel.core_blocks[i])) goto done;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId instruction_id =
                block->instructions[j];
            if (instruction_id >= core->instruction_count) goto done;
            minic_machine_builder_set_origin(&sel.builder, i, instruction_id);
            if (!rv_core_instruction(&sel, instruction_id,
                    &core->instructions[instruction_id])) goto done;
            minic_machine_builder_clear_origin(&sel.builder);
        }
        if (!rv_core_terminator(&sel, i)) goto done;
    }
    if (!minic_riscv_machine_select_epilogue(&sel.builder, frame,
            sel.epilogue_block)) goto done;
    /* External/local-label and asm-goto entry roots belong to Core's common
     * closure. A not-yet-migrated multi-entry MachineCFG must be rejected as
     * a whole, never silently drop or falsely accept its reachable blocks. */
    if (!minic_machine_core_coverage_verify(core, sel.reachable, machine) ||
        !minic_riscv_machine_gnu_asm_policy(target, &policy) ||
        !minic_machine_gnu_asm_preflight(machine, &policy)) goto done;
    success = true;
done:
    free(sel.core_blocks);
    free(sel.far_entries);
    free(sel.reachable);
    free(sel.conditional_fallthroughs);
    free(sel.conditional_others);
    if (!success) minic_machine_function_destroy(machine);
    return success;
}
