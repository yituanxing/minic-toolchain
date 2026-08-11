#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    (ROOT / path).write_text(text)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)

# ---- type.h: stable enum identity in MinicType ----
p = "src/frontend/type.h"
s = read(p)
s = replace_once(s,
'''typedef size_t MinicRecordId;
typedef size_t MinicArrayTypeId;
typedef size_t MinicFunctionTypeId;

#define MINIC_RECORD_INVALID ((MinicRecordId) - 1)
#define MINIC_ARRAY_TYPE_INVALID ((MinicArrayTypeId) - 1)
#define MINIC_FUNCTION_TYPE_INVALID ((MinicFunctionTypeId) - 1)
''',
'''typedef size_t MinicRecordId;
typedef size_t MinicArrayTypeId;
typedef size_t MinicFunctionTypeId;
typedef size_t MinicEnumId;

#define MINIC_RECORD_INVALID ((MinicRecordId) - 1)
#define MINIC_ARRAY_TYPE_INVALID ((MinicArrayTypeId) - 1)
#define MINIC_FUNCTION_TYPE_INVALID ((MinicFunctionTypeId) - 1)
#define MINIC_ENUM_INVALID ((MinicEnumId) - 1)
''', "type ids")
s = replace_once(s,
'''    MINIC_TYPE_BASE_VOID = 0,
    MINIC_TYPE_BASE_INT,
    MINIC_TYPE_BASE_FLOAT,
''',
'''    MINIC_TYPE_BASE_VOID = 0,
    MINIC_TYPE_BASE_INT,
    MINIC_TYPE_BASE_ENUM,
    MINIC_TYPE_BASE_FLOAT,
''', "enum base kind")
s = replace_once(s,
'''    MinicRecordId record_id;
    MinicArrayTypeId array_type_id;
    MinicFunctionTypeId function_type_id;
    MinicIntegerSign integer_sign;
''',
'''    MinicRecordId record_id;
    MinicArrayTypeId array_type_id;
    MinicFunctionTypeId function_type_id;
    MinicEnumId enum_id;
    MinicIntegerSign integer_sign;
''', "enum id field")
s = replace_once(s,
'''MinicType minic_type_unsigned_long_long(void);
MinicType minic_type_int128(void);
''',
'''MinicType minic_type_unsigned_long_long(void);
MinicType minic_type_enum(MinicEnumId enum_id, MinicType compatible_type);
MinicType minic_type_int128(void);
''', "enum constructor declaration")
s = replace_once(s,
'''bool minic_type_is_void(MinicType type);
bool minic_type_is_integer(MinicType type);
''',
'''bool minic_type_is_void(MinicType type);
bool minic_type_is_enum(MinicType type);
bool minic_type_is_integer(MinicType type);
''', "enum query declaration")
write(p, s)

# ---- type.c: enum identity distinct, integer semantics cached by compatible representation ----
p = "src/frontend/type.c"
s = read(p)
s = replace_once(s,
'''    type.function_type_id = MINIC_FUNCTION_TYPE_INVALID;
    type.integer_sign = sign;
''',
'''    type.function_type_id = MINIC_FUNCTION_TYPE_INVALID;
    type.enum_id = MINIC_ENUM_INVALID;
    type.integer_sign = sign;
''', "integer enum init")
s = replace_once(s,
'''    type.function_type_id = MINIC_FUNCTION_TYPE_INVALID;
    type.integer_sign = MINIC_INTEGER_SIGN_NONE;
''',
'''    type.function_type_id = MINIC_FUNCTION_TYPE_INVALID;
    type.enum_id = MINIC_ENUM_INVALID;
    type.integer_sign = MINIC_INTEGER_SIGN_NONE;
''', "scalar enum init")
old = '''static bool minic_type_same_unqualified_identity(MinicType left, MinicType right) {
    return left.base_kind == right.base_kind && left.record_id == right.record_id &&
           left.array_type_id == right.array_type_id &&
           left.function_type_id == right.function_type_id &&
           left.integer_sign == right.integer_sign && left.integer_rank == right.integer_rank &&
           left.is_plain_char == right.is_plain_char && left.pointer_depth == right.pointer_depth;
}
'''
new = '''static bool minic_type_same_unqualified_identity(MinicType left, MinicType right) {
    if (left.base_kind == MINIC_TYPE_BASE_ENUM || right.base_kind == MINIC_TYPE_BASE_ENUM) {
        return left.base_kind == MINIC_TYPE_BASE_ENUM && right.base_kind == MINIC_TYPE_BASE_ENUM &&
               left.enum_id == right.enum_id && left.enum_id != MINIC_ENUM_INVALID &&
               left.record_id == MINIC_RECORD_INVALID && right.record_id == MINIC_RECORD_INVALID &&
               left.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               right.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               left.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
               right.function_type_id == MINIC_FUNCTION_TYPE_INVALID && !left.is_plain_char &&
               !right.is_plain_char && left.pointer_depth == right.pointer_depth;
    }
    return left.base_kind == right.base_kind && left.record_id == right.record_id &&
           left.array_type_id == right.array_type_id &&
           left.function_type_id == right.function_type_id && left.enum_id == right.enum_id &&
           left.integer_sign == right.integer_sign && left.integer_rank == right.integer_rank &&
           left.is_plain_char == right.is_plain_char && left.pointer_depth == right.pointer_depth;
}
'''
s = replace_once(s, old, new, "type identity")
s = replace_once(s,
'''MinicType minic_type_unsigned_long_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG_LONG);
}

MinicType minic_type_int128(void) {
''',
'''MinicType minic_type_unsigned_long_long(void) {
    return minic_type_integer(MINIC_INTEGER_SIGN_UNSIGNED, MINIC_INTEGER_RANK_LONG_LONG);
}

MinicType minic_type_enum(MinicEnumId enum_id, MinicType compatible_type) {
    MinicType type;

    if (enum_id == MINIC_ENUM_INVALID || !minic_type_is_integer(compatible_type) ||
        compatible_type.base_kind != MINIC_TYPE_BASE_INT || compatible_type.pointer_depth != 0U) {
        return minic_type_void();
    }
    type = minic_type_scalar(MINIC_TYPE_BASE_ENUM);
    type.enum_id = enum_id;
    type.integer_sign = compatible_type.integer_sign;
    type.integer_rank = compatible_type.integer_rank;
    return type;
}

MinicType minic_type_int128(void) {
''', "enum constructor")
# Non-enum predicates reject stray enum ids. Keep pointer-to-enum valid through minic_type_is_pointer.
for label, marker in [
    ("void", '''return type.base_kind == MINIC_TYPE_BASE_VOID && type.record_id == MINIC_RECORD_INVALID &&\n           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&\n           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&'''),
    ("float", '''return type.base_kind == MINIC_TYPE_BASE_FLOAT && type.record_id == MINIC_RECORD_INVALID &&\n           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&\n           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&'''),
    ("double", '''return type.base_kind == MINIC_TYPE_BASE_DOUBLE && type.record_id == MINIC_RECORD_INVALID &&\n           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&\n           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&'''),
    ("function", '''return type.base_kind == MINIC_TYPE_BASE_FUNCTION && type.record_id == MINIC_RECORD_INVALID &&\n           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&\n           type.function_type_id != MINIC_FUNCTION_TYPE_INVALID &&'''),
    ("record", '''return type.base_kind == MINIC_TYPE_BASE_RECORD && type.record_id != MINIC_RECORD_INVALID &&\n           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&\n           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&'''),
    ("array", '''return type.base_kind == MINIC_TYPE_BASE_ARRAY && type.record_id == MINIC_RECORD_INVALID &&\n           type.array_type_id != MINIC_ARRAY_TYPE_INVALID &&\n           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&'''),
]:
    repl = marker + '\n           type.enum_id == MINIC_ENUM_INVALID &&'
    s = replace_once(s, marker, repl, f"{label} enum invalid")
old = '''bool minic_type_is_integer(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_INT && type.record_id == MINIC_RECORD_INVALID &&
           type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
            type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
           (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
            type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
            type.integer_rank == MINIC_INTEGER_RANK_SHORT ||
            type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_INT128) &&
           minic_type_plain_char_identity_is_valid(type) &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}
'''
new = '''bool minic_type_is_enum(MinicType type) {
    return type.base_kind == MINIC_TYPE_BASE_ENUM && type.enum_id != MINIC_ENUM_INVALID &&
           type.record_id == MINIC_RECORD_INVALID && type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
            type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
           (type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) && !type.is_plain_char &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}

bool minic_type_is_integer(MinicType type) {
    if (type.base_kind == MINIC_TYPE_BASE_ENUM) {
        return minic_type_is_enum(type);
    }
    return type.base_kind == MINIC_TYPE_BASE_INT && type.enum_id == MINIC_ENUM_INVALID &&
           type.record_id == MINIC_RECORD_INVALID && type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
           type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
           (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
            type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
           (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
            type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
            type.integer_rank == MINIC_INTEGER_RANK_SHORT ||
            type.integer_rank == MINIC_INTEGER_RANK_INT ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG ||
            type.integer_rank == MINIC_INTEGER_RANK_INT128) &&
           minic_type_plain_char_identity_is_valid(type) &&
           type.pointer_qualifiers == MINIC_TYPE_QUALIFIER_NONE && type.pointer_depth == 0U;
}
'''
s = replace_once(s, old, new, "integer enum predicate")
write(p, s)

# ---- ast.h: Program-owned enum entities and enumerators ----
p = "src/frontend/ast.h"
s = read(p)
s = replace_once(s,
'''typedef size_t MinicTypeAliasId;
typedef size_t MinicGlobalObjectId;
''',
'''typedef size_t MinicTypeAliasId;
typedef size_t MinicEnumeratorId;
typedef size_t MinicGlobalObjectId;
''', "enumerator id")
s = replace_once(s,
'''#define MINIC_TYPE_ALIAS_INVALID ((MinicTypeAliasId) - 1)
#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)
''',
'''#define MINIC_TYPE_ALIAS_INVALID ((MinicTypeAliasId) - 1)
#define MINIC_ENUMERATOR_INVALID ((MinicEnumeratorId) - 1)
#define MINIC_GLOBAL_OBJECT_INVALID ((MinicGlobalObjectId) - 1)
''', "enumerator invalid")
s = replace_once(s,
'''typedef struct MinicTypeAlias {
    char *name;
    size_t name_length;
    MinicType type;
} MinicTypeAlias;

typedef struct MinicFixedRegisterBinding {
''',
'''typedef struct MinicTypeAlias {
    char *name;
    size_t name_length;
    MinicType type;
} MinicTypeAlias;

typedef struct MinicEnum {
    char *name;
    size_t name_length;
    MinicType compatible_type;
    bool is_complete;
} MinicEnum;

typedef struct MinicEnumerator {
    char *name;
    size_t name_length;
    MinicEnumId enum_id;
    MinicType type;
    uint64_t bits;
} MinicEnumerator;

typedef struct MinicFixedRegisterBinding {
''', "enum structs")
s = replace_once(s,
'''    MinicTypeAlias *type_aliases;
    size_t type_alias_count;
    size_t type_alias_capacity;

    MinicGlobalObject *global_objects;
''',
'''    MinicTypeAlias *type_aliases;
    size_t type_alias_count;
    size_t type_alias_capacity;

    MinicEnum *enums;
    size_t enum_count;
    size_t enum_capacity;

    MinicEnumerator *enumerators;
    size_t enumerator_count;
    size_t enumerator_capacity;

    MinicGlobalObject *global_objects;
''', "program enum storage")
s = replace_once(s,
'''bool minic_c0_program_add_type_alias(MinicC0Program *program,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     MinicTypeAliasId *alias_id);
bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
''',
'''bool minic_c0_program_add_type_alias(MinicC0Program *program,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     MinicTypeAliasId *alias_id);
bool minic_c0_program_add_enum(MinicC0Program *program,
                               const char *name,
                               size_t name_length,
                               MinicEnumId *enum_id);
bool minic_c0_program_finish_enum(MinicC0Program *program,
                                  MinicEnumId enum_id,
                                  MinicType compatible_type);
bool minic_c0_program_add_enumerator(MinicC0Program *program,
                                     MinicEnumId enum_id,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     uint64_t bits,
                                     MinicEnumeratorId *enumerator_id);
bool minic_c0_types_compatible(const MinicC0Program *program, MinicType left, MinicType right);
bool minic_c0_program_add_fixed_register_binding(MinicC0Program *program,
''', "enum APIs add")
s = replace_once(s,
'''const MinicTypeAlias *minic_c0_program_type_alias(const MinicC0Program *program,
                                                  MinicTypeAliasId alias_id);
const MinicGlobalObject *minic_c0_program_global_object(const MinicC0Program *program,
''',
'''const MinicTypeAlias *minic_c0_program_type_alias(const MinicC0Program *program,
                                                  MinicTypeAliasId alias_id);
const MinicEnum *minic_c0_program_enum(const MinicC0Program *program, MinicEnumId enum_id);
const MinicEnumerator *minic_c0_program_enumerator(const MinicC0Program *program,
                                                   MinicEnumeratorId enumerator_id);
const MinicGlobalObject *minic_c0_program_global_object(const MinicC0Program *program,
''', "enum getters")
write(p, s)

# ---- ast.c: enum ownership, completion refresh, compatible-type service ----
p = "src/frontend/ast.c"
s = read(p)
s = replace_once(s,
'''    for (index = 0U; index < program->type_alias_count; ++index) {
        free(program->type_aliases[index].name);
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
''',
'''    for (index = 0U; index < program->type_alias_count; ++index) {
        free(program->type_aliases[index].name);
    }
    for (index = 0U; index < program->enum_count; ++index) {
        free(program->enums[index].name);
    }
    for (index = 0U; index < program->enumerator_count; ++index) {
        free(program->enumerators[index].name);
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
''', "destroy enum names")
s = replace_once(s,
'''    free(program->type_aliases);
    free(program->global_objects);
''',
'''    free(program->type_aliases);
    free(program->enums);
    free(program->enumerators);
    free(program->global_objects);
''', "destroy enum storage")
# append enum management before first expression add
anchor = '''bool minic_c0_program_add_expression(MinicC0Program *program,
                                     const MinicExpression *expression,
                                     MinicExpressionId *expression_id) {
'''
block = r'''static void minic_refresh_enum_type(MinicType *type,
                                    MinicEnumId enum_id,
                                    MinicType compatible_type) {
    if (type != NULL && type->base_kind == MINIC_TYPE_BASE_ENUM && type->enum_id == enum_id) {
        type->integer_sign = compatible_type.integer_sign;
        type->integer_rank = compatible_type.integer_rank;
    }
}

static void minic_refresh_program_enum_types(MinicC0Program *program,
                                             MinicEnumId enum_id,
                                             MinicType compatible_type) {
    size_t index;

    for (index = 0U; index < program->expression_count; ++index) {
        minic_refresh_enum_type(&program->expressions[index].type, enum_id, compatible_type);
        if (program->expressions[index].kind == MINIC_EXPRESSION_SIZEOF) {
            minic_refresh_enum_type(
                &program->expressions[index].value.sizeof_type, enum_id, compatible_type);
        }
    }
    for (index = 0U; index < program->local_count; ++index) {
        minic_refresh_enum_type(&program->locals[index].type, enum_id, compatible_type);
    }
    for (index = 0U; index < program->function_count; ++index) {
        size_t parameter_index;

        minic_refresh_enum_type(&program->functions[index].return_type, enum_id, compatible_type);
        for (parameter_index = 0U; parameter_index < program->functions[index].parameter_count;
             ++parameter_index) {
            minic_refresh_enum_type(&program->functions[index].parameter_types[parameter_index],
                                    enum_id,
                                    compatible_type);
        }
    }
    for (index = 0U; index < program->record_count; ++index) {
        size_t field_index;

        for (field_index = 0U; field_index < program->records[index].field_count; ++field_index) {
            minic_refresh_enum_type(
                &program->records[index].fields[field_index].type, enum_id, compatible_type);
        }
    }
    for (index = 0U; index < program->array_type_count; ++index) {
        minic_refresh_enum_type(&program->array_types[index].element_type, enum_id, compatible_type);
    }
    for (index = 0U; index < program->function_type_count; ++index) {
        size_t parameter_index;

        minic_refresh_enum_type(
            &program->function_types[index].return_type, enum_id, compatible_type);
        for (parameter_index = 0U;
             parameter_index < program->function_types[index].parameter_count;
             ++parameter_index) {
            minic_refresh_enum_type(&program->function_types[index].parameter_types[parameter_index],
                                    enum_id,
                                    compatible_type);
        }
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        minic_refresh_enum_type(&program->type_aliases[index].type, enum_id, compatible_type);
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        minic_refresh_enum_type(&program->global_objects[index].type, enum_id, compatible_type);
    }
    for (index = 0U; index < program->fixed_register_binding_count; ++index) {
        minic_refresh_enum_type(
            &program->fixed_register_bindings[index].type, enum_id, compatible_type);
    }
}

bool minic_c0_program_add_enum(MinicC0Program *program,
                               const char *name,
                               size_t name_length,
                               MinicEnumId *enum_id) {
    MinicEnum entity;

    if (program == NULL || enum_id == NULL || ((name == NULL) != (name_length == 0U)) ||
        !minic_grow_array((void **)&program->enums,
                          &program->enum_capacity,
                          program->enum_count,
                          sizeof(*program->enums))) {
        return false;
    }
    (void)memset(&entity, 0, sizeof(entity));
    if (name_length != 0U) {
        entity.name = minic_copy_name(name, name_length);
        if (entity.name == NULL) {
            return false;
        }
        entity.name_length = name_length;
    }
    entity.compatible_type = minic_type_int();
    *enum_id = program->enum_count;
    program->enums[program->enum_count] = entity;
    program->enum_count += 1U;
    return true;
}

bool minic_c0_program_finish_enum(MinicC0Program *program,
                                  MinicEnumId enum_id,
                                  MinicType compatible_type) {
    MinicEnum *entity;

    if (program == NULL || enum_id >= program->enum_count || !minic_type_is_integer(compatible_type) ||
        compatible_type.base_kind != MINIC_TYPE_BASE_INT || compatible_type.pointer_depth != 0U) {
        return false;
    }
    entity = &program->enums[enum_id];
    if (entity->is_complete) {
        return false;
    }
    entity->compatible_type = compatible_type;
    entity->is_complete = true;
    minic_refresh_program_enum_types(program, enum_id, compatible_type);
    return true;
}

bool minic_c0_program_add_enumerator(MinicC0Program *program,
                                     MinicEnumId enum_id,
                                     const char *name,
                                     size_t name_length,
                                     MinicType type,
                                     uint64_t bits,
                                     MinicEnumeratorId *enumerator_id) {
    MinicEnumerator enumerator;

    if (program == NULL || enum_id >= program->enum_count || name == NULL || name_length == 0U ||
        enumerator_id == NULL || !minic_type_is_integer(type) || minic_type_is_enum(type) ||
        !minic_grow_array((void **)&program->enumerators,
                          &program->enumerator_capacity,
                          program->enumerator_count,
                          sizeof(*program->enumerators))) {
        return false;
    }
    (void)memset(&enumerator, 0, sizeof(enumerator));
    enumerator.name = minic_copy_name(name, name_length);
    if (enumerator.name == NULL) {
        return false;
    }
    enumerator.name_length = name_length;
    enumerator.enum_id = enum_id;
    enumerator.type = type;
    enumerator.bits = bits;
    *enumerator_id = program->enumerator_count;
    program->enumerators[program->enumerator_count] = enumerator;
    program->enumerator_count += 1U;
    return true;
}

const MinicEnum *minic_c0_program_enum(const MinicC0Program *program, MinicEnumId enum_id) {
    return program != NULL && enum_id < program->enum_count ? &program->enums[enum_id] : NULL;
}

const MinicEnumerator *minic_c0_program_enumerator(const MinicC0Program *program,
                                                   MinicEnumeratorId enumerator_id) {
    return program != NULL && enumerator_id < program->enumerator_count
               ? &program->enumerators[enumerator_id]
               : NULL;
}

bool minic_c0_types_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    MinicType left_unqualified;
    MinicType right_unqualified;
    const MinicEnum *entity;

    if (!minic_type_unqualified(left, &left_unqualified) ||
        !minic_type_unqualified(right, &right_unqualified)) {
        return minic_type_equal(left, right);
    }
    if (minic_type_is_enum(left_unqualified) && minic_type_is_enum(right_unqualified)) {
        return minic_type_equal(left_unqualified, right_unqualified);
    }
    if (minic_type_is_enum(left_unqualified) && right_unqualified.base_kind == MINIC_TYPE_BASE_INT) {
        entity = minic_c0_program_enum(program, left_unqualified.enum_id);
        return entity != NULL && entity->is_complete &&
               minic_type_equal(entity->compatible_type, right_unqualified);
    }
    if (minic_type_is_enum(right_unqualified) && left_unqualified.base_kind == MINIC_TYPE_BASE_INT) {
        entity = minic_c0_program_enum(program, right_unqualified.enum_id);
        return entity != NULL && entity->is_complete &&
               minic_type_equal(entity->compatible_type, left_unqualified);
    }
    return minic_type_equal(left_unqualified, right_unqualified);
}

'''
s = replace_once(s, anchor, block + anchor, "enum ast management")
write(p, s)

# ---- parser_internal.h: parser bindings point to Program IDs, not semantic values ----
p = "src/frontend/parser_internal.h"
s = read(p)
s = replace_once(s,
'''typedef struct MinicParserEnumConstant {
    MinicSourceSpan name_span;
    int value;
} MinicParserEnumConstant;

typedef struct MinicParserEnumTag {
    MinicSourceSpan name_span;
    bool is_complete;
} MinicParserEnumTag;
''',
'''typedef struct MinicParserEnumConstant {
    MinicSourceSpan name_span;
    MinicEnumeratorId enumerator_id;
} MinicParserEnumConstant;

typedef struct MinicParserEnumTag {
    MinicSourceSpan name_span;
    MinicEnumId enum_id;
} MinicParserEnumTag;
''', "parser enum bindings")
s = replace_once(s,
'''bool minic_parser_bind_enum_constant(MinicParser *parser, MinicSourceSpan name_span, int value);
bool minic_parser_find_enum_constant(const MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     int *value);
bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span);
bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span);
''',
'''bool minic_parser_bind_enum_constant(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicEnumeratorId enumerator_id);
MinicEnumeratorId minic_parser_find_enum_constant(const MinicParser *parser,
                                                  MinicSourceSpan name_span);
MinicEnumId minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span);
''', "parser enum APIs")
write(p, s)

# ---- parser_enum.c: rewrite around Program-owned entities and typed numeric range ----
p = "src/frontend/parser_enum.c"
write(p, r'''#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicEnumNumericValue {
    bool negative;
    int64_t signed_value;
    uint64_t unsigned_value;
} MinicEnumNumericValue;

static MinicParserEnumTag *find_enum_tag_binding(MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return NULL;
    }
    for (index = parser->enum_tag_count; index > 0U; --index) {
        if (minic_parser_span_equals(parser, name_span, parser->enum_tags[index - 1U].name_span)) {
            return &parser->enum_tags[index - 1U];
        }
    }
    return NULL;
}

static bool append_enum_tag(MinicParser *parser, MinicSourceSpan name_span, MinicEnumId enum_id) {
    MinicParserEnumTag *resized;
    size_t new_capacity;

    if (parser == NULL || enum_id == MINIC_ENUM_INVALID) {
        return false;
    }
    if (parser->enum_tag_count == parser->enum_tag_capacity) {
        new_capacity = parser->enum_tag_capacity == 0U ? 8U : parser->enum_tag_capacity * 2U;
        if (new_capacity < parser->enum_tag_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_tags)) {
            minic_parser_error(parser, "too many enum tags");
            return false;
        }
        resized = (MinicParserEnumTag *)realloc(parser->enum_tags,
                                                new_capacity * sizeof(*parser->enum_tags));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum tag");
            return false;
        }
        parser->enum_tags = resized;
        parser->enum_tag_capacity = new_capacity;
    }
    parser->enum_tags[parser->enum_tag_count].name_span = name_span;
    parser->enum_tags[parser->enum_tag_count].enum_id = enum_id;
    parser->enum_tag_count += 1U;
    return true;
}

MinicEnumId minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return MINIC_ENUM_INVALID;
    }
    for (index = parser->enum_tag_count; index > 0U; --index) {
        if (minic_parser_span_equals(parser, name_span, parser->enum_tags[index - 1U].name_span)) {
            return parser->enum_tags[index - 1U].enum_id;
        }
    }
    return MINIC_ENUM_INVALID;
}

static bool create_named_enum(MinicParser *parser,
                              MinicSourceSpan name_span,
                              MinicEnumId *enum_id) {
    if (!minic_c0_program_add_enum(parser->program,
                                   parser->source + name_span.begin.offset,
                                   minic_parser_span_length(name_span),
                                   enum_id) ||
        !append_enum_tag(parser, name_span, *enum_id)) {
        minic_parser_error(parser, "cannot create enum tag entity");
        return false;
    }
    return true;
}

static bool get_or_create_enum_tag(MinicParser *parser,
                                   MinicSourceSpan name_span,
                                   MinicEnumId *enum_id) {
    *enum_id = minic_parser_find_enum_tag(parser, name_span);
    return *enum_id != MINIC_ENUM_INVALID || create_named_enum(parser, name_span, enum_id);
}

MinicEnumeratorId minic_parser_find_enum_constant(const MinicParser *parser,
                                                  MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return MINIC_ENUMERATOR_INVALID;
    }
    for (index = parser->enum_constant_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant = &parser->enum_constants[index - 1U];

        if (minic_parser_span_equals(parser, name_span, constant->name_span)) {
            return constant->enumerator_id;
        }
    }
    return MINIC_ENUMERATOR_INVALID;
}

bool minic_parser_bind_enum_constant(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicEnumeratorId enumerator_id) {
    MinicParserEnumConstant *resized;
    size_t new_capacity;

    if (parser == NULL || enumerator_id == MINIC_ENUMERATOR_INVALID ||
        minic_parser_find_enum_constant(parser, name_span) != MINIC_ENUMERATOR_INVALID) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate enumerator name");
        }
        return false;
    }
    if (parser->enum_constant_count == parser->enum_constant_capacity) {
        new_capacity =
            parser->enum_constant_capacity == 0U ? 16U : parser->enum_constant_capacity * 2U;
        if (new_capacity < parser->enum_constant_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_constants)) {
            minic_parser_error(parser, "too many enum constants");
            return false;
        }
        resized = (MinicParserEnumConstant *)realloc(
            parser->enum_constants, new_capacity * sizeof(*parser->enum_constants));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum constant");
            return false;
        }
        parser->enum_constants = resized;
        parser->enum_constant_capacity = new_capacity;
    }
    parser->enum_constants[parser->enum_constant_count].name_span = name_span;
    parser->enum_constants[parser->enum_constant_count].enumerator_id = enumerator_id;
    parser->enum_constant_count += 1U;
    return true;
}

void minic_parser_destroy_enum_constants(MinicParser *parser) {
    if (parser == NULL) {
        return;
    }
    free(parser->enum_constants);
    parser->enum_constants = NULL;
    parser->enum_constant_count = 0U;
    parser->enum_constant_capacity = 0U;
    free(parser->enum_tags);
    parser->enum_tags = NULL;
    parser->enum_tag_count = 0U;
    parser->enum_tag_capacity = 0U;
}

static bool signed_type_fits(MinicParser *parser, MinicType type, int64_t minimum, uint64_t maximum) {
    unsigned int bits;
    int64_t type_minimum;
    uint64_t type_maximum;

    if (!minic_target_info_integer_width(parser->target_info, parser->program, type, &bits) ||
        bits == 0U || bits > 64U || minic_type_is_unsigned_integer(type)) {
        return false;
    }
    if (bits == 64U) {
        type_minimum = INT64_MIN;
        type_maximum = (uint64_t)INT64_MAX;
    } else {
        type_minimum = -(INT64_C(1) << (bits - 1U));
        type_maximum = (UINT64_C(1) << (bits - 1U)) - UINT64_C(1);
    }
    return minimum >= type_minimum && maximum <= type_maximum;
}

static bool unsigned_type_fits(MinicParser *parser, MinicType type, uint64_t maximum) {
    unsigned int bits;
    uint64_t type_maximum;

    if (!minic_target_info_integer_width(parser->target_info, parser->program, type, &bits) ||
        bits == 0U || bits > 64U || !minic_type_is_unsigned_integer(type)) {
        return false;
    }
    type_maximum = bits == 64U ? UINT64_MAX : (UINT64_C(1) << bits) - UINT64_C(1);
    return maximum <= type_maximum;
}

static bool enum_value_type(MinicParser *parser,
                            const MinicEnumNumericValue *value,
                            MinicType *type) {
    if (value->negative) {
        MinicType candidates[] = {minic_type_int(), minic_type_long(), minic_type_long_long()};
        size_t index;

        for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            if (signed_type_fits(parser, candidates[index], value->signed_value, 0U)) {
                *type = candidates[index];
                return true;
            }
        }
        return false;
    }
    if ((uint64_t)INT32_MAX >= value->unsigned_value) {
        *type = minic_type_int();
        return true;
    }
    {
        MinicType candidates[] = {
            minic_type_unsigned_int(), minic_type_unsigned_long(), minic_type_unsigned_long_long()};
        size_t index;

        for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            if (unsigned_type_fits(parser, candidates[index], value->unsigned_value)) {
                *type = candidates[index];
                return true;
            }
        }
    }
    return false;
}

static bool normalize_value_bits(MinicParser *parser,
                                 const MinicEnumNumericValue *value,
                                 MinicType type,
                                 uint64_t *bits) {
    unsigned int width;
    uint64_t raw;

    if (!minic_target_info_integer_width(parser->target_info, parser->program, type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    raw = value->negative ? (uint64_t)value->signed_value : value->unsigned_value;
    if (width != 64U) {
        raw &= (UINT64_C(1) << width) - UINT64_C(1);
    }
    *bits = raw;
    return true;
}

static bool const_value_to_numeric(MinicParser *parser,
                                   const MinicConstValue *constant,
                                   MinicEnumNumericValue *value) {
    unsigned int width;
    uint64_t raw;

    if (constant == NULL || value == NULL || !minic_type_is_integer(constant->type) ||
        !minic_target_info_integer_width(
            parser->target_info, parser->program, constant->type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    raw = constant->bits;
    if (width != 64U) {
        raw &= (UINT64_C(1) << width) - UINT64_C(1);
    }
    (void)memset(value, 0, sizeof(*value));
    if (minic_type_is_signed_integer(constant->type) &&
        (raw & (UINT64_C(1) << (width - 1U))) != 0U) {
        if (width != 64U) {
            raw |= ~((UINT64_C(1) << width) - UINT64_C(1));
        }
        (void)memcpy(&value->signed_value, &raw, sizeof(raw));
        value->negative = true;
        return true;
    }
    value->unsigned_value = raw;
    return true;
}

static bool parse_enum_integer_value(MinicParser *parser,
                                     MinicEnumNumericValue *value,
                                     MinicType *value_type,
                                     uint64_t *bits) {
    const MinicExpression *expression;
    MinicConstValue constant_value;
    MinicExpressionId expression_id;

    if (parser == NULL || value == NULL || value_type == NULL || bits == NULL ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression_id, &constant_value) ||
        !const_value_to_numeric(parser, &constant_value, value) ||
        !enum_value_type(parser, value, value_type) ||
        !normalize_value_bits(parser, value, *value_type, bits)) {
        minic_parser_error(parser, "enum initializer must be a representable integer constant expression");
        return false;
    }
    return true;
}

static bool next_enum_numeric(MinicParser *parser,
                              const MinicEnumNumericValue *current,
                              MinicEnumNumericValue *next,
                              MinicType *next_type,
                              uint64_t *next_bits) {
    *next = *current;
    if (next->negative) {
        if (next->signed_value == INT64_MAX) {
            return false;
        }
        next->signed_value += 1;
        if (next->signed_value >= 0) {
            next->negative = false;
            next->unsigned_value = (uint64_t)next->signed_value;
        }
    } else {
        if (next->unsigned_value == UINT64_MAX) {
            return false;
        }
        next->unsigned_value += 1U;
    }
    return enum_value_type(parser, next, next_type) &&
           normalize_value_bits(parser, next, *next_type, next_bits);
}

static bool choose_enum_compatible_type(MinicParser *parser,
                                        bool saw_negative,
                                        int64_t minimum,
                                        uint64_t maximum,
                                        MinicType *compatible_type) {
    if (saw_negative) {
        MinicType candidates[] = {minic_type_int(), minic_type_long(), minic_type_long_long()};
        size_t index;

        for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            if (signed_type_fits(parser, candidates[index], minimum, maximum)) {
                *compatible_type = candidates[index];
                return true;
            }
        }
        return false;
    }
    {
        MinicType candidates[] = {
            minic_type_unsigned_int(), minic_type_unsigned_long(), minic_type_unsigned_long_long()};
        size_t index;

        for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            if (unsigned_type_fits(parser, candidates[index], maximum)) {
                *compatible_type = candidates[index];
                return true;
            }
        }
    }
    return false;
}

bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type) {
    MinicSourceSpan tag_span;
    MinicEnumId enum_id;
    MinicEnumNumericValue next_value;
    MinicType next_type;
    uint64_t next_bits;
    bool has_next;
    bool has_tag;
    bool saw_negative;
    int64_t minimum;
    uint64_t maximum;

    if (parser == NULL || enum_type == NULL ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_ENUM, "expected keyword 'enum'")) {
        return false;
    }
    (void)memset(&tag_span, 0, sizeof(tag_span));
    has_tag = false;
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        tag_span = parser->current.span;
        has_tag = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        const MinicEnum *entity;

        if (!has_tag || !get_or_create_enum_tag(parser, tag_span, &enum_id)) {
            if (!has_tag) {
                minic_parser_error(parser, "expected enum tag or definition");
            }
            return false;
        }
        entity = minic_c0_program_enum(parser->program, enum_id);
        if (entity == NULL) {
            minic_parser_error(parser, "invalid enum tag entity");
            return false;
        }
        *enum_type = minic_type_enum(enum_id, entity->compatible_type);
        return !minic_type_is_void(*enum_type);
    }

    if (has_tag) {
        const MinicEnum *existing;

        if (!get_or_create_enum_tag(parser, tag_span, &enum_id)) {
            return false;
        }
        existing = minic_c0_program_enum(parser->program, enum_id);
        if (existing == NULL || existing->is_complete) {
            minic_parser_error(parser, "duplicate enum definition");
            return false;
        }
    } else if (!minic_c0_program_add_enum(parser->program, NULL, 0U, &enum_id)) {
        minic_parser_error(parser, "cannot create anonymous enum entity");
        return false;
    }

    if (!minic_parser_advance(parser)) {
        return false;
    }
    (void)memset(&next_value, 0, sizeof(next_value));
    next_type = minic_type_int();
    next_bits = 0U;
    has_next = true;
    saw_negative = false;
    minimum = 0;
    maximum = 0U;

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicSourceSpan name_span;
        MinicEnumNumericValue value;
        MinicType value_type;
        uint64_t value_bits;
        MinicEnumeratorId enumerator_id;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected enumerator name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if (!minic_parser_advance(parser) ||
                !parse_enum_integer_value(parser, &value, &value_type, &value_bits)) {
                return false;
            }
        } else {
            if (!has_next) {
                minic_parser_error(parser, "implicit enumerator value exceeds current 64-bit range");
                return false;
            }
            value = next_value;
            value_type = next_type;
            value_bits = next_bits;
        }
        if (!minic_c0_program_add_enumerator(parser->program,
                                             enum_id,
                                             parser->source + name_span.begin.offset,
                                             minic_parser_span_length(name_span),
                                             value_type,
                                             value_bits,
                                             &enumerator_id) ||
            !minic_parser_bind_enum_constant(parser, name_span, enumerator_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot bind enumerator entity");
            }
            return false;
        }
        if (value.negative) {
            if (!saw_negative || value.signed_value < minimum) {
                minimum = value.signed_value;
            }
            saw_negative = true;
        } else if (value.unsigned_value > maximum) {
            maximum = value.unsigned_value;
        }
        has_next = next_enum_numeric(parser, &value, &next_value, &next_type, &next_bits);

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' after enumerator");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after enum definition")) {
        return false;
    }
    {
        MinicType compatible_type;

        if (!choose_enum_compatible_type(parser, saw_negative, minimum, maximum, &compatible_type) ||
            !minic_c0_program_finish_enum(parser->program, enum_id, compatible_type)) {
            minic_parser_error(parser, "enum values do not fit a supported compatible integer type");
            return false;
        }
        *enum_type = minic_type_enum(enum_id, compatible_type);
        return !minic_type_is_void(*enum_type);
    }
}

bool minic_parser_parse_enum_definition(MinicParser *parser) {
    MinicType enum_type;

    return minic_parser_parse_enum_specifier(parser, &enum_type) &&
           minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after enum definition");
}
''')

# ---- parser_expression.c: enum references keep typed enumerator bits; generic compatibility is Program-aware ----
p = "src/frontend/parser_expression.c"
s = read(p)
s = replace_once(s,
'''static bool generic_types_compatible(MinicType left, MinicType right) {
    MinicType left_unqualified;
    MinicType right_unqualified;

    if (!minic_type_unqualified(left, &left_unqualified) ||
        !minic_type_unqualified(right, &right_unqualified)) {
        return minic_type_equal(left, right);
    }
    return minic_type_equal(left_unqualified, right_unqualified);
}
''',
'''static bool generic_types_compatible(const MinicC0Program *program,
                                     MinicType left,
                                     MinicType right) {
    return minic_c0_types_compatible(program, left, right);
}
''', "generic compatible helper")
s = s.replace("generic_types_compatible(controlling_type, association_type)",
              "generic_types_compatible(parser->program, controlling_type, association_type)")
s = s.replace("generic_types_compatible(left_type, right_type)",
              "generic_types_compatible(parser->program, left_type, right_type)")
s = replace_once(s,
'''    MinicFixedRegisterBindingId fixed_register_binding_id;
    int enum_value;
    bool is_enum_constant;
''',
'''    MinicFixedRegisterBindingId fixed_register_binding_id;
    MinicEnumeratorId enumerator_id;
''', "primary enum vars")
s = replace_once(s,
'''        fixed_register_binding_id = minic_parser_find_fixed_register_binding(parser, name_span);
        is_enum_constant = minic_parser_find_enum_constant(parser, name_span, &enum_value);
        if (!minic_parser_advance(parser)) {
''',
'''        fixed_register_binding_id = minic_parser_find_fixed_register_binding(parser, name_span);
        enumerator_id = minic_parser_find_enum_constant(parser, name_span);
        if (!minic_parser_advance(parser)) {
''', "primary enum lookup")
s = replace_once(s,
'''        if (is_enum_constant) {
            (void)memset(&expression, 0, sizeof(expression));
            expression.kind = MINIC_EXPRESSION_INTEGER;
            expression.span = name_span;
            expression.type = minic_type_int();
            expression.value_category = MINIC_VALUE_RVALUE;
            expression.value.integer_value = enum_value;
            if (!minic_parser_add_expression(parser, &expression, &primary_id) ||
                !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
''',
'''        if (enumerator_id != MINIC_ENUMERATOR_INVALID) {
            const MinicEnumerator *enumerator;

            enumerator = minic_c0_program_enumerator(parser->program, enumerator_id);
            if (enumerator == NULL) {
                minic_parser_error(parser, "invalid enumerator entity");
                return false;
            }
            (void)memset(&expression, 0, sizeof(expression));
            expression.kind = MINIC_EXPRESSION_INTEGER;
            expression.span = name_span;
            expression.type = enumerator->type;
            expression.value_category = MINIC_VALUE_RVALUE;
            (void)memcpy(&expression.value.integer_value, &enumerator->bits, sizeof(enumerator->bits));
            if (!minic_parser_add_expression(parser, &expression, &primary_id) ||
                !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
                return false;
            }
            return finish_value_expression(parser, primary_id, decay_array, expression_id);
        }
''', "primary enum expression")
write(p, s)

# ---- parser_core.c: legacy token ICE consumes Program enumerator typed value, no parser int copy ----
p = "src/frontend/parser_core.c"
s = read(p)
s = replace_once(s,
'''    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        int enum_value;

        if (minic_parser_find_enum_constant(parser, parser->current.span, &enum_value)) {
            *value = (int64_t)enum_value;
            return minic_parser_advance(parser);
        }
    }
''',
'''    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicEnumeratorId enumerator_id;

        enumerator_id = minic_parser_find_enum_constant(parser, parser->current.span);
        if (enumerator_id != MINIC_ENUMERATOR_INVALID) {
            const MinicEnumerator *enumerator;
            MinicConstValue constant;

            enumerator = minic_c0_program_enumerator(parser->program, enumerator_id);
            if (enumerator == NULL) {
                minic_parser_error(parser, "invalid enumerator in integer constant expression");
                return false;
            }
            constant.type = enumerator->type;
            constant.bits = enumerator->bits;
            if (!minic_const_value_as_int64(
                    parser->program, parser->target_info, &constant, value)) {
                minic_parser_error(
                    parser, "enumerator exceeds legacy integer constant expression range");
                return false;
            }
            return minic_parser_advance(parser);
        }
    }
''', "legacy ICE enum lookup")
write(p, s)

# ---- parser_type.c: incomplete enum is an incomplete object, like incomplete record ----
p = "src/frontend/parser_type.c"
s = read(p)
old = '''bool minic_parser_require_complete_object_type(MinicParser *parser,
                                               MinicType type,
                                               const char *message) {
    const MinicRecord *record;

    if (!minic_type_is_record(type)) {
        return true;
    }
    record = minic_c0_program_record(parser->program, type.record_id);
    if (record != NULL && record->is_complete) {
        return true;
    }
    minic_parser_error(parser, "%s", message);
    return false;
}
'''
new = '''bool minic_parser_require_complete_object_type(MinicParser *parser,
                                               MinicType type,
                                               const char *message) {
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, type.record_id);
        if (record != NULL && record->is_complete) {
            return true;
        }
        minic_parser_error(parser, "%s", message);
        return false;
    }
    if (minic_type_is_enum(type)) {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(parser->program, type.enum_id);
        if (entity != NULL && entity->is_complete) {
            return true;
        }
        minic_parser_error(parser, "%s", message);
        return false;
    }
    return true;
}
'''
s = replace_once(s, old, new, "complete enum object")
write(p, s)

# ---- DataLayout: incomplete enum has no object layout; completed enum uses cached compatible rank ----
p = "src/target/data_layout.c"
s = read(p)
s = replace_once(s,
'''    if (minic_type_is_integer(type)) {
        size_t rank = (size_t)type.integer_rank;
''',
'''    if (minic_type_is_enum(type)) {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(program, type.enum_id);
        if (entity == NULL || !entity->is_complete) {
            return false;
        }
    }
    if (minic_type_is_integer(type)) {
        size_t rank = (size_t)type.integer_rank;
''', "enum layout completeness")
write(p, s)

# ---- Verifier: enum entity/type ownership and completeness ----
p = "src/frontend/ast_verifier.c"
s = read(p)
s = replace_once(s,
'''    if (type.is_plain_char && type.base_kind != MINIC_TYPE_BASE_INT) {
        return false;
    }
''',
'''    if (type.base_kind != MINIC_TYPE_BASE_ENUM && type.enum_id != MINIC_ENUM_INVALID) {
        return false;
    }
    if (type.is_plain_char && type.base_kind != MINIC_TYPE_BASE_INT) {
        return false;
    }
''', "verifier stray enum id")
s = replace_once(s,
'''    case MINIC_TYPE_BASE_INT:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
               (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
                type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
               (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
                type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
                type.integer_rank == MINIC_INTEGER_RANK_SHORT ||
                type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_INT128);
    case MINIC_TYPE_BASE_FLOAT:
''',
'''    case MINIC_TYPE_BASE_INT:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
               (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
                type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
               (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
                type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
                type.integer_rank == MINIC_INTEGER_RANK_SHORT ||
                type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_INT128);
    case MINIC_TYPE_BASE_ENUM: {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(program, type.enum_id);
        return entity != NULL && type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID && !type.is_plain_char &&
               (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
                type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
               (type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) &&
               (!entity->is_complete ||
                (type.integer_sign == entity->compatible_type.integer_sign &&
                 type.integer_rank == entity->compatible_type.integer_rank));
    }
    case MINIC_TYPE_BASE_FLOAT:
''', "verifier enum type")
s = replace_once(s,
'''    if (minic_type_is_integer(type) || minic_type_is_float(type) || minic_type_is_double(type) ||
        minic_type_is_pointer(type)) {
        return true;
    }
''',
'''    if (minic_type_is_enum(type)) {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(program, type.enum_id);
        return entity != NULL && entity->is_complete;
    }
    if (minic_type_is_integer(type) || minic_type_is_float(type) || minic_type_is_double(type) ||
        minic_type_is_pointer(type)) {
        return true;
    }
''', "verifier enum completeness")
s = replace_once(s,
'''           storage_is_valid(
               program->type_aliases, program->type_alias_count, program->type_alias_capacity) &&
           storage_is_valid(program->global_objects,
''',
'''           storage_is_valid(
               program->type_aliases, program->type_alias_count, program->type_alias_capacity) &&
           storage_is_valid(program->enums, program->enum_count, program->enum_capacity) &&
           storage_is_valid(program->enumerators,
                            program->enumerator_count,
                            program->enumerator_capacity) &&
           storage_is_valid(program->global_objects,
''', "verifier enum storage")
# Insert entity validation before array descriptor validation.
anchor = '''    for (index = 0U; index < program->array_type_count; ++index) {
'''
block = '''    for (index = 0U; index < program->enum_count; ++index) {
        const MinicEnum *entity;

        entity = &program->enums[index];
        if ((entity->name == NULL) != (entity->name_length == 0U) ||
            !minic_type_is_integer(entity->compatible_type) ||
            minic_type_is_enum(entity->compatible_type) || entity->compatible_type.pointer_depth != 0U) {
            return false;
        }
    }
    for (index = 0U; index < program->enumerator_count; ++index) {
        const MinicEnumerator *enumerator;

        enumerator = &program->enumerators[index];
        if (enumerator->name == NULL || enumerator->name_length == 0U ||
            enumerator->enum_id >= program->enum_count || !minic_type_is_integer(enumerator->type) ||
            minic_type_is_enum(enumerator->type)) {
            return false;
        }
    }
'''
s = replace_once(s, anchor, block + anchor, "verifier enum entities")
write(p, s)

# ---- DEV-0004: update from parser-local/int lowering to first-class bounded enum model ----
p = "docs/DEVIATIONS.md"
s = read(p)
if "DEV-0004" in s:
    s = re.sub(r'(?ms)^### DEV-0004.*?(?=^### DEV-|\Z)', '''### DEV-0004 — enum underlying representation remains bounded to the active target\n\n**Status:** active, narrowed by Foundation EnumEntity v1.\n\nEnum tags and enumerators are now Program-owned entities. `MinicType` carries a stable `EnumId`,\nforward declarations survive later completion as the same type identity, enumerator values retain\ntyped 64-bit constant bits, and completed enums cache a GCC-compatible integer representation.\nThe current representation chooser covers the active RV64 GNU C pressure with `int`, `unsigned int`,\n`long`, `unsigned long`, and their same-width long-long fallbacks.\n\nStill deferred: C23 fixed underlying enum syntax, `-fshort-enums`, enum values beyond the current\n64-bit ConstEval range, and moving target integer-selection policy out of the current RV64-oriented\nType/Target seam. Enumerator/tag lookup also remains parser-linear until the future SymbolTable.\n\n''', s, count=1)
else:
    s += '''\n### DEV-0004 — enum underlying representation remains bounded to the active target\n\n**Status:** active. See Foundation EnumEntity v1.\n'''
write(p, s)

# ---- focused regression ----
write("tests/compiler/c0/gnu_enum_entity.c", r'''enum Forward;
extern enum Forward forward_value(void);

enum Forward {
    FORWARD_ZERO = 0,
    FORWARD_ONE = 1,
};

_Static_assert(__builtin_types_compatible_p(enum Forward, unsigned int),
               "positive enum should be compatible with unsigned int");

enum SignedEnum {
    SIGNED_NEGATIVE = -1,
    SIGNED_ZERO = 0,
};
_Static_assert(__builtin_types_compatible_p(enum SignedEnum, int),
               "negative int-range enum should be compatible with int");

enum MmCidState {
    MM_CID_UNSET_TEST = -1U,
    MM_CID_LAZY_PUT_TEST = (1U << 31),
};
_Static_assert(__builtin_types_compatible_p(enum MmCidState, unsigned int),
               "Linux mm cid enum should be compatible with unsigned int");
_Static_assert(MM_CID_UNSET_TEST == 0xffffffffU, "-1U enumerator must retain unsigned bits");
_Static_assert(MM_CID_LAZY_PUT_TEST == 0x80000000U, "high-bit enumerator must retain unsigned bits");

enum WideEnum {
    WIDE_ABORT_MASK = (0xffffffffULL << 32),
};
_Static_assert(__builtin_types_compatible_p(enum WideEnum, unsigned long),
               "64-bit positive enum should be compatible with unsigned long on RV64");

enum MixedEnum {
    MIXED_NEGATIVE = -1,
    MIXED_HIGH = 0xffffffffU,
};
_Static_assert(__builtin_types_compatible_p(enum MixedEnum, long),
               "mixed negative/high-positive enum should use signed long on RV64");

_Static_assert(!__builtin_types_compatible_p(enum Forward, enum SignedEnum),
               "distinct enum identities must remain distinct");

enum Forward forward_value(void) {
    return FORWARD_ONE;
}

int mm_state_test(int cid) {
    return cid == MM_CID_UNSET_TEST || (cid & MM_CID_LAZY_PUT_TEST);
}

unsigned long wide_enum_test(void) {
    return WIDE_ABORT_MASK;
}
''')
write("tests/compiler/c0/run-gnu-enum-entity.sh", r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-enum-entity
assembly="$work/gnu_enum_entity.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_enum_entity.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'forward_value:' "$assembly" >/dev/null
grep -F 'mm_state_test:' "$assembly" >/dev/null
grep -F 'wide_enum_test:' "$assembly" >/dev/null
grep -F '4294967295' "$assembly" >/dev/null
grep -F -- '-4294967296' "$assembly" >/dev/null

cat >"$work/duplicate.c" <<'EOF'
enum Duplicate;
enum Duplicate { D0 };
enum Duplicate { D1 };
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/duplicate.c" -o "$work/duplicate.i"
if "$minic" -S "$work/duplicate.i" -o "$work/duplicate.s" 2>"$work/duplicate.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_enum_entity: duplicate enum definition accepted' >&2
    exit 1
fi
grep -F 'duplicate enum definition' "$work/duplicate.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_enum_entity program-owned=1 stable-enum-id=1 forward-completion=1 typed-bits=1 uint32=1 ulong64=1 mixed-long=1 compatible-type=1 distinct-enum=1 duplicate=reject'
''')

# Register next to existing enum lifecycle gate.
p = "tools/dev/pr76-focused.sh"
s = read(p)
s = replace_once(s,
'''sh tests/compiler/c0/run-enum-tag-type-references.sh
''',
'''sh tests/compiler/c0/run-enum-tag-type-references.sh
sh tests/compiler/c0/run-gnu-enum-entity.sh
''', "focused enum registration")
write(p, s)

# Existing lifecycle gate no longer claims representation=int.
p = "tests/compiler/c0/run-enum-tag-type-references.sh"
s = read(p)
s = s.replace('representation=int duplicate-definition=reject',
              'stable-identity=1 compatible-representation=1 duplicate-definition=reject')
write(p, s)
