#!/usr/bin/env python3
from pathlib import Path

marker = 'M150_INDIRECT_VARIADIC_CALL_SIGNATURE_OWNER'

# 1) Core IR schema/API owns the variadic bit for indirect-call signatures.
path = Path('src/core/core_ir.h')
text = path.read_text()
if marker in text:
    print('M150 indirect variadic call signature already staged')
    raise SystemExit(0)
old = '''typedef struct MinicCoreCallSignature {
    MinicFunctionTypeId function_type_id;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallSignature;'''
new = '''typedef struct MinicCoreCallSignature {
    MinicFunctionTypeId function_type_id;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
    /* M150_INDIRECT_VARIADIC_CALL_SIGNATURE_OWNER: parameter_types is the
       fixed prefix; variadic tail VALUE arguments carry their own scalar type. */
    bool is_variadic;
} MinicCoreCallSignature;'''
if text.count(old) != 1:
    raise SystemExit(f'M150 expected one CoreCallSignature definition, found {text.count(old)}')
text = text.replace(old, new, 1)
old = '''bool minic_core_function_add_call_signature(MinicCoreFunction *function,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            MinicCoreCallSignatureId *signature_id);'''
new = '''bool minic_core_function_add_call_signature(MinicCoreFunction *function,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            bool is_variadic,
                                            MinicCoreCallSignatureId *signature_id);'''
if text.count(old) != 1:
    raise SystemExit(f'M150 expected one add_call_signature declaration, found {text.count(old)}')
path.write_text(text.replace(old, new, 1))

# 2) Core IR storage, deduplication and verifier mirror direct variadic CALL.
path = Path('src/core/core_ir.c')
text = path.read_text()
old = '''static bool call_signature_equal(const MinicCoreCallSignature *signature,
                                 MinicFunctionTypeId function_type_id,
                                 MinicType return_type,
                                 const MinicType *parameter_types,
                                 size_t parameter_count) {'''
new = '''static bool call_signature_equal(const MinicCoreCallSignature *signature,
                                 MinicFunctionTypeId function_type_id,
                                 MinicType return_type,
                                 const MinicType *parameter_types,
                                 size_t parameter_count,
                                 bool is_variadic) {'''
if text.count(old) != 1:
    raise SystemExit('M150 could not patch call_signature_equal signature')
text = text.replace(old, new, 1)
old = '''        !minic_type_equal(signature->return_type, return_type) ||
        signature->parameter_count != parameter_count) {'''
new = '''        !minic_type_equal(signature->return_type, return_type) ||
        signature->parameter_count != parameter_count ||
        signature->is_variadic != is_variadic) {'''
if text.count(old) != 1:
    raise SystemExit('M150 could not patch call_signature_equal comparison')
text = text.replace(old, new, 1)
old = '''bool minic_core_function_add_call_signature(MinicCoreFunction *function,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            MinicCoreCallSignatureId *signature_id) {'''
new = '''bool minic_core_function_add_call_signature(MinicCoreFunction *function,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            bool is_variadic,
                                            MinicCoreCallSignatureId *signature_id) {'''
if text.count(old) != 1:
    raise SystemExit('M150 could not patch add_call_signature definition')
text = text.replace(old, new, 1)
old = '''                                 return_type,
                                 parameter_types,
                                 parameter_count)) {'''
new = '''                                 return_type,
                                 parameter_types,
                                 parameter_count,
                                 is_variadic)) {'''
if text.count(old) != 1:
    raise SystemExit('M150 could not patch call signature dedup call')
text = text.replace(old, new, 1)
old = '''    stored.function_type_id = function_type_id;
    stored.return_type = return_type;
    stored.parameter_count = parameter_count;'''
new = '''    stored.function_type_id = function_type_id;
    stored.return_type = return_type;
    stored.parameter_count = parameter_count;
    stored.is_variadic = is_variadic;'''
if text.count(old) != 1:
    raise SystemExit('M150 could not store variadic bit')
text = text.replace(old, new, 1)
old = '''        if (function_type.function_type_id != signature->function_type_id ||
            instruction->value.indirect_call.argument_count != signature->parameter_count ||
            !minic_type_equal(instruction->type, signature->return_type)) {'''
new = '''        if (function_type.function_type_id != signature->function_type_id ||
            (!signature->is_variadic &&
             instruction->value.indirect_call.argument_count != signature->parameter_count) ||
            (signature->is_variadic &&
             instruction->value.indirect_call.argument_count < signature->parameter_count) ||
            !minic_type_equal(instruction->type, signature->return_type)) {'''
if text.count(old) != 1:
    raise SystemExit('M150 could not patch indirect-call count verifier')
text = text.replace(old, new, 1)
old = '''            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                return false;
            }
            value_id = argument->value.value_id;
            if (value_id >= function->value_count || !available_values[value_id] ||
                !minic_type_equal(function->values[value_id].type,
                                  signature->parameter_types[parameter_index])) {
                return false;
            }'''
new = '''            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                return false;
            }
            value_id = argument->value.value_id;
            if (value_id >= function->value_count || !available_values[value_id]) {
                return false;
            }
            if (parameter_index >= signature->parameter_count) {
                if (!signature->is_variadic ||
                    !core_call_scalar_type(function->values[value_id].type)) {
                    return false;
                }
                continue;
            }
            if (!minic_type_equal(function->values[value_id].type,
                                  signature->parameter_types[parameter_index])) {
                return false;
            }'''
if text.count(old) != 1:
    raise SystemExit(f'M150 expected one indirect argument verifier, found {text.count(old)}')
text = text.replace(old, new, 1)
path.write_text(text)

# 3) Lowerer transports the actual variadic tail and its semantic scalar types.
path = Path('src/core/core_lower.c')
text = path.read_text()
start = text.find('static MinicCoreLowerStatus lower_indirect_call(')
if start < 0:
    raise SystemExit('M150 could not locate lower_indirect_call')
end = text.find('\nstatic MinicCoreLowerStatus ', start + 1)
if end < 0:
    raise SystemExit('M150 could not locate lower_indirect_call end')
body = text[start:end]
old = '''    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;'''
new = '''    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType argument_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;'''
if body.count(old) != 1:
    raise SystemExit('M150 could not add indirect argument types')
body = body.replace(old, new, 1)
old = '''    MinicType function_type;
    size_t argument_begin;
    size_t argument_index;
    bool returns_void;'''
new = '''    MinicType function_type;
    size_t argument_begin;
    size_t argument_count;
    size_t argument_index;
    bool returns_void;'''
if body.count(old) != 1:
    raise SystemExit('M150 could not add indirect argument_count')
body = body.replace(old, new, 1)
old = '''    if (signature == NULL || signature->is_variadic ||
        expression->value.call.argument_count != signature->parameter_count ||
        !minic_type_equal(expression->type, signature->return_type)) {'''
new = '''    argument_count = expression->value.call.argument_count;
    if (signature == NULL || argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (!signature->is_variadic && argument_count != signature->parameter_count) ||
        (signature->is_variadic && argument_count < signature->parameter_count) ||
        !minic_type_equal(expression->type, signature->return_type)) {'''
if body.count(old) != 1:
    raise SystemExit('M150 could not widen indirect signature gate')
body = body.replace(old, new, 1)
old = '''    for (argument_index = 0U; argument_index < signature->parameter_count; ++argument_index) {
        if (!core_memory_scalar_type(signature->parameter_types[argument_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }'''
new = '''    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (argument_index < signature->parameter_count) {
            argument_types[argument_index] = signature->parameter_types[argument_index];
            if (!core_memory_scalar_type(argument_types[argument_index])) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            const MinicExpression *argument_expression = minic_c0_program_expression(
                context->body->program, expression->value.call.arguments[argument_index]);
            if (argument_expression == NULL ||
                !core_scalar_expression_value_type(
                    context->body, argument_expression, &argument_types[argument_index]) ||
                !core_memory_scalar_type(argument_types[argument_index])) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
    }'''
if body.count(old) != 1:
    raise SystemExit('M150 could not build indirect actual argument types')
body = body.replace(old, new, 1)
# From this point onward every argument transport is actual-count based; fixed
# prefix and variadic tail both carry a scalar MinicType in argument_types.
body = body.replace('arguments = signature->parameter_count == 0U',
                    'arguments = argument_count == 0U', 1)
body = body.replace('signature->parameter_count, sizeof(*arguments)',
                    'argument_count, sizeof(*arguments)', 1)
body = body.replace('if (signature->parameter_count != 0U && arguments == NULL)',
                    'if (argument_count != 0U && arguments == NULL)', 1)
body = body.replace('argument_index < signature->parameter_count',
                    'argument_index < argument_count')
body = body.replace('signature->parameter_types[argument_index]',
                    'argument_types[argument_index]')
old = '''                                                signature->parameter_types,
                                                signature->parameter_count,
                                                &signature_id) ||'''
new = '''                                                signature->parameter_types,
                                                signature->parameter_count,
                                                signature->is_variadic,
                                                &signature_id) ||'''
if body.count(old) != 1:
    raise SystemExit('M150 could not pass variadic bit to Core signature')
body = body.replace(old, new, 1)
body = body.replace('context->function, arguments, signature->parameter_count, &argument_begin)',
                    'context->function, arguments, argument_count, &argument_begin)', 1)
body = body.replace('instruction.value.indirect_call.argument_count = signature->parameter_count;',
                    'instruction.value.indirect_call.argument_count = argument_count;', 1)
# Mark the lowerer seam explicitly even if diagnostic text still prints the
# fixed expected prefix count.
body = body.replace('    argument_count = expression->value.call.argument_count;\n',
                    '    /* M150_INDIRECT_VARIADIC_CALL_SIGNATURE_OWNER */\n    argument_count = expression->value.call.argument_count;\n', 1)
text = text[:start] + body + text[end:]
path.write_text(text)

# 4) RV64 capability checker accepts a variadic tail; emitter already iterates
# the actual argument_count and is intentionally unchanged.
path = Path('src/target/riscv64/core_codegen.c')
text = path.read_text()
old = '''        return signature->parameter_count <= 8U &&
               instruction->value.indirect_call.argument_count == signature->parameter_count &&'''
new = '''        return signature->parameter_count <= 8U &&
               ((!signature->is_variadic &&
                 instruction->value.indirect_call.argument_count == signature->parameter_count) ||
                (signature->is_variadic &&
                 instruction->value.indirect_call.argument_count >= signature->parameter_count)) &&'''
if text.count(old) != 1:
    raise SystemExit(f'M150 expected one RV64 indirect count gate, found {text.count(old)}')
path.write_text(text.replace(old, new, 1))

# Focused semantic regression: fixed prefix plus promoted scalar variadic tail
# through a first-class function pointer.
path = Path('tests/compiler/c0/m150_indirect_variadic_call.c')
path.write_text(r'''typedef int (*sink_fn)(const char *, ...);

static int sink(const char *tag, ...) {
    return tag[0];
}

static int invoke(sink_fn fn, int value, void *pointer) {
    return fn("v", value, pointer);
}

int main(void) {
    return invoke(sink, 7, (void *)0) == 'v' ? 0 : 1;
}
''')

print('M150 indirect variadic call signature owner staged')
