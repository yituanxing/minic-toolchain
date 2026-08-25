#!/usr/bin/env python3
from pathlib import Path

MARKER = "M158_FINAL_STRICT_TAIL"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# Core IR: runtime CTZ is a target-neutral integer semantic primitive; computed
# goto is a CFG terminator whose operand is an already-lowered block address.
path = Path("src/core/core_ir.h")
text = path.read_text()
if MARKER not in text:
    text = replace_once(
        text,
        "    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT,\n    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,",
        "    MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT,\n"
        "    /* M158_FINAL_STRICT_TAIL: semantic count-trailing-zero primitive. */\n"
        "    MINIC_CORE_INSTRUCTION_INTEGER_CTZ,\n"
        "    MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO,",
        "core-ir ctz enum",
    )
    text = replace_once(
        text,
        "    MINIC_CORE_TERMINATOR_BRANCH,\n    MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH,",
        "    MINIC_CORE_TERMINATOR_BRANCH,\n"
        "    /* M158_FINAL_STRICT_TAIL: GNU computed goto consumes a pointer value. */\n"
        "    MINIC_CORE_TERMINATOR_INDIRECT_BRANCH,\n"
        "    MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH,",
        "core-ir indirect terminator enum",
    )
    text = replace_once(
        text,
        "    MinicCoreBlockId branch_target;\n    struct {",
        "    MinicCoreBlockId branch_target;\n"
        "    MinicCoreValueId indirect_target;\n"
        "    struct {",
        "core-ir indirect terminator value",
    )
    text = text.replace(
        "typedef enum MinicCoreInstructionKind {",
        "/* M158_FINAL_STRICT_TAIL */\n"
        "typedef enum MinicCoreInstructionKind {",
        1,
    )
    path.write_text(text)

path = Path("src/core/core_ir.c")
text = path.read_text()
if "M158_FINAL_STRICT_TAIL_CTZ_VERIFY" not in text:
    text = replace_once(
        text,
        "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n"
        "        return instruction_result_is_valid(function, instruction) &&",
        "    /* M158_FINAL_STRICT_TAIL_CTZ_VERIFY: __builtin_ctzl has an\n"
        "       unsigned-long operand and int result in the normalized AST. */\n"
        "    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:\n"
        "        return instruction_result_is_valid(function, instruction) &&\n"
        "               minic_type_equal(instruction->type, minic_type_int()) &&\n"
        "               instruction->value.operand < function->value_count &&\n"
        "               available_values[instruction->value.operand] &&\n"
        "               minic_type_equal(function->values[instruction->value.operand].type,\n"
        "                                minic_type_unsigned_long());\n"
        "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n"
        "        return instruction_result_is_valid(function, instruction) &&",
        "core-ir ctz verifier",
    )
    text = replace_once(
        text,
        "    case MINIC_CORE_TERMINATOR_BRANCH:\n"
        "        return terminator->branch_target < function->block_count;\n"
        "    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:",
        "    case MINIC_CORE_TERMINATOR_BRANCH:\n"
        "        return terminator->branch_target < function->block_count;\n"
        "    /* M158_FINAL_STRICT_TAIL_INDIRECT_BRANCH_VERIFY: the source\n"
        "       expression was accepted by GNU goto semantics; Core owns only\n"
        "       the pointer-valued dynamic control-flow edge. */\n"
        "    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:\n"
        "        return terminator->indirect_target < function->value_count &&\n"
        "               available_values[terminator->indirect_target] &&\n"
        "               minic_type_is_pointer(function->values[terminator->indirect_target].type);\n"
        "    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:",
        "core-ir indirect terminator verifier",
    )
    text = replace_once(
        text,
        "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n"
        "        return fprintf(output,\n"
        "                       \"  %%%\" PRIu32 \" = scalar.is_zero %%%\" PRIu32 \"\\n\",",
        "    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:\n"
        "        return fprintf(output,\n"
        "                       \"  %%%\" PRIu32 \" = ctz.int %%%\" PRIu32 \"\\n\",\n"
        "                       instruction->result,\n"
        "                       instruction->value.operand) >= 0;\n"
        "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n"
        "        return fprintf(output,\n"
        "                       \"  %%%\" PRIu32 \" = scalar.is_zero %%%\" PRIu32 \"\\n\",",
        "core-ir ctz dump",
    )
    text = replace_once(
        text,
        "    case MINIC_CORE_TERMINATOR_BRANCH:\n"
        "        return fprintf(output, \"  br bb%\" PRIu32 \"\\n\", terminator->branch_target) >= 0;\n"
        "    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:",
        "    case MINIC_CORE_TERMINATOR_BRANCH:\n"
        "        return fprintf(output, \"  br bb%\" PRIu32 \"\\n\", terminator->branch_target) >= 0;\n"
        "    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:\n"
        "        return fprintf(output, \"  indirect_br %%%\" PRIu32 \"\\n\",\n"
        "                       terminator->indirect_target) >= 0;\n"
        "    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:",
        "core-ir indirect terminator dump",
    )
    path.write_text(text)

# Lowering: (1) effect-only void statement-expression with an explicit result,
# (2) runtime ctzl, (3) GNU computed goto, plus exact ingress diagnostics for
# the sole remaining no-trace TU.
path = Path("src/core/core_lower.c")
text = path.read_text()
if "M158_FINAL_STRICT_TAIL_VOID_STMT_EXPR" not in text:
    anchor = '''        if (statement_block == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
'''
    replacement = '''        if (statement_block == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M158_FINAL_STRICT_TAIL_VOID_STMT_EXPR: GNU macros commonly wrap an
           effect expression as `(void)({ call(); })`.  Such a statement
           expression has a real result expression id even though both that
           result and the whole expression are void.  Execute the owned block,
           then the result expression, and require that neither manufactures an
           SSA value.  Scalar statement expressions keep the existing path. */
        if (minic_type_is_void(expression->type) &&
            expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID) {
            statement_result = minic_c0_program_expression(
                context->body->program, expression->value.statement_expression.result);
            if (statement_result == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (!minic_type_is_void(statement_result->type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_block(context, statement_block, &terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (terminated) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_expression(
                context, expression->value.statement_expression.result, &result_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (result_value != MINIC_CORE_VALUE_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            *value_id = MINIC_CORE_VALUE_INVALID;
            return MINIC_CORE_LOWER_OK;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
'''
    text = replace_once(text, anchor, replacement, "void statement-expression")

if "M158_FINAL_STRICT_TAIL_CTZ_LOWER" not in text:
    anchor = '''    if (expression->kind == MINIC_EXPRESSION_CALL) {
'''
    replacement = '''    /* M158_FINAL_STRICT_TAIL_CTZ_LOWER: keep the builtin semantic in
       Core instead of expanding a target loop in the frontend.  The RV64
       backend preserves the established ctzl(0) == 64 baseline behavior. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CTZL) {
        const MinicExpression *operand;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_equal(operand->type, minic_type_unsigned_long()) ||
            !minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(
            context, expression->value.builtin_unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type,
                              minic_type_unsigned_long())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CTZ;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    if (expression->kind == MINIC_EXPRESSION_CALL) {
'''
    text = replace_once(text, anchor, replacement, "ctzl lowering")

if "M158_FINAL_STRICT_TAIL_COMPUTED_GOTO" not in text:
    old = '''            case MINIC_STATEMENT_GOTO: {
                const MinicStatement *target_statement;
                MinicCoreBlockId target_block;

                if (statement->target_expression != MINIC_EXPRESSION_INVALID ||
                    statement->expression != MINIC_EXPRESSION_INVALID ||
                    statement->target_statement == MINIC_STATEMENT_INVALID) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                target_statement = minic_c0_program_statement(
                    context->body->program, statement->target_statement);
                if (target_statement == NULL ||
                    target_statement->kind != MINIC_STATEMENT_LABEL) {
                    status = MINIC_CORE_LOWER_ERROR;
                    break;
                }
                status = ensure_statement_block(
                    context, statement->target_statement, &target_block);
                if (status == MINIC_CORE_LOWER_OK) {
                    status = set_branch(
                        context, context->block_id, statement->span, target_block);
                }
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            }
'''
    new = '''            case MINIC_STATEMENT_GOTO: {
                const MinicStatement *target_statement;
                MinicCoreBlockId target_block;

                /* M158_FINAL_STRICT_TAIL_COMPUTED_GOTO: &&label already lowers
                   to BLOCK_ADDRESS.  Preserve GNU `goto *expr` as a first-class
                   Core CFG edge instead of pretending it is an ordinary branch. */
                if (statement->target_expression != MINIC_EXPRESSION_INVALID) {
                    MinicCoreTerminator terminator;
                    MinicCoreValueId target_value;

                    if (statement->expression != MINIC_EXPRESSION_INVALID ||
                        statement->target_statement != MINIC_STATEMENT_INVALID) {
                        status = MINIC_CORE_LOWER_UNSUPPORTED;
                        break;
                    }
                    status = lower_expression(
                        context, statement->target_expression, &target_value);
                    if (status != MINIC_CORE_LOWER_OK) {
                        break;
                    }
                    if (target_value >= context->function->value_count ||
                        !minic_type_is_pointer(context->function->values[target_value].type)) {
                        status = MINIC_CORE_LOWER_UNSUPPORTED;
                        break;
                    }
                    (void)memset(&terminator, 0, sizeof(terminator));
                    terminator.kind = MINIC_CORE_TERMINATOR_INDIRECT_BRANCH;
                    terminator.span = statement->span;
                    terminator.return_value = MINIC_CORE_VALUE_INVALID;
                    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
                    terminator.indirect_target = target_value;
                    status = minic_core_function_set_terminator(
                                 context->function, context->block_id, &terminator)
                                 ? MINIC_CORE_LOWER_OK
                                 : MINIC_CORE_LOWER_ERROR;
                    statement_terminated = status == MINIC_CORE_LOWER_OK;
                    break;
                }
                if (statement->expression != MINIC_EXPRESSION_INVALID ||
                    statement->target_statement == MINIC_STATEMENT_INVALID) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                target_statement = minic_c0_program_statement(
                    context->body->program, statement->target_statement);
                if (target_statement == NULL ||
                    target_statement->kind != MINIC_STATEMENT_LABEL) {
                    status = MINIC_CORE_LOWER_ERROR;
                    break;
                }
                status = ensure_statement_block(
                    context, statement->target_statement, &target_block);
                if (status == MINIC_CORE_LOWER_OK) {
                    status = set_branch(
                        context, context->block_id, statement->span, target_block);
                }
                statement_terminated = status == MINIC_CORE_LOWER_OK;
                break;
            }
'''
    text = replace_once(text, old, new, "computed goto")

if "CORE_M158_INGRESS_DETAIL" not in text:
    old = '''        if (minic_type_is_volatile(parameter->type) || parameter->is_array ||
            parameter->is_register_storage ||
            !minic_type_unqualified(parameter->type, &parameter_value_type) ||
            !minic_type_equal(parameter_value_type,
                              context->source_function->parameter_types[parameter_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_local_object(context, local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
'''
    new = '''        if (minic_type_is_volatile(parameter->type) || parameter->is_array ||
            parameter->is_register_storage ||
            !minic_type_unqualified(parameter->type, &parameter_value_type) ||
            !minic_type_equal(parameter_value_type,
                              context->source_function->parameter_types[parameter_index])) {
            (void)fprintf(stderr,
                          "CORE_M158_INGRESS_DETAIL function=%s parameter=%zu "
                          "volatile=%d array=%d register=%d raw_kind=%d signature_kind=%d\\n",
                          context->source_function->name,
                          parameter_index,
                          minic_type_is_volatile(parameter->type) ? 1 : 0,
                          parameter->is_array ? 1 : 0,
                          parameter->is_register_storage ? 1 : 0,
                          (int)parameter->type.kind,
                          (int)context->source_function->parameter_types[parameter_index].kind);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_local_object(context, local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "CORE_M158_INGRESS_DETAIL function=%s parameter=%zu local_object_status=%d\\n",
                          context->source_function->name,
                          parameter_index,
                          (int)status);
            return status;
        }
'''
    text = replace_once(text, old, new, "ingress detail")
    # Also identify a non-scalar post-object ingress rejection.
    text = replace_once(
        text,
        '''        if (!core_memory_scalar_type(parameter_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
''',
        '''        if (!core_memory_scalar_type(parameter_value_type)) {
            (void)fprintf(stderr,
                          "CORE_M158_INGRESS_DETAIL function=%s parameter=%zu nonscalar_kind=%d\\n",
                          context->source_function->name,
                          parameter_index,
                          (int)parameter_value_type.kind);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
''',
        "ingress nonscalar detail",
    )
    path.write_text(text)

# RV64 backend owns the concrete implementations.
path = Path("src/target/riscv64/core_codegen.c")
text = path.read_text()
if "M158_FINAL_STRICT_TAIL_CTZ_RV64" not in text:
    text = replace_once(
        text,
        "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
        "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n"
        "        return true;",
        "    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:\n"
        "    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:\n"
        "    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:\n"
        "        return true;",
        "rv64 ctz capability",
    )
    text = replace_once(
        text,
        '''    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  seqz t0, t0\\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
''',
        '''    /* M158_FINAL_STRICT_TAIL_CTZ_RV64: RV64I baseline, intentionally
       no Zbb dependency. Keep the legacy compiler's zero-input result (64). */
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  beqz t0, .L%s_core_ctz_zero_%%" PRIu32 "\\n"
                    "  li t1, 0\\n"
                    ".L%s_core_ctz_loop_%%" PRIu32 ":\\n"
                    "  andi t2, t0, 1\\n"
                    "  bnez t2, .L%s_core_ctz_done_%%" PRIu32 "\\n"
                    "  addi t1, t1, 1\\n"
                    "  srli t0, t0, 1\\n"
                    "  j .L%s_core_ctz_loop_%%" PRIu32 "\\n"
                    ".L%s_core_ctz_zero_%%" PRIu32 ":\\n"
                    "  li t1, 64\\n"
                    ".L%s_core_ctz_done_%%" PRIu32 ":\\n",
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  seqz t0, t0\\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
''',
        "rv64 ctz emit",
    )
    text = replace_once(
        text,
        '''    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(
                   file, "  j .L%s_core_bb%" PRIu32 "\\n", symbol_name, terminator->branch_target) >=
               0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
''',
        '''    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(
                   file, "  j .L%s_core_bb%" PRIu32 "\\n", symbol_name, terminator->branch_target) >=
               0;
    /* M158_FINAL_STRICT_TAIL_INDIRECT_BRANCH_RV64 */
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return load_core_value(file, frame, terminator->indirect_target, "t0") &&
               fprintf(file, "  jalr zero, t0, 0\\n") >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
''',
        "rv64 indirect branch emit",
    )
    path.write_text(text)

Path("tests/compiler/c0/m158_final_strict_tail.c").write_text(r'''extern void m158_sink(void);

int m158_ctzl(unsigned long x) {
    return __builtin_ctzl(x);
}

void m158_void_statement_expression(void) {
    (void)({ m158_sink(); });
}

int m158_computed_goto(int which) {
    void *target = which ? &&one : &&two;
    goto *target;
one:
    return 1;
two:
    return 2;
}
''')
print("staged M158 final strict tail")
