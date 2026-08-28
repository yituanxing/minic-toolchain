#!/usr/bin/env python3
# Add first-class fixed-arity scalar indirect calls to Core IR.

from pathlib import Path

MARKER = "M83_FIRST_CLASS_INDIRECT_CALL"
IR = Path("src/core/core_ir.h")
IR_IMPL = Path("src/core/core_ir.c")
LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M83 {name} anchor count={count}")
    return text.replace(old, new, 1)


def patch_ir() -> None:
    text = IR.read_text()
    if MARKER in text:
        print("M83 core_ir.h already applied")
        return

    text = replace_once(
        text,
        '''typedef uint32_t MinicCoreFunctionSymbolId;
typedef uint32_t MinicCoreCalleeId;
typedef uint32_t MinicCoreInlineAsmId;
''',
        '''typedef uint32_t MinicCoreFunctionSymbolId;
typedef uint32_t MinicCoreCalleeId;
typedef uint32_t MinicCoreCallSignatureId;
typedef uint32_t MinicCoreInlineAsmId;
''',
        "signature-id",
    )
    text = replace_once(
        text,
        '''#define MINIC_CORE_FUNCTION_SYMBOL_INVALID UINT32_MAX
#define MINIC_CORE_CALLEE_INVALID UINT32_MAX
#define MINIC_CORE_INLINE_ASM_INVALID UINT32_MAX
''',
        '''#define MINIC_CORE_FUNCTION_SYMBOL_INVALID UINT32_MAX
#define MINIC_CORE_CALLEE_INVALID UINT32_MAX
#define MINIC_CORE_CALL_SIGNATURE_INVALID UINT32_MAX
#define MINIC_CORE_INLINE_ASM_INVALID UINT32_MAX
''',
        "signature-invalid",
    )
    text = replace_once(
        text,
        '''    MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS,
    MINIC_CORE_INSTRUCTION_CALL
} MinicCoreInstructionKind;
''',
        '''    MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS,
    MINIC_CORE_INSTRUCTION_CALL,
    /* M83_FIRST_CLASS_INDIRECT_CALL: callee is an SSA function-pointer value. */
    MINIC_CORE_INSTRUCTION_INDIRECT_CALL
} MinicCoreInstructionKind;
''',
        "instruction-kind",
    )
    text = replace_once(
        text,
        '''typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallee;

typedef struct MinicCoreInlineAsm {
''',
        '''typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallee;

/* M83_FIRST_CLASS_INDIRECT_CALL: Core owns enough static signature data to
   verify an indirect call without consulting frontend Program state. */
typedef struct MinicCoreCallSignature {
    MinicFunctionTypeId function_type_id;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallSignature;

typedef struct MinicCoreInlineAsm {
''',
        "signature-struct",
    )
    text = replace_once(
        text,
        '''        struct {
            MinicCoreCalleeId callee_id;
            size_t argument_begin;
            size_t argument_count;
        } call;
''',
        '''        struct {
            MinicCoreCalleeId callee_id;
            size_t argument_begin;
            size_t argument_count;
        } call;
        struct {
            MinicCoreValueId callee;
            MinicCoreCallSignatureId signature_id;
            size_t argument_begin;
            size_t argument_count;
        } indirect_call;
''',
        "instruction-payload",
    )
    text = replace_once(
        text,
        '''    MinicCoreCallee *callees;
    size_t callee_count;
    size_t callee_capacity;
    MinicCoreInlineAsm *inline_asms;
''',
        '''    MinicCoreCallee *callees;
    size_t callee_count;
    size_t callee_capacity;
    MinicCoreCallSignature *call_signatures;
    size_t call_signature_count;
    size_t call_signature_capacity;
    MinicCoreInlineAsm *inline_asms;
''',
        "signature-storage",
    )
    text = replace_once(
        text,
        '''bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    MinicCoreCalleeId *callee_id);
''',
        '''bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    MinicCoreCalleeId *callee_id);
bool minic_core_function_add_call_signature(MinicCoreFunction *function,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            MinicCoreCallSignatureId *signature_id);
''',
        "signature-api",
    )
    IR.write_text(text)
    print("M83 core_ir.h applied")


def patch_ir_impl() -> None:
    text = IR_IMPL.read_text()
    if MARKER in text:
        print("M83 core_ir.c already applied")
        return

    text = replace_once(
        text,
        '''    size_t block_index;
    size_t callee_index;
    size_t global_index;
''',
        '''    size_t block_index;
    size_t callee_index;
    size_t call_signature_index;
    size_t global_index;
''',
        "destroy-index",
    )
    text = replace_once(
        text,
        '''    for (callee_index = 0U; callee_index < function->callee_count; ++callee_index) {
        free(function->callees[callee_index].name);
        free(function->callees[callee_index].parameter_types);
    }
    for (global_index = 0U; global_index < function->global_count; ++global_index) {
''',
        '''    for (callee_index = 0U; callee_index < function->callee_count; ++callee_index) {
        free(function->callees[callee_index].name);
        free(function->callees[callee_index].parameter_types);
    }
    for (call_signature_index = 0U;
         call_signature_index < function->call_signature_count;
         ++call_signature_index) {
        free(function->call_signatures[call_signature_index].parameter_types);
    }
    for (global_index = 0U; global_index < function->global_count; ++global_index) {
''',
        "destroy-signatures",
    )
    text = replace_once(
        text,
        '''    free(function->function_symbols);
    free(function->callees);
    free(function->inline_asms);
''',
        '''    free(function->function_symbols);
    free(function->callees);
    free(function->call_signatures);
    free(function->inline_asms);
''',
        "destroy-array",
    )

    add_anchor = '''bool minic_core_function_add_opaque_inline_asm(MinicCoreFunction *function,
'''
    add_impl = '''/* M83_FIRST_CLASS_INDIRECT_CALL: signatures are separate from direct
   symbol callees so a function-pointer call never invents a symbolic target. */
static bool call_signature_equal(const MinicCoreCallSignature *signature,
                                 MinicFunctionTypeId function_type_id,
                                 MinicType return_type,
                                 const MinicType *parameter_types,
                                 size_t parameter_count) {
    size_t index;

    if (signature == NULL || signature->function_type_id != function_type_id ||
        !minic_type_equal(signature->return_type, return_type) ||
        signature->parameter_count != parameter_count) {
        return false;
    }
    for (index = 0U; index < parameter_count; ++index) {
        if (!minic_type_equal(signature->parameter_types[index], parameter_types[index])) {
            return false;
        }
    }
    return true;
}

bool minic_core_function_add_call_signature(MinicCoreFunction *function,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
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
        if (!core_call_scalar_type(parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->call_signature_count; ++index) {
        if (call_signature_equal(&function->call_signatures[index],
                                 function_type_id,
                                 return_type,
                                 parameter_types,
                                 parameter_count)) {
            *signature_id = (MinicCoreCallSignatureId)index;
            return true;
        }
    }
    (void)memset(&stored, 0, sizeof(stored));
    stored.function_type_id = function_type_id;
    stored.return_type = return_type;
    stored.parameter_count = parameter_count;
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
'''
    text = replace_once(text, add_anchor, add_impl, "signature-add")

    storage_anchor = '''        !storage_shape_is_valid(
            function->callees, function->callee_count, function->callee_capacity) ||
        !storage_shape_is_valid(
            function->inline_asms, function->inline_asm_count, function->inline_asm_capacity) ||
'''
    storage_new = '''        !storage_shape_is_valid(
            function->callees, function->callee_count, function->callee_capacity) ||
        !storage_shape_is_valid(function->call_signatures,
                                function->call_signature_count,
                                function->call_signature_capacity) ||
        !storage_shape_is_valid(
            function->inline_asms, function->inline_asm_count, function->inline_asm_capacity) ||
'''
    text = replace_once(text, storage_anchor, storage_new, "verify-storage")

    verify_insert_anchor = '''    instruction_seen = function->instruction_count == 0U
'''
    signature_verify = '''    for (index = 0U; index < function->call_signature_count; ++index) {
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
            if (!core_call_scalar_type(signature->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    instruction_seen = function->instruction_count == 0U
'''
    text = replace_once(text, verify_insert_anchor, signature_verify, "verify-signatures")

    call_case_anchor = '''        return true;
    }
    }
    return false;
}

static bool terminator_is_valid'''
    indirect_case = '''        return true;
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
            instruction->value.indirect_call.argument_count != signature->parameter_count ||
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
            MinicCoreValueId argument;
            size_t parameter_index;

            argument = function->call_arguments[argument_index];
            parameter_index =
                argument_index - instruction->value.indirect_call.argument_begin;
            if (argument >= function->value_count || !available_values[argument] ||
                !minic_type_equal(function->values[argument].type,
                                  signature->parameter_types[parameter_index])) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

static bool terminator_is_valid'''
    text = replace_once(text, call_case_anchor, indirect_case, "verify-indirect-call")

    dump_anchor = '''        return fprintf(output, ")\\n") >= 0;
    }
    }
    return false;
}

static bool dump_terminator'''
    dump_indirect = '''        return fprintf(output, ")\\n") >= 0;
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
            MinicCoreValueId argument;

            argument = function->call_arguments[
                instruction->value.indirect_call.argument_begin + argument_index];
            if ((argument_index != 0U && fprintf(output, ", ") < 0) ||
                fprintf(output, "%%%" PRIu32, argument) < 0) {
                return false;
            }
        }
        return fprintf(output,
                       ") signature=%" PRIu32 "\\n",
                       instruction->value.indirect_call.signature_id) >= 0;
    }
    }
    return false;
}

static bool dump_terminator'''
    text = replace_once(text, dump_anchor, dump_indirect, "dump-indirect-call")
    IR_IMPL.write_text(text)
    print("M83 core_ir.c applied")


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        print("M83 core_lower.c already applied")
        return

    helper_anchor = '''static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id) {
    const MinicExpression *expression;
'''
    helper = '''/* M83_FIRST_CLASS_INDIRECT_CALL: keep the callee as a first-class SSA
   function-pointer value; the static signature is copied into Core so
   verification and later backends do not depend on frontend Program state. */
static MinicCoreLowerStatus lower_indirect_call(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id) {
    const MinicExpression *callee_expression;
    const MinicFunctionType *signature;
    MinicCoreCallSignatureId signature_id;
    MinicCoreInstruction instruction;
    MinicCoreValueId callee_value;
    MinicCoreValueId *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;
    MinicType callee_value_type;
    MinicType function_type;
    size_t argument_begin;
    size_t argument_index;
    bool returns_void;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee_expression =
        minic_c0_program_expression(context->body->program, expression->value.call.callee);
    if (callee_expression == NULL ||
        !core_scalar_expression_value_type(context->body, callee_expression, &callee_value_type) ||
        !minic_type_pointee(callee_value_type, &function_type) ||
        !minic_type_is_function(function_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    signature = minic_c0_program_function_type(
        context->body->program, function_type.function_type_id);
    if (signature == NULL || signature->is_variadic ||
        expression->value.call.argument_count != signature->parameter_count ||
        !minic_type_equal(expression->type, signature->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    returns_void = minic_type_is_void(signature->return_type);
    if (!returns_void && !core_memory_scalar_type(signature->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        if (!core_memory_scalar_type(signature->parameter_types[argument_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }

    status = lower_expression(context, expression->value.call.callee, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (callee_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[callee_value].type,
                          callee_value_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    arguments = signature->parameter_count == 0U
                    ? NULL
                    : (MinicCoreValueId *)malloc(
                          signature->parameter_count * sizeof(*arguments));
    if (signature->parameter_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        status = lower_scalar_assignment_value(
            context,
            signature->parameter_types[argument_index],
            expression->value.call.arguments[argument_index],
            &arguments[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index] >= context->function->value_count ||
            !minic_type_equal(context->function->values[arguments[argument_index]].type,
                              signature->parameter_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
        status = spill_scalar_value(context,
                                    expression->span,
                                    signature->parameter_types[argument_index],
                                    arguments[argument_index],
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        status = reload_scalar_value(context,
                                     expression->span,
                                     signature->parameter_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    if (!minic_core_function_add_call_signature(context->function,
                                                function_type.function_type_id,
                                                signature->return_type,
                                                signature->parameter_types,
                                                signature->parameter_count,
                                                &signature_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, signature->parameter_count, &argument_begin)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(arguments);

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    instruction.span = expression->span;
    instruction.type = signature->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.indirect_call.callee = callee_value;
    instruction.value.indirect_call.signature_id = signature_id;
    instruction.value.indirect_call.argument_begin = argument_begin;
    instruction.value.indirect_call.argument_count = signature->parameter_count;
    if (returns_void) {
        *value_id = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id) {
    const MinicExpression *expression;
'''
    text = replace_once(text, helper_anchor, helper, "indirect-helper")

    dispatch_anchor = '''    if (expression->kind == MINIC_EXPRESSION_CALL) {
        return lower_direct_call(context, expression, value_id);
    }
'''
    dispatch_new = '''    if (expression->kind == MINIC_EXPRESSION_CALL) {
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            return lower_indirect_call(context, expression, value_id);
        }
        return lower_direct_call(context, expression, value_id);
    }
'''
    text = replace_once(text, dispatch_anchor, dispatch_new, "call-dispatch")
    LOWER.write_text(text)
    print("M83 core_lower.c applied")


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        print("M83 core_codegen.c already applied")
        return

    text = replace_once(
        text,
        '''        if (kind == MINIC_CORE_INSTRUCTION_CALL ||
            kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS) {
''',
        '''        if (kind == MINIC_CORE_INSTRUCTION_CALL ||
            kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
            kind == MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS) {
''',
        "save-ra",
    )

    support_anchor = '''    case MINIC_CORE_INSTRUCTION_CALL:
        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_count > 8U) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        return callee->name != NULL && callee->name_length != 0U && callee->parameter_count <= 8U;
'''
    support_new = support_anchor + '''    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
        const MinicCoreCallSignature *signature;
        MinicType function_type;

        if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||
            instruction->value.indirect_call.callee >= function->value_count ||
            instruction->value.indirect_call.argument_count > 8U) {
            return false;
        }
        signature =
            &function->call_signatures[instruction->value.indirect_call.signature_id];
        return signature->parameter_count <= 8U &&
               instruction->value.indirect_call.argument_count == signature->parameter_count &&
               minic_type_pointee(
                   function->values[instruction->value.indirect_call.callee].type,
                   &function_type) &&
               minic_type_is_function(function_type) &&
               function_type.function_type_id == signature->function_type_id;
    }
'''
    text = replace_once(text, support_anchor, support_new, "codegen-support")

    helper_anchor = '''static bool emit_field_address(FILE *file,
'''
    emit_indirect = '''static bool emit_indirect_call(FILE *file,
                               const MinicC0Program *program,
                               const MinicCoreFunction *function,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction) {
    size_t argument_index;
    size_t argument_offset;

    if (file == NULL || function == NULL || frame == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
        !core_instruction_supported(NULL, function, instruction)) {
        return false;
    }
    for (argument_index = 0U;
         argument_index < instruction->value.indirect_call.argument_count;
         ++argument_index) {
        argument_offset =
            instruction->value.indirect_call.argument_begin + argument_index;
        if (argument_offset >= function->call_argument_count ||
            !load_core_value(file,
                             frame,
                             function->call_arguments[argument_offset],
                             minic_core_rv64_argument_registers[argument_index])) {
            return false;
        }
    }
    if (!load_core_value(
            file, frame, instruction->value.indirect_call.callee, "t0") ||
        fprintf(file, "  jalr ra, t0, 0\\n") < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "a0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "a0");
}

static bool emit_field_address(FILE *file,
'''
    text = replace_once(text, helper_anchor, emit_indirect, "emit-indirect-helper")

    switch_anchor = '''    case MINIC_CORE_INSTRUCTION_CALL:
        return emit_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
'''
    switch_new = '''    case MINIC_CORE_INSTRUCTION_CALL:
        return emit_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return emit_indirect_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
'''
    text = replace_once(text, switch_anchor, switch_new, "emit-switch")
    CODEGEN.write_text(text)
    print("M83 core_codegen.c applied")


def main() -> int:
    patch_ir()
    patch_ir_impl()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
