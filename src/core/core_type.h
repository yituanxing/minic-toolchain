#ifndef MINIC_CORE_TYPE_H
#define MINIC_CORE_TYPE_H

/* Execution types and ABI-relevant shapes owned by Core, independently of
 * the source-language type arena. A TypeId has meaning only in its context. */
#include "common/calling_convention.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef size_t MinicCoreTypeId;
#define MINIC_CORE_TYPE_INVALID ((MinicCoreTypeId)SIZE_MAX)

typedef enum MinicCoreTypeKind {
    MINIC_CORE_TYPE_VOID = 0,
    MINIC_CORE_TYPE_INTEGER,
    MINIC_CORE_TYPE_FLOAT,
    MINIC_CORE_TYPE_POINTER,
    MINIC_CORE_TYPE_ARRAY,
    MINIC_CORE_TYPE_RECORD,
    MINIC_CORE_TYPE_FUNCTION,
    MINIC_CORE_TYPE_VECTOR,
    MINIC_CORE_TYPE_OPAQUE,
    /* Source-independent memory-access qualifiers wrap a base execution
     * shape without corrupting nominal record identity or ABI layout. */
    MINIC_CORE_TYPE_QUALIFIED
} MinicCoreTypeKind;

typedef enum MinicCoreFloatFormat {
    MINIC_CORE_FLOAT_BINARY32 = 0,
    MINIC_CORE_FLOAT_BINARY64,
    MINIC_CORE_FLOAT_EXTENDED,
    MINIC_CORE_FLOAT_BINARY128,
    MINIC_CORE_FLOAT_EXTENDED32,
    MINIC_CORE_FLOAT_EXTENDED64,
    MINIC_CORE_FLOAT_EXTENDED32X,
    MINIC_CORE_FLOAT_EXTENDED64X
} MinicCoreFloatFormat;

/* Core vectors are classified by the value representation that lowering
 * actually admitted, independently of the source extension naming them.
 * Storage-only vectors are address-backed aggregates. Register-value vectors
 * may participate in typed vector SSA operations and target vector registers.
 * The selected target still owns ABI register assignment and instruction ISA. */
typedef enum MinicCoreVectorRepresentation {
    MINIC_CORE_VECTOR_STORAGE_ONLY = 0,
    MINIC_CORE_VECTOR_REGISTER_VALUE
} MinicCoreVectorRepresentation;

typedef struct MinicCoreRecordField {
    MinicCoreTypeId type_id;
    size_t byte_offset;
    size_t bit_offset;
    size_t bit_width;
    size_t alignment;
    bool is_bit_field;
    /* Non-default layout policy; each target ABI decides its implications. */
    bool has_nondefault_layout_policy;
} MinicCoreRecordField;

typedef struct MinicCoreType {
    MinicCoreTypeKind kind;
    /* False means the type has no object layout, e.g. an incomplete record,
     * function or opaque target declaration. Its use as storage must fail. */
    bool has_layout;
    size_t size;
    size_t alignment;
    bool is_volatile;
    bool is_read_only;
    /* ABI-effective alignment was explicitly overridden on this view. */
    bool has_abi_alignment_override;
    union {
        struct {
            unsigned int bit_width;
            bool is_signed;
            bool is_bool;
        } integer;
        struct {
            MinicCoreFloatFormat format;
        } floating;
        struct {
            MinicCoreTypeId pointee;
        } pointer;
        struct {
            MinicCoreTypeId element;
            size_t count;
            bool is_complete;
        } array;
        struct {
            MinicCoreRecordField *fields;
            size_t field_count;
            /* Source record layout policy is resolved on C ingress. Retain
             * a target-independent nondefault-layout fact alongside the
             * effective size, alignment and member offsets; individual ABIs
             * decide whether or how this constrains register classification. */
            bool has_nondefault_layout_policy;
            bool is_complete;
            bool is_union;
        } record;
        struct {
            MinicCoreTypeId return_type;
            MinicCoreTypeId *parameters;
            size_t parameter_count;
            bool is_variadic;
            MinicFunctionCallingConvention calling_convention;
        } function;
        struct {
            MinicCoreTypeId element;
            size_t element_count;
            MinicCoreVectorRepresentation representation;
        } vector;
        struct {
            MinicCoreTypeId base;
        } qualified;
    } value;
} MinicCoreType;

typedef struct MinicCoreTypeContext {
    MinicCoreType *types;
    size_t type_count;
    size_t type_capacity;
} MinicCoreTypeContext;

void minic_core_type_context_initialize(MinicCoreTypeContext *context);
void minic_core_type_context_destroy(MinicCoreTypeContext *context);
const MinicCoreType *minic_core_type_get(const MinicCoreTypeContext *context,
                                        MinicCoreTypeId id);
/* Peel zero or more memory-effect qualifiers. INVALID signals a bad ID. */
MinicCoreTypeId minic_core_type_unqualified(const MinicCoreTypeContext *context,
                                            MinicCoreTypeId id);
/* Copies owned parameter/member arrays; never retains caller memory. Records
 * may be provisionally registered and then completed by the C ingress. */
bool minic_core_type_register(MinicCoreTypeContext *context,
                              const MinicCoreType *type,
                              MinicCoreTypeId *id);
bool minic_core_type_complete_record(MinicCoreTypeContext *context,
                                     MinicCoreTypeId id,
                                     const MinicCoreType *complete);

#endif
