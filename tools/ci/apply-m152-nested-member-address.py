#!/usr/bin/env python3
from pathlib import Path

marker = "M152_UNSIGNED_ENUM_BIT_FIELD_OWNER"
path = Path("src/core/core_lower.c")
text = path.read_text()

if marker in text:
    print("M152 unsigned enum bit-field owner already staged")
    raise SystemExit(0)
if "M151_INDIRECT_CALL_BATCH_OWNER" not in text:
    raise SystemExit("M152 requires the productized M151 Core baseline")
if "M152_NESTED_RECORD_MEMBER_ADDRESS_OWNER" in text:
    raise SystemExit("obsolete M152 nested-member candidate is unexpectedly productized")

anchor = """static bool core_memory_scalar_type(MinicType type) {\n    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n}\n"""
helper = anchor + r'''

/* M152_UNSIGNED_ENUM_BIT_FIELD_OWNER: enum bit-fields keep their semantic enum
   type in AST/Core values, while C gives every complete enum a compatible
   integer type.  The existing bit-field RMW is intentionally restricted to
   unsigned storage semantics; admit an enum only when its frontend-owned
   compatible integer type is unsigned.  This preserves the signed-bit-field
   fail-closed boundary and avoids inventing enum-specific Core opcodes. */
static bool core_unsigned_bit_field_semantic_type(const MinicCoreLowerContext *context,
                                                  MinicType type) {
    MinicType effective_type;

    if (!minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_unsigned_integer(type)) {
        return true;
    }
    return minic_type_is_enum(type) && context != NULL && context->body != NULL &&
           context->body->program != NULL &&
           minic_c0_type_effective_integer_type(
               context->body->program, type, &effective_type) &&
           minic_type_is_unsigned_integer(effective_type);
}
'''
if text.count(anchor) != 1:
    raise SystemExit("M152 could not locate core_memory_scalar_type anchor")
text = text.replace(anchor, helper, 1)

simple_old = """                !minic_type_is_integer(value_type) ||\n                !minic_type_is_unsigned_integer(value_type) ||\n                minic_type_is_const(target->type) ||\n"""
simple_new = """                !minic_type_is_integer(value_type) ||\n                !core_unsigned_bit_field_semantic_type(context, value_type) ||\n                minic_type_is_const(target->type) ||\n"""
if text.count(simple_old) != 1:
    raise SystemExit("M152 could not locate simple bit-field unsigned gate")
text = text.replace(simple_old, simple_new, 1)

compound_old = """                !minic_type_is_integer(bit_value_type) ||\n                !minic_type_is_unsigned_integer(bit_value_type) ||\n                !minic_type_is_integer(bit_source->type) || context->target == NULL ||\n"""
compound_new = """                !minic_type_is_integer(bit_value_type) ||\n                !core_unsigned_bit_field_semantic_type(context, bit_value_type) ||\n                !minic_type_is_integer(bit_source->type) || context->target == NULL ||\n"""
if text.count(compound_old) != 1:
    raise SystemExit("M152 could not locate compound bit-field unsigned gate")
text = text.replace(compound_old, compound_new, 1)
path.write_text(text)

regression = Path("tests/compiler/c0/m152_enum_bit_field_write.c")
regression.write_text(r'''enum iter_state {
    ITER_INVALID,
    ITER_ACTIVE,
    ITER_DRAINED,
};

struct outer_state {
    int prefix;
    struct {
        void *btf;
        unsigned int btf_id;
        enum iter_state state : 2;
        int depth : 30;
    } iter;
};

enum value_type {
    VALUE_UNDEFINED,
    VALUE_FLAG,
    VALUE_STRING,
};

struct parameter {
    const char *key;
    enum value_type type : 8;
    char *string;
};

void set_iter_state(struct outer_state *st) {
    st->iter.state = ITER_ACTIVE;
}

int set_parameter_type(void) {
    struct parameter param;
    param.type = VALUE_FLAG;
    return 0;
}
''')
print("staged M152 unsigned enum bit-field owner")
