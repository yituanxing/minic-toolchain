#include "core/core_ir_storage.h"
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
    if (function == NULL || function->type_context == NULL ||
        function->blocks == NULL ||
        (function->value_count != 0U && function->values == NULL) ||
        (function->instruction_count != 0U && function->instructions == NULL))
        return 0U;
    for (block_id = 0U; block_id < function->block_count; ++block_id) {
        MinicCoreTerminator *term = &function->blocks[block_id].terminator;
        MinicCoreValueId condition;
        const MinicCoreInstruction *definition;
        const MinicCoreType *type;
        MinicCoreBlockId selected;
        if (!function->blocks[block_id].has_terminator ||
            term->kind != MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH)
            continue;
        condition = term->conditional.condition;
        if (condition >= function->value_count ||
            function->values[condition].definition >= function->instruction_count)
            continue;
        definition = &function->instructions[function->values[condition].definition];
        type = minic_core_type_get(function->type_context,
            minic_core_type_unqualified(function->type_context,
                function->values[condition].core_type_id));
        if (definition->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            definition->result != condition ||
            type == NULL || type->kind != MINIC_CORE_TYPE_INTEGER ||
            definition->core_type_id != function->values[condition].core_type_id ||
            term->conditional.when_true >= function->block_count ||
            term->conditional.when_false >= function->block_count)
            continue;
        selected = definition->value.integer_value != 0
            ? term->conditional.when_true : term->conditional.when_false;
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

bool minic_core_ir_grow_array(void **data, size_t *capacity, size_t count, size_t element_size) {
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

char *minic_core_ir_copy_name(const char *name, size_t name_length) {
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
    function->core_return_type_id = MINIC_CORE_TYPE_INVALID;
}

/* The source adapter supplies its own destructor only while a draft exists.
 * This keeps independent Core builders and link-only Core tests entirely
 * free of frontend/type.c and its representation. */
void minic_core_function_release_source_draft(MinicCoreFunction *function) {
    if (function == NULL || function->source_draft == NULL) return;
    if (function->source_draft_release == NULL) return;
    function->source_draft_release(function->source_draft);
    function->source_draft = NULL;
    function->source_draft_release = NULL;
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
    minic_core_function_release_source_draft(function);
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        free(function->blocks[block_index].instructions);
    }
    for (callee_index = 0U; callee_index < function->callee_count; ++callee_index) {
        free(function->callees[callee_index].name);
        free(function->callees[callee_index].core_parameter_type_ids);
    }
    for (call_signature_index = 0U;
         call_signature_index < function->call_signature_count;
         ++call_signature_index) {
        free(function->call_signatures[call_signature_index].core_parameter_type_ids);
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
    free(function->core_parameter_type_ids);
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

bool minic_core_function_set_variadic(MinicCoreFunction *function, bool is_variadic) {
    if (function == NULL || function->name == NULL) return false;
    function->is_variadic = is_variadic;
    return true;
}




bool minic_core_function_add_block(MinicCoreFunction *function, MinicCoreBlockId *block_id) {
    MinicCoreBlockId new_id;

    if (function == NULL || block_id == NULL || function->block_count >= (size_t)UINT32_MAX ||
        !minic_core_ir_grow_array((void **)&function->blocks,
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



/* M74_GLOBAL_RECORD_ADDRESS: global.addr is an address-forming Core
   primitive. Record globals are addressable even though record values are not
   scalar SSA values. */
/* M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER: a Core global entry names a symbol,
   not allocated private storage.  A void-typed linker symbol may therefore
   participate in GLOBAL_ADDRESS while LOAD/STORE still reject void pointees. */


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
    name_copy = minic_core_ir_copy_name(name, name_length);
    if (name_copy == NULL ||
        !minic_core_ir_grow_array((void **)&function->function_symbols,
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


/* M85_RECORD_CALL_ARGUMENT: direct calls may transport address-backed records
   as object snapshots while return values remain on the existing scalar seam. */

/* M86_DIRECT_RECORD_CALL_RESULT: direct callees may return an address-backed
   record object. Indirect-call signatures stay on the scalar-return seam. */



/* M83_FIRST_CLASS_INDIRECT_CALL: signatures are separate from direct
   symbol callees so a function-pointer call never invents a symbolic target. */

/* M151_INDIRECT_CALL_BATCH_OWNER: indirect fixed parameters share the direct scalar/record domain. */

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
        stored.template_text = minic_core_ir_copy_name(template_text, template_length);
    }
    if (stored.template_text == NULL || !minic_core_ir_grow_array((void **)&function->inline_asms,
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
    name_copy = minic_core_ir_copy_name(name, name_length);
    if (name_copy == NULL ||
        !minic_core_ir_grow_array((void **)&inline_asm->register_clobbers,
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
        if (!minic_core_ir_grow_array((void **)&function->call_arguments,
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
    return minic_core_ir_grow_array((void **)&function->instructions,
                      &function->instruction_capacity,
                      function->instruction_count,
                      sizeof(*function->instructions)) &&
           minic_core_ir_grow_array((void **)&block->instructions,
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
        !minic_core_ir_grow_array((void **)&function->values,
                    &function->value_capacity,
                    function->value_count,
                    sizeof(*function->values))) {
        return false;
    }
    instruction_id = (MinicCoreInstructionId)function->instruction_count;
    result_id = (MinicCoreValueId)function->value_count;
    stored = *instruction;
    stored.result = result_id;
    function->values[function->value_count].core_type_id = stored.core_type_id;
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

bool minic_core_function_attach_type_context(MinicCoreFunction *function,
                                             const MinicCoreTypeContext *context) {
    if (function == NULL || context == NULL || function->type_context != NULL)
        return false;
    function->type_context = context;
    if (minic_core_function_verify_core_type_snapshot(function) &&
        minic_core_function_verify(function)) return true;
    function->type_context = NULL;
    return false;
}

const MinicCoreType *minic_core_function_type_get(const MinicCoreFunction *function,
                                                  MinicCoreTypeId id) {
    if (function == NULL) return NULL;
    return minic_core_type_get(function->type_context, id);
}

bool minic_core_function_type_is_kind(const MinicCoreFunction *function,
                                     MinicCoreTypeId id, MinicCoreTypeKind kind) {
    const MinicCoreType *type;
    if (function == NULL || function->type_context == NULL) return false;
    type = minic_core_type_get(function->type_context,
        minic_core_type_unqualified(function->type_context, id));
    return type != NULL && type->kind == kind;
}

bool minic_core_function_value_is_kind(const MinicCoreFunction *function,
                                      MinicCoreValueId id, MinicCoreTypeKind kind) {
    return function != NULL && id < function->value_count &&
           minic_core_function_type_is_kind(function,
               function->values[id].core_type_id, kind);
}

bool minic_core_function_object_layout(const MinicCoreFunction *function,
                                       MinicCoreObjectId object_id,
                                       size_t *size, size_t *alignment) {
    const MinicCoreObject *object;
    const MinicCoreType *type;
    size_t object_size;
    size_t object_alignment;
    if (function == NULL || size == NULL || alignment == NULL ||
        object_id >= function->object_count || function->objects == NULL) return false;
    object = &function->objects[object_id];
    type = minic_core_function_type_get(function, object->core_type_id);
    if (type == NULL || !type->has_layout || type->alignment == 0U ||
        object->element_count == 0U ||
        type->size > SIZE_MAX / object->element_count) return false;
    object_size = type->size * object->element_count;
    object_alignment = type->alignment > object->explicit_alignment
                           ? type->alignment : object->explicit_alignment;
    if ((object_alignment & (object_alignment - 1U)) != 0U) return false;
    *size = object_size;
    *alignment = object_alignment;
    return true;
}

