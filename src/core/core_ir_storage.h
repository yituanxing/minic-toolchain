#ifndef MINIC_CORE_IR_STORAGE_H
#define MINIC_CORE_IR_STORAGE_H

#include "core/core_ir.h"

/* Source-neutral allocation hooks shared by independent Core construction
 * and the C-language adapter. No source-owned lifecycle belongs to Core. */
bool minic_core_ir_grow_array(void **data, size_t *capacity,
                              size_t count, size_t element_size);
char *minic_core_ir_copy_name(const char *name, size_t name_length);

#endif
