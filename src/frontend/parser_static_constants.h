#ifndef MINIC_FRONTEND_PARSER_STATIC_CONSTANTS_H
#define MINIC_FRONTEND_PARSER_STATIC_CONSTANTS_H
#include "frontend/parser_internal.h"

/* Shared static relocation records and array slots belong to this private
 * constant-decoding interface; both the decoder and object emitter use them. */
typedef struct MinicStaticObjectRelocationTarget {
    MinicGlobalObjectId object_id;
    size_t member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t member_depth;
    int64_t byte_addend;
} MinicStaticObjectRelocationTarget;

typedef struct MinicStaticPointerInitializer {
    bool has_relocation;
    bool relocation_is_function;
    bool relocation_is_label;
    bool has_explicit_pointer_cast;
    uint64_t bits;
    MinicFunctionId function_id;
    MinicStatementId label_statement_id;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;

typedef struct MinicStaticIntegerAddressValue {
    uint64_t bits;
    bool has_relocation;
    bool is_function;
    MinicFunctionId function_id;
    MinicStaticObjectRelocationTarget object_target;
} MinicStaticIntegerAddressValue;

typedef struct MinicStaticArraySlot {
    MinicStaticIntegerAddressValue integer_initializer;
    MinicStaticPointerInitializer pointer_initializer;
} MinicStaticArraySlot;


/* Private static constant decoding boundary. */
bool minic_parser_static_symbol_function_relocation(const MinicC0Program *program, MinicExpressionId expression_id, MinicFunctionId *target_function_id);
bool minic_parser_static_symbol_add_pointer_offset(int64_t base, int64_t delta, int64_t *result);
bool minic_parser_static_symbol_object_relocation_path(const MinicParser *parser, MinicExpressionId expression_id, MinicStaticObjectRelocationTarget *target);
bool minic_parser_static_symbol_pointer_initializer(MinicParser *parser, MinicType target_type, MinicStaticPointerInitializer *initializer);
/* Shared symbolic-address commit for scalar, array and record initializers. */
bool minic_parser_static_commit_pointer_relocation(
    MinicC0Program *program, MinicGlobalObjectId object_id,
    MinicGlobalRelocationLocationKind location_kind, size_t location_index,
    const MinicStaticPointerInitializer *initializer);
bool minic_parser_static_symbol_floating_initializer_bits(MinicParser *parser, MinicType target_type, uint64_t *bits);

#endif
