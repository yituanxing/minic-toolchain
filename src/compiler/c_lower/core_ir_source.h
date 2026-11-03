#ifndef MINIC_CORE_CORE_IR_SOURCE_H
#define MINIC_CORE_CORE_IR_SOURCE_H

/* C-language admission side of Core IR. Machine consumers include core_ir.h,
 * which owns no MinicType, C declaration identity, or source lowering draft.
 * This adapter exists only until source types have been snapshotted. */
#include "core/core_ir.h"
#include "frontend/type.h"

/* A completed C enum needs an effective integer type before source values
 * can be imported. This table has no counterpart in executable Core IR. */
typedef struct MinicCoreEnumType {
    MinicEnumId enum_id;
    MinicType effective_integer_type;
} MinicCoreEnumType;

/* The same source signature storage supports direct callee declarations and
 * indirect call signatures. Only indirect calls carry a C function-type ID;
 * Core consumers use core_function_type_id after the import transaction. */
typedef struct MinicCoreSourceCallType {
    MinicFunctionTypeId function_type_id;
    MinicType return_type;
    MinicType *parameter_types;
} MinicCoreSourceCallType;

/* Parallel arrays are indexed by the corresponding Core collection ID.
 * Source types live solely in this C adapter until publication. */
typedef struct MinicCoreSourceDraft {
    MinicType return_type;
    MinicType *parameter_types;

    MinicCoreEnumType *enum_types;
    size_t enum_type_count;
    size_t enum_type_capacity;

    MinicType *fixed_register_binding_types;
    size_t fixed_register_binding_type_count;
    size_t fixed_register_binding_type_capacity;

    MinicType *object_types;
    size_t object_type_count;
    size_t object_type_capacity;

    MinicType *global_types;
    size_t global_type_count;
    size_t global_type_capacity;

    MinicCoreSourceCallType *callees;
    size_t callee_count;
    size_t callee_capacity;

    MinicCoreSourceCallType *call_signatures;
    size_t call_signature_count;
    size_t call_signature_capacity;

    MinicType *instruction_types;
    size_t instruction_type_count;
    size_t instruction_type_capacity;
} MinicCoreSourceDraft;

/* The C-to-Core construction transaction exclusively owns its unsealed Core
 * function and all C source descriptors. Published MinicCoreFunction values
 * never embed or own C-language adapter state. */
typedef struct MinicCoreSourceBuilder {
    MinicCoreFunction core;
    MinicCoreSourceDraft *source_draft;
} MinicCoreSourceBuilder;

void minic_core_source_builder_initialize(MinicCoreSourceBuilder *builder);
void minic_core_source_builder_destroy(MinicCoreSourceBuilder *builder);
/* Transfer only a sealed function into an initialized Core output. */
bool minic_core_source_builder_take_core(MinicCoreSourceBuilder *builder,
                                         MinicCoreFunction *output);

/* Resolves already checked source descriptors into the shared CoreType arena.
 * Publication is atomic from the function consumer's perspective: failed
 * imports leave the source draft intact and the function unsealed. */
typedef bool (*MinicCoreTypeSnapshotResolver)(void *user_data,
                                              MinicType source_type,
                                              MinicCoreTypeId *core_type_id);

bool minic_core_function_snapshot_types(MinicCoreSourceBuilder *builder,
                                        const MinicCoreTypeContext *context,
                                        MinicCoreTypeSnapshotResolver resolver,
                                        void *user_data);
bool minic_core_scalar_bitcast_types_valid(MinicType target_type,
                                           MinicType source_type);

bool minic_core_function_set_signature(MinicCoreSourceBuilder *builder,
                                       const char *name,
                                       size_t name_length,
                                       MinicType return_type,
                                       const MinicType *parameter_types,
                                       size_t parameter_count);
bool minic_core_function_add_enum_type(MinicCoreSourceBuilder *builder,
                                       MinicEnumId enum_id,
                                       MinicType effective_integer_type);
bool minic_core_function_effective_integer_type(const MinicCoreSourceBuilder *builder,
                                                MinicType type,
                                                MinicType *effective_type);
bool minic_core_function_add_fixed_register_binding(MinicCoreSourceBuilder *builder,
                                                     const char *register_name,
                                                     size_t register_name_length,
                                                     MinicType type,
                                                     bool is_local,
                                                     size_t *binding_id);
bool minic_core_function_add_object(MinicCoreSourceBuilder *builder,
                                    MinicSourceSpan span,
                                    MinicType type,
                                    MinicCoreObjectId *object_id);
bool minic_core_function_add_global(MinicCoreSourceBuilder *builder,
                                    const char *name,
                                    size_t name_length,
                                    MinicType type,
                                    MinicCoreGlobalId *global_id);
bool minic_core_function_add_callee(MinicCoreSourceBuilder *builder,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    bool is_variadic,
                                    MinicFunctionCallingConvention calling_convention,
                                    MinicCoreCalleeId *callee_id);
bool minic_core_function_add_call_signature(MinicCoreSourceBuilder *builder,
                                            MinicFunctionTypeId function_type_id,
                                            MinicType return_type,
                                            const MinicType *parameter_types,
                                            size_t parameter_count,
                                            bool is_variadic,
                                            MinicFunctionCallingConvention calling_convention,
                                            MinicCoreCallSignatureId *signature_id);

/* The common append functions remain available to independent Core producers.
 * C lowering uses these source wrappers to attach exactly one source type to
 * each appended instruction in the transient, parallel source draft. */
bool minic_core_function_append_source_value_instruction(
    MinicCoreSourceBuilder *builder,
    MinicCoreBlockId block_id,
    const MinicCoreInstruction *instruction,
    MinicType source_type,
    MinicCoreValueId *value_id);
bool minic_core_function_append_source_effect_instruction(
    MinicCoreSourceBuilder *builder,
    MinicCoreBlockId block_id,
    const MinicCoreInstruction *instruction,
    MinicType source_type);

#endif
