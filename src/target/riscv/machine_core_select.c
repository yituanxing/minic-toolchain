#include "target/riscv/machine_core_select.h"
#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv/machine_opcode.h"
#include "target/riscv/machine_target_state.h"
#include "target/riscv/instruction_select.h"
#include "target/riscv/register_identity.h"
#include "target/common/core_machine_asm.h"
#include "target/common/core_scalar_conversion.h"
#include "target/common/core_floating_shape.h"
#include "target/common/machine_core_coverage.h"
#include "target/common/elf_module_symbol_info.h"
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { RV_ZERO = 0U, RV_SP = 2U, RV_T0 = 5U, RV_T1 = 6U,
       RV_T2 = 7U, RV_S0 = 8U, RV_A0 = 10U, RV_A1 = 11U,
       RV_A2 = 12U, RV_A3 = 13U, RV_A4 = 14U, RV_A5 = 15U,
       RV_A6 = 16U, RV_A7 = 17U,
       RV_T3 = 28U, RV_T4 = 29U, RV_T5 = 30U, RV_T6 = 31U };
typedef struct RvCoreSelection {
    MinicMachineBuilder builder;
    const MinicRiscvCoreMachinePlan *plan;
    unsigned xlen;
    bool *reachable;
    size_t *core_blocks;
    size_t *far_entries;
    size_t *conditional_fallthroughs;
    size_t *conditional_others;
    size_t *dynamic_check2;
    size_t *dynamic_commit;
    size_t *dynamic_continue;
    size_t *dynamic_overflow;
    size_t *asm_goto_continuations;
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
static MinicMachineOperand rv_freg(unsigned id, unsigned access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){id, 64U,
        MINIC_MACHINE_REGISTER_FLOAT};
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
    unsigned dst, unsigned src, int64_t imm);
static bool rv_fp_rr(RvCoreSelection *sel, uint32_t opcode,
    unsigned dst, bool dst_fp, unsigned src, bool src_fp) {
    const MinicMachineOperand ops[] = {
        dst_fp ? rv_freg(dst, MINIC_MACHINE_REGISTER_WRITE) :
                 rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE),
        src_fp ? rv_freg(src, MINIC_MACHINE_REGISTER_READ) :
                 rv_reg(sel->xlen, src, MINIC_MACHINE_REGISTER_READ)
    };
    return sel->xlen == 64U && rv_emit(sel, opcode, ops, 2U);
}
static bool rv_fp_rrr(RvCoreSelection *sel, uint32_t opcode,
    unsigned dst, bool dst_fp, unsigned left, unsigned right) {
    const MinicMachineOperand ops[] = {
        dst_fp ? rv_freg(dst, MINIC_MACHINE_REGISTER_WRITE) :
                 rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE),
        rv_freg(left, MINIC_MACHINE_REGISTER_READ),
        rv_freg(right, MINIC_MACHINE_REGISTER_READ)
    };
    return sel->xlen == 64U && rv_emit(sel, opcode, ops, 3U);
}
static bool rv_fp_from_bits(RvCoreSelection *sel, unsigned fpr,
                            unsigned gpr, bool double_precision) {
    return rv_fp_rr(sel, double_precision ? MINIC_RISCV_MACHINE_FMV_D_X :
        MINIC_RISCV_MACHINE_FMV_W_X, fpr, true, gpr, false);
}
static bool rv_fp_to_bits(RvCoreSelection *sel, unsigned gpr,
                          unsigned fpr, bool double_precision) {
    return rv_fp_rr(sel, double_precision ? MINIC_RISCV_MACHINE_FMV_X_D :
        MINIC_RISCV_MACHINE_FMV_X_W, gpr, false, fpr, true) &&
        (double_precision ||
         (rv_rri(sel, MINIC_RISCV_MACHINE_SLLI, gpr, gpr, 32) &&
          rv_rri(sel, MINIC_RISCV_MACHINE_SRLI, gpr, gpr, 32)));
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
    size_t floating_register_begin, floating_register_count;
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
        out->floating_register_begin = loc->floating_register_begin;
        out->floating_register_count = loc->floating_register_count;
        out->stack_begin = loc->stack_slot_begin;
        out->stack_count = loc->stack_slot_count;
        out->words = loc->value.slot_count;
        out->bytes = loc->value.storage_size;
        out->record = loc->value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE;
        out->indirect = loc->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
        out->ignored = loc->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE;
        if (loc->value.kind == MINIC_RISCV64_ABI_VALUE_INVALID ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_VOID) return false;
    }
    if (out->floating_register_count != 0U)
        return sel->xlen == 64U &&
            out->floating_register_count == 1U &&
            out->floating_register_begin < 8U &&
            out->register_count == 0U && out->stack_count == 0U &&
            out->words == 1U && !out->record;
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
    const MinicCoreTypeContext *types = sel->plan->core->type_context;
    const MinicCoreType *type = types == NULL ? NULL :
        minic_core_type_get(types,
            minic_core_type_unqualified(types, ins->core_type_id));
    const unsigned word = sel->xlen / 8U;
    if (type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && type->size == 2U * word &&
        type->value.integer.bit_width != 0U &&
        type->value.integer.bit_width <= type->size * 8U) {
        const uint64_t raw = (uint64_t)ins->value.integer_value;
        const int64_t low = sel->xlen == 32U ?
            (int64_t)(int32_t)(uint32_t)raw : ins->value.integer_value;
        const int64_t high = sel->xlen == 32U ?
            (int64_t)(int32_t)(uint32_t)(raw >> 32U) :
            (ins->value.integer_value < 0 ? -1 : 0);
        return rv_li(sel, RV_T0, low) &&
            rv_value_chunk(sel, ins->result, 0U, RV_T0, false) &&
            rv_li(sel, RV_T0, high) &&
            rv_value_chunk(sel, ins->result, 1U, RV_T0, false);
    }
    return rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) &&
        rv_li(sel, RV_T0, ins->value.integer_value) &&
        rv_normalize(sel, RV_T0, bytes, sign) &&
        rv_store(sel, ins->result, RV_T0);
}

/* Preserve exact IEEE-754 storage bits, including negative zero and NaN
 * payloads.  Binary128 needs a separately-qualified wide representation. */
static bool rv_core_float_constant(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreTypeContext *types = sel->plan->core->type_context;
    const MinicCoreType *type = types == NULL ? NULL :
        minic_core_type_get(types,
            minic_core_type_unqualified(types, ins->core_type_id));
    const size_t word = sel->xlen / 8U;
    const uint64_t bits = ins->value.floating_bits;
    size_t chunks, i;
    if (type == NULL || type->kind != MINIC_CORE_TYPE_FLOAT ||
        !type->has_layout || (type->size != 4U && type->size != 8U) ||
        (type->size == 4U && bits >> 32U != 0U)) return false;
    chunks = (type->size + word - 1U) / word;
    for (i = 0U; i < chunks; ++i) {
        const int64_t immediate = sel->xlen == 32U ?
            (int64_t)(int32_t)(uint32_t)(bits >> (32U * i)) :
            (int64_t)bits;
        if (!rv_li(sel, RV_T0, immediate) ||
            !rv_value_chunk(sel, ins->result, i, RV_T0, false))
            return false;
    }
    return true;
}

/* Strict base-I compiler-runtime lowering with fully typed psABI register
 * operands.  Frame finalization, not this selector, owns return-address
 * preservation and all ABI storage offsets.  No M/F/D is emitted. */
static bool rv_core_runtime_binary(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTypeContext *types = fn->type_context;
    const MinicCoreValueId lhs = ins->value.binary.left;
    const MinicCoreValueId rhs = ins->value.binary.right;
    const MinicCoreType *result, *left, *right;
    const size_t word = sel->xlen / 8U;
    const char *helper = NULL;
    MinicMachineOperand symbol = {0};
    MinicRiscvIsa isa = {0};
    size_t chunks, i, operand_index, size;
    bool integer, sign;
    if (!minic_riscv_machine_implicit_runtime_call(fn, ins, sel->xlen) ||
        types == NULL || lhs >= fn->value_count ||
        rhs >= fn->value_count || !sel->plan->frame->saves_return_address ||
        !minic_riscv_target_isa_load(sel->plan->target, &isa)) return false;
    result = minic_core_type_get(types,
        minic_core_type_unqualified(types, ins->core_type_id));
    left = minic_core_type_get(types,
        minic_core_type_unqualified(types, fn->values[lhs].core_type_id));
    right = minic_core_type_get(types,
        minic_core_type_unqualified(types, fn->values[rhs].core_type_id));
    if (result == NULL || left == NULL || right == NULL ||
        !result->has_layout || !left->has_layout || !right->has_layout ||
        result->size != left->size || result->size != right->size ||
        !rv_transport_type(sel, ins->core_type_id,
            &size, &integer, &sign)) return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER) {
        const bool unsigned_integer = !sign;
        if (!integer || result->kind != MINIC_CORE_TYPE_INTEGER ||
            left->kind != MINIC_CORE_TYPE_INTEGER ||
            right->kind != MINIC_CORE_TYPE_INTEGER) return false;
        if (size > word) {
            if (sel->xlen == 32U)
                helper = minic_riscv_integer_arithmetic_symbol(
                    ins->kind, unsigned_integer, 64U);
            else if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY)
                helper = "__multi3";
            else if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE)
                helper = unsigned_integer ? "__udivti3" : "__divti3";
            else
                helper = unsigned_integer ? "__umodti3" : "__modti3";
        } else {
            helper = minic_riscv_integer_arithmetic_symbol(
                ins->kind, unsigned_integer, sel->xlen);
        }
    } else {
        if (integer || result->kind != MINIC_CORE_TYPE_FLOAT ||
            left->kind != MINIC_CORE_TYPE_FLOAT ||
            right->kind != MINIC_CORE_TYPE_FLOAT ||
            (size != 4U && size != 8U) ||
            (sel->xlen == 64U && isa.abi != MINIC_RISCV_ABI_LP64) ||
            (sel->xlen == 32U && isa.abi != MINIC_RISCV_ABI_ILP32))
            return false;
        helper = minic_riscv_softfloat_arithmetic_symbol(ins->kind);
    }
    chunks = (size + word - 1U) / word;
    if (helper == NULL || chunks == 0U || chunks > 2U ||
        2U * chunks > 8U) return false;
    for (operand_index = 0U; operand_index < 2U; ++operand_index) {
        const MinicCoreValueId operand = operand_index ? rhs : lhs;
        for (i = 0U; i < chunks; ++i)
            if (!rv_value_chunk(sel, operand, i,
                    RV_A0 + (unsigned)(operand_index * chunks + i), true))
                return false;
    }
    symbol.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    /* Machine builder duplicates symbol operands; the static helper spelling
     * is only borrowed for the duration of this emission. */
    symbol.value.symbol.name = (char *)helper;
    symbol.value.symbol.relocation = MINIC_RISCV_MACHINE_RELOC_CALL;
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_CALL_DIRECT, &symbol, 1U))
        return false;
    if (integer && size <= word &&
        !rv_normalize(sel, RV_A0, (unsigned)size, sign)) return false;
    for (i = 0U; i < chunks; ++i)
        if (!rv_value_chunk(sel, ins->result, i,
                RV_A0 + (unsigned)i, false)) return false;
    return true;
}

/* Soft-float RV32 ILP32 and RV64 LP64 use integer argument registers for
 * transported IEEE payloads. Keep conversions and ordered comparisons in
 * compiler-rt, preserving strict base-I selected code and NaN predicates. */
static bool rv_core_runtime_unary_float(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const size_t word = sel->xlen / 8U;
    MinicCoreFloatingShape shape = {0};
    MinicRiscvIsa isa = {0};
    const char *helper = NULL;
    size_t source_size = 0U, result_size = 0U, chunks, i;
    bool integer, sign;
    MinicMachineOperand symbol = {0};
    if (!minic_core_floating_shape(fn, ins, &shape) ||
        !minic_riscv_target_isa_load(sel->plan->target, &isa) ||
        (sel->xlen == 32U && isa.abi != MINIC_RISCV_ABI_ILP32) ||
        (sel->xlen == 64U && isa.abi != MINIC_RISCV_ABI_LP64) ||
        ins->value.operand >= fn->value_count) return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE) {
        if (!rv_value_chunk(sel, ins->value.operand, 0U, RV_T0, true))
            return false;
        if (sel->xlen == 32U) {
            if (!rv_value_chunk(sel, ins->value.operand, 1U,
                    RV_T1, true) || !rv_li(sel, RV_T2, INT32_MIN) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                    RV_T1, RV_T1, RV_T2)) return false;
        } else if (!rv_li(sel, RV_T2, INT64_MIN) ||
                   !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                       RV_T0, RV_T0, RV_T2)) return false;
        return rv_value_chunk(sel, ins->result, 0U, RV_T0, false) &&
            (sel->xlen == 64U || rv_value_chunk(sel, ins->result, 1U,
                RV_T1, false));
    }
    if (!minic_riscv_machine_implicit_runtime_call(fn, ins, sel->xlen) ||
        !sel->plan->frame->saves_return_address ||
        !rv_transport_type(sel, shape.source_id, &source_size,
            &integer, &sign) ||
        !rv_transport_type(sel, shape.result_id, &result_size,
            &integer, &sign)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        helper = "__extendsfdf2"; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        helper = "__truncdfsf2"; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        if (shape.integer_bytes <= 4U)
            helper = shape.integer_signed ? "__floatsidf" : "__floatunsidf";
        else if (shape.integer_bytes == 8U)
            helper = shape.integer_signed ? "__floatdidf" : "__floatundidf";
        else if (shape.integer_bytes == 16U && sel->xlen == 64U)
            helper = shape.integer_signed ? "__floattidf" : "__floatuntidf";
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        if (shape.integer_bytes <= 4U)
            helper = shape.integer_signed ? "__fixdfsi" : "__fixunsdfsi";
        else if (shape.integer_bytes == 8U)
            helper = shape.integer_signed ? "__fixdfdi" : "__fixunsdfdi";
        else if (shape.integer_bytes == 16U && sel->xlen == 64U)
            helper = shape.integer_signed ? "__fixdfti" : "__fixunsdfti";
        break;
    default: return false;
    }
    chunks = (source_size + word - 1U) / word;
    if (helper == NULL || chunks == 0U || chunks > 2U ||
        (result_size + word - 1U) / word > 2U) return false;
    for (i = 0U; i < chunks; ++i)
        if (!rv_value_chunk(sel, ins->value.operand, i,
                RV_A0 + (unsigned)i, true)) return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE &&
        shape.integer_bytes <= 4U &&
        !rv_normalize(sel, RV_A0, (unsigned)shape.integer_bytes,
            shape.integer_signed)) return false;
    symbol.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    symbol.value.symbol.name = (char *)helper;
    symbol.value.symbol.relocation = MINIC_RISCV_MACHINE_RELOC_CALL;
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_CALL_DIRECT, &symbol, 1U))
        return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER &&
        shape.integer_bytes <= word &&
        !rv_normalize(sel, RV_A0, (unsigned)shape.integer_bytes,
            shape.integer_signed)) return false;
    chunks = (result_size + word - 1U) / word;
    for (i = 0U; i < chunks; ++i)
        if (!rv_value_chunk(sel, ins->result, i,
                RV_A0 + (unsigned)i, false)) return false;
    return true;
}

static bool rv_core_runtime_double_compare(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    MinicCoreFloatingShape shape = {0};
    MinicRiscvIsa isa = {0};
    MinicMachineOperand symbol = {0};
    const size_t chunks = 8U / (sel->xlen / 8U);
    const char *helper;
    size_t i, index;
    if (!minic_riscv_machine_implicit_runtime_call(fn, ins, sel->xlen) ||
        !minic_core_floating_shape(fn, ins, &shape) ||
        !minic_riscv_target_isa_load(sel->plan->target, &isa) ||
        (sel->xlen == 32U && isa.abi != MINIC_RISCV_ABI_ILP32) ||
        (sel->xlen == 64U && isa.abi != MINIC_RISCV_ABI_LP64) ||
        !sel->plan->frame->saves_return_address || chunks > 2U)
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL: helper = "__eqdf2"; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        helper = "__ledf2"; break;
    default: return false;
    }
    for (index = 0U; index < 2U; ++index) {
        const MinicCoreValueId value = index ?
            ins->value.binary.right : ins->value.binary.left;
        for (i = 0U; i < chunks; ++i)
            if (!rv_value_chunk(sel, value, i,
                    RV_A0 + (unsigned)(index * chunks + i), true))
                return false;
    }
    symbol.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    symbol.value.symbol.name = (char *)helper;
    symbol.value.symbol.relocation = MINIC_RISCV_MACHINE_RELOC_CALL;
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_CALL_DIRECT, &symbol, 1U))
        return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL) {
        if (!rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ,
                RV_A0, RV_A0)) return false;
    } else if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS) {
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_SLT,
                RV_A0, RV_A0, RV_ZERO)) return false;
    } else if (!rv_rri(sel, MINIC_RISCV_MACHINE_SLTI,
            RV_A0, RV_A0, 1)) return false;
    return rv_store(sel, ins->result, RV_A0);
}

/* RV64 LP64D owns native F/D lowering. Core values remain IEEE bit-patterns
 * in the existing general-register spill slots, so no floating register
 * allocator or language types are smuggled into MachineIR. Every temporary
 * ft0/ft1 is a typed, target-verified physical register. */
static bool rv_core_native_fp(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    MinicCoreFloatingShape shape = {0};
    uint32_t opcode = MINIC_RISCV_MACHINE_INVALID;
    bool double_precision = true;
    bool result_is_integer = false;
    MinicCoreValueId left = MINIC_CORE_VALUE_INVALID;
    MinicCoreValueId right = MINIC_CORE_VALUE_INVALID;
    if (sel->xlen != 64U ||
        minic_riscv_target_isa(sel->plan->target).abi !=
            MINIC_RISCV_ABI_LP64D ||
        !minic_core_floating_shape(fn, ins, &shape)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
        opcode = MINIC_RISCV_MACHINE_FADD_S; double_precision = false; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
        opcode = MINIC_RISCV_MACHINE_FSUB_S; double_precision = false; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
        opcode = MINIC_RISCV_MACHINE_FMUL_S; double_precision = false; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        opcode = MINIC_RISCV_MACHINE_FDIV_S; double_precision = false; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        opcode = MINIC_RISCV_MACHINE_FADD_D; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        opcode = MINIC_RISCV_MACHINE_FSUB_D; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
        opcode = MINIC_RISCV_MACHINE_FMUL_D; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        opcode = MINIC_RISCV_MACHINE_FDIV_D; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        opcode = MINIC_RISCV_MACHINE_FEQ_D; result_is_integer = true; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        opcode = MINIC_RISCV_MACHINE_FLT_D; result_is_integer = true; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        opcode = MINIC_RISCV_MACHINE_FLE_D; result_is_integer = true; break;
    default: break;
    }
    if (opcode != MINIC_RISCV_MACHINE_INVALID) {
        left = ins->value.binary.left;
        right = ins->value.binary.right;
        if (left >= fn->value_count || right >= fn->value_count ||
            !rv_load(sel, left, RV_T0) || !rv_load(sel, right, RV_T1) ||
            !rv_fp_from_bits(sel, 0U, RV_T0, double_precision) ||
            !rv_fp_from_bits(sel, 1U, RV_T1, double_precision) ||
            !rv_fp_rrr(sel, opcode, result_is_integer ? RV_T0 : 0U,
                !result_is_integer, 0U, 1U)) return false;
        if (!result_is_integer &&
            !rv_fp_to_bits(sel, RV_T0, 0U, double_precision)) return false;
        return rv_store(sel, ins->result, RV_T0);
    }
    left = ins->value.operand;
    if (left >= fn->value_count || !rv_load(sel, left, RV_T0))
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        if (shape.integer_bytes == 0U || shape.integer_bytes > 8U ||
            !rv_normalize(sel, RV_T0, (unsigned)shape.integer_bytes,
                shape.integer_signed)) return false;
        if (shape.integer_bytes <= 4U)
            opcode = shape.integer_signed ? MINIC_RISCV_MACHINE_FCVT_D_W :
                MINIC_RISCV_MACHINE_FCVT_D_WU;
        else opcode = shape.integer_signed ? MINIC_RISCV_MACHINE_FCVT_D_L :
            MINIC_RISCV_MACHINE_FCVT_D_LU;
        if (!rv_fp_rr(sel, opcode, 0U, true, RV_T0, false) ||
            !rv_fp_to_bits(sel, RV_T1, 0U, true)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        if (!rv_fp_from_bits(sel, 0U, RV_T0, false) ||
            !rv_fp_rr(sel, MINIC_RISCV_MACHINE_FCVT_D_S,
                1U, true, 0U, true) ||
            !rv_fp_to_bits(sel, RV_T1, 1U, true)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        if (!rv_fp_from_bits(sel, 0U, RV_T0, true) ||
            !rv_fp_rr(sel, MINIC_RISCV_MACHINE_FCVT_S_D,
                1U, true, 0U, true) ||
            !rv_fp_to_bits(sel, RV_T1, 1U, false)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        if (shape.integer_bytes == 0U || shape.integer_bytes > 8U)
            return false;
        if (shape.integer_bytes <= 4U)
            opcode = shape.integer_signed ?
                MINIC_RISCV_MACHINE_FCVT_W_D_RTZ :
                MINIC_RISCV_MACHINE_FCVT_WU_D_RTZ;
        else opcode = shape.integer_signed ?
            MINIC_RISCV_MACHINE_FCVT_L_D_RTZ :
            MINIC_RISCV_MACHINE_FCVT_LU_D_RTZ;
        if (!rv_fp_from_bits(sel, 0U, RV_T0, true) ||
            !rv_fp_rr(sel, opcode, RV_T1, false, 0U, true) ||
            !rv_normalize(sel, RV_T1, (unsigned)shape.integer_bytes,
                shape.integer_signed)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        if (!rv_fp_from_bits(sel, 0U, RV_T0, true) ||
            !rv_fp_rrr(sel, MINIC_RISCV_MACHINE_FSGNJN_D,
                0U, true, 0U, 0U) ||
            !rv_fp_to_bits(sel, RV_T1, 0U, true)) return false;
        break;
    default:
        return false;
    }
    return rv_store(sel, ins->result, RV_T1);
}

/* Dynamic allocation is selected as five adjacent physical CFG stages:
 * check1, check2, overflow, commit and continuation. BLTU from check1
 * targets next-next overflow; BGEU from check2 targets next-next commit.
 * Both branches are provably in range under RISC-V's typed 4KiB contract.
 * The old finalized ABI frame exclusively determines outgoing call storage. */
static bool rv_core_dynamic_stack(RvCoreSelection *sel,
    MinicCoreInstructionId id, const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicRiscvPhysicalFrame *frame = sel->plan->frame;
    const size_t outgoing = sel->xlen == 32U ?
        frame->outgoing_size : frame->dynamic_call_area_size;
    const MinicCoreValueId count = ins->value.operand;
    const MinicCoreTypeContext *types = fn->type_context;
    const MinicCoreType *count_type;
    unsigned result_bytes;
    bool sign, pointer;
    MinicMachineOperand branch[3];
    if (id >= fn->instruction_count || count >= fn->value_count ||
        types == NULL || !frame->has_dynamic_stack ||
        outgoing > (size_t)INT32_MAX ||
        sel->dynamic_check2[id] == SIZE_MAX ||
        sel->dynamic_commit[id] == SIZE_MAX ||
        sel->dynamic_continue[id] == SIZE_MAX ||
        sel->dynamic_overflow[id] == SIZE_MAX ||
        !rv_type(sel, ins->core_type_id,
            &result_bytes, &sign, &pointer) || !pointer ||
        ins->result >= fn->value_count ||
        fn->values[ins->result].core_type_id != ins->core_type_id)
        return false;
    count_type = minic_core_type_get(types,
        minic_core_type_unqualified(types,
            fn->values[count].core_type_id));
    if (count_type == NULL || count_type->kind != MINIC_CORE_TYPE_INTEGER ||
        !count_type->has_layout ||
        count_type->size != sel->xlen / 8U ||
        count_type->value.integer.bit_width != sel->xlen ||
        count_type->value.integer.is_signed ||
        count_type->value.integer.is_bool ||
        !rv_load(sel, count, RV_T0) ||
        !rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T1, RV_T0) ||
        !rv_rri(sel, MINIC_RISCV_MACHINE_ADDI, RV_T0, RV_T0, 15))
        return false;
    branch[0] = rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_READ);
    branch[1] = rv_reg(sel->xlen, RV_T1, MINIC_MACHINE_REGISTER_READ);
    branch[2] = rv_target(sel->dynamic_overflow[id], false);
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_BLTU, branch, 3U) ||
        !minic_machine_builder_add_successor(&sel->builder,
            sel->dynamic_overflow[id]) ||
        !minic_machine_builder_add_successor(&sel->builder,
            sel->dynamic_check2[id]) ||
        !minic_machine_builder_set_fallthrough(&sel->builder,
            sel->dynamic_check2[id]) ||
        !minic_machine_builder_position_at(&sel->builder,
            sel->dynamic_check2[id]) ||
        !rv_li(sel, RV_T1, -16) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
            RV_T0, RV_T0, RV_T1) ||
        !rv_li(sel, RV_T1, (int64_t)outgoing) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
            RV_T2, RV_T0, RV_T1)) return false;
    branch[0] = rv_reg(sel->xlen, RV_T2, MINIC_MACHINE_REGISTER_READ);
    branch[1] = rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_READ);
    branch[2] = rv_target(sel->dynamic_commit[id], false);
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_BGEU, branch, 3U) ||
        !minic_machine_builder_add_successor(&sel->builder,
            sel->dynamic_commit[id]) ||
        !minic_machine_builder_add_successor(&sel->builder,
            sel->dynamic_overflow[id]) ||
        !minic_machine_builder_set_fallthrough(&sel->builder,
            sel->dynamic_overflow[id]) ||
        !minic_machine_builder_position_at(&sel->builder,
            sel->dynamic_commit[id]) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_SUB,
            RV_SP, RV_SP, RV_T2) ||
        !rv_li(sel, RV_T0, (int64_t)outgoing) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
            RV_T0, RV_SP, RV_T0) ||
        !rv_store(sel, ins->result, RV_T0) ||
        !rv_j(sel, sel->dynamic_continue[id]) ||
        !minic_machine_builder_position_at(&sel->builder,
            sel->dynamic_continue[id])) return false;
    return true;
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

/* Base-I bit scans need no Zbb instruction.  Each iteration selects a
 * power-of-two half with a register-controlled shift, so neither a
 * control-flow island nor a helper call is required.  An all-zero operand
 * returns the source width, including RV32's two-word uint64_t case. */
static bool rv_core_bitscan(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    unsigned width, scan_width, half, log_half;
    bool wide32, clz;
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTypeContext *types = fn->type_context;
    const MinicCoreType *input, *result;
    if (types == NULL || ins->value.operand >= fn->value_count ||
        ins->result >= fn->value_count) return false;
    input = minic_core_type_get(types, minic_core_type_unqualified(types,
        fn->values[ins->value.operand].core_type_id));
    result = minic_core_type_get(types, minic_core_type_unqualified(types,
        ins->core_type_id));
    if (input == NULL || result == NULL ||
        input->kind != MINIC_CORE_TYPE_INTEGER ||
        input->value.integer.is_signed ||
        result->kind != MINIC_CORE_TYPE_INTEGER ||
        result->value.integer.bit_width != 32U ||
        (width = input->value.integer.bit_width) == 0U ||
        (width != 8U && width != 16U && width != 32U && width != 64U) ||
        width > 2U * sel->xlen) return false;
    wide32 = sel->xlen == 32U && width == 64U;
    clz = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ;
    scan_width = wide32 ? 32U : width;
    if (wide32) {
        /* Select high on CLZ, low on CTZ when nonzero.  The other half is
         * only scanned if the preferred half is all zero.  T4 retains the
         * optional extra 32 counted bits throughout the inner algorithm. */
        if (!rv_value_chunk(sel, ins->value.operand, 0U, RV_T0, true) ||
            !rv_value_chunk(sel, ins->value.operand, 1U, RV_T1, true) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ, RV_T4,
                clz ? RV_T1 : RV_T0) ||
            !rv_rri(sel, MINIC_RISCV_MACHINE_XORI, RV_T4, RV_T4, 1) ||
            !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI, RV_T4, RV_T4, 5) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ, RV_T5,
                clz ? RV_T1 : RV_T0) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SUB, RV_T5, RV_ZERO, RV_T5) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND, RV_T2,
                clz ? RV_T1 : RV_T0, RV_T5) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_NOT, RV_T5, RV_T5) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND, RV_T0,
                clz ? RV_T0 : RV_T1, RV_T5) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_OR, RV_T0, RV_T0, RV_T2))
            return false;
    } else {
        if (!rv_load(sel, ins->value.operand, RV_T0) ||
            (width < sel->xlen &&
             !rv_normalize(sel, RV_T0, width / 8U, false))) return false;
    }
    if (!rv_li(sel, RV_T1, 0)) return false;
    half = scan_width / 2U;
    log_half = 0U;
    for (unsigned value = half; value > 1U; value >>= 1U)
        ++log_half;
    for (; half != 0U; half >>= 1U) {
        if (clz) {
            if (!rv_rri(sel, MINIC_RISCV_MACHINE_SRLI,
                    RV_T2, RV_T0, (int64_t)half) ||
                !rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ, RV_T3, RV_T2))
                return false;
            if ((log_half != 0U &&
                 !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                     RV_T3, RV_T3, log_half)) ||
                !rv_li(sel, RV_T5, half) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_SUB,
                    RV_T5, RV_T5, RV_T3) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                    RV_T1, RV_T1, RV_T5)) return false;
        } else {
            const uint64_t mask = (UINT64_C(1) << half) - 1U;
            if (!rv_li(sel, RV_T5, (int64_t)mask) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                    RV_T2, RV_T0, RV_T5) ||
                !rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T3, RV_T2) ||
                (log_half != 0U &&
                 !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                     RV_T3, RV_T3, log_half)) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                    RV_T1, RV_T1, RV_T3)) return false;
        }
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_SRL, RV_T0, RV_T0, RV_T3))
            return false;
        if (log_half != 0U) --log_half;
    }
    return rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T2, RV_T0) &&
        rv_rrr(sel, MINIC_RISCV_MACHINE_ADD, RV_T1, RV_T1, RV_T2) &&
        (!wide32 || rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
            RV_T1, RV_T1, RV_T4)) &&
        rv_store(sel, ins->result, RV_T1);
}

static bool rv_wide_integer(RvCoreSelection *sel, MinicCoreTypeId id,
    bool *sign) {
    const MinicCoreTypeContext *types = sel->plan->core->type_context;
    const MinicCoreType *type = types == NULL ? NULL :
        minic_core_type_get(types, minic_core_type_unqualified(types, id));
    if (sign == NULL || type == NULL || !type->has_layout ||
        type->kind != MINIC_CORE_TYPE_INTEGER ||
        type->size != 2U * (sel->xlen / 8U) ||
        type->value.integer.bit_width != 2U * sel->xlen)
        return false;
    *sign = type->value.integer.is_signed;
    return true;
}

/* Base-I pair shift, shared by RV32 i64 and RV64 i128. The two halves are
 * transported from the finalized Core frame, never relowered as C types.
 *
 * RISC-V masks register shift counts to log2(XLEN), so we explicitly build
 * three all-bits masks for [0,XLEN), [XLEN,2*XLEN), and >=2*XLEN. A high
 * count word forces the last case. The inverse count is also masked; its
 * spurious carry at count%XLEN == 0 is removed by a nonzero-low mask.
 * This branch-free lowering needs neither a call frame nor compiler-rt
 * __ashldi3/__ashlti3, which real Linux freestanding linkers may omit. */
static bool rv_core_wide_shift(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTypeContext *types = fn->type_context;
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    const MinicCoreType *count;
    const unsigned width = sel->xlen;
    const bool shift_right =
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
    bool sign, result_sign, integer, count_sign;
    size_t count_bytes;
    if (types == NULL || left >= fn->value_count ||
        right >= fn->value_count ||
        !rv_wide_integer(sel, fn->values[left].core_type_id, &sign) ||
        !rv_wide_integer(sel, ins->core_type_id, &result_sign) ||
        sign != result_sign ||
        !rv_transport_type(sel, fn->values[right].core_type_id,
            &count_bytes, &integer, &count_sign) || !integer ||
        count_bytes == 0U || count_bytes > 2U * (width / 8U)) return false;
    count = minic_core_type_get(types, minic_core_type_unqualified(types,
        fn->values[right].core_type_id));
    if (count == NULL || count->kind != MINIC_CORE_TYPE_INTEGER ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT &&
         !shift_right) ||
        !rv_value_chunk(sel, left, 0U, RV_T0, true) ||
        !rv_value_chunk(sel, left, 1U, RV_T1, true) ||
        !rv_value_chunk(sel, right, 0U, RV_T2, true)) return false;
    if (count_bytes > width / 8U &&
        !rv_value_chunk(sel, right, 1U, RV_T5, true)) return false;
    /* a0=s, a1=XLEN-s, a2=smallMask, a3=middleMask,
     * a4=nonzeroMask, a5..a7=scratch/result; t0,t1=original pair. */
    if (!rv_rri(sel, MINIC_RISCV_MACHINE_ANDI,
            RV_A0, RV_T2, (int64_t)(width - 1U)) ||
        !rv_li(sel, RV_A1, (int64_t)width) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_SUB,
            RV_A1, RV_A1, RV_A0) ||
        !rv_rri(sel, MINIC_RISCV_MACHINE_ANDI,
            RV_A1, RV_A1, (int64_t)(width - 1U)) ||
        !rv_li(sel, RV_A4, (int64_t)width) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_SLTU,
            RV_A2, RV_T2, RV_A4) ||
        !rv_li(sel, RV_A4, (int64_t)(2U * width)) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_SLTU,
            RV_A3, RV_T2, RV_A4)) return false;
    if (count_bytes > width / 8U &&
        (!rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_A4, RV_T5) ||
         !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
             RV_A2, RV_A2, RV_A4) ||
         !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
             RV_A3, RV_A3, RV_A4))) return false;
    if (!rv_rr(sel, MINIC_RISCV_MACHINE_NOT, RV_A4, RV_A2) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
            RV_A3, RV_A3, RV_A4) ||
        !rv_rr(sel, MINIC_RISCV_MACHINE_NEG, RV_A2, RV_A2) ||
        !rv_rr(sel, MINIC_RISCV_MACHINE_NEG, RV_A3, RV_A3) ||
        !rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ, RV_A4, RV_A0) ||
        !rv_rr(sel, MINIC_RISCV_MACHINE_NEG, RV_A4, RV_A4))
        return false;
    if (!shift_right) {
        /* [lo,hi]<<s; the old lo contributes to the new hi. */
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_SLL,
                RV_A5, RV_T0, RV_A0) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SLL,
                RV_A6, RV_T1, RV_A0) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SRL,
                RV_A7, RV_T0, RV_A1) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_A7, RV_A7, RV_A4) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                RV_A6, RV_A6, RV_A7) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_A6, RV_A6, RV_A2) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_A7, RV_A5, RV_A3) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                RV_A6, RV_A6, RV_A7) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_A5, RV_A5, RV_A2)) return false;
    } else {
        const uint32_t hi_shift = sign ? MINIC_RISCV_MACHINE_SRA :
            MINIC_RISCV_MACHINE_SRL;
        /* For an arithmetic right shift, the old high sign bit fills
         * the high word at >=XLEN and both words at >=2*XLEN. */
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_SRL,
                RV_A5, RV_T0, RV_A0) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SLL,
                RV_A6, RV_T1, RV_A1) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_A6, RV_A6, RV_A4) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                RV_A5, RV_A5, RV_A6) ||
            !rv_rrr(sel, hi_shift, RV_A6, RV_T1, RV_A0) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_A7, RV_A6, RV_A3) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_A5, RV_A5, RV_A2) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                RV_A5, RV_A5, RV_A7) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_A6, RV_A6, RV_A2)) return false;
        if (sign) {
            if (!rv_rri(sel, MINIC_RISCV_MACHINE_SRAI,
                    RV_A7, RV_T1, (int64_t)(width - 1U)) ||
                !rv_rr(sel, MINIC_RISCV_MACHINE_NOT, RV_A1, RV_A2) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                    RV_A1, RV_A1, RV_A7) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                    RV_A6, RV_A6, RV_A1) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                    RV_A1, RV_A2, RV_A3) ||
                !rv_rr(sel, MINIC_RISCV_MACHINE_NOT, RV_A1, RV_A1) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                    RV_A1, RV_A1, RV_A7) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                    RV_A5, RV_A5, RV_A1)) return false;
        }
    }
    return rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T0, RV_A5) &&
        rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T1, RV_A6) &&
        rv_value_chunk(sel, ins->result, 0U, RV_T0, false) &&
        rv_value_chunk(sel, ins->result, 1U, RV_T1, false);
}

/* Two-GPR values keep an ordinary Core SSA identity; low/high words are
 * transported with the existing final frame offsets.  Carry/borrow and the
 * signed high-word comparison need only RVI, never a M/Zbb instruction. */
static bool rv_core_wide_binary(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    bool left_sign, right_sign, result_sign;
    uint32_t opcode;
    if (ins->value.binary.left >= fn->value_count ||
        ins->value.binary.right >= fn->value_count ||
        !rv_wide_integer(sel,
            fn->values[ins->value.binary.left].core_type_id, &left_sign) ||
        !rv_wide_integer(sel,
            fn->values[ins->value.binary.right].core_type_id, &right_sign) ||
        left_sign != right_sign ||
        !rv_value_chunk(sel, ins->value.binary.left, 0U, RV_T0, true) ||
        !rv_value_chunk(sel, ins->value.binary.left, 1U, RV_T1, true) ||
        !rv_value_chunk(sel, ins->value.binary.right, 0U, RV_T2, true) ||
        !rv_value_chunk(sel, ins->value.binary.right, 1U, RV_T3, true))
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        if (!rv_wide_integer(sel, ins->core_type_id, &result_sign) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                RV_T4, RV_T0, RV_T2) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SLTU,
                RV_T5, RV_T4, RV_T0) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                RV_T1, RV_T1, RV_T3) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                RV_T1, RV_T1, RV_T5) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_MV,
                RV_T0, RV_T4)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        if (!rv_wide_integer(sel, ins->core_type_id, &result_sign) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SLTU,
                RV_T5, RV_T0, RV_T2) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SUB,
                RV_T0, RV_T0, RV_T2) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SUB,
                RV_T1, RV_T1, RV_T3) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SUB,
                RV_T1, RV_T1, RV_T5)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        if (!rv_wide_integer(sel, ins->core_type_id, &result_sign))
            return false;
        opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND ?
            MINIC_RISCV_MACHINE_AND :
            ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR ?
            MINIC_RISCV_MACHINE_OR : MINIC_RISCV_MACHINE_XOR;
        if (!rv_rrr(sel, opcode, RV_T0, RV_T0, RV_T2) ||
            !rv_rrr(sel, opcode, RV_T1, RV_T1, RV_T3)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                RV_T0, RV_T0, RV_T2) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                RV_T1, RV_T1, RV_T3) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                RV_T0, RV_T0, RV_T1) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ,
                RV_T0, RV_T0)) return false;
        return rv_store(sel, ins->result, RV_T0);
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        if (!rv_rrr(sel, left_sign ? MINIC_RISCV_MACHINE_SLT :
                MINIC_RISCV_MACHINE_SLTU, RV_T4, RV_T1, RV_T3) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                RV_T5, RV_T1, RV_T3) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ,
                RV_T5, RV_T5) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SLTU,
                RV_T0, RV_T0, RV_T2) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_T0, RV_T0, RV_T5) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                RV_T0, RV_T0, RV_T4)) return false;
        return rv_store(sel, ins->result, RV_T0);
    default:
        return false;
    }
    return rv_value_chunk(sel, ins->result, 0U, RV_T0, false) &&
        rv_value_chunk(sel, ins->result, 1U, RV_T1, false);
}

static bool rv_core_wide_unary(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    bool source_sign, result_sign;
    unsigned result_bytes;
    bool result_pointer;
    if (ins->value.operand >= fn->value_count ||
        !rv_wide_integer(sel,
            fn->values[ins->value.operand].core_type_id, &source_sign) ||
        !rv_value_chunk(sel, ins->value.operand, 0U, RV_T0, true) ||
        !rv_value_chunk(sel, ins->value.operand, 1U, RV_T1, true))
        return false;
    /* A 2*XLEN input has two truth limbs, but the Core `!x` RESULT is
     * ordinary 32-bit int on BOTH ILP32 and LP64. Never demand a wide
     * result slot for SCALAR_IS_ZERO, and never inspect only its low limb. */
    if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO) {
        if (!rv_type(sel, ins->core_type_id, &result_bytes,
                &result_sign, &result_pointer) || result_pointer ||
            result_bytes != 4U) return false;
        return rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                RV_T0, RV_T0, RV_T1) &&
            rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T0, RV_T0) &&
            rv_store(sel, ins->result, RV_T0);
    }
    if (!rv_wide_integer(sel, ins->core_type_id, &result_sign) ||
        source_sign != result_sign) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (!rv_rr(sel, MINIC_RISCV_MACHINE_NOT, RV_T0, RV_T0) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_NOT, RV_T1, RV_T1)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        if (!rv_rr(sel, MINIC_RISCV_MACHINE_NEG, RV_T0, RV_T0) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ, RV_T2, RV_T0) ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_NEG, RV_T1, RV_T1) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_SUB,
                RV_T1, RV_T1, RV_T2)) return false;
        break;
    default:
        return false;
    }
    return rv_value_chunk(sel, ins->result, 0U, RV_T0, false) &&
        rv_value_chunk(sel, ins->result, 1U, RV_T1, false);
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
    if (loc.floating_register_count != 0U) {
        const MinicCoreType *type = minic_core_type_get(types,
            minic_core_type_unqualified(types, ins->core_type_id));
        if (type == NULL || type->kind != MINIC_CORE_TYPE_FLOAT ||
            (size != 4U && size != 8U)) return false;
        return rv_fp_to_bits(sel, RV_T0,
                   10U + (unsigned)loc.floating_register_begin,
                   size == 8U) &&
            rv_store(sel, ins->result, RV_T0);
    }
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
/* Raw IEEE payloads and two-word integers can share one byte-exact store
 * transport. This remains valid for dynamically unaligned/packed pointers;
 * neither an F/D instruction nor an unaligned RV integer load is needed. */
static bool rv_core_payload_memory(RvCoreSelection *sel,
    const MinicCoreInstruction *ins, bool load,
    MinicCoreValueId address, MinicCoreValueId source, bool is_volatile,
    const MinicCoreType *type) {
    const size_t word = sel->xlen / 8U;
    size_t i, j, chunks;
    if (type == NULL || !type->has_layout ||
        !((type->kind == MINIC_CORE_TYPE_FLOAT &&
            (type->size == 4U || type->size == 8U)) ||
          (type->kind == MINIC_CORE_TYPE_INTEGER &&
            type->size == 2U * word &&
            type->value.integer.bit_width == 2U * sel->xlen)) ||
        type->size > 2U * word ||
        !rv_load(sel, address, RV_T0)) return false;
    chunks = (type->size + word - 1U) / word;
    for (i = 0U; i < chunks; ++i) {
        const size_t count = type->size - i * word < word ?
            type->size - i * word : word;
        if (load) {
            if (!rv_li(sel, RV_T1, 0)) return false;
        } else if (!rv_value_chunk(sel, source, i, RV_T1, true)) {
            return false;
        }
        for (j = 0U; j < count; ++j) {
            if (load) {
                if (!rv_byte_transfer(sel, true, RV_T2, RV_T0,
                        i * word + j, is_volatile) ||
                    (j != 0U &&
                     !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                         RV_T2, RV_T2, (int64_t)(j * 8U))) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                        RV_T1, RV_T1, RV_T2)) return false;
            } else {
                if ((j == 0U
                        ? !rv_rr(sel, MINIC_RISCV_MACHINE_MV,
                            RV_T2, RV_T1)
                        : !rv_rri(sel, MINIC_RISCV_MACHINE_SRLI,
                            RV_T2, RV_T1, (int64_t)(j * 8U))) ||
                    !rv_byte_transfer(sel, false, RV_T2, RV_T0,
                        i * word + j, is_volatile)) return false;
            }
        }
        if (load && !rv_value_chunk(sel, ins->result, i,
                RV_T1, false)) return false;
    }
    return true;
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
    memory_type = fn->type_context == NULL ? NULL :
        minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context, type_id));
    if (!atomic && memory_type != NULL &&
        (memory_type->kind == MINIC_CORE_TYPE_FLOAT ||
         (memory_type->kind == MINIC_CORE_TYPE_INTEGER &&
          memory_type->size == 2U * (sel->xlen / 8U))))
        return rv_core_payload_memory(sel, ins, load, address,
            source, is_volatile, memory_type);
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

/* Checked arithmetic has two distinct results: the C pointer's stored
 * integer and the Core SSA boolean. A full-XLEN multiply may call compiler-rt
 * twice, so no physical t/a register or pre-call address is allowed to
 * survive across those calls. Store the integer through its verified Core
 * pointer before reconstructing the flag, and use the otherwise unwritten
 * SSA boolean slot as an internal word-sized product spill between calls. */
static bool rv_checked_store_result(RvCoreSelection *sel,
    MinicCoreValueId address, unsigned size, unsigned low) {
    unsigned i;
    if (sel->xlen != 64U || size == 0U || size > 8U ||
        low == RV_T0 || low == RV_T1 ||
        !rv_load(sel, address, RV_T0)) return false;
    /* Byte-exact transport also supports a packed/unaligned output pointer.
     * No speculative aligned sd/sw is permitted without proven alignment. */
    for (i = 0U; i < size; ++i) {
        if ((i == 0U
                ? !rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T1, low)
                : !rv_rri(sel, MINIC_RISCV_MACHINE_SRLI,
                    RV_T1, low, (int64_t)(i * 8U))) ||
            !rv_byte_transfer(sel, false, RV_T1, RV_T0, i, false))
            return false;
    }
    return true;
}

static bool rv_checked_call(RvCoreSelection *sel, const char *helper) {
    MinicMachineOperand op = {0};
    if (helper == NULL || helper[0] == '\0') return false;
    op.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    op.value.symbol.name = (char *)helper;
    op.value.symbol.relocation = MINIC_RISCV_MACHINE_RELOC_CALL;
    return rv_emit(sel, MINIC_RISCV_MACHINE_CALL_DIRECT, &op, 1U);
}

/* LP64 checked-add/sub/mul is an entire target-neutral Core instruction
 * family. The old RV64 emitter is a semantic reference only; every operation
 * here produces actual selected MachineIR, including the compiler-rt calls.
 * Input/pointee types must be identical for the 18 canonical GNU builtins.
 * Mixed-width/signed arithmetic is a separate admission case and remains
 * fail-closed rather than silently changing the old overflow contract. */
static bool rv_core_checked_overflow(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTypeContext *types = fn->type_context;
    const MinicCoreValueId left = ins->value.integer_overflow.left;
    const MinicCoreValueId right = ins->value.integer_overflow.right;
    const MinicCoreValueId address =
        ins->value.integer_overflow.result_address;
    const MinicCoreIntegerOverflowOperator op =
        ins->value.integer_overflow.operator_kind;
    const MinicCoreType *pointer, *result, *flag;
    MinicCoreTypeId pointee;
    unsigned size;
    bool sign;
    if (sel->xlen != 64U || types == NULL ||
        left >= fn->value_count || right >= fn->value_count ||
        address >= fn->value_count || ins->result >= fn->value_count ||
        (op != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
         op != MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
         op != MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY)) return false;
    pointer = minic_core_type_get(types, minic_core_type_unqualified(types,
        fn->values[address].core_type_id));
    flag = minic_core_type_get(types, minic_core_type_unqualified(types,
        ins->core_type_id));
    if (pointer == NULL || pointer->kind != MINIC_CORE_TYPE_POINTER ||
        !pointer->has_layout || pointer->size != 8U ||
        flag == NULL || flag->kind != MINIC_CORE_TYPE_INTEGER ||
        !flag->value.integer.is_bool ||
        minic_core_type_unqualified(types,
            fn->values[ins->result].core_type_id) !=
            minic_core_type_unqualified(types, ins->core_type_id))
        return false;
    pointee = minic_core_type_unqualified(types,
        pointer->value.pointer.pointee);
    result = minic_core_type_get(types, pointee);
    if (result == NULL || result->kind != MINIC_CORE_TYPE_INTEGER ||
        !result->has_layout || result->value.integer.is_bool ||
        (result->size != 1U && result->size != 2U &&
         result->size != 4U && result->size != 8U) ||
        result->value.integer.bit_width != result->size * 8U ||
        minic_core_type_unqualified(types,
            fn->values[left].core_type_id) != pointee ||
        minic_core_type_unqualified(types,
            fn->values[right].core_type_id) != pointee)
        return false;
    size = (unsigned)result->size;
    sign = result->value.integer.is_signed;
    if (op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
        if (!minic_riscv_machine_checked_mul_runtime_call(fn, ins,
                sel->xlen) ||
            !sel->plan->frame->saves_return_address ||
            !rv_load(sel, left, RV_A0) ||
            !rv_load(sel, right, RV_A1) ||
            !rv_checked_call(sel, "__muldi3") ||
            !rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T2, RV_A0))
            return false;
    } else if (!rv_load(sel, left, RV_T0) ||
               !rv_load(sel, right, RV_T1) ||
               !rv_rrr(sel, op == MINIC_CORE_INTEGER_OVERFLOW_ADD ?
                   MINIC_RISCV_MACHINE_ADD : MINIC_RISCV_MACHINE_SUB,
                   RV_T2, RV_T0, RV_T1)) return false;

    if (size < 8U || op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
        if (size < 8U) {
            /* A promoted <=32-bit add/sub or multiply fits entirely in
             * signed/unsigned 64-bit scratch; truncation detects overflow. */
            if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T4, RV_T2) ||
                !rv_normalize(sel, RV_T2, size, sign) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                    RV_T4, RV_T4, RV_T2) ||
                !rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ, RV_T4, RV_T4))
                return false;
        } else {
            /* The low product is stored before dividing, as in GCC's
             * __builtin_mul_overflow contract. All t registers are caller
             * clobbered, so both operands are reread after the call. */
            if (!rv_checked_store_result(sel, address, size, RV_T2) ||
                !rv_store(sel, ins->result, RV_T2) ||
                !rv_load(sel, ins->result, RV_A0) ||
                !rv_load(sel, right, RV_A1)) return false;
            if (sign) {
                /* __divdi3 must never receive the mathematically undefined
                 * MIN/-1 pair. Change that denominator to +1 for this
                 * internal verification call; the original pair is reread
                 * below and independently sets the overflow predicate. */
                if (!rv_load(sel, left, RV_T0) ||
                    !rv_li(sel, RV_T5, INT64_MIN) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                        RV_T5, RV_T5, RV_T0) ||
                    !rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T5, RV_T5) ||
                    !rv_li(sel, RV_T6, -1) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                        RV_T6, RV_T6, RV_A1) ||
                    !rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T6, RV_T6) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                        RV_T5, RV_T5, RV_T6) ||
                    !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                        RV_T5, RV_T5, 1) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                        RV_A1, RV_A1, RV_T5)) return false;
            }
            if (!rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T5, RV_A1) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                    RV_A1, RV_A1, RV_T5) ||
                !rv_checked_call(sel, sign ? "__divdi3" : "__udivdi3") ||
                !rv_load(sel, left, RV_T0) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                    RV_T4, RV_A0, RV_T0) ||
                !rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ, RV_T4, RV_T4) ||
                !rv_load(sel, right, RV_T1) ||
                !rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ, RV_T1, RV_T1) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                    RV_T4, RV_T4, RV_T1)) return false;
            if (sign) {
                /* Integer division's MIN/-1 corner is defined by neither
                 * C division nor the ordinary inverse-product check. */
                if (!rv_load(sel, left, RV_T0) ||
                    !rv_li(sel, RV_T5, INT64_MIN) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                        RV_T5, RV_T5, RV_T0) ||
                    !rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T5, RV_T5) ||
                    !rv_load(sel, right, RV_T1) ||
                    !rv_li(sel, RV_T6, -1) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                        RV_T6, RV_T6, RV_T1) ||
                    !rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T6, RV_T6) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                        RV_T5, RV_T5, RV_T6) ||
                    !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                        RV_T4, RV_T4, RV_T5)) return false;
            }
            return rv_store(sel, ins->result, RV_T4);
        }
    } else if (!sign) {
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_SLTU,
                RV_T4, op == MINIC_CORE_INTEGER_OVERFLOW_ADD ?
                    RV_T2 : RV_T0,
                op == MINIC_CORE_INTEGER_OVERFLOW_ADD ?
                    RV_T0 : RV_T1)) return false;
    } else {
        /* ADD: ~(lhs^rhs)&(lhs^sum); SUB: (lhs^rhs)&(lhs^diff).
         * Sign bit alone is the true signed-XLEN overflow predicate. */
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                RV_T4, RV_T0, RV_T1) ||
            (op == MINIC_CORE_INTEGER_OVERFLOW_ADD &&
             !rv_rr(sel, MINIC_RISCV_MACHINE_NOT, RV_T4, RV_T4)) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                RV_T5, RV_T0, RV_T2) ||
            !rv_rrr(sel, MINIC_RISCV_MACHINE_AND,
                RV_T4, RV_T4, RV_T5) ||
            !rv_rri(sel, MINIC_RISCV_MACHINE_SRLI,
                RV_T4, RV_T4, 63)) return false;
    }
    return rv_checked_store_result(sel, address, size, RV_T2) &&
        rv_store(sel, ins->result, RV_T4);
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
    const MinicCoreFunction *fn = sel->plan->core;
    const size_t word = sel->xlen / 8U;
    const bool conversion = ins->kind ==
        MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION;
    MinicCoreIntegerConversionShape cast = {0};
    MinicCoreScalarBitcastShape bitcast = {0};
    size_t source_bytes, target_bytes;
    bool source_signed, target_signed, target_bool, source_pointer;
    if (conversion) {
        if (!minic_core_integer_conversion_shape(fn, ins, &cast))
            return false;
        source_bytes = cast.source_bytes;
        target_bytes = cast.target_bytes;
        source_signed = cast.source_signed;
        target_signed = cast.target_signed;
        target_bool = cast.target_bool;
        source_pointer = false;
    } else {
        if (!minic_core_scalar_bitcast_shape(fn, ins, &bitcast))
            return false;
        source_bytes = bitcast.source_bytes;
        target_bytes = bitcast.target_bytes;
        source_signed = bitcast.source_signed;
        target_signed = bitcast.target_signed;
        target_bool = bitcast.target_bool;
        source_pointer = bitcast.source_pointer;
    }
    /* RV32's historical pointer->long long bitcast sign-extends its pointer;
     * RV64 has no qualified pointer<->__int128 bitcast. */
    if (source_bytes == 0U || target_bytes == 0U ||
        source_bytes > 2U * word || target_bytes > 2U * word ||
        (!conversion && sel->xlen == 64U &&
         (source_bytes > word || target_bytes > word)) ||
        !rv_value_chunk(sel, ins->value.operand, 0U, RV_T0, true))
        return false;
    if (source_bytes > word) {
        if (!rv_value_chunk(sel, ins->value.operand, 1U, RV_T1, true))
            return false;
    } else if (!source_pointer &&
               !rv_normalize(sel, RV_T0, (unsigned)source_bytes,
                   source_signed)) return false;
    if (target_bool) {
        if (source_bytes > word &&
            !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                RV_T0, RV_T0, RV_T1)) return false;
        if (!rv_rr(sel, MINIC_RISCV_MACHINE_SNEZ,
            RV_T0, RV_T0)) return false;
    }
    if (target_bytes > word) {
        if (source_bytes <= word &&
            (conversion ? source_signed : source_pointer) &&
            !rv_rri(sel, MINIC_RISCV_MACHINE_SRAI,
                RV_T1, RV_T0, (int64_t)(sel->xlen - 1U)))
            return false;
        if (source_bytes <= word &&
            !(conversion ? source_signed : source_pointer) &&
            !rv_li(sel, RV_T1, 0)) return false;
        return rv_value_chunk(sel, ins->result, 0U, RV_T0, false) &&
            rv_value_chunk(sel, ins->result, 1U, RV_T1, false);
    }
    return rv_normalize(sel, RV_T0, (unsigned)target_bytes,
               target_signed) &&
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
        out->floating_register_begin = loc->floating_register_begin;
        out->floating_register_count = loc->floating_register_count;
        out->stack_begin = loc->stack_slot_begin;
        out->stack_count = loc->stack_slot_count;
        out->words = loc->value.slot_count;
        out->bytes = loc->value.storage_size;
        out->record = loc->value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE;
        out->indirect = loc->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
        out->ignored = loc->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE;
        if (loc->value.kind == MINIC_RISCV64_ABI_VALUE_INVALID ||
            loc->value.kind == MINIC_RISCV64_ABI_VALUE_VOID) return false;
    }
    if (out->floating_register_count != 0U)
        return sel->xlen == 64U &&
            out->floating_register_count == 1U &&
            out->floating_register_begin < 8U &&
            out->register_count == 0U && out->stack_count == 0U &&
            out->words == 1U && !out->record;
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
                (pass == 1U && loc.register_count == 0U &&
                 loc.floating_register_count == 0U)) continue;
            if (loc.floating_register_count != 0U) {
                const MinicCoreType *fp_type;
                if (pass != 1U || arg->kind !=
                        MINIC_CORE_CALL_ARGUMENT_VALUE ||
                    arg->value.value_id >= fn->value_count ||
                    fn->type_context == NULL) return false;
                fp_type = minic_core_type_get(fn->type_context,
                    minic_core_type_unqualified(fn->type_context,
                        fn->values[arg->value.value_id].core_type_id));
                if (fp_type == NULL || fp_type->kind != MINIC_CORE_TYPE_FLOAT ||
                    (fp_type->size != 4U && fp_type->size != 8U) ||
                    !rv_value_chunk(sel, arg->value.value_id,
                        0U, RV_T0, true) ||
                    !rv_fp_from_bits(sel,
                        10U + (unsigned)loc.floating_register_begin,
                        RV_T0, fp_type->size == 8U)) return false;
                continue;
            }
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
    const MinicCoreFunction *fn = sel->plan->core;
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
    if (sel->xlen == 64U &&
        minic_riscv_target_isa(sel->plan->target).abi ==
            MINIC_RISCV_ABI_LP64D && !integer) {
        const MinicCoreType *fp_type = minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context,
                ins->core_type_id));
        if (fp_type != NULL && fp_type->kind == MINIC_CORE_TYPE_FLOAT) {
            const MinicRiscv64PreparedCall *prepared;
            size_t ordinal;
            if ((size != 4U && size != 8U) ||
                sel->plan->semantic_calls64 == NULL ||
                minic_core_call_index_find(sel->plan->semantic_calls64,
                    instruction_id, &ordinal) == NULL ||
                ordinal >= sel->plan->abi64->outgoing_calls->count ||
                sel->plan->abi64->outgoing_calls->sites == NULL ||
                (prepared = sel->plan->abi64->outgoing_calls->sites[ordinal]) ==
                    NULL ||
                prepared->return_value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                !rv_fp_to_bits(sel, RV_A0, 10U, size == 8U)) return false;
        }
    }
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

/* A fixed register is an already-sealed, target-owned physical identity.
 * The register-variable read is not a GNU source-asm statement: it becomes a
 * typed MV followed by the same word-sized Core spill transport as other
 * scalar instructions.  Alias spellings such as x4/tp are resolved once. */
static bool rv_core_fixed_register_read(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreFixedRegisterBinding *binding;
    unsigned reg, bytes;
    bool sign, pointer;
    if (ins->value.fixed_register_binding_id >=
            fn->fixed_register_binding_count ||
        fn->fixed_register_bindings == NULL) return false;
    binding = &fn->fixed_register_bindings[
        ins->value.fixed_register_binding_id];
    if (binding->register_name == NULL ||
        binding->core_type_id != ins->core_type_id ||
        !minic_riscv_gpr_id(binding->register_name,
            binding->register_name_length, &reg) ||
        !((sel->plan->target->fixed_register_read != NULL &&
           sel->plan->target->fixed_register_read(
               binding->register_name, binding->register_name_length)) ||
          (binding->is_local &&
           sel->plan->target->local_fixed_register != NULL &&
           sel->plan->target->local_fixed_register(
               binding->register_name, binding->register_name_length))) ||
        !rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) ||
        bytes != sel->xlen / 8U || reg == RV_T0 ||
        !rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T0, reg)) return false;
    (void)sign;
    (void)pointer;
    return rv_store(sel, ins->result, RV_T0);
}

static bool rv_core_fixed_register_write(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreFixedRegisterBinding *binding;
    const size_t id = ins->value.fixed_register_write.binding_id;
    const MinicCoreValueId source = ins->value.fixed_register_write.source;
    unsigned reg, bytes;
    bool sign, pointer;
    if (id >= fn->fixed_register_binding_count ||
        fn->fixed_register_bindings == NULL || source >= fn->value_count ||
        ins->result != MINIC_CORE_VALUE_INVALID) return false;
    binding = &fn->fixed_register_bindings[id];
    if (binding->is_local || binding->register_name == NULL ||
        binding->core_type_id != fn->values[source].core_type_id ||
        sel->plan->target->fixed_register_write == NULL ||
        !sel->plan->target->fixed_register_write(
            binding->register_name, binding->register_name_length) ||
        !minic_riscv_gpr_id(binding->register_name,
            binding->register_name_length, &reg) ||
        reg == RV_ZERO || reg == RV_SP || reg == RV_T0 ||
        !rv_type(sel, binding->core_type_id, &bytes, &sign, &pointer) ||
        bytes != sel->xlen / 8U ||
        !rv_load(sel, source, RV_T0)) return false;
    (void)sign;
    (void)pointer;
    return rv_rr(sel, MINIC_RISCV_MACHINE_MV, reg, RV_T0);
}

/* The generic outputless GNU "r" form has no lvalue writeback or target
 * instruction to select. Still materialize EVERY source value into a distinct
 * real hardware register before preserving its original ASM effect. The
 * eight argument GPRs are caller-saved, and the frame transfer helpers use
 * only t-register scratch, so later input loads cannot overwrite earlier
 * bindings. This path is shared by RV32 ILP32 and RV64 LP64. */
static bool rv_core_scalar_input_source_asm(RvCoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->plan->core;
    const size_t count = ins->value.structured_inline_asm.operand_count;
    MinicMachineOperand bindings[8];
    size_t i;
    if (source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED ||
        source->target_machine_form_id != 0U ||
        source->target_machine_form_variant != 0U ||
        source->implicit_memory_effect || source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        source->is_goto || !source->is_volatile ||
        source->template_length == 0U ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        count == 0U || count > 8U) return false;
    /* Prove the entire binding set before emitting any machine instructions.
     * Other GNU roles, fixed/clobbered registers, 2-word RV32 inputs, and a
     * ninth generic register input remain unimplemented, not reinterpreted. */
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &ins->value.structured_inline_asm.operands[i];
        unsigned bytes;
        bool sign, pointer;
        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            binding->operand_index != i ||
            binding->has_fixed_register_binding || binding->early_clobber ||
            binding->physical_early_clobber || binding->physical_read_write ||
            binding->has_tied_output || binding->value >= fn->value_count ||
            !rv_type(sel, fn->values[binding->value].core_type_id,
                     &bytes, &sign, &pointer)) return false;
        (void)bytes;
        (void)sign;
        (void)pointer;
    }
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &ins->value.structured_inline_asm.operands[i];
        if (!rv_load(sel, binding->value, RV_A0 + (unsigned)i)) return false;
        bindings[i] = rv_reg(sel->xlen, RV_A0 + (unsigned)i,
            MINIC_MACHINE_REGISTER_READ);
        bindings[i].asm_constraint = "r";
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, 0U, bindings, count, NULL, 0U, false, false);
}

/* Source-owned GNU =r / +r effects carry a Core pointer to the actual
 * lvalue. The physical output register must be written back AFTER the ASM,
 * including unaligned and narrow objects; it is never a fake C SSA result.
 * Transport every byte explicitly, so a packed Linux field cannot turn an
 * otherwise valid output into an unaligned LW/SW/LD/SD. */
static bool rv_core_asm_lvalue(RvCoreSelection *sel,
    MinicCoreValueId address, unsigned *bytes, bool *sign) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTypeContext *types = fn->type_context;
    const MinicCoreType *pointer;
    bool ptr;
    unsigned ptr_bytes;
    bool ptr_signed;
    if (types == NULL || address >= fn->value_count ||
        !rv_type(sel, fn->values[address].core_type_id,
            &ptr_bytes, &ptr_signed, &ptr) || !ptr)
        return false;
    pointer = minic_core_type_get(types, minic_core_type_unqualified(types,
        fn->values[address].core_type_id));
    return pointer != NULL && pointer->kind == MINIC_CORE_TYPE_POINTER &&
        rv_type(sel, pointer->value.pointer.pointee, bytes, sign, &ptr);
}
static bool rv_core_asm_lvalue_transfer(RvCoreSelection *sel,
    MinicCoreValueId address, unsigned bytes, bool load) {
    unsigned i;
    if (bytes == 0U || bytes > sel->xlen / 8U ||
        !rv_load(sel, address, RV_T2) ||
        (load && !rv_li(sel, RV_T0, 0))) return false;
    for (i = 0U; i < bytes; ++i) {
        if (load) {
            if (!rv_byte_transfer(sel, true, RV_T1, RV_T2, i, false) ||
                (i != 0U && !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                    RV_T1, RV_T1, (int64_t)(i * 8U))) ||
                !rv_rrr(sel, MINIC_RISCV_MACHINE_OR,
                    RV_T0, RV_T0, RV_T1)) return false;
        } else if ((i == 0U
                ? !rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T1, RV_T0)
                : !rv_rri(sel, MINIC_RISCV_MACHINE_SRLI,
                    RV_T1, RV_T0, (int64_t)(i * 8U))) ||
            !rv_byte_transfer(sel, false, RV_T1, RV_T2, i, false))
            return false;
    }
    return true;
}

static bool rv_core_asm_has_z_operand(const MinicCoreInlineAsm *source,
    size_t operand_index) {
    size_t i;
    if (source == NULL || operand_index > 9U ||
        source->template_length < 3U) return false;
    for (i = 0U; i + 2U < source->template_length; ++i)
        if (source->template_text[i] == '%' &&
            source->template_text[i + 1U] == 'z' &&
            source->template_text[i + 2U] ==
                (char)('0' + (int)operand_index) &&
            (i + 3U == source->template_length ||
             source->template_text[i + 3U] < '0' ||
             source->template_text[i + 3U] > '9')) return true;
    return false;
}
static bool rv_core_asm_zero_rj(const MinicCoreFunction *fn,
    const MinicCoreInlineAsm *source, MinicCoreValueId value,
    size_t operand_index) {
    const MinicCoreInstruction *def;
    if (fn == NULL || value >= fn->value_count ||
        fn->values[value].definition >= fn->instruction_count ||
        !rv_core_asm_has_z_operand(source, operand_index)) return false;
    def = &fn->instructions[fn->values[value].definition];
    /* Only a proved literal zero plus source %zN can choose GNU's rJ
     * immediate alternative. Otherwise preserve a physical register. */
    return def->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT &&
        def->value.integer_value == 0;
}

/* This common RV32/RV64 physical binding owns the canonical GNU scalar
 * register output family. There is no temporary alias between output x5
 * and input x6, and the remaining t-registers are reserved for Core frame
 * transfers. Source text stays SOURCE_ASM, never a selected CSR opcode or
 * a manually reproduced assembly string. Complex roles remain fail-closed. */
static bool rv_core_scalar_output_source_asm(RvCoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreStructuredInlineAsmOperand *roles =
        ins->value.structured_inline_asm.operands;
    const size_t count = ins->value.structured_inline_asm.operand_count;
    const bool ssa = count != 0U &&
        roles[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT;
    const bool readwrite = count != 0U &&
        roles[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    MinicMachineOperand bindings[2];
    unsigned bytes, input_bytes;
    bool sign, pointer, input_sign, input_pointer;
    if (source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED &&
        !(source->operand_plan_owner == MINIC_ASM_OPERAND_PLAN_NONE &&
          ssa && count == 1U)) return false;
    if (source->target_machine_form_id != 0U ||
        source->target_machine_form_variant != 0U ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        source->is_goto || !source->is_volatile ||
        source->template_length == 0U ||
        count == 0U || count > 2U ||
        roles[0].operand_index != 0U ||
        (roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
         !ssa && !readwrite) ||
        roles[0].has_fixed_register_binding ||
        roles[0].early_clobber != roles[0].physical_early_clobber ||
        (readwrite && roles[0].early_clobber) ||
        roles[0].has_tied_output ||
        roles[0].physical_read_write != readwrite ||
        (ssa ? (ins->result >= fn->value_count ||
                roles[0].value != MINIC_CORE_VALUE_INVALID ||
                !rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer)) :
               (ins->result != MINIC_CORE_VALUE_INVALID ||
                !rv_core_asm_lvalue(sel, roles[0].value, &bytes, &sign))))
        return false;
    if (count == 2U &&
        (roles[1].operand_index != 1U ||
         roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
         roles[1].has_fixed_register_binding || roles[1].early_clobber ||
         roles[1].physical_early_clobber ||
         roles[1].physical_read_write || roles[1].has_tied_output ||
         roles[1].value >= fn->value_count ||
         !rv_type(sel, fn->values[roles[1].value].core_type_id,
             &input_bytes, &input_sign, &input_pointer))) return false;
    (void)pointer;
    (void)input_bytes;
    (void)input_sign;
    (void)input_pointer;
    /* The output destination is materialized only after SOURCE_ASM. A
     * read/write output carries its pre-ASM value in the SAME register. */
    if ((readwrite && !rv_core_asm_lvalue_transfer(sel,
            roles[0].value, bytes, true)) ||
        (count == 2U && !rv_load(sel, roles[1].value, RV_T1))) return false;
    bindings[0] = rv_reg(sel->xlen, RV_T0,
        MINIC_MACHINE_REGISTER_WRITE |
            (readwrite ? MINIC_MACHINE_REGISTER_READ : 0U));
    bindings[0].asm_constraint = readwrite ? "+r" :
        (roles[0].physical_early_clobber ? "=&r" : "=r");
    if (count == 2U) {
        bindings[1] = rv_reg(sel->xlen, RV_T1,
            MINIC_MACHINE_REGISTER_READ);
        bindings[1].asm_constraint = "r";
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, 0U, bindings, count, NULL, 0U, false, false) ||
        !rv_normalize(sel, RV_T0, bytes, sign)) return false;
    return ssa ? rv_store(sel, ins->result, RV_T0) :
        rv_core_asm_lvalue_transfer(sel, roles[0].value, bytes, false);
}

/* GNU Linux atomics carry a physical memory address as a first-class ASM
 * operand, not a scalar register with a textual `(...)` suffix.  Keep every
 * output, early-clobber, read/write and input in a distinct a-register while
 * the source effect executes.  Frame materialization uses only t-registers,
 * so loading later inputs cannot clobber already bound operands.  This is
 * one shared RV32 ILP32 / RV64 LP64 lowering of the complete mixed +A,
 * register-output and scalar-input family, not an alternate text emitter. */
static bool rv_core_atomic_source_asm(RvCoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreStructuredInlineAsmOperand *roles =
        ins->value.structured_inline_asm.operands;
    const size_t count = ins->value.structured_inline_asm.operand_count;
    MinicMachineOperand bindings[8];
    unsigned widths[8] = {0};
    bool signs[8] = {0};
    size_t i, outputs = 0U, results = 0U;
    bool has_memory = false;
    unsigned int effects = 0U;
    if (source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED ||
        source->target_machine_form_id != 0U ||
        source->target_machine_form_variant != 0U ||
        source->is_goto || source->template_length == 0U ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        count < 2U || count > 8U ||
        !minic_core_asm_output_operand_count(ins, &outputs)) return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *role = &roles[i];
        bool pointer;
        unsigned ptr_bytes;
        bool ptr_signed;
        if (role->operand_index != i || role->has_fixed_register_binding ||
            role->has_tied_output ||
            role->early_clobber != role->physical_early_clobber)
            return false;
        switch (role->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
            if (role->value >= fn->value_count ||
                !rv_type(sel, fn->values[role->value].core_type_id,
                    &ptr_bytes, &ptr_signed, &pointer) || !pointer ||
                !rv_core_asm_lvalue(sel, role->value,
                    &widths[i], &signs[i]) ||
                role->early_clobber ||
                role->physical_read_write != (role->kind ==
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE))
                return false;
            has_memory = true;
            effects |= (role->kind ==
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ?
                    MINIC_MACHINE_EFFECT_MEMORY_READ :
                (role->kind ==
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT) ?
                    MINIC_MACHINE_EFFECT_MEMORY_WRITE :
                    MINIC_MACHINE_EFFECT_MEMORY_READ |
                    MINIC_MACHINE_EFFECT_MEMORY_WRITE;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT:
            if (results++ != 0U || i >= outputs ||
                role->value != MINIC_CORE_VALUE_INVALID ||
                ins->result >= fn->value_count ||
                !rv_type(sel, ins->core_type_id, &widths[i],
                    &signs[i], &pointer) || role->physical_read_write)
                return false;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (i >= outputs || role->value >= fn->value_count ||
                !rv_core_asm_lvalue(sel, role->value,
                    &widths[i], &signs[i]) ||
                role->physical_read_write != (role->kind ==
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) ||
                (role->kind ==
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE &&
                    role->early_clobber)) return false;
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (i < outputs || role->value >= fn->value_count ||
                role->early_clobber || role->physical_read_write)
                return false;
            if (!rv_type(sel, fn->values[role->value].core_type_id,
                    &widths[i], &signs[i], &pointer)) {
                /* GNU rJ on RV32 accepts a 64-bit pair; %zN names its low
                 * word.  This is a source-owned GNU operand selection, not
                 * compiler generation of an extension instruction. */
                if (sel->xlen != 32U ||
                    !rv_wide_integer(sel,
                        fn->values[role->value].core_type_id, &signs[i]) ||
                    !rv_core_asm_has_z_operand(source, i)) return false;
                widths[i] = 2U * (sel->xlen / 8U);
            }
            break;
        default:
            return false;
        }
    }
    /* This path admits mixed memory/register atomics only.  Existing simple
     * scalar roles retain their independently tested exact contract. */
    if (!has_memory || (results == 0U &&
         ins->result != MINIC_CORE_VALUE_INVALID) ||
        (results != 0U && ins->result == MINIC_CORE_VALUE_INVALID))
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *role = &roles[i];
        const unsigned physical = RV_A0 + (unsigned)i;
        switch (role->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
            if (!rv_load(sel, role->value, physical)) return false;
            bindings[i] = rv_addr(sel->xlen, physical);
            bindings[i].asm_constraint = role->kind ==
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ? "+A" :
                role->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ?
                    "=m" : "m";
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            bindings[i] = rv_reg(sel->xlen, physical,
                MINIC_MACHINE_REGISTER_WRITE);
            bindings[i].asm_constraint = role->physical_early_clobber ?
                "=&r" : "=r";
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!rv_core_asm_lvalue_transfer(sel, role->value,
                    widths[i], true) ||
                !rv_rr(sel, MINIC_RISCV_MACHINE_MV, physical, RV_T0))
                return false;
            bindings[i] = rv_reg(sel->xlen, physical,
                MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE);
            bindings[i].asm_constraint = "+r";
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (rv_core_asm_zero_rj(fn, source, role->value, i)) {
                bindings[i] = rv_imm(0);
                bindings[i].asm_constraint = "rJ";
            } else {
                if (!(widths[i] > sel->xlen / 8U
                        ? rv_value_chunk(sel, role->value, 0U,
                            physical, true)
                        : rv_load(sel, role->value, physical))) return false;
                bindings[i] = rv_reg(sel->xlen, physical,
                    MINIC_MACHINE_REGISTER_READ);
                bindings[i].asm_constraint = "r";
            }
            break;
        default:
            return false;
        }
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder, source,
            effects, bindings, count, NULL, 0U, false, false)) return false;
    for (i = 0U; i < outputs; ++i) {
        const MinicCoreStructuredInlineAsmOperand *role = &roles[i];
        const unsigned physical = RV_A0 + (unsigned)i;
        if (role->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT ||
            role->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            role->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
            if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T0, physical) ||
                !rv_normalize(sel, RV_T0, widths[i], signs[i])) return false;
            if (role->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_RESULT) {
                if (!rv_store(sel, ins->result, RV_T0)) return false;
            } else if (!rv_core_asm_lvalue_transfer(sel, role->value,
                       widths[i], false)) return false;
        }
    }
    return true;
}

/* One source-origin branch with exact C-ingress typed `i` witnesses and
 * label destinations. No target-side recovery of GNU named operands from
 * the source template is permitted: Core retains every original name. */
static bool rv_core_source_asm_goto(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->plan->core;
    MinicMachineOperand bindings[5];
    size_t i, count, continuation;
    if (ins->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        !source->is_goto || !source->is_volatile ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_NONE ||
        source->target_machine_form_id != 0U ||
        source->target_machine_form_variant != 0U ||
        source->has_memory_clobber || source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        source->resolved_immediate_count == 0U ||
        source->resolved_immediate_count > 3U ||
        source->goto_target >= fn->block_count ||
        !sel->reachable[source->goto_target] ||
        source->source_asm_unique_id == 0U ||
        source->template_length == 0U ||
        instruction_id >= fn->instruction_count ||
        sel->asm_goto_continuations == NULL ||
        (continuation = sel->asm_goto_continuations[instruction_id]) == SIZE_MAX ||
        continuation != sel->builder.current_block + 1U)
        return false;
    count = source->resolved_immediate_count;
    for (i = 0U; i < count; ++i) {
        const MinicCoreInlineAsmImmediate *input =
            &source->resolved_immediates[i];
        if (input->kind == MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER) {
            if (input->symbol != NULL || input->byte_addend != 0)
                return false;
            bindings[i] = rv_imm(input->integer);
        } else if (input->kind == MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL) {
            if (!minic_core_inline_asm_relocation_valid(input)) return false;
            memset(&bindings[i], 0, sizeof(bindings[i]));
            bindings[i].kind = MINIC_MACHINE_OPERAND_SYMBOL;
            bindings[i].value.symbol.name = input->symbol;
            bindings[i].value.symbol.relocation =
                MINIC_RISCV_MACHINE_RELOC_SOURCE_IMMEDIATE;
            bindings[i].value.symbol.addend = input->byte_addend;
        } else return false;
        bindings[i].asm_constraint = "i";
        bindings[i].asm_name = input->asm_name;
    }
    if (source->goto_second_target != MINIC_CORE_BLOCK_INVALID &&
        (source->goto_second_target >= fn->block_count ||
         !sel->reachable[source->goto_second_target] ||
         source->goto_second_target == source->goto_target))
        return false;
    bindings[count] = rv_target(sel->core_blocks[source->goto_target], false);
    bindings[count].asm_name = source->goto_label_names[0];
    ++count;
    if (source->goto_second_target != MINIC_CORE_BLOCK_INVALID) {
        bindings[count] = rv_target(
            sel->core_blocks[source->goto_second_target], false);
        bindings[count].asm_name = source->goto_label_names[1];
        ++count;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder, source,
            MINIC_MACHINE_EFFECT_BRANCH, bindings, count, NULL, 0U,
            true, true) ||
        !minic_machine_builder_add_successor(&sel->builder,
            sel->core_blocks[source->goto_target]) ||
        (source->goto_second_target != MINIC_CORE_BLOCK_INVALID &&
         !minic_machine_builder_add_successor(&sel->builder,
             sel->core_blocks[source->goto_second_target])) ||
        !minic_machine_builder_add_successor(&sel->builder, continuation) ||
        !minic_machine_builder_set_fallthrough(&sel->builder, continuation) ||
        !minic_machine_builder_position_at(&sel->builder, continuation))
        return false;
    return true;
}

/* Preserve exactly the source-owned, sealed target asm event. All accepted
 * scalar register roles have explicit pre/post-ASM machine transfers. */
static bool rv_core_unbound_source_asm(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreInlineAsm *source;
    MinicCoreInlineAsmId id;
    if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM)
        id = ins->value.inline_asm_id;
    else if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
        id = ins->value.structured_inline_asm.inline_asm_id;
    } else return false;
    if (id >= fn->inline_asm_count || fn->inline_asms == NULL) return false;
    source = &fn->inline_asms[id];
    if (source->template_text == NULL) return false;
    if (source->is_goto)
        return rv_core_source_asm_goto(sel, instruction_id, ins, source);
    if (
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID)
        return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM &&
        ins->value.structured_inline_asm.operand_count != 0U) {
        size_t i;
        for (i = 0U; i < ins->value.structured_inline_asm.operand_count; ++i) {
            const MinicCoreStructuredInlineAsmOperandKind kind =
                ins->value.structured_inline_asm.operands[i].kind;
            if (kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
                kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
                kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT)
                return rv_core_atomic_source_asm(sel, ins, source);
        }
        if (ins->value.structured_inline_asm.operands[0].kind ==
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)
            return rv_core_scalar_input_source_asm(sel, ins, source);
        return rv_core_scalar_output_source_asm(sel, ins, source);
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, 0U, NULL, 0U, NULL, 0U, false, false);
}
static bool rv_core_instruction(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins) {
    bool wide_signed;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return rv_core_constant(sel, ins);
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        return rv_core_float_constant(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return rv_core_parameter(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return rv_core_parameter_object(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
        return rv_core_fixed_register_read(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE:
        return rv_core_fixed_register_write(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        return rv_core_bitscan(sel, ins);
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
        if (ins->value.binary.left < sel->plan->core->value_count &&
            rv_wide_integer(sel, sel->plan->core->values[
                ins->value.binary.left].core_type_id, &wide_signed))
            return (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
                    ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT)
                ? rv_core_wide_shift(sel, ins) : rv_core_wide_binary(sel, ins);
        return rv_core_binary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        /* Hard-float ABI is orthogonal to integer arithmetic. LP64D still
         * emits only base-I opcodes; all three integer operations use the
         * same typed compiler-rt CALL transport as LP64/ILP32. */
        return rv_core_runtime_binary(sel, ins);
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        return sel->xlen == 64U &&
            minic_riscv_target_isa(sel->plan->target).abi ==
                MINIC_RISCV_ABI_LP64D
            ? rv_core_native_fp(sel, ins)
            : rv_core_runtime_binary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        return rv_core_checked_overflow(sel, ins);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        return sel->xlen == 64U &&
            minic_riscv_target_isa(sel->plan->target).abi ==
                MINIC_RISCV_ABI_LP64D
            ? rv_core_native_fp(sel, ins)
            : rv_core_runtime_double_compare(sel, ins);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return sel->xlen == 64U &&
            minic_riscv_target_isa(sel->plan->target).abi ==
                MINIC_RISCV_ABI_LP64D
            ? rv_core_native_fp(sel, ins)
            : rv_core_runtime_unary_float(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (ins->value.operand < sel->plan->core->value_count &&
            rv_wide_integer(sel, sel->plan->core->values[
                ins->value.operand].core_type_id, &wide_signed))
            return rv_core_wide_unary(sel, ins);
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
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return rv_core_dynamic_stack(sel, instruction_id, ins);
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return rv_core_variadic_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL:
        return rv_core_call(sel, instruction_id, ins);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return rv_core_indirect_call(sel, instruction_id, ins);
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return rv_core_unbound_source_asm(sel, instruction_id, ins);
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
            if (sel->xlen == 64U &&
                minic_riscv_target_isa(sel->plan->target).abi ==
                    MINIC_RISCV_ABI_LP64D &&
                sel->plan->abi64->return_value.kind ==
                    MINIC_RISCV64_ABI_VALUE_FLOAT) {
                const MinicCoreType *type = minic_core_type_get(
                    fn->type_context, minic_core_type_unqualified(
                        fn->type_context, fn->values[value].core_type_id));
                if (type == NULL || type->kind != MINIC_CORE_TYPE_FLOAT ||
                    chunks != 1U || (size != 4U && size != 8U) ||
                    !rv_fp_from_bits(sel, 10U, RV_A0, size == 8U))
                    return false;
            }
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
    const char *reject_phase = "input_contract";
    const MinicCoreFunction *trace_core = plan != NULL ? plan->core : NULL;
    size_t reject_block = SIZE_MAX;
    unsigned reject_terminator_kind = UINT_MAX;
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
               plan->abi64->named_parameter_cursor.lp64_soft_float !=
                   (isa.abi == MINIC_RISCV_ABI_LP64) ||
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
            isa.codegen_features)) {
        if (getenv("MINIC_RV_MACHINE_TRACE") != NULL)
            (void)fprintf(stderr,
                "RV_TYPED_REJECT_PHASE function=%s stage=input_contract "
                "block=%zu terminator_kind=%u\n",
                trace_core != NULL && trace_core->name != NULL
                    ? trace_core->name : "<unknown>",
                SIZE_MAX, UINT_MAX);
        return false;
    }
    machine->calling_convention =
        minic_core_function_calling_convention(core);
    machine->frame_size = frame->frame_size;
    machine->frame_alignment = 16U;
    machine->has_dynamic_stack = frame->has_dynamic_stack;
    sel.plan = plan;
    sel.xlen = isa.xlen;
    reject_phase = "builder_alloc";
    if (!minic_machine_builder_initialize(&sel.builder, machine)) goto done;
    if (core->block_count > SIZE_MAX / sizeof(*sel.core_blocks)) goto done;
    sel.core_blocks = calloc(core->block_count, sizeof(*sel.core_blocks));
    sel.far_entries = calloc(core->block_count, sizeof(*sel.far_entries));
    sel.reachable = calloc(core->block_count, sizeof(*sel.reachable));
    sel.conditional_fallthroughs = malloc(
        core->block_count * sizeof(*sel.conditional_fallthroughs));
    sel.conditional_others = malloc(
        core->block_count * sizeof(*sel.conditional_others));
    sel.dynamic_check2 = malloc(core->instruction_count *
        sizeof(*sel.dynamic_check2));
    sel.dynamic_commit = malloc(core->instruction_count *
        sizeof(*sel.dynamic_commit));
    sel.dynamic_continue = malloc(core->instruction_count *
        sizeof(*sel.dynamic_continue));
    sel.dynamic_overflow = malloc(core->instruction_count *
        sizeof(*sel.dynamic_overflow));
    sel.asm_goto_continuations = malloc(core->instruction_count *
        sizeof(*sel.asm_goto_continuations));
    if (sel.core_blocks == NULL || sel.far_entries == NULL ||
        sel.reachable == NULL ||
        sel.conditional_fallthroughs == NULL ||
        sel.conditional_others == NULL ||
        (core->instruction_count != 0U &&
         (sel.dynamic_check2 == NULL || sel.dynamic_commit == NULL ||
          sel.dynamic_continue == NULL || sel.dynamic_overflow == NULL ||
          sel.asm_goto_continuations == NULL)))
        goto done;
    for (i = 0U; i < core->instruction_count; ++i) {
        sel.dynamic_check2[i] = SIZE_MAX;
        sel.dynamic_commit[i] = SIZE_MAX;
        sel.dynamic_continue[i] = SIZE_MAX;
        sel.dynamic_overflow[i] = SIZE_MAX;
        sel.asm_goto_continuations[i] = SIZE_MAX;
    }
    if (!minic_core_function_mark_reachable_blocks(
            core, sel.reachable, core->block_count)) goto done;
    reject_phase = "block_map";
    if (!minic_machine_builder_create_block(&sel.builder,
        NULL, false, true, &id) || id != 0U) goto done;
    /* Core entry first (the physical ABI prologue falls through to block1),
     * then the remaining reachable blocks in deterministic Core id order. */
    for (i = 0U; i < core->block_count; ++i) {
        const size_t core_id = i == 0U ? core->entry_block :
            (i <= core->entry_block ? i - 1U : i);
        const MinicCoreBlock *block = &core->blocks[core_id];
        if (!sel.reachable[core_id]) continue;
        reject_block = core_id;
        if (!minic_machine_builder_create_block(&sel.builder,
            NULL, true, true, &sel.far_entries[core_id])) goto done;
        if (!minic_machine_builder_create_block(&sel.builder,
            block->local_label_symbol, block->indirect_entry, false,
            &sel.core_blocks[core_id]) ||
            !minic_machine_builder_mark_core_block(&sel.builder, core_id)) goto done;
        /* Each checked branch reaches only its next-next physical block.
         * The failure island splits check2 and commit but cannot be entered
         * by the successful path; consecutive allocations stay contiguous. */
        {
            size_t j;
            for (j = 0U; j < block->instruction_count; ++j) {
                const MinicCoreInstructionId ins_id = block->instructions[j];
                const MinicCoreInstruction *ins;
                if (ins_id >= core->instruction_count) goto done;
                ins = &core->instructions[ins_id];
                if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
                    const MinicCoreInlineAsmId source_id =
                        ins->value.inline_asm_id;
                    if (source_id >= core->inline_asm_count) goto done;
                    if (core->inline_asms[source_id].is_goto &&
                        !minic_machine_builder_create_block(&sel.builder,
                            NULL, false, true,
                            &sel.asm_goto_continuations[ins_id]))
                        goto done;
                }
                if (ins->kind !=
                    MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC) continue;
                if (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.dynamic_check2[ins_id]) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.dynamic_overflow[ins_id]) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.dynamic_commit[ins_id]) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.dynamic_continue[ins_id]))
                    goto done;
            }
        }
        if (block->terminator.kind ==
            MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH) {
            if (!minic_machine_builder_create_block(&sel.builder,
                NULL, false, true, &sel.conditional_fallthroughs[core_id]) ||
                !minic_machine_builder_create_block(&sel.builder,
                NULL, false, true, &sel.conditional_others[core_id]))
                goto done;
        }
    }
    reject_phase = "prologue";
    reject_block = core->entry_block;
    if (sel.far_entries[core->entry_block] != 1U ||
        !minic_machine_builder_create_block(&sel.builder,
            NULL, true, true, &sel.epilogue_block) ||
        !minic_riscv_machine_select_prologue(&sel.builder, frame, 1U))
        goto done;
    for (i = 0U; i < core->block_count; ++i) {
        const MinicCoreBlock *block = &core->blocks[i];
        size_t j;
        if (!sel.reachable[i]) continue;
        reject_phase = "block_entry";
        reject_block = i;
        reject_terminator_kind = UINT_MAX;
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
                    &core->instructions[instruction_id])) {
                if (getenv("MINIC_RV_MACHINE_TRACE") != NULL)
                    (void)fprintf(stderr,
                        "RV%u_TYPED_REJECT function=%s block=%zu instruction=%u kind=%u\n",
                        sel.xlen, core->name, i, (unsigned)instruction_id,
                        (unsigned)core->instructions[instruction_id].kind);
                goto done;
            }
            minic_machine_builder_clear_origin(&sel.builder);
        }
        reject_phase = "terminator";
        reject_terminator_kind = (unsigned)block->terminator.kind;
        if (!rv_core_terminator(&sel, i)) goto done;
        reject_phase = "overflow_island";
        reject_terminator_kind = UINT_MAX;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId instruction_id =
                block->instructions[j];
            if (sel.dynamic_overflow[instruction_id] == SIZE_MAX) continue;
            if (!minic_machine_builder_position_at(&sel.builder,
                    sel.dynamic_overflow[instruction_id]) ||
                !rv_emit(&sel, MINIC_RISCV_MACHINE_EBREAK, NULL, 0U))
                goto done;
        }
    }
    reject_phase = "epilogue";
    reject_block = SIZE_MAX;
    if (!minic_riscv_machine_select_epilogue(&sel.builder, frame,
            sel.epilogue_block)) goto done;
    /* External/local-label and asm-goto entry roots belong to Core's common
     * closure. A not-yet-migrated multi-entry MachineCFG must be rejected as
     * a whole, never silently drop or falsely accept its reachable blocks. */
    reject_phase = "coverage";
    if (!minic_machine_core_coverage_verify(core, sel.reachable, machine))
        goto done;
    reject_phase = "gnu_policy";
    if (!minic_riscv_machine_gnu_asm_policy(target, &policy)) goto done;
    reject_phase = "gnu_preflight";
    if (!minic_machine_gnu_asm_preflight(machine, &policy)) goto done;
    success = true;
done:
    if (!success && getenv("MINIC_RV_MACHINE_TRACE") != NULL)
        (void)fprintf(stderr,
            "RV%u_TYPED_REJECT_PHASE function=%s stage=%s "
            "block=%zu terminator_kind=%u\n",
            sel.xlen, core->name, reject_phase, reject_block,
            reject_terminator_kind);
    free(sel.core_blocks);
    free(sel.far_entries);
    free(sel.reachable);
    free(sel.conditional_fallthroughs);
    free(sel.conditional_others);
    free(sel.dynamic_check2);
    free(sel.dynamic_commit);
    free(sel.dynamic_continue);
    free(sel.dynamic_overflow);
    free(sel.asm_goto_continuations);
    if (!success) minic_machine_function_destroy(machine);
    return success;
}
