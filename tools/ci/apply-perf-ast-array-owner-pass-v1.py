#!/usr/bin/env python3
"""One-factor OPT-IN performance experiment (never modifies production on Git).

AST verifier currently does a full Program-wide ownership scan for EVERY
incomplete array descriptor, and repeats the entire process in PARSED and
NORMALIZED verification. This creates O(A_incomplete * (E+F+...)) cost.
Build one O(A+E+...) bitset of semantic ownership per verifier invocation.
Exact original scans remain the OOM fallback. Preserves error stage/order.
"""
from pathlib import Path

path=Path("src/frontend/ast_verifier.c")
s=path.read_text()

def replace_one(old, new):
    global s
    if s.count(old)!=1:
        raise SystemExit("AST_ARRAY_OWNER=ANCHOR_MISMATCH count=%d %r" % (s.count(old),old[:130]))
    s=s.replace(old,new,1)

anchor="static bool function_alias_signature_matches(const MinicC0Program *program,\n"
helper=r'''
/* A type can own a descriptor by value (only expression / typedef / extern
 * array) or only through a pointer (the remaining categories). Do not follow
 * nested array element types: exact old semantics compare direct type IDs. */
static void minic_verify_mark_array_owner(
    unsigned char *owned, size_t count, MinicType type, bool requires_pointer) {
    if (type.base_kind == MINIC_TYPE_BASE_ARRAY &&
        type.array_type_id < count &&
        (!requires_pointer || type.pointer_depth != 0U)) {
        owned[type.array_type_id] = 1U;
    }
}

/* Allocation/index creation is an optional accelerator; malformed Programs
 * still use the existing verifier's validation/error policy, with guards for
 * invalid nested field buffers not validated until later verification stages. */
static unsigned char *minic_verify_build_array_owners(const MinicC0Program *program) {
    unsigned char *owned;
    size_t i;
    bool has_incomplete = false;

    if (program == NULL || program->array_type_count == 0U) {
        return NULL;
    }
    for (i = 0U; i < program->array_type_count; ++i) {
        const MinicArrayType *array_type = &program->array_types[i];
        if (array_type->element_count == 0U && !array_type->is_zero_length &&
            !array_type->is_query_materialized) {
            has_incomplete = true;
            break;
        }
    }
    if (!has_incomplete) {
        return NULL;
    }
    owned = (unsigned char *)calloc(program->array_type_count, sizeof(*owned));
    if (owned == NULL) {
        return NULL;
    }
    for (i = 0U; i < program->expression_count; ++i) {
        minic_verify_mark_array_owner(
            owned, program->array_type_count, program->expressions[i].type, false);
    }
    for (i = 0U; i < program->type_alias_count; ++i) {
        minic_verify_mark_array_owner(
            owned, program->array_type_count, program->type_aliases[i].type, false);
    }
    for (i = 0U; i < program->global_object_count; ++i) {
        const MinicGlobalObject *object = &program->global_objects[i];
        bool requires_pointer =
            !(object->is_extern && minic_type_is_array(object->type));
        minic_verify_mark_array_owner(
            owned, program->array_type_count, object->type, requires_pointer);
    }
    for (i = 0U; i < program->local_count; ++i) {
        minic_verify_mark_array_owner(
            owned, program->array_type_count, program->locals[i].type, true);
    }
    for (i = 0U; i < program->record_count; ++i) {
        const MinicRecord *record = &program->records[i];
        size_t j;
        if (record->fields == NULL) {
            continue;
        }
        for (j = 0U; j < record->field_count; ++j) {
            minic_verify_mark_array_owner(
                owned, program->array_type_count, record->fields[j].type, true);
        }
    }
    for (i = 0U; i < program->function_count; ++i) {
        const MinicFunction *function = &program->functions[i];
        size_t j;
        minic_verify_mark_array_owner(
            owned, program->array_type_count, function->return_type, true);
        for (j = 0U; j < function->parameter_count &&
                       j < MINIC_MAX_FUNCTION_PARAMETERS; ++j) {
            minic_verify_mark_array_owner(
                owned, program->array_type_count, function->parameter_types[j], true);
        }
    }
    for (i = 0U; i < program->function_type_count; ++i) {
        const MinicFunctionType *ftype = &program->function_types[i];
        size_t j;
        minic_verify_mark_array_owner(
            owned, program->array_type_count, ftype->return_type, true);
        for (j = 0U; j < ftype->parameter_count &&
                       j < MINIC_MAX_FUNCTION_PARAMETERS; ++j) {
            minic_verify_mark_array_owner(
                owned, program->array_type_count, ftype->parameter_types[j], true);
        }
    }
    return owned;
}

'''
replace_one(anchor,helper+anchor)
main="bool minic_c0_program_verify_target_detailed(const MinicC0Program *program,"
start=s.index(main)
method=s[start:]
# All changes are scoped to the public verifier; avoid changing helpers.
def method_one(old,new):
    global method
    if method.count(old)!=1:
        raise SystemExit("AST_ARRAY_OWNER=METHOD_ANCHOR_MISMATCH count=%d %r"%
                         (method.count(old),old[:100]))
    method=method.replace(old,new,1)
method_one("    MinicC0AstVerifyStage stage;\n",
  "    MinicC0AstVerifyStage stage;\n    unsigned char *array_owners = NULL;\n")
method_one("        return false;                                                                         \\\n",
  "        free(array_owners);                                                                    \\\n"
  "        return false;                                                                         \\\n")
stage_anchor="    stage = MINIC_C0_AST_VERIFY_ENUM;\n"
method_one(stage_anchor,
  "    /* The storage contract is checked above before indexing any Program arrays. */\n"
  "    array_owners = minic_verify_build_array_owners(program);\n"
  +stage_anchor)
method_one("            !incomplete_array_has_semantic_owner(program, index)) {",
  "            !(array_owners != NULL ? array_owners[index] != 0U\n"
  "                                    : incomplete_array_has_semantic_owner(program, index))) {")
# One historical direct return used for special local metadata diagnostic.
oldDirect='''            return false;
        }
    }
    stage = MINIC_C0_AST_VERIFY_FIXED_REGISTER;'''
newDirect='''            free(array_owners);
            return false;
        }
    }
    stage = MINIC_C0_AST_VERIFY_FIXED_REGISTER;'''
method_one(oldDirect,newDirect)
method_one("#undef MINIC_AST_VERIFY_FAIL\n    return true;",
           "#undef MINIC_AST_VERIFY_FAIL\n    free(array_owners);\n    return true;")
s=s[:start]+method
if s.count("minic_verify_build_array_owners(")!=2:
    raise SystemExit("AST_ARRAY_OWNER=BUILD_REF_COUNT")
path.write_text(s)
print("MINIC_PERF_AST_ARRAY_OWNER_PASS_V1=APPLIED")
