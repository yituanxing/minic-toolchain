#include "core/core_ir_internal.h"
#include "core/address_alignment.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static size_t core_address_alignment_gcd(size_t base, size_t offset) {
    while (offset != 0U) {
        const size_t next = base % offset;
        base = offset;
        offset = next;
    }
    return base == 0U ? 1U : base;
}

static size_t core_address_known_alignment_visit(
    const MinicCoreFunction *function, MinicCoreValueId address,
    MinicCoreAddressRootAlignment root_alignment, void *context,
    unsigned int depth, unsigned int max_depth) {
    const MinicCoreInstruction *definition;
    size_t base_alignment, offset;
    if (function == NULL || root_alignment == NULL ||
        address >= function->value_count || function->values == NULL ||
        depth > max_depth || function->instructions == NULL ||
        function->values[address].definition >= function->instruction_count)
        return 1U;
    definition = &function->instructions[function->values[address].definition];
    if (definition->result != address) return 1U;
    switch (definition->kind) {
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        base_alignment = root_alignment(function, definition, context);
        return base_alignment != 0U &&
               (base_alignment & (base_alignment - 1U)) == 0U
            ? base_alignment : 1U;
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        base_alignment = core_address_known_alignment_visit(
            function, definition->value.field_address.base,
            root_alignment, context, depth + 1U, max_depth);
        offset = definition->value.field_address.byte_offset;
        return core_address_alignment_gcd(base_alignment, offset);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        base_alignment = core_address_known_alignment_visit(
            function, definition->value.pointer_offset.base,
            root_alignment, context, depth + 1U, max_depth);
        offset = definition->value.pointer_offset.element_size;
        return core_address_alignment_gcd(base_alignment, offset);
    default:
        /* Parameter pointers, reloaded pointers, bitcasts, call results and
         * unknown SSA origins carry no trusted alignment proof. */
        return 1U;
    }
}

size_t minic_core_address_known_alignment(
    const MinicCoreFunction *function, MinicCoreValueId address,
    MinicCoreAddressRootAlignment root_alignment, void *context) {
    return minic_core_address_known_alignment_bounded(
        function, address, root_alignment, context, 64U);
}

size_t minic_core_address_known_alignment_bounded(
    const MinicCoreFunction *function, MinicCoreValueId address,
    MinicCoreAddressRootAlignment root_alignment, void *context,
    unsigned int max_depth) {
    if (max_depth > 64U) max_depth = 64U;
    return core_address_known_alignment_visit(
        function, address, root_alignment, context, 0U, max_depth);
}

size_t minic_core_function_fold_literal_branches(MinicCoreFunction *function) {
    size_t block_id, count = 0U;
    if (function == NULL || function->blocks == NULL ||
        (function->value_count != 0U && function->values == NULL) ||
        (function->instruction_count != 0U && function->instructions == NULL))
        return 0U;
    for (block_id = 0U; block_id < function->block_count; ++block_id) {
        MinicCoreTerminator *term = &function->blocks[block_id].terminator;
        MinicCoreValueId condition;
        const MinicCoreInstruction *definition;
        MinicCoreBlockId selected;
        if (!function->blocks[block_id].has_terminator ||
            term->kind != MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
            continue;
        condition = term->conditional.condition;
        if (condition >= function->value_count ||
            function->values[condition].definition >= function->instruction_count)
            continue;
        definition = &function->instructions[function->values[condition].definition];
        if (definition->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            definition->result != condition ||
            !minic_type_is_integer(function->values[condition].type))
            continue;
        selected = definition->value.integer_value != 0
            ? term->conditional.when_true : term->conditional.when_false;
        if (selected >= function->block_count) continue;
        term->kind = MINIC_CORE_TERMINATOR_BRANCH;
        term->branch_target = selected;
        ++count;
    }
    return count;
}

/* One target-independent conservative CFG filter for every Core backend.
 * Keep source labels as additional roots: they may be reached through a
 * computed goto, asm-goto or forward function-level goto even when their
 * lexical switch/loop arm has no ordinary CFG predecessor. All targets must
 * agree on this reachability set for instructions and ELF symbol uses. */
bool minic_core_function_mark_reachable_blocks(const MinicCoreFunction *function,
                                               bool *reachable, size_t capacity) {
    MinicCoreBlockId *queue;
    size_t begin = 0U, end = 0U, index;

    if (function == NULL || reachable == NULL || function->block_count == 0U ||
        capacity < function->block_count ||
        function->entry_block >= function->block_count ||
        function->block_count > SIZE_MAX / sizeof(*queue))
        return false;
    memset(reachable, 0, function->block_count * sizeof(*reachable));
    queue = (MinicCoreBlockId *)malloc(function->block_count * sizeof(*queue));
    if (queue == NULL) return false;
#define MINIC_CORE_REACH_ENQUEUE(id_)                                           \
    do {                                                                       \
        MinicCoreBlockId next_ = (id_);                                        \
        if (next_ >= function->block_count) { free(queue); return false; }     \
        if (!reachable[next_]) { reachable[next_] = true; queue[end++] = next_; } \
    } while (0)

    MINIC_CORE_REACH_ENQUEUE(function->entry_block);
    for (index = 0U; index < function->block_count; ++index)
        if (function->blocks[index].source_label_id != SIZE_MAX)
            MINIC_CORE_REACH_ENQUEUE((MinicCoreBlockId)index);
    while (begin < end) {
        const MinicCoreBlock *block = &function->blocks[queue[begin++]];
        if (!block->has_terminator) { free(queue); return false; }
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            MINIC_CORE_REACH_ENQUEUE(block->terminator.branch_target);
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
            MINIC_CORE_REACH_ENQUEUE(block->terminator.conditional.when_true);
            MINIC_CORE_REACH_ENQUEUE(block->terminator.conditional.when_false);
            break;
        default:
            free(queue);
            return false;
        }
    }
#undef MINIC_CORE_REACH_ENQUEUE
    free(queue);
    return true;
}

/* M91_BUILTIN_UNREACHABLE_TERMINATOR */

static bool grow_array(void **data, size_t *capacity, size_t count, size_t element_size) {
    void *resized;
    size_t new_capacity;

    if (data == NULL || capacity == NULL || element_size == 0U || count > *capacity) {
        return false;
    }
    if (count < *capacity) {
        return true;
    }
    new_capacity = *capacity == 0U ? 8U : *capacity * 2U;
    if (new_capacity <= count || new_capacity < *capacity ||
        new_capacity > SIZE_MAX / element_size) {
        return false;
    }
    resized = realloc(*data, new_capacity * element_size);
    if (resized == NULL) {
        return false;
    }
    *data = resized;
    *capacity = new_capacity;
    return true;
}

static char *copy_name(const char *name, size_t name_length) {
    char *copy;

    if (name == NULL || name_length == 0U || name_length == SIZE_MAX) {
        return NULL;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    return copy;
}

void minic_core_function_initialize(MinicCoreFunction *function) {
    if (function == NULL) {
        return;
    }
    (void)memset(function, 0, sizeof(*function));
    function->entry_block = MINIC_CORE_BLOCK_INVALID;
}

void minic_core_function_destroy(MinicCoreFunction *function) {
    size_t block_index;
    size_t callee_index;
    size_t call_signature_index;
    size_t global_index;
    size_t function_symbol_index;
    size_t inline_asm_index;
    size_t fixed_register_index;

    if (function == NULL) {
        return;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        free(function->blocks[block_index].instructions);
    }
    for (callee_index = 0U; callee_index < function->callee_count; ++callee_index) {
        free(function->callees[callee_index].name);
        free(function->callees[callee_index].parameter_types);
    }
    for (call_signature_index = 0U;
         call_signature_index < function->call_signature_count;
         ++call_signature_index) {
        free(function->call_signatures[call_signature_index].parameter_types);
    }
    for (global_index = 0U; global_index < function->global_count; ++global_index) {
        free(function->globals[global_index].name);
    }
    for (function_symbol_index = 0U;
         function_symbol_index < function->function_symbol_count;
         ++function_symbol_index) {
        free(function->function_symbols[function_symbol_index].name);
    }
    for (fixed_register_index = 0U;
         fixed_register_index < function->fixed_register_binding_count;
         ++fixed_register_index) {
        free(function->fixed_register_bindings[fixed_register_index].register_name);
    }
    for (inline_asm_index = 0U; inline_asm_index < function->inline_asm_count; ++inline_asm_index) {
        MinicCoreInlineAsm *inline_asm = &function->inline_asms[inline_asm_index];
        size_t clobber_index;

        free(inline_asm->template_text);
        {
            size_t resolved_index;
            for (resolved_index = 0U;
                 resolved_index < inline_asm->resolved_immediate_count &&
                     resolved_index < 3U; ++resolved_index)
                free(inline_asm->resolved_immediates[resolved_index]);
        }
        for (clobber_index = 0U; clobber_index < inline_asm->register_clobber_count;
             ++clobber_index) {
            free(inline_asm->register_clobbers[clobber_index].name);
        }
        free(inline_asm->register_clobbers);
    }
    free(function->name);
    free(function->parameter_types);
    free(function->enum_types);
    free(function->fixed_register_bindings);
    free(function->globals);
    free(function->function_symbols);
    free(function->callees);
    free(function->call_signatures);
    free(function->inline_asms);
    free(function->call_arguments);
    free(function->objects);
    free(function->values);
    free(function->instructions);
    free(function->blocks);
    minic_core_function_initialize(function);
}

bool minic_core_function_set_signature(MinicCoreFunction *function,
                                       const char *name,
                                       size_t name_length,
                                       MinicType return_type,
                                       const MinicType *parameter_types,
                                       size_t parameter_count) {
    char *name_copy;
    MinicType *parameters_copy;

    if (function == NULL || function->name != NULL || function->parameter_types != NULL ||
        function->parameter_count != 0U || name == NULL || name_length == 0U ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*parameters_copy)) {
        return false;
    }
    name_copy = copy_name(name, name_length);
    if (name_copy == NULL) {
        return false;
    }
    parameters_copy = NULL;
    if (parameter_count != 0U) {
        parameters_copy = (MinicType *)malloc(parameter_count * sizeof(*parameters_copy));
        if (parameters_copy == NULL) {
            free(name_copy);
            return false;
        }
        (void)memcpy(parameters_copy, parameter_types, parameter_count * sizeof(*parameters_copy));
    }
    function->name = name_copy;
    function->name_length = name_length;
    function->return_type = return_type;
    function->parameter_types = parameters_copy;
    function->parameter_count = parameter_count;
    return true;
}

bool minic_core_function_set_calling_convention(
    MinicCoreFunction *function, MinicFunctionCallingConvention convention) {
    if (function == NULL || function->name == NULL ||
        !minic_function_calling_convention_valid(convention) ||
        (function->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT &&
         function->calling_convention != convention))
        return false;
    function->calling_convention = convention;
    return true;
}

bool minic_core_function_add_enum_type(MinicCoreFunction *function,
                                       MinicEnumId enum_id,
                                       MinicType effective_integer_type) {
    size_t index;

    if (function == NULL || enum_id == MINIC_ENUM_INVALID ||
        !minic_type_is_integer(effective_integer_type) ||
        minic_type_is_enum(effective_integer_type) ||
        minic_type_is_pointer(effective_integer_type)) {
        return false;
    }
    for (index = 0U; index < function->enum_type_count; ++index) {
        const MinicCoreEnumType *existing = &function->enum_types[index];

        if (existing->enum_id == enum_id) {
            return minic_type_equal(existing->effective_integer_type, effective_integer_type);
        }
    }
    if (!grow_array((void **)&function->enum_types,
                    &function->enum_type_capacity,
                    function->enum_type_count,
                    sizeof(*function->enum_types))) {
        return false;
    }
    function->enum_types[function->enum_type_count].enum_id = enum_id;
    function->enum_types[function->enum_type_count].effective_integer_type =
        effective_integer_type;
    function->enum_type_count += 1U;
    return true;
}

bool minic_core_function_effective_integer_type(const MinicCoreFunction *function,
                                                MinicType type,
                                                MinicType *effective_type) {
    size_t index;

    if (function == NULL || effective_type == NULL || !minic_type_is_integer(type) ||
        minic_type_is_pointer(type)) {
        return false;
    }
    if (!minic_type_is_enum(type)) {
        *effective_type = type;
        return true;
    }
    for (index = 0U; index < function->enum_type_count; ++index) {
        if (function->enum_types[index].enum_id == type.enum_id) {
            *effective_type = function->enum_types[index].effective_integer_type;
            effective_type->base_qualifiers = type.base_qualifiers;
            effective_type->explicit_alignment = type.explicit_alignment;
            return true;
        }
    }
    return false;
}

bool minic_core_function_add_fixed_register_binding(MinicCoreFunction *function,
                                                    const char *register_name,
                                                    size_t register_name_length,
                                                    MinicType type,
                                                    bool is_local,
                                                    size_t *binding_id) {
    char *name_copy;
    size_t index;

    if (function == NULL || register_name == NULL || register_name_length == 0U ||
        binding_id == NULL ||
        (!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&
         !minic_type_is_double(type))) {
        return false;
    }
    for (index = 0U; index < function->fixed_register_binding_count; ++index) {
        const MinicCoreFixedRegisterBinding *existing =
            &function->fixed_register_bindings[index];

        if (existing->is_local == is_local && minic_type_equal(existing->type, type) &&
            existing->register_name_length == register_name_length &&
            memcmp(existing->register_name, register_name, register_name_length) == 0) {
            *binding_id = index;
            return true;
        }
    }
    if (!grow_array((void **)&function->fixed_register_bindings,
                    &function->fixed_register_binding_capacity,
                    function->fixed_register_binding_count,
                    sizeof(*function->fixed_register_bindings))) {
        return false;
    }
    name_copy = copy_name(register_name, register_name_length);
    if (name_copy == NULL) {
        return false;
    }
    index = function->fixed_register_binding_count;
    function->fixed_register_bindings[index].register_name = name_copy;
    function->fixed_register_bindings[index].register_name_length = register_name_length;
    function->fixed_register_bindings[index].type = type;
    function->fixed_register_bindings[index].is_local = is_local;
    function->fixed_register_binding_count += 1U;
    *binding_id = index;
    return true;
}

bool minic_core_function_add_block(MinicCoreFunction *function, MinicCoreBlockId *block_id) {
    MinicCoreBlockId new_id;

    if (function == NULL || block_id == NULL || function->block_count >= (size_t)UINT32_MAX ||
        !grow_array((void **)&function->blocks,
                    &function->block_capacity,
                    function->block_count,
                    sizeof(*function->blocks))) {
        return false;
    }
    new_id = (MinicCoreBlockId)function->block_count;
    (void)memset(&function->blocks[function->block_count], 0, sizeof(*function->blocks));
    function->blocks[function->block_count].source_label_id = SIZE_MAX;
    function->block_count += 1U;
    if (function->entry_block == MINIC_CORE_BLOCK_INVALID) {
        function->entry_block = new_id;
    }
    *block_id = new_id;
    return true;
}

bool minic_core_function_add_repeated_object(MinicCoreFunction *function,
                                             MinicSourceSpan span,
                                             MinicType element_type,
                                             size_t element_count,
                                             MinicCoreObjectId *object_id) {
    MinicCoreObjectId new_id;

    if (function == NULL || object_id == NULL || element_count == 0U ||
        function->object_count >= (size_t)UINT32_MAX ||
        minic_type_is_void(element_type) || minic_type_is_function(element_type) ||
        !grow_array((void **)&function->objects,
                    &function->object_capacity,
                    function->object_count,
                    sizeof(*function->objects))) {
        return false;
    }
    new_id = (MinicCoreObjectId)function->object_count;
    function->objects[function->object_count].span = span;
    function->objects[function->object_count].type = element_type;
    function->objects[function->object_count].element_count = element_count;
    function->objects[function->object_count].explicit_alignment = 0U;
    function->object_count += 1U;
    *object_id = new_id;
    return true;
}

bool minic_core_function_add_object(MinicCoreFunction *function,
                                    MinicSourceSpan span,
                                    MinicType type,
                                    MinicCoreObjectId *object_id) {
    return minic_core_function_add_repeated_object(function, span, type, 1U, object_id);
}

/* M74_GLOBAL_RECORD_ADDRESS: global.addr is an address-forming Core
   primitive. Record globals are addressable even though record values are not
   scalar SSA values. */
/* M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER: a Core global entry names a symbol,
   not allocated private storage.  A void-typed linker symbol may therefore
   participate in GLOBAL_ADDRESS while LOAD/STORE still reject void pointees. */
bool minic_core_ir_global_addressable_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           /* Floating globals remain addressable even when their arithmetic
            * is target-constrained. Linux's x87 probes consume float64
            * objects through GNU `m` operands without loading a C scalar;
            * the Core global-address contract must agree with the existing
            * shared address/aggregate lowering predicate. */
           minic_type_is_float(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type) ||
           minic_type_is_array(type) || minic_type_is_record(type) ||
           minic_type_is_void(type);
}

bool minic_core_function_add_global(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType type,
                                    MinicCoreGlobalId *global_id) {
    char *name_copy;
    size_t index;

    if (function == NULL || name == NULL || name_length == 0U || global_id == NULL ||
        function->global_count >= (size_t)UINT32_MAX ||
        !minic_core_ir_global_addressable_type(type)) {
        return false;
    }
    for (index = 0U; index < function->global_count; ++index) {
        const MinicCoreGlobal *existing;

        existing = &function->globals[index];
        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            if (!minic_type_equal(existing->type, type)) {
                return false;
            }
            *global_id = (MinicCoreGlobalId)index;
            return true;
        }
    }
    name_copy = copy_name(name, name_length);
    if (name_copy == NULL || !grow_array((void **)&function->globals,
                                         &function->global_capacity,
                                         function->global_count,
                                         sizeof(*function->globals))) {
        free(name_copy);
        return false;
    }
    function->globals[function->global_count].name = name_copy;
    function->globals[function->global_count].name_length = name_length;
    function->globals[function->global_count].type = type;
    *global_id = (MinicCoreGlobalId)function->global_count;
    function->global_count += 1U;
    return true;
}

/* M81_FUNCTION_ADDRESS_VALUE: function symbols are names, not call sites.
   Keep them independent from MinicCoreCallee so merely taking a function
   address never inherits scalar-call ABI restrictions. */
bool minic_core_function_add_function_symbol(MinicCoreFunction *function,
                                             const char *name,
                                             size_t name_length,
                                             MinicCoreFunctionSymbolId *symbol_id) {
    char *name_copy;
    size_t index;

    if (function == NULL || name == NULL || name_length == 0U || symbol_id == NULL ||
        function->function_symbol_count >= (size_t)UINT32_MAX) {
        return false;
    }
    for (index = 0U; index < function->function_symbol_count; ++index) {
        const MinicCoreFunctionSymbol *existing = &function->function_symbols[index];
        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            *symbol_id = (MinicCoreFunctionSymbolId)index;
            return true;
        }
    }
    name_copy = copy_name(name, name_length);
    if (name_copy == NULL ||
        !grow_array((void **)&function->function_symbols,
                    &function->function_symbol_capacity,
                    function->function_symbol_count,
                    sizeof(*function->function_symbols))) {
        free(name_copy);
        return false;
    }
    function->function_symbols[function->function_symbol_count].name = name_copy;
    function->function_symbols[function->function_symbol_count].name_length = name_length;
    *symbol_id = (MinicCoreFunctionSymbolId)function->function_symbol_count;
    function->function_symbol_count += 1U;
    return true;
}

bool minic_core_ir_call_scalar_type(MinicType type) {
    /* Scalar call transport is an IR contract, not an arithmetic capability.
       Keep binary32 and binary64 on the same call seam; target ABI placement
       still decides the physical floating registers. */
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_float(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type);
}

/* M85_RECORD_CALL_ARGUMENT: direct calls may transport address-backed records
   as object snapshots while return values remain on the existing scalar seam. */
bool minic_core_ir_call_parameter_type(MinicType type) {
    return minic_core_ir_call_scalar_type(type) || minic_type_is_record(type);
}

/* M86_DIRECT_RECORD_CALL_RESULT: direct callees may return an address-backed
   record object. Indirect-call signatures stay on the scalar-return seam. */
bool minic_core_ir_direct_call_return_type(MinicType type) {
    return minic_type_is_void(type) || minic_core_ir_call_scalar_type(type) || minic_type_is_record(type);
}

static bool callee_signature_equal(const MinicCoreCallee *callee,
                                   const char *name,
                                   size_t name_length,
                                   MinicType return_type,
                                   const MinicType *parameter_types,
                                   size_t parameter_count,
                                   bool is_variadic,
                                   MinicFunctionCallingConvention calling_convention) {
    size_t index;

    if (callee == NULL || name == NULL || callee->name == NULL ||
        callee->name_length != name_length || memcmp(callee->name, name, name_length) != 0 ||
        !minic_type_equal(callee->return_type, return_type) ||
        callee->parameter_count != parameter_count || callee->is_variadic != is_variadic ||
        callee->calling_convention != calling_convention) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_type_equal(callee->parameter_types[index], parameter_types[index])) {
            return false;
        }
    }
    return true;
}

bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    bool is_variadic,
                                    MinicFunctionCallingConvention calling_convention,
                                    MinicCoreCalleeId *callee_id) {
    MinicCoreCallee stored;
    size_t index;

    if (function == NULL || name == NULL || name_length == 0U || callee_id == NULL ||
        function->callee_count >= (size_t)UINT32_MAX ||
        !minic_core_ir_direct_call_return_type(return_type) ||
        !minic_function_calling_convention_valid(calling_convention) ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*stored.parameter_types)) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_core_ir_call_parameter_type(parameter_types[index])) {
            return false;
        }
    }
    /* The inline-ASM ingress validates its own records and the Core verifier
     * validates consumers. Adding a CALL must not rescan an unrelated ASM
     * table: doing so made a function with many CALLs and ASM sites quadratic
     * and coupled two independently owned IR collections. */
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreCallee *existing;

        existing = &function->callees[index];
        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            if (!callee_signature_equal(existing,
                                        name,
                                        name_length,
                                        return_type,
                                        parameter_types,
                                        parameter_count,
                                        is_variadic,
                                        calling_convention)) {
                return false;
            }
            *callee_id = (MinicCoreCalleeId)index;
            return true;
        }
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.name = copy_name(name, name_length);
    if (stored.name == NULL) {
        return false;
    }
    if (parameter_count != 0U) {
        stored.parameter_types =
            (MinicType *)malloc(parameter_count * sizeof(*stored.parameter_types));
        if (stored.parameter_types == NULL) {
            free(stored.name);
            return false;
        }
        (void)memcpy(stored.parameter_types,
                     parameter_types,
                     parameter_count * sizeof(*stored.parameter_types));
    }
    stored.name_length = name_length;
    stored.return_type = return_type;
    stored.parameter_count = parameter_count;
    stored.is_variadic = is_variadic;
    stored.calling_convention = calling_convention;
    if (!grow_array((void **)&function->callees,
                    &function->callee_capacity,
                    function->callee_count,
                    sizeof(*function->callees))) {
        free(stored.name);
        free(stored.parameter_types);
        return false;
    }
    function->callees[function->callee_count] = stored;
    *callee_id = (MinicCoreCalleeId)function->callee_count;
    function->callee_count += 1U;
    return true;
}

/* M83_FIRST_CLASS_INDIRECT_CALL: signatures are separate from direct
   symbol callees so a function-pointer call never invents a symbolic target. */
static bool call_signature_equal(const MinicCoreCallSignature *signature,
                                 MinicFunctionTypeId function_type_id,
                                 MinicType return_type,
                                 const MinicType *parameter_types,
                                 size_t parameter_count,
                                 bool is_variadic,
                                 MinicFunctionCallingConvention calling_convention) {
    size_t index;

    if (signature == NULL || signature->function_type_id != function_type_id ||
        !minic_type_equal(signature->return_type, return_type) ||
        signature->parameter_count != parameter_count ||
        signature->is_variadic != is_variadic ||
        signature->calling_convention != calling_convention) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_type_equal(signature->parameter_types[index], parameter_types[index])) {
            return false;
        }
    }
    return true;
}

/* M151_INDIRECT_CALL_BATCH_OWNER: indirect fixed parameters share the direct scalar/record domain. */
bool minic_core_function_add_call_signature(MinicCoreFunction *function,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            bool is_variadic,
                                            MinicFunctionCallingConvention calling_convention,
                                            MinicCoreCallSignatureId *signature_id) {
    MinicCoreCallSignature stored;
    size_t index;

    if (function == NULL || signature_id == NULL ||
        function_type_id == MINIC_FUNCTION_TYPE_INVALID ||
        !minic_function_calling_convention_valid(calling_convention) ||
        function->call_signature_count >= (size_t)UINT32_MAX ||
        (!minic_type_is_void(return_type) && !minic_type_is_record(return_type) &&
         !minic_core_ir_call_scalar_type(return_type)) ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*stored.parameter_types)) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_core_ir_call_parameter_type(parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        if (call_signature_equal(&function->call_signatures[index],
                                 function_type_id,
                                 return_type,
                                 parameter_types,
                                 parameter_count,
                                 is_variadic,
                                 calling_convention)) {
            *signature_id = (MinicCoreCallSignatureId)index;
            return true;
        }
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.function_type_id = function_type_id;
    stored.return_type = return_type;
    stored.parameter_count = parameter_count;
    stored.is_variadic = is_variadic;
    stored.calling_convention = calling_convention;
    if (parameter_count != 0U) {
        stored.parameter_types =
            (MinicType *)malloc(parameter_count * sizeof(*stored.parameter_types));
        if (stored.parameter_types == NULL) {
            return false;
        }
        (void)memcpy(stored.parameter_types,
                     parameter_types,
                     parameter_count * sizeof(*stored.parameter_types));
    }
    if (!grow_array((void **)&function->call_signatures,
                    &function->call_signature_capacity,
                    function->call_signature_count,
                    sizeof(*function->call_signatures))) {
        free(stored.parameter_types);
        return false;
    }
    function->call_signatures[function->call_signature_count] = stored;
    *signature_id = (MinicCoreCallSignatureId)function->call_signature_count;
    function->call_signature_count += 1U;
    return true;
}

bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
                                               const char *template_text,
                                               size_t template_length,
                                               bool is_volatile,
                                               bool has_memory_clobber,
                                               MinicCoreInlineAsmId *inline_asm_id) {
    MinicCoreInlineAsm stored;

    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: an empty volatile asm is still an
       explicit compiler-side effect even though the target text is zero bytes.
       Keep it in the opaque-asm table rather than erasing it or strengthening it
       into a memory-clobber barrier. */
    if (function == NULL || template_text == NULL || template_length == SIZE_MAX ||
        inline_asm_id == NULL || !is_volatile ||
        function->inline_asm_count >= (size_t)UINT32_MAX) {
        return false;
    }
    (void)memset(&stored, 0, sizeof(stored));
    if (template_length == 0U) {
        stored.template_text = (char *)malloc(1U);
        if (stored.template_text != NULL) {
            stored.template_text[0] = '\0';
        }
    } else {
        stored.template_text = copy_name(template_text, template_length);
    }
    if (stored.template_text == NULL || !grow_array((void **)&function->inline_asms,
                                                    &function->inline_asm_capacity,
                                                    function->inline_asm_count,
                                                    sizeof(*function->inline_asms))) {
        free(stored.template_text);
        return false;
    }
    stored.template_length = template_length;
    stored.is_volatile = is_volatile;
    stored.has_memory_clobber = has_memory_clobber;
    stored.goto_target = MINIC_CORE_BLOCK_INVALID;
    stored.goto_second_target = MINIC_CORE_BLOCK_INVALID;
    /* Source provenance is opt-in.  In particular zero is a valid AST id,
     * not proof that this Core effect came from source inline ASM #0.
     * Opaque asm-goto uses this same field for a deferred-immediate owner;
     * only an ingress with a verified source identity may populate it. */
    stored.source_inline_asm_id = SIZE_MAX;
    function->inline_asms[function->inline_asm_count] = stored;
    *inline_asm_id = (MinicCoreInlineAsmId)function->inline_asm_count;
    function->inline_asm_count += 1U;
    return true;
}


bool minic_core_function_add_inline_asm_register_clobber(
    MinicCoreFunction *function,
    MinicCoreInlineAsmId inline_asm_id,
    const char *name,
    size_t name_length) {
    MinicCoreInlineAsm *inline_asm;
    MinicCoreInlineAsmRegisterClobber *stored;
    char *name_copy;

    if (function == NULL || inline_asm_id >= function->inline_asm_count ||
        name == NULL || name_length == 0U || name_length == SIZE_MAX) {
        return false;
    }
    inline_asm = &function->inline_asms[inline_asm_id];
    name_copy = copy_name(name, name_length);
    if (name_copy == NULL ||
        !grow_array((void **)&inline_asm->register_clobbers,
                    &inline_asm->register_clobber_capacity,
                    inline_asm->register_clobber_count,
                    sizeof(*inline_asm->register_clobbers))) {
        free(name_copy);
        return false;
    }
    stored = &inline_asm->register_clobbers[inline_asm->register_clobber_count++];
    stored->name = name_copy;
    stored->name_length = name_length;
    return true;
}

/* The input span must be independent of function->call_arguments: growth may
 * realloc that array before the next element is read. Current Core callers
 * provide separate temporary argument buffers. */
bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreCallArgument *arguments,
                                               size_t argument_count,
                                               size_t *argument_begin) {
    size_t index;
    size_t start;

    if (function == NULL || argument_begin == NULL || (argument_count != 0U && arguments == NULL) ||
        argument_count > SIZE_MAX - function->call_argument_count) {
        return false;
    }
    start = function->call_argument_count;
    for (index = 0U; index < argument_count; ++index) {
        if (!grow_array((void **)&function->call_arguments,
                        &function->call_argument_capacity,
                        function->call_argument_count,
                        sizeof(*function->call_arguments))) {
            function->call_argument_count = start;
            return false;
        }
        function->call_arguments[function->call_argument_count] = arguments[index];
        function->call_argument_count += 1U;
    }
    *argument_begin = start;
    return true;
}

static bool reserve_instruction(MinicCoreFunction *function, MinicCoreBlock *block) {
    return grow_array((void **)&function->instructions,
                      &function->instruction_capacity,
                      function->instruction_count,
                      sizeof(*function->instructions)) &&
           grow_array((void **)&block->instructions,
                      &block->instruction_capacity,
                      block->instruction_count,
                      sizeof(*block->instructions));
}

static void append_reserved_instruction(MinicCoreFunction *function,
                                        MinicCoreBlock *block,
                                        const MinicCoreInstruction *instruction,
                                        MinicCoreInstructionId instruction_id) {
    function->instructions[function->instruction_count] = *instruction;
    function->instruction_count += 1U;
    block->instructions[block->instruction_count] = instruction_id;
    block->instruction_count += 1U;
}

/* Input instruction must not point into function->instructions: reserve may
 * realloc it before the input is copied. The existing lowerers pass a local
 * instruction value; ValueId/InstructionId remain stable after growth. */
bool minic_core_function_append_value_instruction(MinicCoreFunction *function,
                                                  MinicCoreBlockId block_id,
                                                  const MinicCoreInstruction *instruction,
                                                  MinicCoreValueId *value_id) {
    MinicCoreBlock *block;
    MinicCoreInstruction stored;
    MinicCoreInstructionId instruction_id;
    MinicCoreValueId result_id;

    if (function == NULL || instruction == NULL || value_id == NULL ||
        block_id >= function->block_count || function->instruction_count >= (size_t)UINT32_MAX ||
        function->value_count >= (size_t)UINT32_MAX) {
        return false;
    }
    block = &function->blocks[block_id];
    if (block->has_terminator || !reserve_instruction(function, block) ||
        !grow_array((void **)&function->values,
                    &function->value_capacity,
                    function->value_count,
                    sizeof(*function->values))) {
        return false;
    }
    instruction_id = (MinicCoreInstructionId)function->instruction_count;
    result_id = (MinicCoreValueId)function->value_count;
    stored = *instruction;
    stored.result = result_id;
    function->values[function->value_count].type = stored.type;
    function->values[function->value_count].definition = instruction_id;
    function->value_count += 1U;
    append_reserved_instruction(function, block, &stored, instruction_id);
    *value_id = result_id;
    return true;
}

/* Same non-aliasing input contract as append_value_instruction above. */
bool minic_core_function_append_effect_instruction(MinicCoreFunction *function,
                                                   MinicCoreBlockId block_id,
                                                   const MinicCoreInstruction *instruction) {
    MinicCoreBlock *block;
    MinicCoreInstruction stored;
    MinicCoreInstructionId instruction_id;

    if (function == NULL || instruction == NULL || block_id >= function->block_count ||
        function->instruction_count >= (size_t)UINT32_MAX) {
        return false;
    }
    block = &function->blocks[block_id];
    if (block->has_terminator || !reserve_instruction(function, block)) {
        return false;
    }
    instruction_id = (MinicCoreInstructionId)function->instruction_count;
    stored = *instruction;
    stored.result = MINIC_CORE_VALUE_INVALID;
    append_reserved_instruction(function, block, &stored, instruction_id);
    return true;
}

bool minic_core_function_set_terminator(MinicCoreFunction *function,
                                        MinicCoreBlockId block_id,
                                        const MinicCoreTerminator *terminator) {
    MinicCoreBlock *block;

    if (function == NULL || terminator == NULL || block_id >= function->block_count) {
        return false;
    }
    block = &function->blocks[block_id];
    if (block->has_terminator) {
        return false;
    }
    block->terminator = *terminator;
    block->has_terminator = true;
    return true;
}

