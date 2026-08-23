from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()

marker = 'M70_ARRAY_LVALUE_POINTER_DECAY'
if marker not in text:
    anchor = '''    if (minic_type_is_integer(target_type) && minic_type_is_integer(expression->type)) {
        return lower_integer_assignment_value(context, target_type, expression_id, value_id);
    }

    status = lower_expression(context, expression_id, &source_value);
'''
    replacement = '''    if (minic_type_is_integer(target_type) && minic_type_is_integer(expression->type)) {
        return lower_integer_assignment_value(context, target_type, expression_id, value_id);
    }

    /* M70_ARRAY_LVALUE_POINTER_DECAY: C array arguments/assignments decay to
       a pointer to their first element. Core already owns address formation for
       addressable arrays (including record array members); materialize that
       address and reinterpret pointer-to-array as the assignment-compatible
       pointer value instead of asking scalar lowering to load an array. */
    if (minic_type_is_pointer(target_type) && minic_type_is_array(expression->type)) {
        MinicCoreValueId array_address;
        MinicType array_pointer_type;

        if (expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_pointer_to(expression->type, &array_pointer_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_address(context, expression_id, &array_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (array_address >= context->function->value_count ||
            !minic_type_equal(context->function->values[array_address].type,
                              array_pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return append_scalar_bitcast(
            context, expression->span, target_type, array_address, value_id);
    }

    status = lower_expression(context, expression_id, &source_value);
'''
    if text.count(anchor) != 1:
        raise SystemExit(f'M70 anchor count={text.count(anchor)}')
    text = text.replace(anchor, replacement, 1)

trace_marker = 'M70_FAST_FRONTIER_TRACE'
if trace_marker not in text:
    helper_anchor = '''static bool core_memory_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}
'''
    helper_replacement = '''/* M70_FAST_FRONTIER_TRACE: temporary fast-job-only diagnostics. */
static bool core_fast_frontier_trace_enabled(void) {
    const char *job = getenv("GITHUB_JOB");
    return job != NULL && strcmp(job, "fast-frontier") == 0;
}

static bool core_memory_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}
'''
    if text.count(helper_anchor) != 1:
        raise SystemExit(f'M70 trace helper anchor count={text.count(helper_anchor)}')
    text = text.replace(helper_anchor, helper_replacement, 1)

    indirect_anchor = '''    if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    indirect_replacement = '''    if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
        if (core_fast_frontier_trace_enabled()) {
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=direct-call reason=indirect span=%zu:%zu\\n",
                          expression->span.begin.line,
                          expression->span.begin.column);
        }
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    if text.count(indirect_anchor) != 1:
        raise SystemExit(f'M70 indirect anchor count={text.count(indirect_anchor)}')
    text = text.replace(indirect_anchor, indirect_replacement, 1)

    signature_anchor = '''    if (callee->is_variadic || expression->value.call.argument_count != callee->parameter_count ||
        (!returns_void && !core_memory_scalar_type(callee->return_type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    signature_replacement = '''    if (callee->is_variadic || expression->value.call.argument_count != callee->parameter_count ||
        (!returns_void && !core_memory_scalar_type(callee->return_type))) {
        if (core_fast_frontier_trace_enabled()) {
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=direct-call reason=signature callee=%s variadic=%d argc=%zu params=%zu ret_base=%d ret_ptr=%u\\n",
                          callee->name,
                          callee->is_variadic ? 1 : 0,
                          expression->value.call.argument_count,
                          callee->parameter_count,
                          (int)callee->return_type.base_kind,
                          (unsigned int)callee->return_type.pointer_depth);
        }
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    if text.count(signature_anchor) != 1:
        raise SystemExit(f'M70 signature anchor count={text.count(signature_anchor)}')
    text = text.replace(signature_anchor, signature_replacement, 1)

    parameter_anchor = '''    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (!core_memory_scalar_type(callee->parameter_types[argument_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
'''
    parameter_replacement = '''    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (!core_memory_scalar_type(callee->parameter_types[argument_index])) {
            if (core_fast_frontier_trace_enabled()) {
                (void)fprintf(stderr,
                              "CORE_FAST_TRACE stage=direct-call reason=param-type callee=%s arg=%zu base=%d ptr=%u\\n",
                              callee->name,
                              argument_index,
                              (int)callee->parameter_types[argument_index].base_kind,
                              (unsigned int)callee->parameter_types[argument_index].pointer_depth);
            }
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
'''
    if text.count(parameter_anchor) != 1:
        raise SystemExit(f'M70 parameter anchor count={text.count(parameter_anchor)}')
    text = text.replace(parameter_anchor, parameter_replacement, 1)

    argument_anchor = '''        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index] >= context->function->value_count ||
'''
    argument_replacement = '''        if (status != MINIC_CORE_LOWER_OK) {
            if (core_fast_frontier_trace_enabled()) {
                const MinicExpression *argument_expression = minic_c0_program_expression(
                    context->body->program, expression->value.call.arguments[argument_index]);
                (void)fprintf(stderr,
                              "CORE_FAST_TRACE stage=direct-call reason=arg-lower callee=%s arg=%zu status=%d kind=%d base=%d ptr=%u vc=%d span=%zu:%zu\\n",
                              callee->name,
                              argument_index,
                              (int)status,
                              argument_expression == NULL ? -1 : (int)argument_expression->kind,
                              argument_expression == NULL ? -1 : (int)argument_expression->type.base_kind,
                              argument_expression == NULL ? 0U : (unsigned int)argument_expression->type.pointer_depth,
                              argument_expression == NULL ? -1 : (int)argument_expression->value_category,
                              argument_expression == NULL ? 0U : argument_expression->span.begin.line,
                              argument_expression == NULL ? 0U : argument_expression->span.begin.column);
            }
            free(arguments);
            return status;
        }
        if (arguments[argument_index] >= context->function->value_count ||
'''
    direct_begin = text.find('static MinicCoreLowerStatus lower_direct_call(')
    if direct_begin < 0:
        raise SystemExit('M70 direct-call helper not found')
    direct_end = text.find('\nstatic MinicCoreLowerStatus ', direct_begin + 1)
    if direct_end < 0:
        raise SystemExit('M70 direct-call helper end not found')
    direct_text = text[direct_begin:direct_end]
    if direct_text.count(argument_anchor) != 1:
        raise SystemExit(f'M70 direct-call argument anchor count={direct_text.count(argument_anchor)}')
    direct_text = direct_text.replace(argument_anchor, argument_replacement, 1)
    text = text[:direct_begin] + direct_text + text[direct_end:]

    return_anchor = '''        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
'''
    return_replacement = '''        if (status != MINIC_CORE_LOWER_OK) {
            if (core_fast_frontier_trace_enabled()) {
                const MinicExpression *return_expression = minic_c0_program_expression(
                    context->body->program, statement->expression);
                (void)fprintf(stderr,
                              "CORE_FAST_TRACE stage=return function=%s status=%d kind=%d base=%d ptr=%u vc=%d span=%zu:%zu\\n",
                              context->source_function->name,
                              (int)status,
                              return_expression == NULL ? -1 : (int)return_expression->kind,
                              return_expression == NULL ? -1 : (int)return_expression->type.base_kind,
                              return_expression == NULL ? 0U : (unsigned int)return_expression->type.pointer_depth,
                              return_expression == NULL ? -1 : (int)return_expression->value_category,
                              return_expression == NULL ? 0U : return_expression->span.begin.line,
                              return_expression == NULL ? 0U : return_expression->span.begin.column);
            }
            return status;
        }
    }
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
'''
    if text.count(return_anchor) != 1:
        raise SystemExit(f'M70 return anchor count={text.count(return_anchor)}')
    text = text.replace(return_anchor, return_replacement, 1)

path.write_text(text)
print('M70 array lvalue pointer decay + fast frontier trace applied')
