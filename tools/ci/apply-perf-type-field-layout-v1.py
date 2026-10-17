#!/usr/bin/env python3
"""One-factor disposable-checkout MinicType field-order candidate.

Move 4/1-byte scalar fields before four 8-byte IDs; keep every field,
its exact C type, and all business semantics intact. No reduced ID range,
no pointer ownership, no array growth changes, no new allocations.
"""
from pathlib import Path
p=Path("src/frontend/type.h")
s=p.read_text()
old="""typedef struct MinicType {
    MinicTypeBaseKind base_kind;
    MinicRecordId record_id;
    MinicArrayTypeId array_type_id;
    MinicFunctionTypeId function_type_id;
    MinicEnumId enum_id;
    MinicIntegerSign integer_sign;
    MinicIntegerRank integer_rank;
    bool is_plain_char;
    unsigned int base_qualifiers;
    unsigned int pointer_qualifiers;
    unsigned int pointer_volatile_qualifiers;
    unsigned int pointer_depth;
    size_t explicit_alignment;
} MinicType;"""
new="""typedef struct MinicType {
    /* Identity, qualifiers and pointer metadata first to avoid the
       ABI alignment hole before the size_t IDs. No field changes type. */
    MinicTypeBaseKind base_kind;
    MinicIntegerSign integer_sign;
    MinicIntegerRank integer_rank;
    bool is_plain_char;
    unsigned int base_qualifiers;
    unsigned int pointer_qualifiers;
    unsigned int pointer_volatile_qualifiers;
    unsigned int pointer_depth;
    MinicRecordId record_id;
    MinicArrayTypeId array_type_id;
    MinicFunctionTypeId function_type_id;
    MinicEnumId enum_id;
    size_t explicit_alignment;
} MinicType;"""
if s.count(old)!=1: raise SystemExit("MINIC_TYPE_LAYOUT=ANCHOR_MISMATCH")
s=s.replace(old,new,1)
p.write_text(s)
print("MINIC_TYPE_LAYOUT=APPLIED same_fields_same_types=true")
