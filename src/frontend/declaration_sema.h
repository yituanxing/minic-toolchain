#ifndef MINIC_FRONTEND_DECLARATION_SEMA_H
#define MINIC_FRONTEND_DECLARATION_SEMA_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>

#define MINIC_DECLARATION_MAX_ARRAY_DIMENSIONS 8U

typedef struct MinicDeclarationArraySuffix {
    size_t bounds[MINIC_DECLARATION_MAX_ARRAY_DIMENSIONS];
    size_t dimension_count;
    unsigned int zero_length_mask;
    bool outermost_incomplete;
} MinicDeclarationArraySuffix;

typedef enum MinicDeclarationArrayMaterializeStatus {
    MINIC_DECLARATION_ARRAY_MATERIALIZE_OK = 0,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_INVALID,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_INCOMPLETE_FAILED,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_ZERO_LENGTH_FAILED,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_FIXED_FAILED,
    MINIC_DECLARATION_ARRAY_MATERIALIZE_TRANSACTION_ESCAPED
} MinicDeclarationArrayMaterializeStatus;

typedef struct MinicDeclarationExternalObjectAttributes {
    const char *section_name;
    size_t section_name_length;
    size_t explicit_alignment;
    MinicSymbolVisibility visibility;
    bool has_section;
    bool has_visibility;
} MinicDeclarationExternalObjectAttributes;

typedef enum MinicDeclarationExternalObjectMergeStatus {
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_OK = 0,
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_INVALID,
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_TYPE_CONFLICT,
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_ATTRIBUTE_CONFLICT,
    MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_COMMIT_FAILED
} MinicDeclarationExternalObjectMergeStatus;

typedef enum MinicDeclarationExternalObjectCreateStatus {
    MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK = 0,
    MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_INVALID,
    MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_COMMIT_FAILED
} MinicDeclarationExternalObjectCreateStatus;

bool minic_declaration_array_suffix_valid(const MinicDeclarationArraySuffix *suffix);

MinicDeclarationArrayMaterializeStatus
minic_declaration_materialize_array_suffix(MinicC0Program *program,
                                           MinicType element_type,
                                           const MinicDeclarationArraySuffix *suffix,
                                           MinicType *type);

bool minic_declaration_external_object_types_compatible(const MinicC0Program *program,
                                                        MinicType existing_type,
                                                        MinicType declared_type);

bool minic_declaration_merge_external_array_composite_type(MinicC0Program *program,
                                                           MinicType existing_type,
                                                           MinicType declared_type);

bool minic_declaration_external_object_attributes_valid(
    const MinicDeclarationExternalObjectAttributes *attributes);

bool minic_declaration_apply_object_attributes(
    MinicC0Program *program,
    MinicGlobalObjectId object_id,
    const MinicDeclarationExternalObjectAttributes *attributes);

bool minic_declaration_mark_file_scope_object(MinicC0Program *program,
                                              MinicGlobalObjectId object_id);

MinicDeclarationExternalObjectCreateStatus
minic_declaration_create_external_object(
    MinicC0Program *program,
    const char *name,
    size_t name_length,
    MinicType declared_type,
    bool is_read_only,
    bool is_weak,
    bool is_block_scope_extern_only,
    const MinicDeclarationExternalObjectAttributes *attributes,
    MinicGlobalObjectId *object_id);

MinicDeclarationExternalObjectMergeStatus minic_declaration_merge_external_object(
    MinicC0Program *program,
    MinicGlobalObjectId object_id,
    MinicType declared_type,
    const MinicDeclarationExternalObjectAttributes *attributes);

bool minic_declaration_build_function_type(
    MinicC0Program *program,
    MinicType return_type,
    const MinicType *parameter_types,
    size_t parameter_count,
    bool is_variadic,
    size_t pointer_depth,
    unsigned int pointer_const_qualifiers,
    unsigned int pointer_volatile_qualifiers,
    const MinicDeclarationArraySuffix *array_suffix,
    MinicType *type);

#endif
