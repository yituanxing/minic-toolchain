#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


def sub_once(path, pattern, repl, flags=0):
    text = read(path)
    new_text, count = re.subn(pattern, repl, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"{path}: expected one regex occurrence, found {count}: {pattern}")
    write(path, new_text)


def replace_all_checked(path, old, new, minimum=1):
    text = read(path)
    count = text.count(old)
    if count < minimum:
        raise SystemExit(f"{path}: expected at least {minimum} occurrences, found {count}: {old!r}")
    write(path, text.replace(old, new))
    return count


# 1. Enum MinicType is identity only. Compatible integer facts live on MinicEnum.
replace_once(
    "src/frontend/type.h",
    "MinicType minic_type_enum(MinicEnumId enum_id, MinicType compatible_type);",
    "MinicType minic_type_enum(MinicEnumId enum_id);",
)
sub_once(
    "src/frontend/type.c",
    r"MinicType minic_type_enum\(MinicEnumId enum_id, MinicType compatible_type\) \{.*?\n\}",
    """MinicType minic_type_enum(MinicEnumId enum_id) {
    MinicType type;

    if (enum_id == MINIC_ENUM_INVALID) {
        return minic_type_void();
    }
    type = minic_type_scalar(MINIC_TYPE_BASE_ENUM);
    type.enum_id = enum_id;
    return type;
}""",
    re.S,
)
sub_once(
    "src/frontend/type.c",
    r"bool minic_type_is_enum\(MinicType type\) \{.*?\n\}",
    """bool minic_type_is_enum(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_ENUM && type.enum_id != MINIC_ENUM_INVALID &&
           type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
           type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}""",
    re.S,
)

# Parser no longer bakes provisional/final compatible types into enum descriptors.
p = "src/frontend/parser_enum.c"
text = read(p)
text = re.sub(r"minic_type_enum\(enum_id,\s*entity->compatible_type\)", "minic_type_enum(enum_id)", text)
text = re.sub(r"minic_type_enum\(enum_id,\s*compatible_type\)", "minic_type_enum(enum_id)", text)
write(p, text)

# 2. Delete whole-program enum repair and add one canonical semantic query.
p = "src/frontend/ast.c"
text = read(p)
text, count = re.subn(
    r"static void\s+minic_refresh_enum_type\(.*?\n\}\n\nstatic void minic_refresh_program_enum_types\(.*?\n\}\n\n",
    "",
    text,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("ast.c: could not remove enum refresh block")
text = text.replace(
    "    minic_refresh_program_enum_types(program, enum_id, compatible_type);\n",
    "",
    1,
)
anchor = """const MinicEnum *minic_c0_program_enum(const MinicC0Program *program, MinicEnumId enum_id) {
    if (program == NULL || enum_id >= program->enum_count) {
        return NULL;
    }
    return &program->enums[enum_id];
}
"""
if anchor not in text:
    raise SystemExit("ast.c: enum accessor anchor not found")
query = anchor + """

bool minic_c0_type_effective_integer_type(const MinicC0Program *program,
                                          MinicType type,
                                          MinicType *result) {
    const MinicEnum *entity;

    if (program == NULL || result == NULL || !minic_type_is_integer(type) ||
        minic_type_is_pointer(type)) {
        return false;
    }
    if (!minic_type_is_enum(type)) {
        *result = type;
        return true;
    }
    entity = minic_c0_program_enum(program, type.enum_id);
    if (entity == NULL || !entity->is_complete || !minic_type_is_integer(entity->compatible_type) ||
        minic_type_is_enum(entity->compatible_type)) {
        return false;
    }
    *result = entity->compatible_type;
    result->base_qualifiers = type.base_qualifiers;
    result->explicit_alignment = type.explicit_alignment;
    return true;
}
"""
text = text.replace(anchor, query, 1)
write(p, text)

replace_once(
    "src/frontend/ast.h",
    "bool minic_c0_types_compatible(const MinicC0Program *program, MinicType left, MinicType right);",
    """bool minic_c0_type_effective_integer_type(const MinicC0Program *program,
                                          MinicType type,
                                          MinicType *result);
bool minic_c0_types_compatible(const MinicC0Program *program, MinicType left, MinicType right);""",
)

# 3. Verifier validates enum identity, not duplicated compatible-type facts.
p = "src/frontend/ast_verifier.c"
text = read(p)
pattern = r"    case MINIC_TYPE_BASE_ENUM: \{.*?\n    \}\n    case MINIC_TYPE_BASE_FLOAT:"
replacement = """    case MINIC_TYPE_BASE_ENUM: {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(program, type.enum_id);
        return entity != NULL && type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID && !type.is_plain_char &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE;
    }
    case MINIC_TYPE_BASE_FLOAT:"""
text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
if count != 1:
    raise SystemExit("ast_verifier.c: enum verifier block not found")
write(p, text)

# 4. DataLayout resolves enum compatible type from the canonical entity.
p = "src/target/data_layout.c"
old = """    if (minic_type_is_enum(type)) {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(program, type.enum_id);
        if (entity == NULL || !entity->is_complete) {
            return false;
        }
    }
    if (minic_type_is_integer(type)) {
        size_t rank = (size_t)type.integer_rank;
"""
new = """    if (minic_type_is_enum(type)) {
        MinicType effective_type;

        if (!minic_c0_type_effective_integer_type(program, type, &effective_type)) {
            return false;
        }
        type = effective_type;
    }
    if (minic_type_is_integer(type)) {
        size_t rank = (size_t)type.integer_rank;
"""
replace_once(p, old, new)

# 5. TargetInfo gets program-aware promotion/common queries. Existing plain-integer API remains
# for target-model unit tests and literal selection.
replace_once(
    "src/target/target_info.h",
    """bool minic_target_info_integer_promotion(const MinicTargetInfo *target,
                                         MinicType type,
                                         MinicType *result);
bool minic_target_info_integer_common(const MinicTargetInfo *target,
                                      MinicType left,
                                      MinicType right,
                                      MinicType *result);""",
    """bool minic_target_info_integer_promotion(const MinicTargetInfo *target,
                                         MinicType type,
                                         MinicType *result);
bool minic_target_info_integer_promotion_for_program(const MinicTargetInfo *target,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     MinicType *result);
bool minic_target_info_integer_common(const MinicTargetInfo *target,
                                      MinicType left,
                                      MinicType right,
                                      MinicType *result);
bool minic_target_info_integer_common_for_program(const MinicTargetInfo *target,
                                                  const MinicC0Program *program,
                                                  MinicType left,
                                                  MinicType right,
                                                  MinicType *result);""",
)
replace_once(
    "src/target/target_info.c",
    """bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits) {
    if (target == NULL || program == NULL) {
        return false;
    }
    return integer_model_semantic_width(target->integer_model, type, bits);
}
""",
    """bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits) {
    MinicType effective_type;

    if (target == NULL || program == NULL ||
        !minic_c0_type_effective_integer_type(program, type, &effective_type)) {
        return false;
    }
    return integer_model_semantic_width(target->integer_model, effective_type, bits);
}
""",
)
# Insert program-aware wrappers after the old common implementation.
p = "src/target/target_info.c"
text = read(p)
needle = "bool minic_target_info_integer_literal_type(const MinicTargetInfo *target,"
idx = text.find(needle)
if idx < 0:
    raise SystemExit("target_info.c: integer literal anchor missing")
wrappers = """bool minic_target_info_integer_promotion_for_program(const MinicTargetInfo *target,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     MinicType *result) {
    MinicType effective_type;

    return target != NULL && program != NULL && result != NULL &&
           minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_target_info_integer_promotion(target, effective_type, result);
}

bool minic_target_info_integer_common_for_program(const MinicTargetInfo *target,
                                                  const MinicC0Program *program,
                                                  MinicType left,
                                                  MinicType right,
                                                  MinicType *result) {
    MinicType effective_left;
    MinicType effective_right;

    return target != NULL && program != NULL && result != NULL &&
           minic_c0_type_effective_integer_type(program, left, &effective_left) &&
           minic_c0_type_effective_integer_type(program, right, &effective_right) &&
           minic_target_info_integer_common(target, effective_left, effective_right, result);
}

"""
text = text[:idx] + wrappers + text[idx:]
write(p, text)

# 6. Frontend semantic consumers use the program-aware integer model.
p = "src/frontend/ast_verifier.c"
text = read(p)
text = text.replace("minic_target_info_integer_promotion(target, left->type, &expected_type)",
                    "minic_target_info_integer_promotion_for_program(\n                    target, program, left->type, &expected_type)")
text = text.replace("minic_target_info_integer_common(\n                       target, left->type, right->type, &expected_type)",
                    "minic_target_info_integer_common_for_program(\n                       target, program, left->type, right->type, &expected_type)")
write(p, text)

p = "src/frontend/parser_expression.c"
text = read(p)
text = re.sub(r"minic_target_info_integer_common\(\s*parser->target_info,",
              "minic_target_info_integer_common_for_program(\n                    parser->target_info, parser->program,", text)
text = re.sub(r"minic_target_info_integer_common\(target,\s*left,\s*left,\s*result\)",
              "minic_target_info_integer_common_for_program(target, program, left, left, result)", text)
text = re.sub(r"minic_target_info_integer_common\(target,\s*left,\s*right,\s*result\)",
              "minic_target_info_integer_common_for_program(target, program, left, right, result)", text)
write(p, text)

p = "src/frontend/parser_statement.c"
text = read(p)
text = re.sub(r"minic_target_info_integer_common\(\s*parser->target_info,",
              "minic_target_info_integer_common_for_program(\n                       parser->target_info, parser->program,", text)
write(p, text)

p = "src/frontend/expression_semantics.c"
text = read(p)
text = text.replace("static bool conditional_type_only(const MinicTargetInfo *target,",
                    "static bool conditional_type_only(const MinicC0Program *program,\n                                  const MinicTargetInfo *target,")
text = text.replace("return minic_target_info_integer_common(target, when_true, when_false, result);",
                    "return minic_target_info_integer_common_for_program(\n            target, program, when_true, when_false, result);")
text = text.replace("return conditional_type_only(target, when_true->type, when_false->type, result);",
                    "return conditional_type_only(\n        program, target, when_true->type, when_false->type, result);")
# Signedness of a source/destination enum is also canonical.
helper = """static bool integer_type_is_signed(const MinicC0Program *program, MinicType type) {
    MinicType effective_type;

    return minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_type_is_signed_integer(effective_type);
}

"""
text = text.replace("bool minic_c0_integer_assignment_value_type", helper + "bool minic_c0_integer_assignment_value_type", 1)
text = text.replace("minic_type_is_signed_integer(source_type)", "integer_type_is_signed(program, source_type)")
text = text.replace("minic_type_is_signed_integer(destination_type)",
                    "integer_type_is_signed(program, destination_type)")
write(p, text)

p = "src/frontend/const_eval.c"
text = read(p)
helper = """static bool integer_type_is_signed(const MinicC0Program *program, MinicType type) {
    MinicType effective_type;

    return minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_type_is_signed_integer(effective_type);
}

"""
text = text.replace("static uint64_t width_mask", helper + "static uint64_t width_mask", 1)
text = text.replace("minic_type_is_signed_integer(", "integer_type_is_signed(program, ")
text = re.sub(r"minic_target_info_integer_common\(target,",
              "minic_target_info_integer_common_for_program(target, program,", text)
write(p, text)

# 7. RV64 low-level scalar helpers gain program-aware variants, leaving old plain-type helpers
# intact for isolated target tests.
replace_once(
    "src/target/riscv64/codegen_internal.h",
    """bool minic_riscv64_emit_integer_conversion(FILE *file, MinicType type, const char *register_name);
bool minic_riscv64_emit_scalar_load(FILE *file,
                                    MinicType type,
                                    const char *destination_register,
                                    const char *address_register);
bool minic_riscv64_emit_scalar_store(FILE *file,
                                     MinicType type,
                                     const char *source_register,
                                     const char *address_register);""",
    """bool minic_riscv64_emit_integer_conversion(FILE *file, MinicType type, const char *register_name);
bool minic_riscv64_emit_integer_conversion_for_program(FILE *file,
                                                       const MinicC0Program *program,
                                                       MinicType type,
                                                       const char *register_name);
bool minic_riscv64_emit_scalar_load(FILE *file,
                                    MinicType type,
                                    const char *destination_register,
                                    const char *address_register);
bool minic_riscv64_emit_scalar_load_for_program(FILE *file,
                                                const MinicC0Program *program,
                                                MinicType type,
                                                const char *destination_register,
                                                const char *address_register);
bool minic_riscv64_emit_scalar_store(FILE *file,
                                     MinicType type,
                                     const char *source_register,
                                     const char *address_register);
bool minic_riscv64_emit_scalar_store_for_program(FILE *file,
                                                 const MinicC0Program *program,
                                                 MinicType type,
                                                 const char *source_register,
                                                 const char *address_register);""",
)

p = "src/target/riscv64/codegen_support.c"
text = read(p)
# Scalar width for locals must use canonical layout, not enum cached rank.
text = re.sub(
    r"static bool minic_riscv64_scalar_width\(MinicType type, size_t \*width\) \{.*?\n\}",
    """static bool minic_riscv64_scalar_width(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *width) {
    size_t alignment;

    if (program == NULL || width == NULL ||
        (!minic_type_is_pointer(type) && !minic_type_is_integer(type) &&
         !minic_type_is_float(type) && !minic_type_is_double(type))) {
        return false;
    }
    return minic_riscv64_type_layout(program, type, width, &alignment) && *width <= 8U;
}""",
    text,
    count=1,
    flags=re.S,
)
text = text.replace("!minic_riscv64_scalar_width(object->type, &object_width)",
                    "!minic_riscv64_scalar_width(program, object->type, &object_width)")
# Program-aware wrappers resolve enum identity before using existing target spelling helpers.
anchor = """bool minic_riscv64_emit_scalar_load(FILE *file,
                                    MinicType type,
                                    const char *destination_register,
                                    const char *address_register) {"""
idx = text.find(anchor)
if idx < 0:
    raise SystemExit("codegen_support.c: scalar load anchor missing")
wrappers = """bool minic_riscv64_emit_integer_conversion_for_program(FILE *file,
                                                       const MinicC0Program *program,
                                                       MinicType type,
                                                       const char *register_name) {
    MinicType effective_type;

    return minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_riscv64_emit_integer_conversion(file, effective_type, register_name);
}

"""
text = text[:idx] + wrappers + text[idx:]
# Add scalar load/store wrappers after old store definition.
store_end_pattern = r"bool minic_riscv64_emit_scalar_store\(FILE \*file,.*?\n\}\n"
m = re.search(store_end_pattern, text, flags=re.S)
if not m:
    raise SystemExit("codegen_support.c: scalar store definition missing")
scalar_wrappers = """
bool minic_riscv64_emit_scalar_load_for_program(FILE *file,
                                                const MinicC0Program *program,
                                                MinicType type,
                                                const char *destination_register,
                                                const char *address_register) {
    MinicType effective_type;

    if (minic_type_is_enum(type)) {
        if (!minic_c0_type_effective_integer_type(program, type, &effective_type)) {
            return false;
        }
        type = effective_type;
    }
    return minic_riscv64_emit_scalar_load(file, type, destination_register, address_register);
}

bool minic_riscv64_emit_scalar_store_for_program(FILE *file,
                                                 const MinicC0Program *program,
                                                 MinicType type,
                                                 const char *source_register,
                                                 const char *address_register) {
    MinicType effective_type;

    if (minic_type_is_enum(type)) {
        if (!minic_c0_type_effective_integer_type(program, type, &effective_type)) {
            return false;
        }
        type = effective_type;
    }
    return minic_riscv64_emit_scalar_store(file, type, source_register, address_register);
}
"""
text = text[:m.end()] + scalar_wrappers + text[m.end():]
# Object load/store also bypass public scalar helpers, so resolve enum locally.
text = text.replace("instruction = minic_riscv64_load_instruction(local->type);",
                    """{
        MinicType access_type = local->type;
        if (minic_type_is_enum(access_type) &&
            !minic_c0_type_effective_integer_type(program, access_type, &access_type)) {
            return false;
        }
        instruction = minic_riscv64_load_instruction(access_type);
    }""")
text = text.replace("instruction = minic_riscv64_store_instruction(local->type);",
                    """{
        MinicType access_type = local->type;
        if (minic_type_is_enum(access_type) &&
            !minic_c0_type_effective_integer_type(program, access_type, &access_type)) {
            return false;
        }
        instruction = minic_riscv64_store_instruction(access_type);
    }""")
write(p, text)

# Helpers for applying program-aware scalar operations throughout target code.
def target_program_calls(path):
    text = read(path)
    text = text.replace("minic_riscv64_emit_integer_conversion(file,",
                        "minic_riscv64_emit_integer_conversion_for_program(file, program,")
    text = text.replace("minic_riscv64_emit_scalar_load(file,",
                        "minic_riscv64_emit_scalar_load_for_program(file, program,")
    text = text.replace("minic_riscv64_emit_scalar_store(file,",
                        "minic_riscv64_emit_scalar_store_for_program(file, program,")
    return text

# codegen_statement's normalize helper needs the program threaded once.
p = "src/target/riscv64/codegen_statement.c"
text = target_program_calls(p)
text = text.replace("minic_riscv64_emit_normalize_word(FILE *file, MinicType type,",
                    "minic_riscv64_emit_normalize_word(FILE *file, const MinicC0Program *program, MinicType type,")
text = text.replace("minic_riscv64_emit_normalize_word(file, common_type,",
                    "minic_riscv64_emit_normalize_word(file, program, common_type,")
text = text.replace("minic_riscv64_emit_normalize_word(file, target->type,",
                    "minic_riscv64_emit_normalize_word(file, program, target->type,")
text = text.replace("minic_target_info_integer_common(\n            minic_default_target_info(), target->type, value->type, &common_type)",
                    "minic_target_info_integer_common_for_program(\n            minic_default_target_info(), program, target->type, value->type, &common_type)")
write(p, text)

# codegen_expression threads program through the small helpers that classify integer representation.
p = "src/target/riscv64/codegen_expression.c"
text = target_program_calls(p)
text = text.replace("minic_riscv64_emit_normalize_integer(FILE *file, MinicType type,",
                    "minic_riscv64_emit_normalize_integer(FILE *file, const MinicC0Program *program, MinicType type,")
text = text.replace("minic_riscv64_emit_normalize_integer(file,", "minic_riscv64_emit_normalize_integer(file, program,")
text = text.replace("static bool minic_riscv64_emit_variadic_argument_conversion(FILE *file, MinicType type)",
                    "static bool minic_riscv64_emit_variadic_argument_conversion(FILE *file, const MinicC0Program *program, MinicType type)")
text = text.replace("minic_riscv64_emit_variadic_argument_conversion(file, argument->type)",
                    "minic_riscv64_emit_variadic_argument_conversion(file, program, argument->type)")
# Resolve enum before variadic long-rank classification.
text = text.replace("""    if (!minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_long_integer(type)) {""",
                    """    if (!minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_enum(type) &&
        !minic_c0_type_effective_integer_type(program, type, &type)) {
        return false;
    }
    if (minic_type_is_long_integer(type)) {""", 1)
text = text.replace("static bool minic_riscv64_emit_integer_result_conversion(FILE *file,",
                    "static bool minic_riscv64_emit_integer_result_conversion(FILE *file,\n                                                         const MinicC0Program *program,")
text = text.replace("minic_riscv64_emit_integer_result_conversion(file,", "minic_riscv64_emit_integer_result_conversion(file, program,")
text = text.replace("static const char *minic_riscv64_integer_to_double_instruction(MinicType type)",
                    "static const char *minic_riscv64_integer_to_double_instruction(const MinicC0Program *program, MinicType type)")
text = text.replace("""    if (!minic_type_is_integer(type)) {
        return NULL;
    }
    if (minic_type_is_long_integer(type)) {""",
                    """    if (!minic_type_is_integer(type)) {
        return NULL;
    }
    if (minic_type_is_enum(type) &&
        !minic_c0_type_effective_integer_type(program, type, &type)) {
        return NULL;
    }
    if (minic_type_is_long_integer(type)) {""", 1)
text = text.replace("static bool minic_riscv64_emit_integer_to_double(FILE *file,",
                    "static bool minic_riscv64_emit_integer_to_double(FILE *file,\n                                                 const MinicC0Program *program,")
text = text.replace("instruction = minic_riscv64_integer_to_double_instruction(type);",
                    "instruction = minic_riscv64_integer_to_double_instruction(program, type);")
text = text.replace("minic_riscv64_emit_integer_to_double(file,", "minic_riscv64_emit_integer_to_double(file, program,")
text = text.replace("static bool minic_riscv64_emit_conditional_result_conversion(FILE *file,",
                    "static bool minic_riscv64_emit_conditional_result_conversion(FILE *file,\n                                                             const MinicC0Program *program,")
text = text.replace("minic_riscv64_emit_conditional_result_conversion(file,",
                    "minic_riscv64_emit_conditional_result_conversion(file, program,")
text = text.replace("minic_target_info_integer_common(\n                    minic_default_target_info(), target->type, value->type, &common_type)",
                    "minic_target_info_integer_common_for_program(\n                    minic_default_target_info(), program, target->type, value->type, &common_type)")
text = text.replace("minic_target_info_integer_common(\n                minic_default_target_info(), left->type, right->type, &common_integer_type)",
                    "minic_target_info_integer_common_for_program(\n                minic_default_target_info(), program, left->type, right->type, &common_integer_type)")
# Overflow result signedness must resolve enum identity.
text = text.replace("is_unsigned = minic_type_is_unsigned_integer(result_type);",
                    """{
        MinicType effective_result_type;
        if (!minic_c0_type_effective_integer_type(program, result_type, &effective_result_type)) {
            return false;
        }
        is_unsigned = minic_type_is_unsigned_integer(effective_result_type);
    }""")
# Bit-field helper has no program in its old signature; thread it through.
text = text.replace("static bool minic_riscv64_emit_bit_field_load_from_address(FILE *file,",
                    "static bool minic_riscv64_emit_bit_field_load_from_address(FILE *file,\n                                                           const MinicC0Program *program,")
text = text.replace("minic_riscv64_emit_bit_field_load_from_address(file,",
                    "minic_riscv64_emit_bit_field_load_from_address(file, program,")
text = text.replace("minic_type_is_signed_integer(field->type) &&",
                    """({
                           MinicType effective_field_type;
                           minic_c0_type_effective_integer_type(
                               program, field->type, &effective_field_type) &&
                               minic_type_is_signed_integer(effective_field_type);
                       }) &&""", 1)
write(p, text)

# Core lowering/emission has program available at the instruction dispatcher; thread it into the
# two helper functions that previously did not receive it.
p = "src/target/riscv64/core_codegen.c"
text = target_program_calls(p)
text = text.replace("static bool emit_parameter(FILE *file,\n                           const MinicCoreFunction *function,",
                    "static bool emit_parameter(FILE *file,\n                           const MinicC0Program *program,\n                           const MinicCoreFunction *function,")
text = text.replace("static bool emit_call(FILE *file,\n                      const MinicCoreFunction *function,",
                    "static bool emit_call(FILE *file,\n                      const MinicC0Program *program,\n                      const MinicCoreFunction *function,")
text = text.replace("return emit_parameter(file, function, frame, instruction);",
                    "return emit_parameter(file, program, function, frame, instruction);")
text = text.replace("return emit_call(file, function, frame, instruction);",
                    "return emit_call(file, program, function, frame, instruction);")
write(p, text)

# Remaining RV64 source files already carry a `program` argument around scalar operations.
for p in [
    "src/target/riscv64/codegen_function.c",
    "src/target/riscv64/codegen_inline_asm.c",
]:
    write(p, target_program_calls(p))

# 8. A focused compile fixture freezes the critical invariant: a type captured before completion
# must acquire final 64-bit layout/semantics through its enum identity, without mutation.
fixture = ROOT / "tests/compiler/c0/enum_forward_completion.c"
fixture.write_text("""enum wide;
extern enum wide global_wide;
enum wide read_wide(enum wide value);

enum wide {
    WIDE_VALUE = 1ULL << 40,
};

enum wide global_wide = WIDE_VALUE;

enum wide read_wide(enum wide value) {
    return value;
}

unsigned long use_wide(void) {
    return (unsigned long)read_wide(global_wide);
}
""")
runner = ROOT / "tests/compiler/c0/run-enum-forward-completion.sh"
runner.write_text("""#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/enum-forward-completion
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/enum_forward_completion.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F '.dword 1099511627776' "$work/output.s" >/dev/null
grep -F 'global_wide:' "$work/output.s" >/dev/null
grep -F 'read_wide:' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/enum-forward-completion identity=enum-id compatible=canonical no-program-refresh=1'
""")
runner.chmod(0o755)
p = "tests/compiler/c0/run.sh"
text = read(p)
if "run-enum-forward-completion.sh" not in text:
    text += """

MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
  sh "$root/tests/compiler/c0/run-enum-forward-completion.sh"
"""
write(p, text)

# Hard structural assertions: enum completion must no longer repair users, and production semantic
# code must not use the old target common/promotion API where enum operands can occur.
ast = read("src/frontend/ast.c")
if "minic_refresh_program_enum_types" in ast or "minic_refresh_enum_type" in ast:
    raise SystemExit("enum refresh owner survived materialization")
if "minic_type_enum(enum_id," in read("src/frontend/parser_enum.c"):
    raise SystemExit("parser still embeds enum compatible type")

print("ENUM_TYPE_OWNERSHIP_MATERIALIZED")
