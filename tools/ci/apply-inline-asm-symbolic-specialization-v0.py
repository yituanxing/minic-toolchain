#!/usr/bin/env python3
from pathlib import Path

# Symbolic specialization runs after integer specialization. Revisit already
# existing integer-specialized callers too: their closed integer facts can turn
# expressions such as &global[a][b] into static symbolic addresses for the next
# always-inline callee. Keep original_function_count for diagnostics/lifetime
# bookkeeping, but start this caller walk at zero and let is_integer_specialization
# filter the eligible functions.
p = Path("src/compiler/compiler.c")
text = p.read_text()
old_loop = "        for (caller_index = original_function_count;\n"
if text.count(old_loop) != 1:
    raise SystemExit(
        f"expected one symbolic existing-clone loop anchor, found {text.count(old_loop)}")
text = text.replace(
    old_loop,
    "        (void)original_function_count;\n        for (caller_index = 0U;\n",
    1,
)
p.write_text(text)

# Keep the late asm-boundary fallback as a conservative second line of defense
# for a symbolic parameter that reaches Core directly.
p = Path("src/core/core_lower_asm.c")
text = p.read_text()

anchor = '''static bool core_inline_asm_immediate_text(\n'''
helper = r'''
static const char *core_inline_asm_specialized_symbolic_name_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;
    const MinicFunction *specialized;
    const MinicFunction *source;
    MinicLocalId local_id;
    size_t parameter_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || depth > 16U) {
        return NULL;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return NULL;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_inline_asm_specialized_symbolic_name_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind != MINIC_EXPRESSION_LOCAL) {
        return NULL;
    }

    specialized = context->source_function;
    if (!specialized->is_integer_specialization ||
        specialized->specialization_source >= program->function_count) {
        return NULL;
    }
    source = minic_c0_program_function(program, specialized->specialization_source);
    if (source == NULL) {
        return NULL;
    }
    local_id = expression->value.local_id;
    if (local_id < source->local_begin) {
        return NULL;
    }
    parameter_index = local_id - source->local_begin;
    if (parameter_index >= source->parameter_count ||
        parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
        !specialized->specialization_symbolic_known[parameter_index] ||
        specialized->specialization_symbolic_expression[parameter_index] ==
            MINIC_EXPRESSION_INVALID) {
        return NULL;
    }
    return core_inline_asm_symbolic_immediate_name(
        program,
        context->target,
        specialized->specialization_symbolic_expression[parameter_index]);
}

static const char *core_inline_asm_specialized_symbolic_name(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id) {
    return core_inline_asm_specialized_symbolic_name_depth(context, expression_id, 0U);
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"expected one inline asm immediate anchor, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)

old = '''    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
    if (symbol == NULL) {
        return false;
    }
'''
new = '''    symbol = core_inline_asm_symbolic_immediate_name(
        context->body->program, context->target, operand->expression);
    if (symbol == NULL) {
        symbol = core_inline_asm_specialized_symbolic_name(
            context, operand->expression);
    }
    if (symbol == NULL) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one symbolic immediate fallback, found {text.count(old)}")
text = text.replace(old, new, 1)

p.write_text(text)
print("MINIC_INLINE_ASM_SYMBOLIC_SPECIALIZATION_V0=APPLIED preexisting_integer_clones=1")
