from pathlib import Path

root = Path(__file__).resolve().parents[2]

ast = root / "src/frontend/ast.c"
text = ast.read_text()
old = '''bool minic_c0_types_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
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
    if (minic_type_is_enum(left_unqualified) &&
        right_unqualified.base_kind == MINIC_TYPE_BASE_INT) {
        entity = minic_c0_program_enum(program, left_unqualified.enum_id);
        return entity != NULL && entity->is_complete &&
               minic_type_equal(entity->compatible_type, right_unqualified);
    }
    if (minic_type_is_enum(right_unqualified) &&
        left_unqualified.base_kind == MINIC_TYPE_BASE_INT) {
        entity = minic_c0_program_enum(program, right_unqualified.enum_id);
        return entity != NULL && entity->is_complete &&
               minic_type_equal(entity->compatible_type, left_unqualified);
    }
    return minic_type_equal(left_unqualified, right_unqualified);
}
'''
new = '''static bool minic_c0_array_shapes_compatible(const MinicC0Program *program,
                                            MinicType left,
                                            MinicType right) {
    const MinicArrayType *left_array;
    const MinicArrayType *right_array;

    if (program == NULL || left.base_kind != MINIC_TYPE_BASE_ARRAY ||
        right.base_kind != MINIC_TYPE_BASE_ARRAY || left.pointer_depth != right.pointer_depth ||
        left.base_qualifiers != right.base_qualifiers ||
        left.pointer_qualifiers != right.pointer_qualifiers ||
        left.pointer_volatile_qualifiers != right.pointer_volatile_qualifiers) {
        return false;
    }
    left_array = minic_c0_program_array_type(program, left.array_type_id);
    right_array = minic_c0_program_array_type(program, right.array_type_id);
    return left_array != NULL && right_array != NULL &&
           left_array->element_count == right_array->element_count &&
           left_array->is_zero_length == right_array->is_zero_length &&
           minic_c0_types_compatible(program, left_array->element_type, right_array->element_type);
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
    if (minic_type_is_enum(left_unqualified) &&
        right_unqualified.base_kind == MINIC_TYPE_BASE_INT) {
        entity = minic_c0_program_enum(program, left_unqualified.enum_id);
        return entity != NULL && entity->is_complete &&
               minic_type_equal(entity->compatible_type, right_unqualified);
    }
    if (minic_type_is_enum(right_unqualified) &&
        left_unqualified.base_kind == MINIC_TYPE_BASE_INT) {
        entity = minic_c0_program_enum(program, right_unqualified.enum_id);
        return entity != NULL && entity->is_complete &&
               minic_type_equal(entity->compatible_type, left_unqualified);
    }
    return minic_type_equal(left_unqualified, right_unqualified) ||
           minic_c0_array_shapes_compatible(program, left_unqualified, right_unqualified);
}
'''
if text.count(old) != 1:
    raise SystemExit("types compatible owner shape changed")
text = text.replace(old, new)
old = '''    if (minic_type_assignment_compatible(target_type, source->type)) {
        return true;
    }
    return minic_type_is_pointer(target_type) &&
           minic_c0_expression_is_null_pointer_constant_v0(program, source_expression_id);
'''
new = '''    if (minic_type_assignment_compatible(target_type, source->type)) {
        return true;
    }
    if (minic_type_is_pointer(target_type) && minic_type_is_pointer(source->type) &&
        minic_c0_types_compatible(program, target_type, source->type)) {
        return true;
    }
    return minic_type_is_pointer(target_type) &&
           minic_c0_expression_is_null_pointer_constant_v0(program, source_expression_id);
'''
if text.count(old) != 1:
    raise SystemExit("assignment compatible owner shape changed")
ast.write_text(text.replace(old, new))

fixture = root / "tests/compiler/c0/gnu_array_object_identity.c"
text = fixture.read_text()
append = r'''

typedef struct CpuMask CpuMaskVar[1];
struct RqMaskHolder {
    CpuMaskVar scratch_mask;
};
static int accept_mask_pointer(CpuMaskVar *mask) {
    return (int)sizeof(**mask);
}
int typedef_array_member_address_call(struct RqMaskHolder *rq) {
    return accept_mask_pointer(&rq->scratch_mask);
}
'''
if "typedef_array_member_address_call" in text:
    raise SystemExit("array compatibility fixture already present")
fixture.write_text(text + append)

(root / "tests/compiler/c0/invalid_typedef_array_member_address_call.c").write_text(r'''struct CpuMask {
    unsigned long bits[1];
};
typedef struct CpuMask OneMask[1];
typedef struct CpuMask TwoMasks[2];
struct WrongMaskHolder {
    TwoMasks scratch_mask;
};
static int accept_one(OneMask *mask) {
    return (int)sizeof(**mask);
}
int reject_wrong_bound(struct WrongMaskHolder *rq) {
    return accept_one(&rq->scratch_mask);
}
''')

runner = root / "tests/compiler/c0/run-gnu-array-object-identity.sh"
text = runner.read_text()
old = '''sed -n '/fixed_member_index:/,/^\\.size/p' "$assembly" | grep -F '  slli a0, a0, 3' >/dev/null
for invalid in invalid_record_array_assignment invalid_record_array_update invalid_flexible_array_sizeof; do
'''
new = '''sed -n '/fixed_member_index:/,/^\\.size/p' "$assembly" | grep -F '  slli a0, a0, 3' >/dev/null
grep -F 'typedef_array_member_address_call:' "$assembly" >/dev/null
for invalid in invalid_record_array_assignment invalid_record_array_update invalid_flexible_array_sizeof invalid_typedef_array_member_address_call; do
'''
if text.count(old) != 1:
    raise SystemExit("array identity runner loop shape changed")
text = text.replace(old, new)
old = '''grep -F 'sizeof requires a supported complete type' "$work/invalid_flexible_array_sizeof.err" >/dev/null
printf '%s\\n' 'PASS compiler/c0/gnu_array_object_identity record=fixed+flexible local=legacy decay=shared address-of=pointer-to-array sizeof=no-decay typeof=no-decay subscript=shared mutation=reject'
'''
new = '''grep -F 'sizeof requires a supported complete type' "$work/invalid_flexible_array_sizeof.err" >/dev/null
grep -F 'call argument type does not match declaration' "$work/invalid_typedef_array_member_address_call.err" >/dev/null
printf '%s\\n' 'PASS compiler/c0/gnu_array_object_identity record=fixed+flexible+typedef-array local=legacy decay=shared address-of=pointer-to-array structural-array-compatibility=1 bounds=mismatch-reject sizeof=no-decay typeof=no-decay subscript=shared mutation=reject'
'''
if text.count(old) != 1:
    raise SystemExit("array identity runner message shape changed")
runner.write_text(text.replace(old, new))
