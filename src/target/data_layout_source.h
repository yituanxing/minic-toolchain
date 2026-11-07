#ifndef MINIC_TARGET_DATA_LAYOUT_SOURCE_H
#define MINIC_TARGET_DATA_LAYOUT_SOURCE_H

#include "target/data_layout.h"
#include "frontend/type.h"

/* Source object traversal and C nominal-record semantics are never part of
 * machine TargetInfo. Their implementation is source_policy/data_layout_source.c. */
typedef struct MinicC0Program MinicC0Program;
typedef struct MinicGlobalObject MinicGlobalObject;
typedef struct MinicRecord MinicRecord;
typedef struct MinicGlobalRelocation MinicGlobalRelocation;
struct MinicTargetInvocation;

bool minic_data_layout_type(const struct MinicTargetInvocation *invocation,
                            const MinicC0Program *program,
                            MinicType type,
                            size_t *size,
                            size_t *alignment);
/* Machine-neutral validation of the complete C0 object and record layout.
 * Physical frame allocation, value slots, and target ABI are NOT computed.
 * This is the unique translation-unit layout admission for all five targets. */
typedef enum MinicDataLayoutProgramCheck {
    MINIC_DATA_LAYOUT_PROGRAM_OK = 0,
    MINIC_DATA_LAYOUT_PROGRAM_INVALID_INPUT,
    MINIC_DATA_LAYOUT_PROGRAM_INVALID_RECORD,
    MINIC_DATA_LAYOUT_PROGRAM_INVALID_GLOBAL
} MinicDataLayoutProgramCheck;
MinicDataLayoutProgramCheck minic_data_layout_validate_program(
    const struct MinicTargetInvocation *, const MinicC0Program *);

bool minic_data_layout_global_object(const struct MinicTargetInvocation *invocation,
                                     const MinicC0Program *program,
                                     const MinicGlobalObject *object,
                                     size_t *size,
                                     size_t *alignment);
bool minic_data_layout_record_field_alignment(const struct MinicTargetInvocation *invocation,
                                              const MinicC0Program *program,
                                              const MinicRecord *record,
                                              size_t field_index,
                                              size_t *alignment);
/* The C storage boundary, including for bit-fields. Unlike the public
 * __alignof__(member) query above, this permits bit-field members and
 * applies the unnamed zero-width natural-alignment fence. */
bool minic_data_layout_record_field_storage_alignment(
    const struct MinicTargetInvocation *invocation, const MinicC0Program *program,
    const MinicRecord *record, size_t field_index, size_t *alignment);
bool minic_data_layout_record_field_layout(const struct MinicTargetInvocation *invocation,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset,
                                           size_t *bit_offset);
bool minic_data_layout_record_field_offset(const struct MinicTargetInvocation *invocation,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset);
bool minic_data_layout_global_relocation_offset(const struct MinicTargetInvocation *invocation,
                                                const MinicC0Program *program,
                                                const MinicGlobalObject *object,
                                                const MinicGlobalRelocation *relocation,
                                                size_t *offset);
bool minic_data_layout_global_relocation_target_addend(const struct MinicTargetInvocation *invocation,
                                                       const MinicC0Program *program,
                                                       const MinicGlobalRelocation *relocation,
                                                       int64_t *addend);

#endif
