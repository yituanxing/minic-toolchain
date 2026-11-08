#include "target/x86_64/codegen.h"
#include "target/x86_64/machine_core_select.h"
#include "target/x86_64/machine_opcode.h"
#include "target/common/core_call_view.h"
#include "target/common/program_visit.h"
#include "target/common/program_emit.h"
#include "target/common/elf_alias_emit.h"
#include "target/common/global_emit.h"
#include "target/common/module_global_emit.h"
#include "target/common/machine_production_preflight.h"

#include "target/x86_64/global_emit.h"
#include "target/x86_64/abi.h"
#include "target/x86_64/frame.h"
#include "target/x86_64/codegen_options.h"
#include "target/x86_64/machine_target_state.h"
#include "core/value_uses.h"
#include "core/symbol_identity.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void x86_error(MinicDiagnostic *diag, const char *path, const char *message) {
    if (diag != NULL) {
        diag->path = path;
        diag->line = 1U;
        diag->column = 1U;
        (void)snprintf(diag->message, sizeof(diag->message), "%s", message);
    }
}

/* Function codegen consumes the frozen Core type graph.  In particular an
 * enum's arithmetic shape and the pointee of a qualified pointer must not
 * be reconstructed from the source-language type arena. */
static const MinicCoreType *x86_function_type(const MinicCoreFunction *fn,
                                              MinicCoreTypeId id) {
    if (fn == NULL || fn->type_context == NULL) return NULL;
    return minic_core_type_get(fn->type_context,
                               minic_core_type_unqualified(fn->type_context, id));
}

static bool x86_function_kind(const MinicCoreFunction *fn,
                              MinicCoreTypeId id, MinicCoreTypeKind kind) {
    const MinicCoreType *type = x86_function_type(fn, id);
    return type != NULL && type->kind == kind;
}

/* A .head.text address is a symbol identity PLUS a use representation, not
 * a property of an individual GLOBAL_ADDRESS instruction. The same Core
 * value may be used as a displaced physical memory address and as a kernel
 * link-VMA integer. Preserve the distinction across local scalar slots. */
typedef struct X86HeadAddressOrigin {
    MinicCoreGlobalId global;
    bool crossed_vma_cast;
} X86HeadAddressOrigin;

typedef struct X86HeadLocalAddress {
    MinicCoreObjectId object;
    size_t byte_offset;
} X86HeadLocalAddress;

typedef struct X86HeadLocalSlot {
    MinicCoreObjectId object;
    size_t byte_offset;
} X86HeadLocalSlot;

static X86HeadAddressOrigin x86_head_unknown_origin(void) {
    X86HeadAddressOrigin origin = {MINIC_CORE_GLOBAL_INVALID, false};
    return origin;
}

static bool x86_head_same_origin(X86HeadAddressOrigin lhs,
                                 X86HeadAddressOrigin rhs) {
    return lhs.global == rhs.global &&
           (lhs.global == MINIC_CORE_GLOBAL_INVALID ||
            lhs.crossed_vma_cast == rhs.crossed_vma_cast);
}

static bool x86_head_scalar_eight_bytes(const MinicCoreFunction *fn,
                                        MinicCoreTypeId type_id) {
    const MinicCoreType *type = x86_function_type(fn, type_id);
    return type != NULL && type->has_layout && type->size == 8U &&
           (type->kind == MINIC_CORE_TYPE_POINTER ||
            type->kind == MINIC_CORE_TYPE_INTEGER);
}

/* Only a Core object with a statically exact byte offset can be a local
 * provenance slot. A nonconstant index, integer-address cast, or dynamic
 * alias is NOT an object identity. This is intentionally independent of the
 * backend's stack offset and the shared object lifetime/reuse planner. */
static bool x86_head_local_address(
    const MinicCoreFunction *fn, const MinicCoreInstruction *const *defs,
    MinicCoreValueId value, X86HeadLocalAddress *address) {
    size_t offset = 0U, depth;
    if (fn == NULL || address == NULL) return false;
    for (depth = 0U; depth < fn->value_count && value < fn->value_count;
         ++depth) {
        const MinicCoreInstruction *ins = defs[value];
        if (ins == NULL) return false;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
            if (ins->value.object_id >= fn->object_count) return false;
            address->object = ins->value.object_id;
            address->byte_offset = offset;
            return true;
        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
            if (ins->value.field_address.byte_offset > SIZE_MAX - offset)
                return false;
            offset += ins->value.field_address.byte_offset;
            value = ins->value.field_address.base;
            continue;
        case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
            const MinicCoreValueId index = ins->value.pointer_offset.index;
            const MinicCoreInstruction *index_ins =
                index < fn->value_count ? defs[index] : NULL;
            size_t displacement;
            if (index_ins == NULL ||
                index_ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
                index_ins->value.integer_value < 0 ||
                ins->value.pointer_offset.subtract ||
                ins->value.pointer_offset.element_size == 0U ||
                (uint64_t)index_ins->value.integer_value >
                    SIZE_MAX / ins->value.pointer_offset.element_size)
                return false;
            displacement = (size_t)index_ins->value.integer_value *
                           ins->value.pointer_offset.element_size;
            if (displacement > SIZE_MAX - offset) return false;
            offset += displacement;
            value = ins->value.pointer_offset.base;
            continue;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
            if (ins->value.operand >= fn->value_count ||
                !x86_function_kind(fn, ins->core_type_id,
                                   MINIC_CORE_TYPE_POINTER) ||
                !x86_function_kind(fn,
                    fn->values[ins->value.operand].core_type_id,
                    MINIC_CORE_TYPE_POINTER))
                return false;
            value = ins->value.operand;
            continue;
        default:
            return false;
        }
    }
    return false;
}

static size_t x86_head_find_local_slot(const X86HeadLocalSlot *slots,
                                       size_t count,
                                       X86HeadLocalAddress address) {
    size_t i;
    for (i = 0U; i < count; ++i)
        if (slots[i].object == address.object &&
            slots[i].byte_offset == address.byte_offset)
            return i;
    return SIZE_MAX;
}

/* The entire Core object must be private before any of its pointer-shaped
 * slots can carry a proof. This predicate is applied to EVERY SSA use of an
 * object-derived address, including terminators and CALL/ASM inputs. A root
 * consumed as a value rather than a direct local memory address escapes. */
typedef struct X86HeadLocalEscapeCheck {
    const MinicCoreFunction *fn;
    const MinicCoreInstruction *const *defs;
    const MinicCoreInstruction *consumer;
    bool *private_objects;
} X86HeadLocalEscapeCheck;

static bool x86_head_check_local_address_use(MinicCoreValueId value,
                                              void *opaque) {
    X86HeadLocalEscapeCheck *check = (X86HeadLocalEscapeCheck *)opaque;
    X86HeadLocalAddress address;
    const MinicCoreInstruction *ins = check->consumer;
    bool allowed = false;
    if (!x86_head_local_address(check->fn, check->defs, value, &address))
        return true;
    if (ins != NULL) {
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
            allowed = ins->value.field_address.base == value &&
                      x86_head_local_address(check->fn, check->defs,
                                             ins->result, &address);
            break;
        case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
            allowed = ins->value.pointer_offset.base == value &&
                      x86_head_local_address(check->fn, check->defs,
                                             ins->result, &address);
            break;
        case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
            allowed = ins->value.operand == value &&
                x86_function_kind(check->fn, ins->core_type_id,
                                  MINIC_CORE_TYPE_POINTER) &&
                x86_head_local_address(check->fn, check->defs,
                                       ins->result, &address);
            break;
        case MINIC_CORE_INSTRUCTION_LOAD:
            allowed = ins->value.load.address == value;
            break;
        case MINIC_CORE_INSTRUCTION_STORE:
            allowed = ins->value.store.address == value &&
                      ins->value.store.stored_value != value;
            break;
        default:
            break;
        }
    }
    if (!allowed) check->private_objects[address.object] = false;
    return true;
}

static bool x86_head_local_slots_private(
    const MinicCoreFunction *fn, const MinicCoreInstruction *const *defs,
    const bool *reachable, bool *private_objects) {
    X86HeadLocalEscapeCheck check = {fn, defs, NULL, private_objects};
    size_t i, j;
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        /* Computed goto and asm goto create edges outside the two explicit
         * direct-branch terminators; no exact predecessor join is available. */
        if (block->has_terminator &&
            block->terminator.kind == MINIC_CORE_TERMINATOR_INDIRECT_BRANCH) {
            (void)memset(private_objects, 0,
                         fn->object_count * sizeof(*private_objects));
        }
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            if (id >= fn->instruction_count) return false;
            ins = &fn->instructions[id];
            if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
                ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
                const MinicCoreInlineAsmId asm_id =
                    ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM
                    ? ins->value.structured_inline_asm.inline_asm_id
                    : ins->value.inline_asm_id;
                if (asm_id >= fn->inline_asm_count) return false;
                if (fn->inline_asms[asm_id].is_goto)
                    (void)memset(private_objects, 0,
                                 fn->object_count * sizeof(*private_objects));
            }
            if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT &&
                ins->value.parameter_object.object_id < fn->object_count)
                private_objects[ins->value.parameter_object.object_id] = false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD &&
                ins->value.record_load.destination_object < fn->object_count)
                private_objects[ins->value.record_load.destination_object] = false;
            if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                 ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL)) {
                const MinicCoreObjectId result =
                    ins->kind == MINIC_CORE_INSTRUCTION_CALL
                    ? ins->value.call.result_object
                    : ins->value.indirect_call.result_object;
                size_t begin = ins->kind == MINIC_CORE_INSTRUCTION_CALL
                    ? ins->value.call.argument_begin
                    : ins->value.indirect_call.argument_begin;
                size_t count = ins->kind == MINIC_CORE_INSTRUCTION_CALL
                    ? ins->value.call.argument_count
                    : ins->value.indirect_call.argument_count;
                size_t k;
                if (result < fn->object_count) private_objects[result] = false;
                if (begin > fn->call_argument_count ||
                    count > fn->call_argument_count - begin) return false;
                for (k = 0U; k < count; ++k) {
                    const MinicCoreCallArgument *arg =
                        &fn->call_arguments[begin + k];
                    if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT &&
                        arg->value.object_id < fn->object_count)
                        private_objects[arg->value.object_id] = false;
                }
            }
            check.consumer = ins;
            if (!minic_core_visit_instruction_value_uses(
                    fn, ins, x86_head_check_local_address_use, &check))
                return false;
        }
        check.consumer = NULL;
        if (block->has_terminator &&
            !minic_core_visit_terminator_value_uses(
                &block->terminator, x86_head_check_local_address_use, &check))
            return false;
        if (block->has_terminator &&
            block->terminator.kind == MINIC_CORE_TERMINATOR_RETURN &&
            block->terminator.return_object < fn->object_count)
            private_objects[block->terminator.return_object] = false;
    }
    return true;
}

/* Core has no PHI instruction. Cross-block scalar transport is via actual
 * Core objects, so only explicit branch successors participate in the
 * local-memory fixed point. Source-label and indirect-jump objects already
 * lost their private proof in x86_head_local_slots_private. */
static bool x86_head_branch_targets(const MinicCoreBlock *block,
                                    MinicCoreBlockId candidate) {
    if (block == NULL || !block->has_terminator) return false;
    if (block->terminator.kind == MINIC_CORE_TERMINATOR_BRANCH)
        return block->terminator.branch_target == candidate;
    if (block->terminator.kind == MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
        return block->terminator.conditional.when_true == candidate ||
               block->terminator.conditional.when_false == candidate;
    return false;
}

static X86HeadAddressOrigin x86_head_origin_from_value(
    const MinicCoreFunction *fn, const X86HeadAddressOrigin *origins,
    MinicCoreValueId value) {
    return value < fn->value_count ? origins[value] : x86_head_unknown_origin();
}

static X86HeadAddressOrigin x86_head_origin_from_instruction(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const X86HeadAddressOrigin *origins) {
    X86HeadAddressOrigin origin = x86_head_unknown_origin();
    if (ins == NULL) return origin;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (ins->value.global_id < fn->global_count)
            origin.global = ins->value.global_id;
        break;
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        origin = x86_head_origin_from_value(
            fn, origins, ins->value.field_address.base);
        break;
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        origin = x86_head_origin_from_value(
            fn, origins, ins->value.pointer_offset.base);
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        origin = x86_head_origin_from_value(fn, origins, ins->value.operand);
        if (origin.global != MINIC_CORE_GLOBAL_INVALID &&
            ins->value.operand < fn->value_count &&
            x86_function_kind(fn,
                              fn->values[ins->value.operand].core_type_id,
                              MINIC_CORE_TYPE_POINTER) &&
            x86_function_kind(fn, ins->core_type_id,
                              MINIC_CORE_TYPE_INTEGER))
            origin.crossed_vma_cast = true;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        origin = x86_head_origin_from_value(fn, origins, ins->value.operand);
        break;
    default:
        break;
    }
    return origin;
}

/* The byte range of a direct Core STORE. If it intersects a tracked local
 * pointer slot but is not the exact eight-byte scalar write, that slot's
 * symbolic value becomes unknown at this program point. */
static size_t x86_head_stored_bytes(const MinicCoreFunction *fn,
                                    const MinicCoreInstruction *ins) {
    const MinicCoreValueId source = ins->value.store.stored_value;
    const MinicCoreType *type;
    if (source >= fn->value_count) return 0U;
    type = x86_function_type(fn, fn->values[source].core_type_id);
    return type != NULL && type->has_layout ? type->size : 0U;
}

static bool x86_head_ranges_overlap(size_t a, size_t a_size,
                                    size_t b, size_t b_size) {
    if (a_size == 0U || b_size == 0U ||
        a_size > SIZE_MAX - a || b_size > SIZE_MAX - b)
        return true;
    return a < b + b_size && b < a + a_size;
}

static void x86_head_store_local_origin(
    const MinicCoreFunction *fn, const MinicCoreInstruction *const *defs,
    const MinicCoreInstruction *ins, const bool *private_objects,
    const X86HeadLocalSlot *slots, size_t slot_count,
    const X86HeadAddressOrigin *origins, X86HeadAddressOrigin *state) {
    X86HeadLocalAddress target;
    size_t width, i;
    if (!x86_head_local_address(fn, defs, ins->value.store.address, &target) ||
        !private_objects[target.object]) return;
    width = x86_head_stored_bytes(fn, ins);
    for (i = 0U; i < slot_count; ++i) {
        if (slots[i].object != target.object ||
            !x86_head_ranges_overlap(target.byte_offset, width,
                                     slots[i].byte_offset, 8U))
            continue;
        state[i] = width == 8U && target.byte_offset == slots[i].byte_offset &&
                   ins->value.store.stored_value < fn->value_count &&
                   x86_head_scalar_eight_bytes(fn,
                       fn->values[ins->value.store.stored_value].core_type_id)
                   ? origins[ins->value.store.stored_value]
                   : x86_head_unknown_origin();
    }
}

/* Both head-address passes inspect exactly the same memory-address roles.
 * Aggregate COPY and vector storage have two independently rebased
 * addresses, unlike scalar LOAD/STORE. Keep this Core decoding local to
 * the head-address policy and do not infer pointer provenance through PHI. */
static size_t x86_head_memory_addresses(
    const MinicCoreInstruction *ins, MinicCoreValueId addresses[2]) {
    addresses[0] = MINIC_CORE_VALUE_INVALID;
    addresses[1] = MINIC_CORE_VALUE_INVALID;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_LOAD:
        addresses[0] = ins->value.load.address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_STORE:
        addresses[0] = ins->value.store.address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
        addresses[0] = ins->value.atomic_load.address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        addresses[0] = ins->value.atomic_store.address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        addresses[0] = ins->value.record_load.source_address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        addresses[0] = ins->value.record_copy.destination_address;
        addresses[1] = ins->value.record_copy.source_address;
        return 2U;
    case MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY:
        addresses[0] = ins->value.vector_storage_copy.destination_address;
        addresses[1] = ins->value.vector_storage_copy.source_address;
        return 2U;
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        addresses[0] = ins->value.integer_overflow.result_address;
        return 1U;
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
        addresses[0] = ins->value.operand;
        return 1U;
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        addresses[0] = ins->value.variadic_copy.destination;
        addresses[1] = ins->value.variadic_copy.source;
        return 2U;
    default:
        return 0U;
    }
}

/* A head CALL can transport a physical pointer selected by its caller into
 * another function, where a pointer-to-integer cast observes the wrong VMA.
 * The local-slot proof cannot see across this ABI boundary.  Recognize only
 * the narrow, same-module parameter-to-numeric path for a fail-closed gate;
 * a helper that only dereferences the incoming physical pointer remains
 * legal.  Do not infer a positive interprocedural alias contract here. */
static bool x86_head_callee_parameter_numeric(
    const MinicCoreFunction *callee, size_t parameter_index,
    bool *unproven, bool *returns_pointer) {
    const MinicCoreInstruction **defs = NULL;
    bool *derived = NULL, *derived_objects = NULL;
    bool changed, numeric = false;
    size_t i;
    if (unproven != NULL) *unproven = false;
    if (returns_pointer != NULL) *returns_pointer = false;
    if (callee == NULL ||
        callee->value_count > SIZE_MAX / sizeof(*derived) ||
        callee->value_count > SIZE_MAX / sizeof(*defs) ||
        callee->object_count > SIZE_MAX / sizeof(*derived_objects)) {
        if (unproven != NULL) *unproven = true;
        return false;
    }
    derived = calloc(callee->value_count == 0U ? 1U : callee->value_count,
                     sizeof(*derived));
    derived_objects = calloc(callee->object_count == 0U ? 1U :
                             callee->object_count, sizeof(*derived_objects));
    defs = calloc(callee->value_count == 0U ? 1U : callee->value_count,
                  sizeof(*defs));
    if (derived == NULL || derived_objects == NULL || defs == NULL) {
        if (unproven != NULL) *unproven = true;
        goto done;
    }
    for (i = 0U; i < callee->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &callee->instructions[i];
        if (ins->result < callee->value_count) defs[ins->result] = ins;
        if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER &&
            ins->value.parameter_index == parameter_index &&
            ins->result < callee->value_count &&
            x86_function_kind(callee, ins->core_type_id,
                              MINIC_CORE_TYPE_POINTER))
            derived[ins->result] = true;
    }
    do {
        changed = false;
        for (i = 0U; i < callee->instruction_count; ++i) {
            const MinicCoreInstruction *ins = &callee->instructions[i];
            MinicCoreValueId source = MINIC_CORE_VALUE_INVALID;
            X86HeadLocalAddress address;
            if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
                ins->value.store.stored_value < callee->value_count &&
                derived[ins->value.store.stored_value] &&
                x86_head_local_address(callee, defs, ins->value.store.address,
                                       &address) &&
                !derived_objects[address.object]) {
                derived_objects[address.object] = true;
                changed = true;
            }
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                ins->result < callee->value_count && !derived[ins->result] &&
                x86_function_kind(callee, ins->core_type_id,
                                  MINIC_CORE_TYPE_POINTER) &&
                x86_head_local_address(callee, defs, ins->value.load.address,
                                       &address) && derived_objects[address.object]) {
                derived[ins->result] = true;
                changed = true;
            }
            if (ins->kind == MINIC_CORE_INSTRUCTION_FIELD_ADDRESS)
                source = ins->value.field_address.base;
            else if (ins->kind == MINIC_CORE_INSTRUCTION_POINTER_OFFSET)
                source = ins->value.pointer_offset.base;
            else if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_BITCAST)
                source = ins->value.operand;
            if (source >= callee->value_count || !derived[source]) continue;
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_BITCAST &&
                x86_function_kind(callee, ins->core_type_id,
                                  MINIC_CORE_TYPE_INTEGER)) {
                numeric = true;
                goto done;
            }
            if (ins->result < callee->value_count &&
                !derived[ins->result] &&
                x86_function_kind(callee, ins->core_type_id,
                                  MINIC_CORE_TYPE_POINTER)) {
                derived[ins->result] = true;
                changed = true;
            }
        }
    } while (changed);
    /* A pointer returned unchanged can be interpreted numerically by the
     * caller even though the callee itself never casts it.  A physical
     * caller argument must not silently become a link-VMA integer across
     * that ABI round-trip. */
    if (returns_pointer != NULL) {
        for (i = 0U; i < callee->block_count; ++i) {
            const MinicCoreBlock *block = &callee->blocks[i];
            const MinicCoreValueId value = block->terminator.return_value;
            if (block->has_terminator &&
                block->terminator.kind == MINIC_CORE_TERMINATOR_RETURN &&
                value < callee->value_count && derived[value]) {
                *returns_pointer = true;
                break;
            }
        }
    }
done:
    free(defs);
    free(derived);
    free(derived_objects);
    return numeric;
}

static bool x86_head_call_numeric_alias(
    const MinicCoreFunction *caller,
    const MinicCoreInstruction *call,
    const bool *reachable,
    const MinicCoreFunction *module_functions,
    size_t module_function_count,
    const X86HeadAddressOrigin *origins,
    const bool *direct) {
    const MinicCoreCallee *callee;
    size_t i, j, begin, count;
    if (caller == NULL || call == NULL || module_functions == NULL ||
        call->kind != MINIC_CORE_INSTRUCTION_CALL ||
        call->value.call.callee_id >= caller->callee_count)
        return false;
    callee = &caller->callees[call->value.call.callee_id];
    begin = call->value.call.argument_begin;
    count = call->value.call.argument_count;
    if (begin > caller->call_argument_count ||
        count > caller->call_argument_count - begin) return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreCallArgument *arg = &caller->call_arguments[begin + i];
        X86HeadAddressOrigin origin;
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= caller->value_count ||
            !x86_function_kind(caller,
                caller->values[arg->value.value_id].core_type_id,
                MINIC_CORE_TYPE_POINTER)) continue;
        origin = origins[arg->value.value_id];
        if (origin.global >= caller->global_count ||
            !direct[origin.global] || origin.crossed_vma_cast) continue;
        for (j = 0U; j < module_function_count; ++j) {
            const MinicCoreFunction *target = &module_functions[j];
            bool unproven = false, returns_pointer = false;
            bool caller_observes_numeric = false;
            size_t block_index, ins_index;
            if (target->name == NULL || callee->name == NULL ||
                target->name_length != callee->name_length ||
                memcmp(target->name, callee->name,
                       callee->name_length) != 0) continue;
            if (x86_head_callee_parameter_numeric(target, i, &unproven,
                                                  &returns_pointer) ||
                unproven) return true;
            if (!returns_pointer || call->result >= caller->value_count ||
                reachable == NULL) continue;
            for (block_index = 0U; block_index < caller->block_count;
                 ++block_index) {
                const MinicCoreBlock *block = &caller->blocks[block_index];
                if (!reachable[block_index]) continue;
                for (ins_index = 0U; ins_index < block->instruction_count;
                     ++ins_index) {
                    const MinicCoreInstructionId id =
                        block->instructions[ins_index];
                    const MinicCoreInstruction *ins;
                    if (id >= caller->instruction_count) return true;
                    ins = &caller->instructions[id];
                    if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_BITCAST &&
                        ins->value.operand == call->result &&
                        x86_function_kind(caller, ins->core_type_id,
                                          MINIC_CORE_TYPE_INTEGER)) {
                        caller_observes_numeric = true;
                        break;
                    }
                }
                if (caller_observes_numeric) break;
            }
            if (caller_observes_numeric) return true;
        }
    }
    return false;
}

/* Compute ONE checked early-address plan for all machine consumers. The
 * direct/global, scalar-cast and memory-rebase arrays are three different
 * views of this same source-of-truth, not three independently guessed role
 * analyses. Private Core object slots carry origins across STORE/LOAD and
 * explicit CFG joins; unknown aliases do not manufacture global identities.
 * Numeric link-VMA conversions stay use-site effects, never change the
 * GLOBAL_ADDRESS producer shared with a direct physical memory consumer. */
static bool x86_head_plan_address_roles(
    const MinicCoreFunction *fn, const bool *reachable, bool *direct,
    MinicCoreGlobalId *vma_cast_globals,
    MinicCoreGlobalId *memory_rebase_globals, bool *unproven_local,
    bool *unproven_call, const MinicCoreFunction *module_functions,
    size_t module_function_count) {
    const MinicCoreInstruction **defs = NULL;
    X86HeadAddressOrigin *origins = NULL, *previous_origins = NULL;
    X86HeadAddressOrigin *out_states = NULL, *scratch = NULL;
    X86HeadLocalSlot *slots = NULL;
    bool *private_objects = NULL, *global_object_writes = NULL;
    bool *global_slot_writes = NULL;
    size_t slot_count = 0U, pass = 0U, pass_limit, i, j;
    bool success = false, changed;
    if (unproven_local != NULL) *unproven_local = false;
    if (unproven_call != NULL) *unproven_call = false;
    if (fn == NULL || reachable == NULL || direct == NULL ||
        vma_cast_globals == NULL || memory_rebase_globals == NULL ||
        fn->value_count > SIZE_MAX / sizeof(*defs) ||
        fn->value_count > SIZE_MAX / sizeof(*origins) ||
        fn->object_count > SIZE_MAX / sizeof(*private_objects) ||
        fn->object_count > SIZE_MAX / sizeof(*global_object_writes) ||
        fn->instruction_count > SIZE_MAX / sizeof(*slots)) return false;
    defs = (const MinicCoreInstruction **)calloc(
        fn->value_count == 0U ? 1U : fn->value_count, sizeof(*defs));
    origins = (X86HeadAddressOrigin *)malloc(
        (fn->value_count == 0U ? 1U : fn->value_count) * sizeof(*origins));
    previous_origins = (X86HeadAddressOrigin *)malloc(
        (fn->value_count == 0U ? 1U : fn->value_count) *
        sizeof(*previous_origins));
    slots = (X86HeadLocalSlot *)malloc(
        (fn->instruction_count == 0U ? 1U : fn->instruction_count) *
        sizeof(*slots));
    private_objects = (bool *)malloc(
        (fn->object_count == 0U ? 1U : fn->object_count) *
        sizeof(*private_objects));
    global_object_writes = (bool *)calloc(
        fn->object_count == 0U ? 1U : fn->object_count,
        sizeof(*global_object_writes));
    global_slot_writes = (bool *)calloc(
        fn->instruction_count == 0U ? 1U : fn->instruction_count,
        sizeof(*global_slot_writes));
    if (defs == NULL || origins == NULL || previous_origins == NULL ||
        slots == NULL || private_objects == NULL ||
        global_object_writes == NULL || global_slot_writes == NULL) goto done;
    for (i = 0U; i < fn->value_count; ++i)
        previous_origins[i] = x86_head_unknown_origin();
    for (i = 0U; i < fn->object_count; ++i) private_objects[i] = true;
    for (i = 0U; i < fn->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &fn->instructions[i];
        if (ins->result != MINIC_CORE_VALUE_INVALID &&
            ins->result < fn->value_count)
            defs[ins->result] = ins;
    }
    if (!x86_head_local_slots_private(fn, defs, reachable, private_objects))
        goto done;
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            X86HeadLocalAddress address;
            const MinicCoreInstruction *ins;
            const MinicCoreType *object_type;
            if (id >= fn->instruction_count) goto done;
            ins = &fn->instructions[id];
            if (ins->kind != MINIC_CORE_INSTRUCTION_LOAD ||
                !x86_head_scalar_eight_bytes(fn, ins->core_type_id) ||
                !x86_head_local_address(fn, defs, ins->value.load.address,
                                        &address) ||
                !private_objects[address.object])
                continue;
            object_type = x86_function_type(
                fn, fn->objects[address.object].core_type_id);
            if (object_type == NULL || !object_type->has_layout ||
                address.byte_offset > object_type->size ||
                object_type->size - address.byte_offset < 8U ||
                x86_head_find_local_slot(slots, slot_count, address) != SIZE_MAX)
                continue;
            slots[slot_count].object = address.object;
            slots[slot_count].byte_offset = address.byte_offset;
            ++slot_count;
        }
    }

    if (slot_count != 0U) {
        if (fn->block_count > SIZE_MAX / slot_count ||
            fn->block_count * slot_count > SIZE_MAX / sizeof(*out_states) ||
            fn->block_count > (SIZE_MAX - 8U) / 4U)
            goto done;
        out_states = (X86HeadAddressOrigin *)malloc(
            fn->block_count * slot_count * sizeof(*out_states));
        scratch = (X86HeadAddressOrigin *)malloc(
            slot_count * sizeof(*scratch));
        if (out_states == NULL || scratch == NULL) goto done;
        for (i = 0U; i < fn->block_count * slot_count; ++i)
            out_states[i] = x86_head_unknown_origin();
    }
    pass_limit = fn->block_count <= (SIZE_MAX - 8U) / 4U
        ? fn->block_count * 4U + 8U : SIZE_MAX;
    do {
        changed = false;
        for (i = 0U; i < fn->value_count; ++i)
            origins[i] = previous_origins[i];
        for (i = 0U; i < fn->block_count; ++i) {
            const MinicCoreBlock *block = &fn->blocks[i];
            bool has_predecessor = false;
            size_t pred, k;
            if (!reachable[i]) continue;
            if (slot_count != 0U) {
                for (k = 0U; k < slot_count; ++k)
                    scratch[k] = x86_head_unknown_origin();
                /* A label can be independently reachable due to asm-goto.
                 * It has an UNKNOWN initial environment unless all explicit
                 * edges agree on one stored origin. */
                for (pred = 0U; pred < fn->block_count; ++pred) {
                    const X86HeadAddressOrigin *candidate;
                    if (!reachable[pred] ||
                        !x86_head_branch_targets(&fn->blocks[pred],
                                                 (MinicCoreBlockId)i))
                        continue;
                    candidate = out_states + pred * slot_count;
                    if (!has_predecessor) {
                        (void)memcpy(scratch, candidate,
                                     slot_count * sizeof(*scratch));
                        has_predecessor = true;
                    } else for (k = 0U; k < slot_count; ++k)
                        if (!x86_head_same_origin(scratch[k], candidate[k]))
                            scratch[k] = x86_head_unknown_origin();
                }
                if (i == fn->entry_block ||
                    block->indirect_entry ||
                    block->local_label_symbol != NULL) {
                    /* Backedges cannot bootstrap an uninitialized slot, and
                     * a source-visible label can also have an opaque asm or
                     * computed-goto predecessor not present in the ordinary
                     * CFG successor arrays. */
                    for (k = 0U; k < slot_count; ++k)
                        scratch[k] = x86_head_unknown_origin();
                }
            }
            for (j = 0U; j < block->instruction_count; ++j) {
                const MinicCoreInstructionId id = block->instructions[j];
                const MinicCoreInstruction *ins;
                X86HeadAddressOrigin origin;
                if (id >= fn->instruction_count) goto done;
                ins = &fn->instructions[id];
                origin = x86_head_origin_from_instruction(fn, ins, origins);
                if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                    slot_count != 0U &&
                    x86_head_scalar_eight_bytes(fn, ins->core_type_id)) {
                    X86HeadLocalAddress address;
                    if (x86_head_local_address(fn, defs,
                            ins->value.load.address, &address) &&
                        private_objects[address.object]) {
                        k = x86_head_find_local_slot(slots, slot_count, address);
                        if (k != SIZE_MAX) origin = scratch[k];
                    }
                }
                if (ins->result != MINIC_CORE_VALUE_INVALID &&
                    ins->result < fn->value_count)
                    origins[ins->result] = origin;
                if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
                    slot_count != 0U)
                    x86_head_store_local_origin(fn, defs, ins, private_objects,
                                                slots, slot_count, origins, scratch);
            }
            if (slot_count != 0U) {
                for (k = 0U; k < slot_count; ++k)
                    if (!x86_head_same_origin(
                            out_states[i * slot_count + k], scratch[k])) {
                        out_states[i * slot_count + k] = scratch[k];
                        changed = true;
                    }
            }
        }
        /* The Core verifier constrains value definitions but must not rely
         * on block-id order. Stabilize the per-SSA facts separately from
         * object-slot out states, so reversed block schedules and loops do
         * not silently keep a stale unknown (or stale known) provenance. */
        for (i = 0U; i < fn->value_count; ++i) {
            if (!x86_head_same_origin(previous_origins[i], origins[i])) {
                previous_origins[i] = origins[i];
                changed = true;
            }
        }
        ++pass;
    } while (changed && pass < pass_limit);
    if (changed) goto done;

    /* First determine the genuinely proven physical memory roles. An
     * unresolved local value is hazardous only if one of its possible
     * global sources is already selected physical by a known consumer. */
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            MinicCoreValueId addresses[2];
            size_t k, count;
            if (id >= fn->instruction_count) goto done;
            ins = &fn->instructions[id];
            count = x86_head_memory_addresses(ins, addresses);
            for (k = 0U; k < count; ++k) {
                const X86HeadAddressOrigin origin =
                    x86_head_origin_from_value(fn, origins, addresses[k]);
                if (origin.global < fn->global_count)
                    direct[origin.global] = true;
            }
        }
    }

    /* Refuse the observed cross-function VMA wrongcode before selecting any
     * machine instructions.  This is intentionally scoped to a callee whose
     * matching pointer parameter is proven to be interpreted numerically;
     * blindly rejecting every head pointer argument would also reject
     * legitimate early physical-dereference helpers. */
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            if (id >= fn->instruction_count) goto done;
            if (x86_head_call_numeric_alias(fn, &fn->instructions[id],
                    reachable, module_functions, module_function_count,
                    origins, direct)) {
                if (unproven_call != NULL) *unproven_call = true;
                goto done;
            }
        }
    }

    /* An escaped object or conflicting predecessor is NOT harmless merely
     * because its local LOAD has an unknown origin: a different direct use
     * of the same global may already select a physical pointer producer.
     * Refuse to emit an ambiguous pointer-to-integer or dereference rather
     * than silently return a displaced physical address as a link VMA.
     * The only positive transport contract is a private, exact, consistently
     * sourced scalar slot. This conservative gate protects aliasing/joins
     * until an explicit Core memory-provenance representation exists. */
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            X86HeadLocalAddress address;
            X86HeadAddressOrigin source;
            size_t k;
            if (id >= fn->instruction_count) goto done;
            ins = &fn->instructions[id];
            if (ins->kind != MINIC_CORE_INSTRUCTION_STORE ||
                !x86_head_local_address(fn, defs, ins->value.store.address,
                                        &address))
                continue;
            source = x86_head_origin_from_value(
                fn, origins, ins->value.store.stored_value);
            if (source.global >= fn->global_count || !direct[source.global] ||
                source.crossed_vma_cast)
                continue;
            global_object_writes[address.object] = true;
            k = x86_head_find_local_slot(slots, slot_count, address);
            if (k != SIZE_MAX) global_slot_writes[k] = true;
        }
    }
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            X86HeadLocalAddress address;
            size_t k;
            if (id >= fn->instruction_count) goto done;
            ins = &fn->instructions[id];
            if (ins->kind != MINIC_CORE_INSTRUCTION_LOAD ||
                ins->result >= fn->value_count ||
                !x86_head_scalar_eight_bytes(fn, ins->core_type_id) ||
                origins[ins->result].global != MINIC_CORE_GLOBAL_INVALID ||
                !x86_head_local_address(fn, defs, ins->value.load.address,
                                        &address))
                continue;
            k = x86_head_find_local_slot(slots, slot_count, address);
            if ((!private_objects[address.object] &&
                 global_object_writes[address.object]) ||
                (k != SIZE_MAX && global_slot_writes[k])) {
                if (unproven_local != NULL) *unproven_local = true;
                goto done;
            }
        }
    }

    /* Only after the global data-use census is complete can we choose the
     * needed representation of a scalar cast and an eventual dereference. */
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            MinicCoreValueId addresses[2];
            X86HeadAddressOrigin origin;
            size_t k, count;
            if (id >= fn->instruction_count) goto done;
            ins = &fn->instructions[id];
            count = x86_head_memory_addresses(ins, addresses);
            switch (ins->kind) {
            case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
                if (ins->result < fn->value_count &&
                    ins->value.operand < fn->value_count &&
                    x86_function_kind(fn,
                                      fn->values[ins->value.operand].core_type_id,
                                      MINIC_CORE_TYPE_POINTER) &&
                    x86_function_kind(fn, ins->core_type_id,
                                      MINIC_CORE_TYPE_INTEGER)) {
                    origin = x86_head_origin_from_value(
                        fn, origins, ins->value.operand);
                    if (origin.global < fn->global_count &&
                        direct[origin.global] && !origin.crossed_vma_cast)
                        vma_cast_globals[ins->result] = origin.global;
                }
                break;
            default:
                break;
            }
            for (k = 0U; k < count; ++k) {
                MinicCoreValueId address = addresses[k];
                origin = x86_head_origin_from_value(fn, origins, address);
                if (address < fn->value_count && origin.global < fn->global_count &&
                    direct[origin.global] && origin.crossed_vma_cast)
                    memory_rebase_globals[address] = origin.global;
            }
        }
    }
    success = true;
done:
    free(defs);
    free(origins);
    free(previous_origins);
    free(out_states);
    free(scratch);
    free(slots);
    free(private_objects);
    free(global_object_writes);
    free(global_slot_writes);
    return success;
}

static bool x86_no_sse_fp_type(const MinicCoreFunction *fn,
                              MinicCoreTypeId id) {
    return x86_function_kind(fn, id, MINIC_CORE_TYPE_FLOAT);
}

/* -mno-sse is a code-generation contract, not permission to emit an
 * instruction that will #UD before the kernel enables FPU/XMM state. Reject
 * reachable floating ABI/operations until x86 has a genuine soft-FP path.
 * Do not reject unused floating declarations or unreachable lexical arms. */
static bool x86_no_sse_function_safe(
    const MinicCoreFunction *fn, const bool *reachable) {
    size_t i, j;
    if (fn == NULL || fn->type_context == NULL ||
        (minic_core_function_parameter_count(fn) != 0U && minic_core_function_parameter_types(fn) == NULL))
        return false;
    if (x86_no_sse_fp_type(fn, minic_core_function_return_type(fn))) return false;
    for (i = 0U; i < minic_core_function_parameter_count(fn); ++i)
        if (x86_no_sse_fp_type(fn, minic_core_function_parameter_types(fn)[i]))
            return false;
    for (i = 0U; i < fn->block_count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        if (!reachable[i]) continue;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId id = block->instructions[j];
            const MinicCoreInstruction *ins;
            if (id >= fn->instruction_count) return false;
            ins = &fn->instructions[id];
            if (x86_no_sse_fp_type(fn, ins->core_type_id) ||
                (ins->result < fn->value_count &&
                 x86_no_sse_fp_type(fn,
                     fn->values[ins->result].core_type_id)))
                return false;
            if (ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) {
                MinicCoreCallView call;
                size_t k;
                if (!minic_core_call_view_build(fn, ins, &call) ||
                    (call.parameter_count != 0U &&
                     call.core_parameter_type_ids == NULL) ||
                    x86_no_sse_fp_type(fn, call.core_return_type_id))
                    return false;
                for (k = 0U; k < call.parameter_count; ++k)
                    if (x86_no_sse_fp_type(fn,
                            call.core_parameter_type_ids[k]))
                        return false;
            }
        }
    }
    return true;
}

static bool x86_emit_function(FILE *out,
                              const MinicTargetInfo *target,
                              const MinicCoreModuleFunctionSymbol *module_decl,
                              const MinicCoreModule *module,
                              const MinicCoreFunction *fn, const MinicCoreFunctionFacts *facts,
                              const MinicCoreObjectStoragePlan *storage,
                              size_t function_id, bool function_sections,
                              bool inherited_head_address_context,
                              const MinicCoreFunction *module_functions,
                              size_t module_function_count,
                              MinicDiagnostic *diag, const char *path) {
    MinicX86Frame frame = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy machine_policy = {0};
    MinicMachineGNUAsmPrepared *prepared_body = NULL;
    const char *name = module_decl->name;
    const char *section_name = module_decl->section_name;
    const size_t explicit_alignment = module_decl->explicit_alignment;
    const bool is_internal = module_decl->is_internal;
    const bool is_weak = module_decl->is_weak;
    const bool is_constructor = module_decl->is_constructor;
    const bool *reachable = NULL;
    bool *owned_reachable = NULL;
    bool *head_direct_data_globals = NULL;
    MinicCoreGlobalId *head_vma_cast_globals = NULL;
    MinicCoreGlobalId *head_memory_rebase_globals = NULL;
    const size_t *function_value_use_counts = NULL;
    size_t *owned_value_use_counts = NULL;
    bool success = false;
    {
        const bool named = name != NULL && name[0] != '\0';
        const bool verified = named &&
            (minic_core_function_facts_verified(facts, fn) ||
             minic_core_function_verify(fn));
        const bool planned = verified &&
            minic_x86_64_frame_init_with_storage(
                target, fn, facts, storage, &frame);
        if (!planned) {
            if (getenv("MINIC_M1_TRACE") != NULL) {
                (void)fprintf(stderr,
                    "MINIC_X86_FRAME_REJECT function=%s stage=%s "
                    "values=%zu params=%zu blocks=%zu\n",
                    named ? name : "(unnamed)",
                    !named ? "invalid-name" :
                    !verified ? "core-verification" : "abi-or-frame",
                    fn->value_count, minic_core_function_parameter_count(fn), fn->block_count);
                if (verified)
                    (void)minic_core_function_dump(stderr, fn);
            }
            minic_x86_64_frame_destroy(&frame);
            x86_error(diag, path,
                      "x86-64 cannot allocate or verify Core function frame");
            return false;
        }
    }
    frame.module = module;
    if (minic_core_function_facts_verified(facts, fn)) {
        reachable = facts->reachable_blocks;
        function_value_use_counts = facts->value_use_counts;
    } else {
        owned_reachable = calloc(fn->block_count == 0U ? 1U : fn->block_count,
                                 sizeof(*owned_reachable));
        if (owned_reachable != NULL &&
            minic_core_function_mark_reachable_blocks(
                fn, owned_reachable, fn->block_count))
            reachable = owned_reachable;
    }
    if (reachable == NULL) {
        x86_error(diag, path, "x86-64 cannot discover reachable Core blocks");
        goto done;
    }
    if ((minic_x86_64_target_codegen_options(target) & MINIC_X86_CODEGEN_NO_SSE) != 0U &&
        !x86_no_sse_function_safe(fn, reachable)) {
        x86_error(diag, path,
                  "x86-64 -mno-sse rejects reachable floating ABI or operation");
        goto done;
    }
    if (function_value_use_counts == NULL) {
        if (fn->value_count > SIZE_MAX / sizeof(*owned_value_use_counts) ||
            (owned_value_use_counts = calloc(
                fn->value_count == 0U ? 1U : fn->value_count,
                sizeof(*owned_value_use_counts))) == NULL) {
            x86_error(diag, path, "x86-64 cannot allocate Core value-use census");
            goto done;
        }
        /* Standalone function entry is not owned by ProgramVisitor. */
        if (!minic_core_function_value_use_counts(
                fn, owned_value_use_counts, fn->value_count)) {
            x86_error(diag, path, "x86-64 cannot count Core function SSA uses");
            goto done;
        }
        function_value_use_counts = owned_value_use_counts;
    }
    frame.function_value_use_counts = function_value_use_counts;
    frame.kernel_head_link_addresses = !target->position_independent &&
        (inherited_head_address_context ||
         (section_name != NULL && strcmp(section_name, ".head.text") == 0));
    if (frame.kernel_head_link_addresses) {
        size_t i;
        if (fn->value_count > SIZE_MAX / sizeof(*head_memory_rebase_globals) ||
            fn->value_count > SIZE_MAX / sizeof(*head_vma_cast_globals)) {
            x86_error(diag, path, "x86-64 early head address plan exceeds memory limits");
            goto done;
        }
        head_direct_data_globals = (bool *)calloc(
            fn->global_count == 0U ? 1U : fn->global_count,
            sizeof(*head_direct_data_globals));
        head_vma_cast_globals = (MinicCoreGlobalId *)malloc(
            (fn->value_count == 0U ? 1U : fn->value_count) *
            sizeof(*head_vma_cast_globals));
        head_memory_rebase_globals = (MinicCoreGlobalId *)malloc(
            (fn->value_count == 0U ? 1U : fn->value_count) *
            sizeof(*head_memory_rebase_globals));
        if (head_direct_data_globals == NULL ||
            head_vma_cast_globals == NULL ||
            head_memory_rebase_globals == NULL) {
            x86_error(diag, path, "x86-64 cannot analyze early head data addresses");
            goto done;
        }
        for (i = 0U; i < fn->value_count; ++i) {
            head_vma_cast_globals[i] = MINIC_CORE_GLOBAL_INVALID;
            head_memory_rebase_globals[i] = MINIC_CORE_GLOBAL_INVALID;
        }
        bool unproven_local = false, unproven_call = false;
        if (!x86_head_plan_address_roles(
                fn, reachable, head_direct_data_globals,
                head_vma_cast_globals, head_memory_rebase_globals,
                &unproven_local, &unproven_call,
                module_functions, module_function_count)) {
            x86_error(diag, path, unproven_local
                ? "x86-64 early head unproven local address provenance"
                : unproven_call
                ? "x86-64 early head unproven cross-function numeric pointer provenance"
                : "x86-64 cannot plan early head address roles");
            goto done;
        }
        frame.kernel_head_direct_data_globals = head_direct_data_globals;
        frame.kernel_head_vma_cast_globals = head_vma_cast_globals;
        frame.kernel_head_memory_rebase_globals = head_memory_rebase_globals;
    }
    {
        const MinicMachineProductionSource source = {
            .core = fn, .reachable_blocks = reachable,
            .target = target, .symbol = name
        };
        const bool selected = minic_x86_64_machine_select_core_for_production(
            &machine, target, fn, &frame);
        const bool has_policy = selected &&
            minic_x86_64_machine_gnu_asm_policy(target, &machine_policy);
        const bool sealed = has_policy &&
            minic_machine_production_preflight_prepare(
                &source, &machine, &machine_policy, &prepared_body);
        if (!sealed) {
            if (getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL) {
                const bool covered = has_policy &&
                    minic_machine_core_coverage_verify_with_policy(
                        fn, reachable, &machine, source.core_asm_policy);
                const bool physically_sealed = covered &&
                    minic_machine_gnu_asm_preflight(
                        &machine, &machine_policy);
                fprintf(stderr,"x86 typed reject: function=%s stage=%s\n",
                    name, !selected ? "select" :
                          !has_policy ? "policy" :
                          !covered ? "coverage" :
                          !physically_sealed ? "physical" : "preflight");
            }
            x86_error(diag, path,
                "x86-64 cannot select complete typed MachineIR function");
            goto done;
        }
    }
    if (section_name != NULL) {
        if (fprintf(out, ".section %s,\"ax\",@progbits\n", section_name) < 0)
            goto done;
    } else if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",@progbits\n", name) < 0) goto done;
    } else if (fprintf(out, ".text\n") < 0) goto done;
    if (!is_internal) {
        if (fprintf(out, is_weak ? ".weak %s\n" : ".globl %s\n", name) < 0)
            goto done;
        if (!minic_multi_module_emit_elf_symbol_visibility(
                out, module_decl->visibility, name)) goto done;
    }
    if (explicit_alignment != 0U &&
        fprintf(out, ".balign %zu\n", explicit_alignment) < 0)
        goto done;
    /* GNU Linux's 16-byte entry patch area is emitted before the function
     * symbol, and its start is 16-byte aligned for live ftrace patching. */
    if ((minic_x86_64_target_codegen_options(target) &
         MINIC_X86_CODEGEN_PATCHABLE_ENTRY_16_16) != 0U &&
        fputs("  .balign 16\n", out) == EOF)
        goto done;
    if ((minic_x86_64_target_codegen_options(target) &
         MINIC_X86_CODEGEN_PATCHABLE_ENTRY_16_16) != 0U &&
        fprintf(out,
                "  .pushsection __patchable_function_entries,\"awo\",@progbits,.Lmini_x86_patch_%zu\n"
                "  .balign 8\n"
                "  .quad .Lmini_x86_patch_%zu\n"
                "  .popsection\n"
                ".Lmini_x86_patch_%zu:\n"
                "  .rept 16\n"
                "  nop\n"
                "  .endr\n", function_id, function_id, function_id) < 0)
        goto done;
    if (fprintf(out, ".type %s,@function\n%s:\n", name, name) < 0)
        goto done;
    if (!minic_machine_gnu_asm_emit_prepared_body(out, prepared_body) ||
        fprintf(out, ".size %s,.-%s\n", name, name) < 0 ||
        (is_constructor &&
         fprintf(out, ".section .init_array,\"aw\",@init_array\n"
                      ".p2align 3\n  .quad %s\n", name) < 0))
        goto done;
    success = true;
done:
    minic_machine_gnu_asm_prepared_destroy(prepared_body);
    minic_machine_function_destroy(&machine);
    free(head_direct_data_globals);
    free(head_vma_cast_globals);
    free(head_memory_rebase_globals);
    free(owned_value_use_counts);
    free(owned_reachable);
    minic_x86_64_frame_destroy(&frame);
    return success;
}

typedef struct MinicX86VisitContext {
    MinicDiagnostic *diagnostic;
    const char *path;
    const bool *head_address_context;
    const MinicCoreModule *module;
    const MinicCoreFunction *functions;
    size_t function_count;
} MinicX86VisitContext;

/* An unannotated static helper called exclusively from .head.text executes
 * before the normal kernel virtual mapping exists. The section annotation
 * belongs to the caller, but its address-role contract reaches the callee:
 * fixup_pointer(&global, physbase) needs the *link VMA*, not a RIP-relative
 * already-relocated pointer. Do not extend this to a helper shared by normal
 * code or whose address escapes through a function pointer. */
static bool *x86_head_address_call_closure(
    const MinicCoreFunction *functions, size_t function_count,
    const MinicCoreModule *module) {
    MinicCoreModuleFunctionNameIndex names = {0};
    bool *head = NULL, *head_call = NULL, *normal_call = NULL, *address_taken = NULL;
    bool changed;
    size_t i;
    if (module == NULL || module->function_count != function_count ||
        (function_count != 0U && functions == NULL) ||
        !minic_core_module_function_name_index_build(module, &names))
        return NULL;
    head = calloc(function_count == 0U ? 1U : function_count, sizeof(*head));
    head_call = calloc(function_count == 0U ? 1U : function_count,
                       sizeof(*head_call));
    normal_call = calloc(function_count == 0U ? 1U : function_count,
                         sizeof(*normal_call));
    address_taken = calloc(function_count == 0U ? 1U : function_count,
                           sizeof(*address_taken));
    if (!head || !head_call || !normal_call || !address_taken)
        goto fail;
    for (i = 0U; i < function_count; ++i) {
        const MinicCoreModuleFunctionSymbol *module_decl =
            &module->function_symbols[i];
        const char *section = module_decl->section_name;
        head[i] = section != NULL && strcmp(section, ".head.text") == 0;
    }
    /* Taking a function's address permits an opaque, possibly non-head
     * caller, regardless of the direct-call census. */
    for (i = 0U; i < function_count; ++i) {
        const MinicCoreFunction *fn = &functions[i];
        size_t j;
        /* The source of an address edge must itself be emitted. In
         * particular, same-asm-name static candidates can be lowered for
         * Core reachability without surviving its final fixed point. */
        if (!minic_program_module_function_selected(
                &module->function_symbols[i],
                MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS)) continue;
        for (j = 0U; j < fn->instruction_count; ++j) {
            const MinicCoreInstruction *ins = &fn->instructions[j];
            size_t begin, end, k;
            const MinicCoreFunctionSymbol *symbol;
            if (ins->kind != MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS ||
                ins->value.function_symbol_id >= fn->function_symbol_count)
                continue;
            symbol = &fn->function_symbols[ins->value.function_symbol_id];
            if (!minic_core_module_function_name_index_find(
                    &names, symbol->name, symbol->name_length, &begin, &end))
                goto fail;
            for (k = begin; k < end; ++k)
                address_taken[names.entries[k].function_index] = true;
        }
    }
    do {
        changed = false;
        memset(head_call, 0, function_count * sizeof(*head_call));
        memset(normal_call, 0, function_count * sizeof(*normal_call));
        for (i = 0U; i < function_count; ++i) {
            const MinicCoreFunction *fn = &functions[i];
            size_t j;
            if (!minic_program_module_function_selected(
                    &module->function_symbols[i],
                    MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS)) continue;
            for (j = 0U; j < fn->instruction_count; ++j) {
                const MinicCoreInstruction *ins = &fn->instructions[j];
                size_t begin, end, k;
                const MinicCoreCallee *callee;
                if (ins->kind != MINIC_CORE_INSTRUCTION_CALL ||
                    ins->value.call.callee_id >= fn->callee_count)
                    continue;
                callee = &fn->callees[ins->value.call.callee_id];
                if (!minic_core_module_function_name_index_find(
                        &names, callee->name, callee->name_length,
                        &begin, &end)) goto fail;
                for (k = begin; k < end; ++k) {
                    const size_t target_index = names.entries[k].function_index;
                    if (head[i]) head_call[target_index] = true;
                    else normal_call[target_index] = true;
                }
            }
        }
        for (i = 0U; i < function_count; ++i) {
            const MinicCoreModuleFunctionSymbol *module_decl =
                &module->function_symbols[i];
            const bool internal = module_decl->is_internal;
            const char *section = module_decl->section_name;
            if (!head[i] && head_call[i] && !normal_call[i] &&
                !address_taken[i] && internal && section == NULL) {
                head[i] = true;
                changed = true;
            }
        }
    } while (changed);
    minic_core_module_function_name_index_destroy(&names);
    free(head_call);
    free(normal_call);
    free(address_taken);
    return head;
fail:
    minic_core_module_function_name_index_destroy(&names);
    free(head);
    free(head_call);
    free(normal_call);
    free(address_taken);
    return NULL;
}


/* Production ModuleIR path: global payload and source symbol facts were
 * already sealed before target emission.  No C AST/global descriptor reads. */
static bool x86_visit_module_global(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleGlobal *global, size_t index, void *opaque) {
    MinicX86VisitContext *ctx = opaque;
    if (global->definition_kind == MINIC_CORE_MODULE_DEFINITION_DECLARATION) {
        if (global->alias_target_index != SIZE_MAX)
            return minic_multi_elf_emit_module_object_alias(
                out, module, global, "@object");
        return true;
    }
    if (minic_x86_64_emit_module_global(out, target, module, index))
        return true;
    if (getenv("MINIC_X86_GLOBAL_TRACE") != NULL)
        (void)fprintf(stderr,
            "MINIC_X86_MODULE_GLOBAL_REJECT object=%s index=%zu fragments=%zu\n",
            global->name != NULL ? global->name : "<unnamed>",
            index, global->fragment_count);
    x86_error(ctx->diagnostic, ctx->path,
              "x86-64 cannot emit Core module global");
    return false;
}

static bool x86_visit_function(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t index,
    bool function_sections, void *opaque) {
    const MinicX86VisitContext *ctx = opaque;
    return x86_emit_function(out, target, module_decl,
                             module, fn, facts, storage, index,
                             function_sections,
                             ctx->head_address_context != NULL &&
                             ctx->head_address_context[index],
                             ctx->functions, ctx->function_count,
                             ctx->diagnostic, ctx->path);
}

static bool x86_between(
    FILE *out, const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, void *context) {
    (void)functions;
    (void)function_count;
    (void)function_sections;
    (void)context;
    if (!minic_program_emit_source_file_asm_module(out, module) ||
        !minic_multi_elf_emit_module_function_aliases(
            out, module, "@function"))
        return false;
    if ((minic_x86_64_target_codegen_options(target) & MINIC_X86_CODEGEN_IBT_ENDBR) != 0U &&
        fputs(".section .note.gnu.property,\"a\"\n"
              "  .balign 8\n"
              "  .long 1f-0f\n"
              "  .long 4f-1f\n"
              "  .long 5\n"
              "0: .string \"GNU\"\n"
              "1: .balign 8\n"
              "  .long 0xc0000002\n"
              "  .long 3f-2f\n"
              "2: .long 1\n"
              "3: .balign 8\n"
              "4:\n", out) == EOF)
        return false;
    return true;
}

static bool x86_write_core_module_impl(
    const char *path, const MinicTargetInfo *target,
    const MinicCoreFunction *core_functions, size_t core_function_count,
    bool function_sections, const MinicCoreModule *module,
    MinicDiagnostic *diagnostic) {
    static const MinicProgramEmitConfig config = {
        x86_visit_function, NULL,
        x86_between,
        ".section .note.GNU-stack,\"\",@progbits\n", NULL,
        MINIC_PROGRAM_FUNCTION_LIVE_DEFINITIONS, false
    };
    bool success;
    MinicX86VisitContext visits = { diagnostic, path, NULL, module,
                                   core_functions, core_function_count };
    if (path == NULL || module == NULL || target == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        (core_function_count != 0U && core_functions == NULL) ||
        module->function_count != core_function_count) {
        x86_error(diagnostic, path, "x86-64 invalid module or target");
        return false;
    }
    if (!target->position_independent) {
        visits.head_address_context = x86_head_address_call_closure(
            core_functions, core_function_count, module);
        if (visits.head_address_context == NULL) {
            x86_error(diagnostic, path, "x86-64 cannot plan early head call roles");
            return false;
        }
    }
    success = minic_program_emit_write_module(
        path, module, target, core_functions, core_function_count,
        function_sections, &config, (void *)&visits,
        x86_visit_module_global);
    free((void *)visits.head_address_context);
    if (!success && diagnostic != NULL && diagnostic->message[0] == '\0')
        x86_error(diagnostic, path, "x86-64 cannot emit assembly");
    return success;
}

bool minic_x86_64_write_core_module(
    const char *path, const MinicTargetInfo *target,
    const MinicCoreFunction *core_functions, size_t core_function_count,
    bool function_sections, const MinicCoreModule *module,
    MinicDiagnostic *diagnostic) {
    return minic_program_emit_module_preflight(
               module, core_functions, core_function_count) &&
           x86_write_core_module_impl(path, target, core_functions,
                                       core_function_count, function_sections,
                                       module, diagnostic);
}
