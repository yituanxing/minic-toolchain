from pathlib import Path
import re


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def once(text, old, new, label):
    if text.count(old) != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {text.count(old)}")
    return text.replace(old, new, 1)

# AST relocation schema + APIs.
p = "src/frontend/ast.h"
t = read(p)
t = once(t,
'''    size_t target_member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t target_member_depth;
    bool has_explicit_pointer_cast;''',
'''    size_t target_member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t target_member_depth;
    int64_t target_byte_addend;
    bool has_explicit_pointer_cast;''', "ast relocation field")
needle = '''bool minic_c0_global_object_add_object_relocation_path_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth);'''
insert = needle + '''
bool minic_c0_global_object_add_object_relocation_path_addend(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth,
    int64_t target_byte_addend);
bool minic_c0_global_object_add_object_relocation_path_addend_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth,
    int64_t target_byte_addend);'''
t = once(t, needle, insert, "ast relocation APIs")
write(p, t)

# Persistent relocation owner.
p = "src/frontend/ast_global.c"
t = read(p)
t = once(t,
'''                                         const size_t *target_member_indices,
                                         size_t target_member_depth,
                                         bool has_explicit_pointer_cast) {''',
'''                                         const size_t *target_member_indices,
                                         size_t target_member_depth,
                                         int64_t target_byte_addend,
                                         bool has_explicit_pointer_cast) {''', "relocation owner signature")
t = once(t,
'''        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         (target_id >= program->function_count || target_member_depth != 0U))) {''',
'''        (target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
         (target_id >= program->function_count || target_member_depth != 0U ||
          target_byte_addend != 0))) {''', "function addend invariant")
t = once(t,
'''    relocation->target_member_depth = target_member_depth;
    relocation->has_explicit_pointer_cast = has_explicit_pointer_cast;''',
'''    relocation->target_member_depth = target_member_depth;
    relocation->target_byte_addend = target_byte_addend;
    relocation->has_explicit_pointer_cast = has_explicit_pointer_cast;''', "persist addend")
# Function relocation wrappers always use zero addend.
t = t.replace('''                                        function_id,
                                        NULL,
                                        0U,
                                        false);''', '''                                        function_id,
                                        NULL,
                                        0U,
                                        0,
                                        false);''')
t = t.replace('''                                        function_id,
                                        NULL,
                                        0U,
                                        true);''', '''                                        function_id,
                                        NULL,
                                        0U,
                                        0,
                                        true);''')
start = t.index("bool minic_c0_global_object_add_object_relocation_path(\n")
end = t.index("bool minic_c0_global_object_add_object_relocation(MinicC0Program *program,", start)
new_block = '''bool minic_c0_global_object_add_object_relocation_path_addend(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth,
    int64_t target_byte_addend) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        target_byte_addend,
                                        false);
}

bool minic_c0_global_object_add_object_relocation_path_addend_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth,
    int64_t target_byte_addend) {
    return add_global_symbol_relocation(program,
                                        global_object_id,
                                        location_kind,
                                        location_index,
                                        MINIC_GLOBAL_RELOCATION_OBJECT,
                                        target_object_id,
                                        target_member_indices,
                                        target_member_depth,
                                        target_byte_addend,
                                        true);
}

bool minic_c0_global_object_add_object_relocation_path(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth) {
    return minic_c0_global_object_add_object_relocation_path_addend(program,
                                                                    global_object_id,
                                                                    location_kind,
                                                                    location_index,
                                                                    target_object_id,
                                                                    target_member_indices,
                                                                    target_member_depth,
                                                                    0);
}

bool minic_c0_global_object_add_object_relocation_path_cast(
    MinicC0Program *program,
    MinicGlobalObjectId global_object_id,
    MinicGlobalRelocationLocationKind location_kind,
    size_t location_index,
    MinicGlobalObjectId target_object_id,
    const size_t *target_member_indices,
    size_t target_member_depth) {
    return minic_c0_global_object_add_object_relocation_path_addend_cast(program,
                                                                         global_object_id,
                                                                         location_kind,
                                                                         location_index,
                                                                         target_object_id,
                                                                         target_member_indices,
                                                                         target_member_depth,
                                                                         0);
}

'''
t = t[:start] + new_block + t[end:]
write(p, t)

# DataLayout: member-path offset plus signed relocation byte addend.
p = "src/target/data_layout.h"
t = read(p)
t = once(t, "                                                       size_t *addend);",
         "                                                       int64_t *addend);", "layout addend API")
write(p, t)

p = "src/target/data_layout.c"
t = read(p)
t = once(t,
'''                                                       const MinicGlobalRelocation *relocation,
                                                       size_t *addend) {''',
'''                                                       const MinicGlobalRelocation *relocation,
                                                       int64_t *addend) {''', "layout addend signature")
t = once(t,
'''    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        if (relocation->target_member_depth != 0U) {''',
'''    if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
        if (relocation->target_member_depth != 0U || relocation->target_byte_addend != 0) {''', "function layout addend")
t = once(t,
'''    *addend = result;
    return true;
}''',
'''    if (result > (size_t)INT64_MAX ||
        (relocation->target_byte_addend > 0 &&
         (uint64_t)relocation->target_byte_addend > (uint64_t)INT64_MAX - (uint64_t)result)) {
        return false;
    }
    *addend = (int64_t)result + relocation->target_byte_addend;
    return true;
}''', "compose target addend")
write(p, t)

# RV64 textual relocation emission supports signed addends.
p = "src/target/riscv64/codegen_function.c"
t = read(p)
t = once(t, "    size_t target_addend;", "    int64_t target_addend;", "rv64 addend type")
t = once(t,
'''    return fprintf(file, "  %s %s+%zu\\n", directive, target_name, target_addend) >= 0;''',
'''    if (target_addend > 0) {
        return fprintf(file, "  %s %s+%" PRId64 "\\n", directive, target_name, target_addend) >= 0;
    }
    return fprintf(file, "  %s %s%" PRId64 "\\n", directive, target_name, target_addend) >= 0;''', "rv64 signed addend emission")
write(p, t)

# Verifier consumes the signed addend contract.
p = "src/frontend/ast_verifier.c"
t = read(p)
t = once(t, "                    size_t target_addend;", "                    int64_t target_addend;", "verifier addend type")
write(p, t)

# Frontend static address constants.
p = "src/frontend/parser_global.c"
t = read(p)
old_start = t.index("static bool static_object_address_relocation_target(")
old_end = t.index("typedef struct MinicStaticObjectRelocationTarget", old_start)
new_target = r'''static bool static_pointer_offset_bytes(const MinicParser *parser,
                                        MinicType pointee_type,
                                        MinicExpressionId offset_expression_id,
                                        bool subtract,
                                        int64_t *byte_offset) {
    MinicConstValue constant;
    int64_t count;
    size_t size;
    size_t alignment;
    uint64_t magnitude;
    uint64_t limit;
    uint64_t product;

    if (parser == NULL || byte_offset == NULL ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, offset_expression_id, &constant) ||
        !minic_const_value_as_int64(
            parser->program, parser->target_info, &constant, &count) ||
        !minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                parser->program,
                                pointee_type,
                                &size,
                                &alignment) ||
        size == 0U) {
        return false;
    }
    (void)alignment;
    if (count >= 0) {
        magnitude = (uint64_t)count;
        limit = (uint64_t)INT64_MAX;
    } else {
        magnitude = (uint64_t)(-(count + 1)) + UINT64_C(1);
        limit = (uint64_t)INT64_MAX + UINT64_C(1);
    }
    if (magnitude != 0U && size > limit / magnitude) {
        return false;
    }
    product = magnitude * size;
    if (count < 0) {
        *byte_offset = product == (uint64_t)INT64_MAX + UINT64_C(1)
                           ? INT64_MIN
                           : -(int64_t)product;
    } else {
        *byte_offset = (int64_t)product;
    }
    if (!subtract) {
        return true;
    }
    if (*byte_offset == INT64_MIN) {
        return false;
    }
    *byte_offset = -*byte_offset;
    return true;
}

static bool static_add_pointer_offset(int64_t base, int64_t delta, int64_t *result) {
    if (result == NULL || (delta > 0 && base > INT64_MAX - delta) ||
        (delta < 0 && base < INT64_MIN - delta)) {
        return false;
    }
    *result = base + delta;
    return true;
}

static bool static_object_address_relocation_target(const MinicParser *parser,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id,
                                                    int64_t *target_byte_addend) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    MinicGlobalObjectId object_id;
    int64_t byte_addend;

    if (parser == NULL || target_object_id == NULL || target_byte_addend == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(parser->program, expression_id);
    }
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        MinicType pointee_type;
        int64_t delta;

        left = minic_c0_program_expression(parser->program, expression->value.binary.left);
        if (left == NULL || !minic_type_pointee(left->type, &pointee_type) ||
            !static_object_address_relocation_target(parser,
                                                     expression->value.binary.left,
                                                     &object_id,
                                                     &byte_addend) ||
            !static_pointer_offset_bytes(
                parser,
                pointee_type,
                expression->value.binary.right,
                expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT,
                &delta) ||
            !static_add_pointer_offset(byte_addend, delta, &byte_addend)) {
            return false;
        }
        *target_object_id = object_id;
        *target_byte_addend = byte_addend;
        return true;
    }
    if (expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    addressed = minic_c0_program_expression(parser->program, expression->value.unary.operand);
    if (addressed == NULL) {
        return false;
    }
    byte_addend = 0;
    if (addressed->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        object_id = addressed->value.global_object_id;
    } else if (addressed->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicGlobalObject *object;
        const MinicArrayType *array_type;

        base = minic_c0_program_expression(parser->program, addressed->value.subscript.base);
        if (base == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT ||
            !static_pointer_offset_bytes(parser,
                                         addressed->type,
                                         addressed->value.subscript.index,
                                         false,
                                         &byte_addend)) {
            return false;
        }
        object_id = base->value.global_object_id;
        object = minic_c0_program_global_object(parser->program, object_id);
        array_type = object != NULL && minic_type_is_array(object->type)
                         ? minic_c0_program_array_type(parser->program, object->type.array_type_id)
                         : NULL;
        if (array_type == NULL || !minic_type_equal(array_type->element_type, addressed->type)) {
            return false;
        }
    } else {
        return false;
    }
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        minic_c0_program_global_object(parser->program, object_id) == NULL) {
        return false;
    }
    *target_object_id = object_id;
    *target_byte_addend = byte_addend;
    return true;
}

'''
t = t[:old_start] + new_target + t[old_end:]
t = once(t,
'''    size_t member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t member_depth;
} MinicStaticObjectRelocationTarget;''',
'''    size_t member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t member_depth;
    int64_t byte_addend;
} MinicStaticObjectRelocationTarget;''', "parser relocation target addend")
t = once(t,
'''static bool static_object_address_relocation_path(const MinicC0Program *program,
                                                  MinicExpressionId expression_id,
                                                  MinicStaticObjectRelocationTarget *target) {
    const MinicExpression *expression;''',
'''static bool static_object_address_relocation_path(const MinicParser *parser,
                                                  MinicExpressionId expression_id,
                                                  MinicStaticObjectRelocationTarget *target) {
    const MinicC0Program *program;
    const MinicExpression *expression;''', "path signature")
t = once(t,
'''    if (program == NULL || target == NULL) {
        return false;
    }
    if (static_object_address_relocation_target(program, expression_id, &target->object_id)) {
        target->member_depth = 0U;
        return true;
    }''',
'''    if (parser == NULL || target == NULL) {
        return false;
    }
    program = parser->program;
    if (static_object_address_relocation_target(
            parser, expression_id, &target->object_id, &target->byte_addend)) {
        target->member_depth = 0U;
        return true;
    }''', "path base target")
t = once(t,
'''    target->member_depth = depth;
    for (index = 0U; index < depth; ++index) {''',
'''    target->member_depth = depth;
    target->byte_addend = 0;
    for (index = 0U; index < depth; ++index) {''', "member path zero addend")
t = t.replace("static_object_address_relocation_path(\n            parser->program, expression_id, &initializer->relocation_target)",
              "static_object_address_relocation_path(\n            parser, expression_id, &initializer->relocation_target)")
# Materialize aggregate pointer relocations with the planned target addend.
t = t.replace("minic_c0_global_object_add_object_relocation_path_cast(\n", "minic_c0_global_object_add_object_relocation_path_addend_cast(\n")
t = t.replace("minic_c0_global_object_add_object_relocation_path(\n", "minic_c0_global_object_add_object_relocation_path_addend(\n")
t = t.replace("                                     initializer.relocation_target.member_depth);",
              "                                     initializer.relocation_target.member_depth,\n                                     initializer.relocation_target.byte_addend);")
# Reuse the shared pointer initializer for scalar globals as well.
func_start = t.index("static bool parse_static_scalar(MinicParser *parser")
pointer_start = t.index("    } else if (minic_type_is_pointer(type)) {", func_start)
pointer_end = t.index('''    } else {
        minic_parser_error(parser, "unsupported static scalar type");''', pointer_start)
new_pointer = '''    } else if (minic_type_is_pointer(type)) {
        MinicStaticPointerInitializer initializer;

        if (!parse_static_pointer_initializer(parser, type, &initializer)) {
            return false;
        }
        if (initializer.has_relocation) {
            bool recorded;

            if (initializer.relocation_is_function) {
                recorded = initializer.has_explicit_pointer_cast
                               ? minic_c0_global_object_add_function_relocation_cast(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                     0U,
                                     initializer.function_id)
                               : minic_c0_global_object_add_function_relocation(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                     0U,
                                     initializer.function_id);
            } else {
                recorded = initializer.has_explicit_pointer_cast
                               ? minic_c0_global_object_add_object_relocation_path_addend_cast(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                     0U,
                                     initializer.relocation_target.object_id,
                                     initializer.relocation_target.member_indices,
                                     initializer.relocation_target.member_depth,
                                     initializer.relocation_target.byte_addend)
                               : minic_c0_global_object_add_object_relocation_path_addend(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                     0U,
                                     initializer.relocation_target.object_id,
                                     initializer.relocation_target.member_indices,
                                     initializer.relocation_target.member_depth,
                                     initializer.relocation_target.byte_addend);
            }
            if (!recorded ||
                !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                minic_parser_error(parser, "cannot record static symbolic pointer relocation");
                return false;
            }
        } else if (!minic_c0_global_object_add_initializer_bits(
                       parser->program, object_id, initializer.bits)) {
            minic_parser_error(parser, "cannot record static pointer constant bits");
            return false;
        }
'''
t = t[:pointer_start] + new_pointer + t[pointer_end:]
t = t.replace("null or zero-addend symbol address constant", "null or static symbol address constant")
write(p, t)

# Regression: scalar + aggregate + array-element addends; runtime pointer base stays rejected.
p = "tests/compiler/c0/static_object_address_relocation.c"
t = read(p)
t = once(t, "int global_address_array[2] = {1, 2};", "int global_address_array[10] = {1, 2};", "test array size")
t = once(t,
'''static int *array_zero_address = &global_address_array[0];
''',
'''static int *array_zero_address = &global_address_array[0];
static int *array_one_address = &global_address_array[1];
static int *object_plus_one_address = &internal_address_target + 1;
static struct FunctionAddressHolder aggregate_array_nine = {
    (void *)&global_address_array[9],
};
''', "positive addend cases")
t = once(t,
'''           string_literal_address != (void *)0 && array_decay_address != (void *)0 &&
           array_zero_address != (void *)0;''',
'''           string_literal_address != (void *)0 && array_decay_address != (void *)0 &&
           array_zero_address != (void *)0 && array_one_address != (void *)0 &&
           object_plus_one_address != (void *)0 && aggregate_array_nine.address != (void *)0;''', "use addend cases")
write(p, t)

p = "tests/compiler/c0/run-static-object-address-relocation.sh"
t = read(p)
t = once(t,
'''array_count=$(grep -F -c '.dword global_address_array' "$work/static_object_address_relocation.s")
test "$array_count" -eq 2''',
'''array_count=$(grep -F -c '.dword global_address_array' "$work/static_object_address_relocation.s")
test "$array_count" -eq 4
grep -F '.dword global_address_array+4' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword global_address_array+36' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword internal_address_target+4' "$work/static_object_address_relocation.s" >/dev/null''', "assembly addend assertions")
start = t.index('''if "$minic" -S "$root/tests/compiler/c0/invalid_static_object_address_addend.c"''')
end = t.index('''if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_subscript_relocation.c"''', start)
t = t[:start] + t[end:]
t = t.replace("null or zero-addend symbol address constant", "null or static symbol address constant")
t = t.replace("zero-offset-array-decay+string-literal null=shared addend=fail-closed",
              "zero-offset-array-decay+string-literal null=shared addend=signed-static")
write(p, t)
Path("tests/compiler/c0/invalid_static_object_address_addend.c").unlink()
