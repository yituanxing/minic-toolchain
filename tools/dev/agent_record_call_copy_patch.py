from pathlib import Path

root = Path(__file__).resolve().parents[2]

def replace_exact(path, old, new, expected=1, label="replacement"):
    text = path.read_text()
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected}, found {count}")
    path.write_text(text.replace(old, new))

# Frontend semantic source classification: address-backed values plus direct
# record-returning calls are valid copy sources. Keep arbitrary record rvalues bounded.
ast_h = root / "src/frontend/ast.h"
replace_exact(
    ast_h,
    '''bool minic_c0_record_value_is_address_backed(const MinicC0Program *program,
                                             MinicExpressionId expression_id);
''',
    '''bool minic_c0_record_value_is_address_backed(const MinicC0Program *program,
                                             MinicExpressionId expression_id);
bool minic_c0_record_value_is_copy_source(const MinicC0Program *program,
                                          MinicExpressionId expression_id);
''',
    label="copy-source declaration",
)

ast_c = root / "src/frontend/ast.c"
text = ast_c.read_text()
anchor = '''static bool expression_is_null_pointer_value(const MinicC0Program *program,
                                             MinicExpressionId expression_id) {
'''
helper = '''bool minic_c0_record_value_is_copy_source(const MinicC0Program *program,
                                          MinicExpressionId expression_id) {
    const MinicExpression *expression;

    if (program == NULL) {
        return false;
    }
    if (minic_c0_record_value_is_address_backed(program, expression_id)) {
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
    return expression != NULL && expression->kind == MINIC_EXPRESSION_CALL &&
           expression->value_category == MINIC_VALUE_RVALUE &&
           minic_type_is_record(expression->type);
}

'''
if text.count(anchor) != 1:
    raise SystemExit("copy-source insertion anchor mismatch")
ast_c.write_text(text.replace(anchor, helper + anchor, 1))

# Parser and verifier consume the broader copy-source contract, while the old
# address-backed predicate remains available to the target address path.
for relative, expected in [
    ("src/frontend/parser_statement.c", 3),
    ("src/frontend/parser_expression.c", 1),
    ("src/frontend/ast_verifier.c", 2),
]:
    path = root / relative
    text = path.read_text()
    old = "minic_c0_record_value_is_address_backed"
    if text.count(old) != expected:
        raise SystemExit(f"{relative}: expected {expected} address-backed checks, found {text.count(old)}")
    path.write_text(text.replace(old, "minic_c0_record_value_is_copy_source"))

# Make diagnostics describe the semantic contract rather than one storage form.
parser_statement = root / "src/frontend/parser_statement.c"
text = parser_statement.read_text()
text = text.replace(
    "record local initializer requires a matching address-backed record value",
    "record local initializer requires a matching record copy source",
)
text = text.replace(
    "record assignment requires matching address-backed record values",
    "record assignment requires a matching record copy source",
)
text = text.replace(
    "GNU __auto_type record initializer must be address-backed",
    "GNU __auto_type record initializer requires a supported record copy source",
)
parser_statement.write_text(text)

parser_expression = root / "src/frontend/parser_expression.c"
text = parser_expression.read_text()
text = text.replace(
    "record assignment expression requires matching address-backed record values",
    "record assignment expression requires a matching record copy source",
)
parser_expression.write_text(text)

# One RV64 record-copy implementation owns both expression and statement lowering.
internal_h = root / "src/target/riscv64/codegen_internal.h"
replace_exact(
    internal_h,
    '''bool minic_riscv64_emit_address_backed_record_value(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicFunction *function,
                                                    MinicExpressionId expression_id);
''',
    '''bool minic_riscv64_emit_address_backed_record_value(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicFunction *function,
                                                    MinicExpressionId expression_id);
bool minic_riscv64_emit_record_copy_value(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          MinicExpressionId target_id,
                                          MinicExpressionId source_id,
                                          bool preserve_target_address);
''',
    label="record-copy target prototype",
)

codegen_expression = root / "src/target/riscv64/codegen_expression.c"
text = codegen_expression.read_text()
start = text.find("static bool minic_riscv64_emit_record_assignment_expression(")
end = text.find("static bool minic_riscv64_emit_builtin_unary(", start)
if start < 0 or end < 0:
    raise SystemExit("record assignment emitter boundaries missing")
replacement = r'''static bool minic_riscv64_emit_record_value_temporary(FILE *file,
                                                       const MinicC0Program *program,
                                                       const MinicFunction *function,
                                                       MinicExpressionId source_id,
                                                       size_t storage_size,
                                                       size_t temporary_size) {
    const MinicExpression *source;

    source = minic_c0_program_expression(program, source_id);
    if (source == NULL || !minic_type_is_record(source->type) ||
        !minic_c0_record_value_is_copy_source(program, source_id)) {
        return false;
    }
    if (minic_c0_record_value_is_address_backed(program, source_id)) {
        size_t index;

        if (!minic_riscv64_emit_address_backed_record_value(file, program, function, source_id) ||
            !minic_riscv64_emit_stack_allocate(file, temporary_size) ||
            fprintf(file, "  mv t2, a0\n  mv t3, sp\n") < 0) {
            return false;
        }
        for (index = 0U; index < storage_size; ++index) {
            if (fprintf(file,
                        "  lbu t0, 0(t2)\n"
                        "  sb t0, 0(t3)\n"
                        "  addi t2, t2, 1\n"
                        "  addi t3, t3, 1\n") < 0) {
                return false;
            }
        }
        return true;
    }
    if (source->kind == MINIC_EXPRESSION_CALL) {
        size_t aggregate_size;
        size_t aggregate_chunks;

        if (!minic_riscv64_integer_aggregate_abi(
                program, source->type, &aggregate_size, &aggregate_chunks) ||
            aggregate_size != storage_size ||
            !minic_riscv64_emit_expression(file, program, function, source_id) ||
            !minic_riscv64_emit_stack_allocate(file, temporary_size) ||
            fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
            (aggregate_chunks == 2U && fprintf(file, "  sd a1, 8(sp)\n") < 0)) {
            return false;
        }
        return aggregate_chunks == 1U || aggregate_chunks == 2U;
    }
    return false;
}

bool minic_riscv64_emit_record_copy_value(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          MinicExpressionId target_id,
                                          MinicExpressionId source_id,
                                          bool preserve_target_address) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    size_t storage_size;
    size_t temporary_size;
    size_t index;

    target = minic_c0_program_expression(program, target_id);
    source = minic_c0_program_expression(program, source_id);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(target->type) || !minic_type_is_record(target->type) ||
        !minic_type_is_record(source->type) || target->type.record_id != source->type.record_id ||
        !minic_c0_record_value_is_copy_source(program, source_id)) {
        return false;
    }
    record = minic_c0_program_record(program, target->type.record_id);
    if (record == NULL || !record->is_complete || record->storage_size == 0U ||
        record->storage_size > SIZE_MAX - 15U) {
        return false;
    }
    storage_size = record->storage_size;
    temporary_size = (storage_size + 15U) & ~(size_t)15U;

    if (!minic_riscv64_emit_record_value_temporary(
            file, program, function, source_id, storage_size, temporary_size) ||
        !minic_riscv64_emit_lvalue_address(file, program, function, target_id) ||
        (preserve_target_address && fprintf(file, "  mv t4, a0\n") < 0) ||
        fprintf(file, "  mv t2, sp\n  mv t3, a0\n") < 0) {
        return false;
    }
    for (index = 0U; index < storage_size; ++index) {
        if (fprintf(file,
                    "  lbu t0, 0(t2)\n"
                    "  sb t0, 0(t3)\n"
                    "  addi t2, t2, 1\n"
                    "  addi t3, t3, 1\n") < 0) {
            return false;
        }
    }
    if (!minic_riscv64_emit_stack_release(file, temporary_size)) {
        return false;
    }
    return !preserve_target_address || fprintf(file, "  mv a0, t4\n") >= 0;
}

static bool minic_riscv64_emit_record_assignment_expression(FILE *file,
                                                            const MinicC0Program *program,
                                                            const MinicFunction *function,
                                                            const MinicExpression *expression) {
    const MinicExpression *target;

    target = minic_c0_program_expression(program, expression->value.binary.left);
    return target != NULL && minic_type_equal(expression->type, target->type) &&
           minic_riscv64_emit_record_copy_value(file,
                                                program,
                                                function,
                                                expression->value.binary.left,
                                                expression->value.binary.right,
                                                true);
}

'''
codegen_expression.write_text(text[:start] + replacement + text[end:])

codegen_statement = root / "src/target/riscv64/codegen_statement.c"
text = codegen_statement.read_text()
start = text.find("static bool minic_riscv64_emit_record_copy(")
end = text.find("static bool minic_riscv64_emit_xor_assignment(", start)
if start < 0 or end < 0:
    raise SystemExit("statement record-copy emitter boundaries missing")
replacement = r'''static bool minic_riscv64_emit_record_copy(FILE *file,
                                           const MinicC0Program *program,
                                           const MinicFunction *function,
                                           const MinicStatement *statement) {
    return statement != NULL &&
           minic_riscv64_emit_record_copy_value(file,
                                                program,
                                                function,
                                                statement->target_expression,
                                                statement->expression,
                                                false);
}

'''
codegen_statement.write_text(text[:start] + replacement + text[end:])

# Evolve the existing record-value contract: preserve address-backed behavior,
# now accept 8/16-byte integer-aggregate calls, while keeping larger returns bounded.
runner = root / "tests/compiler/c0/run-gnu-statement-record-value.sh"
text = runner.read_text()
old = r'''cat >"$work/record_call.c" <<'EOF'
typedef struct item { long value; } item_t;
extern item_t make_item(void);
item_t still_bounded(void)
{
    item_t value = make_item();
    return value;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/record_call.c" -o "$work/record_call.i"
if "$minic" -S "$work/record_call.i" -o "$work/record_call.s" \
    >"$work/record_call.stdout" 2>"$work/record_call.stderr"; then
    printf '%s\n' 'record call rvalue unexpectedly entered address-backed RECORD_COPY path' >&2
    exit 1
fi
grep -F 'record local initializer requires a matching address-backed record value' \
    "$work/record_call.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/gnu_statement_record_value initializer=record-rvalue assignment=record-rvalue source=address-backed statement-expression lvalue=unchanged call-rvalue=bounded'
'''
new = r'''cat >"$work/record_call.c" <<'EOF'
typedef struct item { long value; } item_t;
typedef struct pair { long first; long second; } pair_t;
typedef struct large { long first; long second; long third; } large_t;
extern item_t make_item(void);
extern pair_t make_pair(long value);
extern large_t make_large(void);

item_t initialize_from_call(void)
{
    item_t value = make_item();
    return value;
}

void assign_from_call(pair_t *target, long value)
{
    *target = make_pair(value);
}

pair_t infer_from_call(long value)
{
    __auto_type inferred = make_pair(value);
    return inferred;
}

void unsupported_large_call(large_t *target)
{
    *target = make_large();
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/record_call.c" -o "$work/record_call.i"
# The supported 8/16-byte forms must compile; keep the 24-byte target boundary separate.
sed '/void unsupported_large_call/,$d' "$work/record_call.i" >"$work/record_call_supported.i"
"$minic" -S "$work/record_call_supported.i" -o "$work/record_call.s"
grep -F '  call make_item' "$work/record_call.s" >/dev/null
grep -F '  call make_pair' "$work/record_call.s" >/dev/null
grep -F '  sd a0, 0(sp)' "$work/record_call.s" >/dev/null
grep -F '  sd a1, 8(sp)' "$work/record_call.s" >/dev/null
call_copy_loads=$(grep -c '^  lbu t0, 0(t2)$' "$work/record_call.s")
call_copy_stores=$(grep -c '^  sb t0, 0(t3)$' "$work/record_call.s")
test "$call_copy_loads" -ge 40
test "$call_copy_stores" -ge 40

cat >"$work/large_call.c" <<'EOF'
typedef struct large { long first; long second; long third; } large_t;
extern large_t make_large(void);
void unsupported_large_call(large_t *target)
{
    *target = make_large();
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/large_call.c" -o "$work/large_call.i"
if "$minic" -S "$work/large_call.i" -o "$work/large_call.s" \
    >"$work/large_call.stdout" 2>"$work/large_call.stderr"; then
    printf '%s\n' 'unsupported 24-byte record call copy unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'PASS compiler/c0/gnu_statement_record_value initializer=record-rvalue assignment=record-rvalue address-backed=preserved call-rvalue=8+16-byte register-backed auto-type=1 large-call=bounded lvalue=unchanged'
'''
if text.count(old) != 1:
    raise SystemExit("record-call test contract anchor mismatch")
runner.write_text(text.replace(old, new, 1))

print("PASS generated record-call copy-source slice")
