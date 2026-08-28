#include "core/core_ir.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

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
static bool core_global_addressable_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
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
        !core_global_addressable_type(type)) {
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

static bool core_call_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type) ||
           minic_type_is_double(type);
}

/* M85_RECORD_CALL_ARGUMENT: direct calls may transport address-backed records
   as object snapshots while return values remain on the existing scalar seam. */
static bool core_call_parameter_type(MinicType type) {
    return core_call_scalar_type(type) || minic_type_is_record(type);
}

/* M86_DIRECT_RECORD_CALL_RESULT: direct callees may return an address-backed
   record object. Indirect-call signatures stay on the scalar-return seam. */
static bool core_direct_call_return_type(MinicType type) {
    return minic_type_is_void(type) || core_call_scalar_type(type) || minic_type_is_record(type);
}

static bool callee_signature_equal(const MinicCoreCallee *callee,
                                   const char *name,
                                   size_t name_length,
                                   MinicType return_type,
                                   const MinicType *parameter_types,
                                   size_t parameter_count,
                                   bool is_variadic) {
    size_t index;

    if (callee == NULL || name == NULL || callee->name == NULL ||
        callee->name_length != name_length || memcmp(callee->name, name, name_length) != 0 ||
        !minic_type_equal(callee->return_type, return_type) ||
        callee->parameter_count != parameter_count || callee->is_variadic != is_variadic) {
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
                                    MinicCoreCalleeId *callee_id) {
    MinicCoreCallee stored;
    size_t index;

    if (function == NULL || name == NULL || name_length == 0U || callee_id == NULL ||
        function->callee_count >= (size_t)UINT32_MAX ||
        !core_direct_call_return_type(return_type) ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*stored.parameter_types)) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!core_call_parameter_type(parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->inline_asm_count; ++index) {
        const MinicCoreInlineAsm *inline_asm;

        inline_asm = &function->inline_asms[index];
        if (inline_asm->template_text == NULL || !inline_asm->is_volatile) {
            return false;
        }
    }
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
                                        is_variadic)) {
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
                                 bool is_variadic) {
    size_t index;

    if (signature == NULL || signature->function_type_id != function_type_id ||
        !minic_type_equal(signature->return_type, return_type) ||
        signature->parameter_count != parameter_count ||
        signature->is_variadic != is_variadic) {
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
                                            MinicCoreCallSignatureId *signature_id) {
    MinicCoreCallSignature stored;
    size_t index;

    if (function == NULL || signature_id == NULL ||
        function_type_id == MINIC_FUNCTION_TYPE_INVALID ||
        function->call_signature_count >= (size_t)UINT32_MAX ||
        (!minic_type_is_void(return_type) && !core_call_scalar_type(return_type)) ||
        (parameter_count != 0U && parameter_types == NULL) ||
        parameter_count > SIZE_MAX / sizeof(*stored.parameter_types)) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!core_call_parameter_type(parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        if (call_signature_equal(&function->call_signatures[index],
                                 function_type_id,
                                 return_type,
                                 parameter_types,
                                 parameter_count,
                                 is_variadic)) {
            *signature_id = (MinicCoreCallSignatureId)index;
            return true;
        }
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.function_type_id = function_type_id;
    stored.return_type = return_type;
    stored.parameter_count = parameter_count;
    stored.is_variadic = is_variadic;
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

static bool storage_shape_is_valid(const void *data, size_t count, size_t capacity) {
    return count <= capacity && (count == 0U || data != NULL);
}

static bool instruction_result_is_valid(const MinicCoreFunction *function,
                                        const MinicCoreInstruction *instruction) {
    return instruction->result < function->value_count &&
           minic_type_equal(function->values[instruction->result].type, instruction->type);
}

static bool available_pointer_pointee(const MinicCoreFunction *function,
                                      const bool *available_values,
                                      MinicCoreValueId address,
                                      MinicType *pointee) {
    if (address >= function->value_count || !available_values[address]) {
        return false;
    }
    return minic_type_pointee(function->values[address].type, pointee);
}

bool minic_core_scalar_bitcast_types_valid(MinicType target_type, MinicType source_type) {
    return (minic_type_is_pointer(target_type) &&
            (minic_type_is_pointer(source_type) || minic_type_is_integer(source_type))) ||
           (minic_type_is_integer(target_type) && minic_type_is_pointer(source_type));
}

static bool instruction_is_valid(const MinicCoreFunction *function,
                                 const MinicCoreInstruction *instruction,
                                 const bool *available_values) {
    const MinicCoreValue *left;
    const MinicCoreValue *right;

    if (function == NULL || instruction == NULL) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type);
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_double(instruction->type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_is_double(instruction->type) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_double(left->type) &&
               minic_type_equal(left->type, instruction->type) &&
               minic_type_equal(right->type, instruction->type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_double(left->type) &&
               minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_is_integer(instruction->type) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_equal(left->type, instruction->type) &&
               minic_type_equal(right->type, instruction->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_is_integer(instruction->type) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_equal(left->type, instruction->type) &&
               minic_type_is_integer(right->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_integer(left->type) && minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return minic_type_is_pointer(left->type) && minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_int()) ||
            instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !available_values[instruction->value.binary.left] ||
            !available_values[instruction->value.binary.right]) {
            return false;
        }
        left = &function->values[instruction->value.binary.left];
        right = &function->values[instruction->value.binary.right];
        return (minic_type_is_integer(left->type) || minic_type_is_pointer(left->type)) &&
               minic_type_equal(left->type, right->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType result_type;

        if (!instruction_result_is_valid(function, instruction) ||
            !minic_type_equal(instruction->type, minic_type_bool()) ||
            (instruction->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
             instruction->value.integer_overflow.operator_kind !=
                 MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
            instruction->value.integer_overflow.left >= function->value_count ||
            instruction->value.integer_overflow.right >= function->value_count ||
            instruction->value.integer_overflow.result_address >= function->value_count ||
            !available_values[instruction->value.integer_overflow.left] ||
            !available_values[instruction->value.integer_overflow.right] ||
            !available_values[instruction->value.integer_overflow.result_address] ||
            !minic_type_pointee(
                function->values[instruction->value.integer_overflow.result_address].type,
                &result_type) ||
            !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
            minic_type_is_const(result_type) || minic_type_is_volatile(result_type)) {
            return false;
        }
        left = &function->values[instruction->value.integer_overflow.left];
        right = &function->values[instruction->value.integer_overflow.right];
        return minic_type_is_integer(left->type) && minic_type_is_integer(right->type);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_double(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_integer(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_double(function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_core_scalar_bitcast_types_valid(
                   instruction->type, function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_double(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_equal(function->values[instruction->value.operand].type,
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_integer(instruction->type) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_equal(function->values[instruction->value.operand].type,
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_equal(instruction->type, minic_type_int()) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               minic_type_is_unsigned_integer(
                   function->values[instruction->value.operand].type);
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_equal(instruction->type, minic_type_int()) &&
               instruction->value.operand < function->value_count &&
               available_values[instruction->value.operand] &&
               (minic_type_is_integer(function->values[instruction->value.operand].type) ||
                minic_type_is_pointer(function->values[instruction->value.operand].type));
    /* M79_CALL_FRAME_RETURN_ADDRESS: Core validates the semantic shape only.
       Backend support for a particular kind/level pair is a target concern. */
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS: {
        MinicType pointee;

        return instruction_result_is_valid(function, instruction) &&
               (instruction->value.call_frame_address.kind ==
                    MINIC_CORE_CALL_FRAME_ADDRESS_RETURN ||
                instruction->value.call_frame_address.kind ==
                    MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) &&
               minic_type_pointee(instruction->type, &pointee) && minic_type_is_void(pointee);
    }
    /* M123_VARIADIC_ARGUMENT_ADDRESS: Core validates only that the semantic
       cursor is represented as a pointer value. Whether a target ABI can
       materialize it is a backend capability question. */
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type);
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.parameter_index < function->parameter_count &&
               minic_type_equal(function->parameter_types[instruction->value.parameter_index],
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        size_t binding_id = instruction->value.fixed_register_binding_id;

        return instruction_result_is_valid(function, instruction) &&
               binding_id < function->fixed_register_binding_count &&
               function->fixed_register_bindings[binding_id].register_name != NULL &&
               function->fixed_register_bindings[binding_id].register_name_length != 0U &&
               minic_type_equal(function->fixed_register_bindings[binding_id].type,
                                instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT: {
        MinicCoreObjectId object_id;
        MinicType object_value_type;
        size_t parameter_index;

        object_id = instruction->value.parameter_object.object_id;
        parameter_index = instruction->value.parameter_object.parameter_index;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type) &&
               parameter_index < function->parameter_count && object_id < function->object_count &&
               minic_type_is_record(function->parameter_types[parameter_index]) &&
               minic_type_unqualified(function->objects[object_id].type, &object_value_type) &&
               minic_type_equal(function->parameter_types[parameter_index], object_value_type);
    }
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
        MinicType pointer_type;

        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.object_id >= function->object_count ||
            !minic_type_pointer_to(function->objects[instruction->value.object_id].type,
                                   &pointer_type)) {
            return false;
        }
        return minic_type_equal(pointer_type, instruction->type);
    }
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: block addresses are first-class pointer values. */
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type) &&
               instruction->value.block_id < function->block_count;
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
        MinicType pointer_type;

        if (!instruction_result_is_valid(function, instruction) ||
            instruction->value.global_id >= function->global_count ||
            !minic_type_pointer_to(function->globals[instruction->value.global_id].type,
                                   &pointer_type)) {
            return false;
        }
        return minic_type_equal(pointer_type, instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
        MinicType function_type;
        MinicCoreFunctionSymbolId symbol_id;

        symbol_id = instruction->value.function_symbol_id;
        return instruction_result_is_valid(function, instruction) &&
               symbol_id < function->function_symbol_count &&
               function->function_symbols[symbol_id].name != NULL &&
               function->function_symbols[symbol_id].name_length != 0U &&
               minic_type_pointee(instruction->type, &function_type) &&
               minic_type_is_function(function_type);
    }
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
        MinicCoreValueId base;
        MinicType base_pointee;
        MinicType field_type;

        base = instruction->value.field_address.base;
        if (!instruction_result_is_valid(function, instruction) || base >= function->value_count ||
            !available_values[base] ||
            !minic_type_pointee(function->values[base].type, &base_pointee) ||
            !minic_type_is_record(base_pointee) ||
            base_pointee.record_id != instruction->value.field_address.record_id ||
            instruction->value.field_address.record_id == MINIC_RECORD_INVALID ||
            !minic_type_pointee(instruction->type, &field_type) || minic_type_is_void(field_type) ||
            minic_type_is_function(field_type)) {
            return false;
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        MinicCoreValueId base;
        MinicCoreValueId index;

        base = instruction->value.pointer_offset.base;
        index = instruction->value.pointer_offset.index;
        return instruction_result_is_valid(function, instruction) &&
               minic_type_is_pointer(instruction->type) && base < function->value_count &&
               index < function->value_count && available_values[base] && available_values[index] &&
               minic_type_equal(function->values[base].type, instruction->type) &&
               /* M131_ZERO_STRIDE_POINTER_OFFSET: zero is a valid GNU object
                  stride. Keeping POINTER_OFFSET rather than folding it in the
                  producer preserves index evaluation and gives all pointer
                  arithmetic producers one Core semantic owner. */
               minic_type_is_integer(function->values[index].type);
    }
    case MINIC_CORE_INSTRUCTION_LOAD: {
        MinicType pointee;
        MinicType value_type;

        if (!instruction_result_is_valid(function, instruction) ||
            !available_pointer_pointee(
                function, available_values, instruction->value.load.address, &pointee) ||
            !minic_type_unqualified(pointee, &value_type)) {
            return false;
        }
        return minic_type_equal(value_type, instruction->type) &&
               instruction->value.load.is_volatile == minic_type_is_volatile(pointee);
    }
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicType pointee;
        MinicType value_type;
        MinicCoreValueId stored_value;

        stored_value = instruction->value.store.stored_value;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) || stored_value >= function->value_count ||
            !available_values[stored_value] ||
            !available_pointer_pointee(
                function, available_values, instruction->value.store.address, &pointee) ||
            !minic_type_unqualified(pointee, &value_type)) {
            return false;
        }
        return minic_type_equal(value_type, function->values[stored_value].type) &&
               instruction->value.store.is_volatile == minic_type_is_volatile(pointee);
    }
    /* BATCH_M_RECORD_LOAD: source qualification is semantic metadata.  The
       destination is an unqualified private snapshot object. */
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        MinicCoreObjectId destination_object;
        MinicType record_type;
        MinicType source_pointee;
        MinicType source_type;

        destination_object = instruction->value.record_load.destination_object;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_record(instruction->type) &&
               minic_type_unqualified(instruction->type, &record_type) &&
               minic_type_equal(record_type, instruction->type) &&
               destination_object < function->object_count &&
               minic_type_equal(function->objects[destination_object].type,
                                instruction->type) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_load.source_address,
                                         &source_pointee) &&
               minic_type_unqualified(source_pointee, &source_type) &&
               minic_type_equal(source_type, instruction->type) &&
               instruction->value.record_load.is_volatile ==
                   minic_type_is_volatile(source_pointee);
    }
    /* M80_ADDRESS_BACKED_RECORD_COPY: both SSA operands are addresses to the
       same unqualified record type; legality of writing a const-qualified
       destination is already established by the frontend initializer/copy node. */
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        MinicType destination_pointee;
        MinicType destination_type;
        MinicType source_pointee;
        MinicType source_type;
        MinicType record_type;

        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_record(instruction->type) &&
               minic_type_unqualified(instruction->type, &record_type) &&
               minic_type_equal(record_type, instruction->type) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_copy.destination_address,
                                         &destination_pointee) &&
               available_pointer_pointee(function,
                                         available_values,
                                         instruction->value.record_copy.source_address,
                                         &source_pointee) &&
               minic_type_unqualified(destination_pointee, &destination_type) &&
               minic_type_unqualified(source_pointee, &source_type) &&
               minic_type_equal(destination_type, instruction->type) &&
               minic_type_equal(source_type, instruction->type);
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->is_volatile;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (!instruction_result_is_valid(function, instruction) ||
            (!minic_type_is_integer(instruction->type) &&
             !minic_type_is_pointer(instruction->type)) ||
            instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId operand;

        operand = instruction->value.register_output_input_inline_asm.operand;
        if (!instruction_result_is_valid(function, instruction) ||
            (!minic_type_is_integer(instruction->type) &&
             !minic_type_is_pointer(instruction->type)) ||
            operand >= function->value_count || !available_values[operand] ||
            (!minic_type_is_integer(function->values[operand].type) &&
             !minic_type_is_pointer(function->values[operand].type)) ||
            instruction->value.register_output_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.register_output_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId memory_address;
        MinicCoreValueId operand;
        MinicType pointee;
        size_t memory_index;
        size_t register_index;
        size_t scalar_index;
        bool has_register_output;

        memory_address = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address;
        operand = instruction->value.memory_readwrite_scalar_input_inline_asm.operand;
        memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
        register_index =
            instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
        scalar_index =
            instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
        has_register_output = register_index != SIZE_MAX;
        if (memory_address >= function->value_count || operand >= function->value_count ||
            !available_values[memory_address] || !available_values[operand] ||
            !minic_type_pointee(function->values[memory_address].type, &pointee) ||
            (!minic_type_is_integer(pointee) && !minic_type_is_pointer(pointee)) ||
            (!minic_type_is_integer(function->values[operand].type) &&
             !minic_type_is_pointer(function->values[operand].type)) ||
            memory_index > 9U || scalar_index > 9U || memory_index == scalar_index ||
            (has_register_output &&
             (register_index > 9U || register_index == memory_index || register_index == scalar_index)) ||
            instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        if (has_register_output) {
            if (!instruction_result_is_valid(function, instruction) ||
                (!minic_type_is_integer(instruction->type) &&
                 !minic_type_is_pointer(instruction->type))) {
                return false;
            }
        } else if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                   !minic_type_is_void(instruction->type)) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile && inline_asm->has_memory_clobber;
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreValueId operand;

        operand = instruction->value.scalar_input_inline_asm.operand;
        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) || operand >= function->value_count ||
            !available_values[operand] ||
            (!minic_type_is_integer(function->values[operand].type) &&
             !minic_type_is_pointer(function->values[operand].type)) ||
            instruction->value.scalar_input_inline_asm.inline_asm_id >=
                function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.scalar_input_inline_asm.inline_asm_id];
        return inline_asm->template_text != NULL && inline_asm->template_length != 0U &&
               inline_asm->is_volatile;
    }
    /* M67_STRUCTURED_MULTI_OPERAND_INLINE_ASM: Core records operand roles and
       semantic values/addresses; target register assignment stays in the backend. */
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        bool used_indices[10] = {false};
        bool has_memory_readwrite = false;
        size_t operand_index;

        if (instruction->result != MINIC_CORE_VALUE_INVALID ||
            !minic_type_is_void(instruction->type) ||
            instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
            instruction->value.structured_inline_asm.operand_count == 0U ||
            instruction->value.structured_inline_asm.operand_count >
                MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
            return false;
        }
        inline_asm = &function->inline_asms[
            instruction->value.structured_inline_asm.inline_asm_id];
        if (inline_asm->template_text == NULL || !inline_asm->is_volatile) {
            return false;
        }
        for (operand_index = 0U;
             operand_index < instruction->value.structured_inline_asm.operand_count;
             ++operand_index) {
            const MinicCoreStructuredInlineAsmOperand *binding;
            MinicType pointee;
            MinicType value_type;

            binding = &instruction->value.structured_inline_asm.operands[operand_index];
            if (binding->operand_index > 9U || used_indices[binding->operand_index] ||
                binding->value >= function->value_count || !available_values[binding->value] ||
                (binding->has_fixed_register_binding &&
                 binding->fixed_register_binding_id >=
                     function->fixed_register_binding_count) ||
                (binding->early_clobber &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
                 binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)) {
                return false;
            }
            used_indices[binding->operand_index] = true;
            switch (binding->kind) {
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
            case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
                if (!available_pointer_pointee(
                        function, available_values, binding->value, &pointee) ||
                    (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT &&
                     minic_type_is_const(pointee)) ||
                    !minic_type_unqualified(pointee, &value_type) ||
                    (!minic_type_is_integer(value_type) && !minic_type_is_pointer(value_type))) {
                    return false;
                }
                if (binding->kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
                    has_memory_readwrite = true;
                }
                break;
            case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
                if (!minic_type_is_integer(function->values[binding->value].type) &&
                    !minic_type_is_pointer(function->values[binding->value].type)) {
                    return false;
                }
                break;
            default:
                return false;
            }
        }
        return !has_memory_readwrite || inline_asm->has_memory_clobber;
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type);
    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;
        size_t argument_end;
        bool returns_void;

        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_begin > function->call_argument_count ||
            instruction->value.call.argument_count >
                function->call_argument_count - instruction->value.call.argument_begin) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if ((!callee->is_variadic &&
             instruction->value.call.argument_count != callee->parameter_count) ||
            (callee->is_variadic &&
             instruction->value.call.argument_count < callee->parameter_count) ||
            !minic_type_equal(instruction->type, callee->return_type)) {
            return false;
        }
        returns_void = minic_type_is_void(callee->return_type);
        if (returns_void) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID) {
                return false;
            }
        } else if (minic_type_is_record(callee->return_type)) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object >= function->object_count ||
                !minic_type_equal(
                    function->objects[instruction->value.call.result_object].type,
                    callee->return_type)) {
                return false;
            }
        } else if (!core_call_scalar_type(callee->return_type) ||
                   instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
                   !instruction_result_is_valid(function, instruction)) {
            return false;
        }
        argument_end =
            instruction->value.call.argument_begin + instruction->value.call.argument_count;
        for (argument_index = instruction->value.call.argument_begin; argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index = argument_index - instruction->value.call.argument_begin;
            if (parameter_index >= callee->parameter_count) {
                if (!callee->is_variadic) {
                    return false;
                }
                if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    MinicCoreValueId value_id = argument->value.value_id;

                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !core_call_scalar_type(function->values[value_id].type)) {
                        return false;
                    }
                    continue;
                }
                if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    MinicCoreObjectId object_id = argument->value.object_id;

                    if (object_id >= function->object_count ||
                        !minic_type_is_record(function->objects[object_id].type)) {
                        return false;
                    }
                    continue;
                }
                return false;
            }
            {
                MinicType parameter_type = callee->parameter_types[parameter_index];

                if (core_call_scalar_type(parameter_type)) {
                    MinicCoreValueId value_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                        return false;
                    }
                    value_id = argument->value.value_id;
                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !minic_type_equal(function->values[value_id].type, parameter_type)) {
                        return false;
                    }
                } else if (minic_type_is_record(parameter_type)) {
                    MinicCoreObjectId object_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                        return false;
                    }
                    object_id = argument->value.object_id;
                    if (object_id >= function->object_count ||
                        !minic_type_equal(function->objects[object_id].type, parameter_type)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
        const MinicCoreCallSignature *signature;
        MinicCoreValueId callee_value;
        MinicType function_type;
        size_t argument_index;
        size_t argument_end;
        bool returns_void;

        callee_value = instruction->value.indirect_call.callee;
        if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||
            callee_value >= function->value_count || !available_values[callee_value] ||
            !minic_type_pointee(function->values[callee_value].type, &function_type) ||
            !minic_type_is_function(function_type) ||
            instruction->value.indirect_call.argument_begin > function->call_argument_count ||
            instruction->value.indirect_call.argument_count >
                function->call_argument_count - instruction->value.indirect_call.argument_begin) {
            return false;
        }
        signature =
            &function->call_signatures[instruction->value.indirect_call.signature_id];
        if (function_type.function_type_id != signature->function_type_id ||
            (!signature->is_variadic &&
             instruction->value.indirect_call.argument_count != signature->parameter_count) ||
            (signature->is_variadic &&
             instruction->value.indirect_call.argument_count < signature->parameter_count) ||
            !minic_type_equal(instruction->type, signature->return_type)) {
            return false;
        }
        returns_void = minic_type_is_void(signature->return_type);
        if ((returns_void && instruction->result != MINIC_CORE_VALUE_INVALID) ||
            (!returns_void && !instruction_result_is_valid(function, instruction))) {
            return false;
        }
        argument_end = instruction->value.indirect_call.argument_begin +
                       instruction->value.indirect_call.argument_count;
        for (argument_index = instruction->value.indirect_call.argument_begin;
             argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index =
                argument_index - instruction->value.indirect_call.argument_begin;
            if (parameter_index >= signature->parameter_count) {
                MinicCoreValueId value_id;

                if (!signature->is_variadic ||
                    argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    return false;
                }
                value_id = argument->value.value_id;
                if (value_id >= function->value_count || !available_values[value_id] ||
                    !core_call_scalar_type(function->values[value_id].type)) {
                    return false;
                }
                continue;
            }
            {
                MinicType parameter_type = signature->parameter_types[parameter_index];

                if (core_call_scalar_type(parameter_type)) {
                    MinicCoreValueId value_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                        return false;
                    }
                    value_id = argument->value.value_id;
                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !minic_type_equal(function->values[value_id].type, parameter_type)) {
                        return false;
                    }
                } else if (minic_type_is_record(parameter_type)) {
                    MinicCoreObjectId object_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                        return false;
                    }
                    object_id = argument->value.object_id;
                    if (object_id >= function->object_count ||
                        !minic_type_equal(function->objects[object_id].type, parameter_type)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }
        return true;
    }
    }
    return false;
}

static bool terminator_is_valid(const MinicCoreFunction *function,
                                const MinicCoreTerminator *terminator,
                                const bool *available_values) {
    if (function == NULL || terminator == NULL) {
        return false;
    }
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (minic_type_is_void(function->return_type)) {
            return terminator->return_value == MINIC_CORE_VALUE_INVALID;
        }
        if (minic_type_is_record(function->return_type)) {
            return terminator->return_value == MINIC_CORE_VALUE_INVALID &&
                   terminator->return_object < function->object_count &&
                   minic_type_equal(function->objects[terminator->return_object].type,
                                    function->return_type);
        }
        return terminator->return_value < function->value_count &&
               available_values[terminator->return_value] &&
               minic_type_equal(function->values[terminator->return_value].type,
                                function->return_type);
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return terminator->return_value == MINIC_CORE_VALUE_INVALID &&
               terminator->return_object == MINIC_CORE_OBJECT_INVALID;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return terminator->branch_target < function->block_count;
    /* M158_FINAL_STRICT_TAIL_INDIRECT_BRANCH_VERIFY: the source
       expression was accepted by GNU goto semantics; Core owns only
       the pointer-valued dynamic control-flow edge. */
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return terminator->indirect_target < function->value_count &&
               available_values[terminator->indirect_target] &&
               minic_type_is_pointer(function->values[terminator->indirect_target].type);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return terminator->conditional.condition < function->value_count &&
               available_values[terminator->conditional.condition] &&
               minic_type_is_integer(function->values[terminator->conditional.condition].type) &&
               terminator->conditional.when_true < function->block_count &&
               terminator->conditional.when_false < function->block_count;
    }
    return false;
}

static bool verify_block(const MinicCoreFunction *function,
                         MinicCoreBlockId block_id,
                         bool *instruction_seen,
                         bool *value_seen,
                         bool *available_values) {
    const MinicCoreBlock *block;
    size_t index;

    block = &function->blocks[block_id];
    if (!storage_shape_is_valid(
            block->instructions, block->instruction_count, block->instruction_capacity) ||
        !block->has_terminator) {
        return false;
    }
    if (function->value_count != 0U) {
        (void)memset(available_values, 0, function->value_count * sizeof(*available_values));
    }
    for (index = 0U; index < block->instruction_count; ++index) {
        MinicCoreInstructionId instruction_id;
        const MinicCoreInstruction *instruction;

        instruction_id = block->instructions[index];
        if (instruction_id >= function->instruction_count || instruction_seen[instruction_id]) {
            return false;
        }
        instruction = &function->instructions[instruction_id];
        if (!instruction_is_valid(function, instruction, available_values)) {
            return false;
        }
        if (instruction->result != MINIC_CORE_VALUE_INVALID) {
            const MinicCoreValue *result;

            result = &function->values[instruction->result];
            if (value_seen[instruction->result] || result->definition != instruction_id) {
                return false;
            }
            available_values[instruction->result] = true;
            value_seen[instruction->result] = true;
        }
        instruction_seen[instruction_id] = true;
    }
    return terminator_is_valid(function, &block->terminator, available_values);
}

bool minic_core_function_verify(const MinicCoreFunction *function) {
    bool *available_values;
    bool *instruction_seen;
    bool *value_seen;
    size_t block_index;
    size_t index;
    bool valid;

    if (function == NULL || function->name == NULL || function->name_length == 0U ||
        (function->parameter_count != 0U && function->parameter_types == NULL) ||
        !storage_shape_is_valid(
            function->globals, function->global_count, function->global_capacity) ||
        !storage_shape_is_valid(
            function->callees, function->callee_count, function->callee_capacity) ||
        !storage_shape_is_valid(function->call_signatures,
                                function->call_signature_count,
                                function->call_signature_capacity) ||
        !storage_shape_is_valid(
            function->inline_asms, function->inline_asm_count, function->inline_asm_capacity) ||
        !storage_shape_is_valid(function->call_arguments,
                                function->call_argument_count,
                                function->call_argument_capacity) ||
        !storage_shape_is_valid(
            function->objects, function->object_count, function->object_capacity) ||
        !storage_shape_is_valid(
            function->values, function->value_count, function->value_capacity) ||
        !storage_shape_is_valid(
            function->instructions, function->instruction_count, function->instruction_capacity) ||
        !storage_shape_is_valid(
            function->blocks, function->block_count, function->block_capacity) ||
        function->block_count == 0U || function->entry_block != 0U ||
        function->value_count > function->instruction_count) {
        return false;
    }
    for (index = 0U; index < function->object_count; ++index) {
        const MinicCoreObject *object = &function->objects[index];
        if (object->element_count == 0U || minic_type_is_void(object->type) ||
            minic_type_is_function(object->type)) {
            return false;
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        const MinicCoreGlobal *global;

        global = &function->globals[index];
        if (global->name == NULL || global->name_length == 0U ||
            !core_global_addressable_type(global->type)) {
            return false;
        }
    }
    for (index = 0U; index < function->callee_count; ++index) {
        const MinicCoreCallee *callee;
        size_t parameter_index;

        callee = &function->callees[index];
        if (callee->name == NULL || callee->name_length == 0U ||
            !core_direct_call_return_type(callee->return_type) ||
            (callee->parameter_count != 0U && callee->parameter_types == NULL)) {
            return false;
        }
        /* M85B_RECORD_CALLEE_VERIFIER: direct callees share the same
           scalar-or-record parameter contract used by creation and CALL
           instruction verification. Indirect signatures remain scalar-only. */
        for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
            if (!core_call_parameter_type(callee->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        const MinicCoreCallSignature *signature;
        size_t parameter_index;

        signature = &function->call_signatures[index];
        if (signature->function_type_id == MINIC_FUNCTION_TYPE_INVALID ||
            (!minic_type_is_void(signature->return_type) &&
             !core_call_scalar_type(signature->return_type)) ||
            (signature->parameter_count != 0U && signature->parameter_types == NULL)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < signature->parameter_count;
             ++parameter_index) {
            if (!core_call_parameter_type(signature->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    instruction_seen = function->instruction_count == 0U
                           ? NULL
                           : (bool *)calloc(function->instruction_count, sizeof(*instruction_seen));
    value_seen = function->value_count == 0U
                     ? NULL
                     : (bool *)calloc(function->value_count, sizeof(*value_seen));
    available_values = function->value_count == 0U
                           ? NULL
                           : (bool *)calloc(function->value_count, sizeof(*available_values));
    if ((function->instruction_count != 0U && instruction_seen == NULL) ||
        (function->value_count != 0U && (value_seen == NULL || available_values == NULL))) {
        free(instruction_seen);
        free(value_seen);
        free(available_values);
        return false;
    }
    valid = true;
    for (block_index = 0U; valid && block_index < function->block_count; ++block_index) {
        valid = verify_block(function,
                             (MinicCoreBlockId)block_index,
                             instruction_seen,
                             value_seen,
                             available_values);
    }
    for (index = 0U; valid && index < function->instruction_count; ++index) {
        valid = instruction_seen[index];
    }
    for (index = 0U; valid && index < function->value_count; ++index) {
        valid = value_seen[index];
    }
    free(instruction_seen);
    free(value_seen);
    free(available_values);
    return valid;
}

static bool dump_instruction(FILE *output,
                             const MinicCoreFunction *function,
                             const MinicCoreInstruction *instruction) {
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return fprintf(output,
                       "  %%%" PRIu32 " = const.int %" PRId64 "\n",
                       instruction->result,
                       instruction->value.integer_value) >= 0;
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        return fprintf(output,
                       "  %%%" PRIu32 " = const.double.bits 0x%016" PRIx64 "\n",
                       instruction->result,
                       instruction->value.floating_bits) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        return fprintf(output,
                       "  %%%" PRIu32 " = add.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        return fprintf(output,
                       "  %%%" PRIu32 " = sub.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
        return fprintf(output,
                       "  %%%" PRIu32 " = mul.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        return fprintf(output,
                       "  %%%" PRIu32 " = div.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        return fprintf(output,
                       "  %%%" PRIu32 " = eq.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = lt.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        return fprintf(output,
                       "  %%%" PRIu32 " = le.double %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        return fprintf(output,
                       "  %%%" PRIu32 " = add.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        return fprintf(output,
                       "  %%%" PRIu32 " = sub.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        return fprintf(output,
                       "  %%%" PRIu32 " = mul.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        return fprintf(output,
                       "  %%%" PRIu32 " = div.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        return fprintf(output,
                       "  %%%" PRIu32 " = rem.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        return fprintf(output,
                       "  %%%" PRIu32 " = and.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        return fprintf(output,
                       "  %%%" PRIu32 " = xor.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        return fprintf(output,
                       "  %%%" PRIu32 " = or.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        return fprintf(output,
                       "  %%%" PRIu32 " = shl.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        return fprintf(output,
                       "  %%%" PRIu32 " = shr.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = lt.int %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = lt.ptr %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return fprintf(output,
                       "  %%%" PRIu32 " = eq.scalar %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.binary.left,
                       instruction->value.binary.right) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        const char *operator_name =
            instruction->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_ADD
                ? "add"
            : instruction->value.integer_overflow.operator_kind ==
                    MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT
                ? "sub"
                : "mul";
        return fprintf(output,
                       "  %%%" PRIu32 " = %s.overflow.int %%%" PRIu32 ", %%%" PRIu32 ", %%%" PRIu32
                       "\n",
                       instruction->result,
                       operator_name,
                       instruction->value.integer_overflow.left,
                       instruction->value.integer_overflow.right,
                       instruction->value.integer_overflow.result_address) >= 0;
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.int-to-double %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return fprintf(output,
                       "  %%%" PRIu32 " = convert.double-to-int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return fprintf(output,
                       "  %%%" PRIu32 " = bitcast.scalar %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        return fprintf(output,
                       "  %%%" PRIu32 " = neg.double %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        return fprintf(output,
                       "  %%%" PRIu32 " = ineg %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        return fprintf(output,
                       "  %%%" PRIu32 " = inot %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
        return fprintf(output,
                       "  %%%" PRIu32 " = clz.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        return fprintf(output,
                       "  %%%" PRIu32 " = ctz.int %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return fprintf(output,
                       "  %%%" PRIu32 " = scalar.is_zero %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.operand) >= 0;
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = call.frame.%s level=%u\n",
                       instruction->result,
                       instruction->value.call_frame_address.kind ==
                               MINIC_CORE_CALL_FRAME_ADDRESS_RETURN
                           ? "return"
                           : "frame",
                       instruction->value.call_frame_address.level) >= 0;
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = variadic.argument.address\n",
                       instruction->result) >= 0;
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return fprintf(output,
                       "  %%%" PRIu32 " = parameter %zu\n",
                       instruction->result,
                       instruction->value.parameter_index) >= 0;
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
        return fprintf(output,
                       "  %%%" PRIu32 " = fixed.register.read binding=%zu\n",
                       instruction->result,
                       instruction->value.fixed_register_binding_id) >= 0;
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return fprintf(output,
                       "  parameter.object %zu, %%o%" PRIu32 "\n",
                       instruction->value.parameter_object.parameter_index,
                       instruction->value.parameter_object.object_id) >= 0;
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = object.addr %%o%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.object_id) >= 0;
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = block.addr %%bb%" PRIu32 "\n",
                       instruction->result, instruction->value.block_id) >= 0;
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (function == NULL || instruction->value.global_id >= function->global_count) {
            return false;
        }
        return fprintf(output,
                       "  %%%" PRIu32 " = global.addr @%s\n",
                       instruction->result,
                       function->globals[instruction->value.global_id].name) >= 0;
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        if (function == NULL ||
            instruction->value.function_symbol_id >= function->function_symbol_count) {
            return false;
        }
        return fprintf(output,
                       "  %%%" PRIu32 " = function.addr @%s\n",
                       instruction->result,
                       function->function_symbols[instruction->value.function_symbol_id].name) >= 0;
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return fprintf(output,
                       "  %%%" PRIu32 " = field.addr %%%" PRIu32 ", record=%zu, field=%zu\n",
                       instruction->result,
                       instruction->value.field_address.base,
                       instruction->value.field_address.record_id,
                       instruction->value.field_address.field_index) >= 0;
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        return fprintf(output,
                       "  %%%" PRIu32 " = pointer.offset %%%" PRIu32 ", %%%" PRIu32
                       ", stride=%zu\n",
                       instruction->result,
                       instruction->value.pointer_offset.base,
                       instruction->value.pointer_offset.index,
                       instruction->value.pointer_offset.element_size) >= 0;
    case MINIC_CORE_INSTRUCTION_LOAD:
        return fprintf(output,
                       "  %%%" PRIu32 " = load%s %%%" PRIu32 "\n",
                       instruction->result,
                       instruction->value.load.is_volatile ? ".volatile" : "",
                       instruction->value.load.address) >= 0;
    case MINIC_CORE_INSTRUCTION_STORE:
        return fprintf(output,
                       "  store%s %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->value.store.is_volatile ? ".volatile" : "",
                       instruction->value.store.stored_value,
                       instruction->value.store.address) >= 0;
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return fprintf(output,
                       "  record.load%s %%%" PRIu32 ", %%o%" PRIu32 "\n",
                       instruction->value.record_load.is_volatile ? ".volatile" : "",
                       instruction->value.record_load.source_address,
                       instruction->value.record_load.destination_object) >= 0;
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return fprintf(output,
                       "  record.copy %%%" PRIu32 ", %%%" PRIu32 "\n",
                       instruction->value.record_copy.source_address,
                       instruction->value.record_copy.destination_address) >= 0;
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return fprintf(output,
                       "  asm.opaque id=%" PRIu32 "%s%s\n",
                       instruction->value.inline_asm_id,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;

        if (function == NULL || instruction->value.inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
        return fprintf(output,
                       "  %%%" PRIu32 " = asm.register_output id=%" PRIu32 "%s%s\n",
                       instruction->result,
                       instruction->value.inline_asm_id,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreInlineAsmId inline_asm_id;

        inline_asm_id = instruction->value.register_output_input_inline_asm.inline_asm_id;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        return fprintf(output,
                       "  %%%" PRIu32 " = asm.register_output_input id=%" PRIu32
                       " %%%" PRIu32 "%s%s\n",
                       instruction->result,
                       inline_asm_id,
                       instruction->value.register_output_input_inline_asm.operand,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreInlineAsmId inline_asm_id;
        size_t register_index;

        inline_asm_id = instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id;
        register_index =
            instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        if (register_index == SIZE_MAX) {
            return fprintf(output,
                           "  asm.memory_rw_input id=%" PRIu32 " mem=%%%" PRIu32
                           " input=%%%" PRIu32 " operands=%zu,-,%zu%s%s\n",
                           inline_asm_id,
                           instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
                           instruction->value.memory_readwrite_scalar_input_inline_asm.operand,
                           instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index,
                           instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index,
                           inline_asm->is_volatile ? " volatile" : "",
                           inline_asm->has_memory_clobber ? " memory" : "") >= 0;
        }
        return fprintf(output,
                       "  %%%" PRIu32 " = asm.memory_rw_input id=%" PRIu32
                       " mem=%%%" PRIu32 " input=%%%" PRIu32 " operands=%zu,%zu,%zu%s%s\n",
                       instruction->result,
                       inline_asm_id,
                       instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
                       instruction->value.memory_readwrite_scalar_input_inline_asm.operand,
                       instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index,
                       register_index,
                       instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM: {
        const MinicCoreInlineAsm *inline_asm;
        MinicCoreInlineAsmId inline_asm_id;

        inline_asm_id = instruction->value.scalar_input_inline_asm.inline_asm_id;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        return fprintf(output,
                       "  asm.scalar_input id=%" PRIu32 " %%%" PRIu32 "%s%s\n",
                       inline_asm_id,
                       instruction->value.scalar_input_inline_asm.operand,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM: {
        MinicCoreInlineAsmId inline_asm_id;
        const MinicCoreInlineAsm *inline_asm;

        inline_asm_id = instruction->value.structured_inline_asm.inline_asm_id;
        if (function == NULL || inline_asm_id >= function->inline_asm_count) {
            return false;
        }
        inline_asm = &function->inline_asms[inline_asm_id];
        return fprintf(output,
                       "  asm.structured id=%" PRIu32 " operands=%zu%s%s\n",
                       inline_asm_id,
                       instruction->value.structured_inline_asm.operand_count,
                       inline_asm->is_volatile ? " volatile" : "",
                       inline_asm->has_memory_clobber ? " memory" : "") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return fprintf(output, "  compiler.barrier\n") >= 0;
    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;

        if (function == NULL || instruction->value.call.callee_id >= function->callee_count) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if (minic_type_is_record(callee->return_type)) {
            if (fprintf(output,
                        "  %%o%" PRIu32 " = call @",
                        instruction->value.call.result_object) < 0) {
                return false;
            }
        } else if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            if (fprintf(output, "  call @") < 0) {
                return false;
            }
        } else if (fprintf(output, "  %%%" PRIu32 " = call @", instruction->result) < 0) {
            return false;
        }
        if (fwrite(callee->name, 1U, callee->name_length, output) != callee->name_length ||
            fprintf(output, "(") < 0) {
            return false;
        }
        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument =
                &function->call_arguments[instruction->value.call.argument_begin + argument_index];

            if (argument_index != 0U && fprintf(output, ", ") < 0) {
                return false;
            }
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (fprintf(output, "%%%" PRIu32, argument->value.value_id) < 0) {
                    return false;
                }
            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (fprintf(output, "%%o%" PRIu32, argument->value.object_id) < 0) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return fprintf(output, ")\n") >= 0;
    }
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
        size_t argument_index;

        if (function == NULL ||
            instruction->value.indirect_call.signature_id >= function->call_signature_count ||
            instruction->value.indirect_call.callee >= function->value_count) {
            return false;
        }
        if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            if (fprintf(output,
                        "  call.indirect %%%" PRIu32 "(",
                        instruction->value.indirect_call.callee) < 0) {
                return false;
            }
        } else if (fprintf(output,
                           "  %%%" PRIu32 " = call.indirect %%%" PRIu32 "(",
                           instruction->result,
                           instruction->value.indirect_call.callee) < 0) {
            return false;
        }
        for (argument_index = 0U;
             argument_index < instruction->value.indirect_call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.indirect_call.argument_begin + argument_index];

            if (argument_index != 0U && fprintf(output, ", ") < 0) {
                return false;
            }
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (fprintf(output, "%%%" PRIu32, argument->value.value_id) < 0) {
                    return false;
                }
            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (fprintf(output, "%%o%" PRIu32, argument->value.object_id) < 0) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return fprintf(output,
                       ") signature=%" PRIu32 "\n",
                       instruction->value.indirect_call.signature_id) >= 0;
    }
    }
    return false;
}

static bool dump_terminator(FILE *output,
                            const MinicCoreFunction *function,
                            const MinicCoreTerminator *terminator) {
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (function != NULL && minic_type_is_record(function->return_type)) {
            return fprintf(output, "  return.object %%o%" PRIu32 "\n", terminator->return_object) >=
                   0;
        }
        if (terminator->return_value == MINIC_CORE_VALUE_INVALID) {
            return fprintf(output, "  return\n") >= 0;
        }
        return fprintf(output, "  return %%%" PRIu32 "\n", terminator->return_value) >= 0;
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return fprintf(output, "  unreachable\n") >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(output, "  br bb%" PRIu32 "\n", terminator->branch_target) >= 0;
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return fprintf(output, "  indirect_br %%%" PRIu32 "\n",
                       terminator->indirect_target) >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return fprintf(output,
                       "  cond_br %%%" PRIu32 ", bb%" PRIu32 ", bb%" PRIu32 "\n",
                       terminator->conditional.condition,
                       terminator->conditional.when_true,
                       terminator->conditional.when_false) >= 0;
    }
    return false;
}

bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function) {
    size_t block_index;
    size_t index;

    if (output == NULL || !minic_core_function_verify(function) ||
        fprintf(output, "core function @") < 0 ||
        fwrite(function->name, 1U, function->name_length, output) != function->name_length ||
        fprintf(output, "\n") < 0) {
        return false;
    }
    for (index = 0U; index < function->object_count; ++index) {
        if (fprintf(output,
                    "object %%o%" PRIu32 "%s\n",
                    (MinicCoreObjectId)index,
                    minic_type_is_volatile(function->objects[index].type) ? " volatile" : "") < 0) {
            return false;
        }
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block;

        block = &function->blocks[block_index];
        if (fprintf(output, "bb%zu:\n", block_index) < 0) {
            return false;
        }
        for (index = 0U; index < block->instruction_count; ++index) {
            const MinicCoreInstruction *instruction;

            instruction = &function->instructions[block->instructions[index]];
            if (!dump_instruction(output, function, instruction)) {
                return false;
            }
        }
        if (!dump_terminator(output, function, &block->terminator)) {
            return false;
        }
    }
    return true;
}
