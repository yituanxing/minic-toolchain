#!/usr/bin/env python3
from pathlib import Path

# Expose a perf-only ownership helper that reuses the canonical FunctionBody
# traversal. It walks each unique, non-specialized source body exactly once and
# records which source function structurally owns each expression.
h = Path("src/frontend/function_body.h")
text = h.read_text()
decl_anchor = "/* Verify structural function-body ownership and function-local semantic references. */\n"
decl = r'''/* Perf-only helper used by the Linux specialization audit.  Owners are
 * source-function ids; MINIC_FUNCTION_INVALID means unknown/orphan and
 * program->function_count is reserved internally for ambiguous shared nodes. */
bool minic_perf_program_build_expression_source_owners(
    const MinicC0Program *program,
    size_t function_limit,
    MinicFunctionId *owners,
    size_t owner_count);

'''
if text.count(decl_anchor) != 1:
    raise SystemExit(f"function-body header anchor count={text.count(decl_anchor)}")
text = text.replace(decl_anchor, decl + decl_anchor, 1)
h.write_text(text)

p = Path("src/frontend/function_body.c")
text = p.read_text()
impl_anchor = "bool minic_c0_program_validate_function_body_ownership(const MinicC0Program *program) {\n"
impl = r'''
bool minic_perf_program_build_expression_source_owners(
    const MinicC0Program *program,
    size_t function_limit,
    MinicFunctionId *owners,
    size_t owner_count) {
    MinicFunctionBodyValidation validation;
    MinicFunctionId ambiguous_owner;
    size_t function_index;
    size_t expression_index;
    bool success = false;

    if (program == NULL || (owner_count != 0U && owners == NULL) ||
        owner_count != program->expression_count ||
        function_limit > program->function_count) {
        return false;
    }
    ambiguous_owner = (MinicFunctionId)program->function_count;
    for (expression_index = 0U; expression_index < owner_count; ++expression_index) {
        owners[expression_index] = MINIC_FUNCTION_INVALID;
    }
    if (!initialize_validation(program, &validation)) {
        return false;
    }

    /* Integer/symbolic specialization clones share the source FunctionBody and
     * LocalId range.  Assign ownership only from canonical source functions;
     * the source functions are created before their clones. */
    for (function_index = 0U; function_index < function_limit; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        size_t local_index;

        if (!function->is_defined || function->is_integer_specialization) {
            continue;
        }
        if (function->body_block >= program->block_count ||
            function->local_begin > program->local_count ||
            function->local_count > program->local_count - function->local_begin ||
            function->parameter_count > function->local_count) {
            goto done;
        }
        for (local_index = 0U; local_index < function->local_count; ++local_index) {
            MinicLocalId local_id = function->local_begin + local_index;
            if (validation.local_owners[local_id] != MINIC_FUNCTION_INVALID) {
                goto done;
            }
            validation.local_owners[local_id] = (MinicFunctionId)function_index;
        }
    }

    for (function_index = 0U; function_index < function_limit; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];

        if (!function->is_defined || function->is_integer_specialization) {
            continue;
        }
        if (!validate_one_function(&validation, (MinicFunctionId)function_index)) {
            goto done;
        }
        for (expression_index = 0U;
             expression_index < validation.expression_work_count;
             ++expression_index) {
            MinicExpressionId expression_id =
                validation.expression_work[expression_index];
            MinicFunctionId prior;

            if (expression_id >= owner_count) {
                goto done;
            }
            prior = owners[expression_id];
            if (prior == MINIC_FUNCTION_INVALID) {
                owners[expression_id] = (MinicFunctionId)function_index;
            } else if (prior != (MinicFunctionId)function_index &&
                       prior != ambiguous_owner) {
                /* Keep shared/orphan-like nodes on the legacy path instead of
                 * filtering them by a guessed owner. */
                owners[expression_id] = ambiguous_owner;
            }
        }
    }
    success = true;

done:
    destroy_validation(&validation);
    return success;
}

'''
if text.count(impl_anchor) != 1:
    raise SystemExit(f"function-body impl anchor count={text.count(impl_anchor)}")
text = text.replace(impl_anchor, impl + impl_anchor, 1)
p.write_text(text)

# Use the ownership map only as an early reject in the existing transitive
# symbolic loop.  Keep the loop order and all rewrite semantics unchanged.
p = Path("src/compiler/compiler.c")
text = p.read_text()

block_anchor = '''    {
        size_t caller_index;
        (void)original_function_count;
        for (caller_index = 0U;
'''
block_repl = '''    {
        size_t caller_index;
        size_t symbolic_owned_expression_count = program->expression_count;
        MinicFunctionId *symbolic_expression_owners =
            symbolic_owned_expression_count == 0U
                ? NULL
                : (MinicFunctionId *)malloc(
                      symbolic_owned_expression_count *
                      sizeof(*symbolic_expression_owners));

        if ((symbolic_owned_expression_count != 0U &&
             symbolic_expression_owners == NULL) ||
            !minic_perf_program_build_expression_source_owners(
                program,
                original_function_count,
                symbolic_expression_owners,
                symbolic_owned_expression_count)) {
            free(symbolic_expression_owners);
            return false;
        }
        (void)original_function_count;
        for (caller_index = 0U;
'''
if text.count(block_anchor) != 1:
    raise SystemExit(f"symbolic owner block anchor count={text.count(block_anchor)}")
text = text.replace(block_anchor, block_repl, 1)

nested_anchor = '''                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];
                MinicFunctionId current_id;
'''
nested_repl = '''                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];
                MinicFunctionId current_id;

                if (nested_expression_index < symbolic_owned_expression_count) {
                    MinicFunctionId nested_owner =
                        symbolic_expression_owners[nested_expression_index];
                    if (nested_owner != MINIC_FUNCTION_INVALID &&
                        nested_owner < original_function_count &&
                        nested_owner != caller.specialization_source) {
                        continue;
                    }
                }
'''
owner_pos = text.find("size_t symbolic_owned_expression_count")
nested_pos = text.find(nested_anchor, owner_pos)
if nested_pos < 0:
    raise SystemExit("cannot find transitive symbolic nested site")
text = text[:nested_pos] + text[nested_pos:].replace(nested_anchor, nested_repl, 1)

failure_anchor = '''                    if (!minic_add_inline_symbolic_specialization(
                            program,
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            &variant_id)) {
                        return false;
                    }
'''
failure_repl = '''                    if (!minic_add_inline_symbolic_specialization(
                            program,
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            &variant_id)) {
                        free(symbolic_expression_owners);
                        return false;
                    }
'''
# There is one transitive failure site and one direct site with nearly the same
# shape; target the indented transitive occurrence after the owner block.
owner_pos = text.find("size_t symbolic_owned_expression_count")
failure_pos = text.find(failure_anchor, owner_pos)
if failure_pos < 0:
    raise SystemExit("cannot find transitive symbolic failure site")
text = text[:failure_pos] + text[failure_pos:].replace(failure_anchor, failure_repl, 1)

end_anchor = '''        }
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
'''
end_repl = '''        }
        free(symbolic_expression_owners);
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
'''
if text.count(end_anchor) != 1:
    raise SystemExit(f"symbolic owner end anchor count={text.count(end_anchor)}")
text = text.replace(end_anchor, end_repl, 1)

p.write_text(text)
print("MINIC_PERF_SYMBOLIC_FUNCTION_BODY_OWNER_FILTER_V0=APPLIED")
