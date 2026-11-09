#include "core/core_type.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A provisional incomplete RECORD is published before importing recursive
 * fields. A first-member-only union initializer leaves its field_count and
 * is_complete storage unspecified. Exercise the real CoreType arena. */
static int check_one(MinicCoreTypeContext *context, int union_record) {
    MinicCoreType type;
    MinicCoreTypeId id;
    const MinicCoreType *stored;
    (void)memset(&type, 0, sizeof(type));
    type.kind = MINIC_CORE_TYPE_RECORD;
    type.value.record.is_union = union_record != 0;
    if (type.value.record.fields != NULL ||
        type.value.record.field_count != 0U ||
        type.value.record.is_complete ||
        type.value.record.has_nondefault_layout_policy) return 1;
    if (!minic_core_type_register(context, &type, &id)) return 2;
    stored = minic_core_type_get(context, id);
    if (stored == NULL || stored->kind != MINIC_CORE_TYPE_RECORD ||
        stored->value.record.is_union != (union_record != 0) ||
        stored->value.record.is_complete ||
        stored->value.record.fields != NULL ||
        stored->value.record.field_count != 0U) return 3;
    return 0;
}

int main(void) {
    MinicCoreTypeContext context;
    int first, second;
    minic_core_type_context_initialize(&context);
    first = check_one(&context, 0);
    second = check_one(&context, 1);
    minic_core_type_context_destroy(&context);
    return first != 0 ? first : second;
}
