#ifndef MINIC_TARGET_DATA_LAYOUT_H
#define MINIC_TARGET_DATA_LAYOUT_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct MinicDataLayout {
    size_t pointer_size;
    size_t pointer_alignment;
    size_t integer_size[MINIC_INTEGER_RANK_INT128 + 1U];
    size_t integer_alignment[MINIC_INTEGER_RANK_INT128 + 1U];
    size_t float_size;
    size_t float_alignment;
    size_t double_size;
    size_t double_alignment;
    size_t long_double_size;
    size_t long_double_alignment;
    /* Separate IEEE binary128 identity; 0 means unsupported target. */
    size_t float128_size;
    size_t float128_alignment;
} MinicDataLayout;

const MinicDataLayout *minic_default_data_layout(void);
/* x86-64 Linux SysV has a distinct target identity even when sizes coincide. */
const MinicDataLayout *minic_x86_64_linux_data_layout(void);
bool minic_data_layout_type(const MinicDataLayout *layout,
                            const MinicC0Program *program,
                            MinicType type,
                            size_t *size,
                            size_t *alignment);
bool minic_data_layout_global_object(const MinicDataLayout *layout,
                                     const MinicC0Program *program,
                                     const MinicGlobalObject *object,
                                     size_t *size,
                                     size_t *alignment);
bool minic_data_layout_record_field_alignment(const MinicDataLayout *layout,
                                              const MinicC0Program *program,
                                              const MinicRecord *record,
                                              size_t field_index,
                                              size_t *alignment);
bool minic_data_layout_record_field_layout(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset,
                                           size_t *bit_offset);
bool minic_data_layout_record_field_offset(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset);
bool minic_data_layout_global_relocation_offset(const MinicDataLayout *layout,
                                                const MinicC0Program *program,
                                                const MinicGlobalObject *object,
                                                const MinicGlobalRelocation *relocation,
                                                size_t *offset);
bool minic_data_layout_global_relocation_target_addend(const MinicDataLayout *layout,
                                                       const MinicC0Program *program,
                                                       const MinicGlobalRelocation *relocation,
                                                       int64_t *addend);

#endif
