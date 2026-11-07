/* Core's public headers must compile as a source-language-independent API.
 * Deliberately poison C declaration identities before including every Core
 * header: a future accidental C descriptor import is a compile error. */
#define MinicType MINIC_FORBIDDEN_C_SOURCE_TYPE
#define MinicTypeBaseKind MINIC_FORBIDDEN_C_SOURCE_BASE_KIND
#define MinicEnumId MINIC_FORBIDDEN_C_ENUM_ID
#define MinicRecordId MINIC_FORBIDDEN_C_RECORD_ID
#define MinicFunctionTypeId MINIC_FORBIDDEN_C_FUNCTION_TYPE_ID
#define MinicExpression MINIC_FORBIDDEN_C_EXPRESSION
#define MinicProgram MINIC_FORBIDDEN_C_PROGRAM

#include "core/address_alignment.h"
#include "core/core_ir.h"
#include "core/core_ir_storage.h"
#include "core/core_type.h"
#include "core/module_ir.h"
#include "core/symbol_identity.h"
#include "core/value_uses.h"

/* Check that the only type identities exposed to producers and consumers are
 * Core-owned. This probe intentionally needs no Frontend/C lowerer object. */
static MinicCoreTypeId core_type_identity(const MinicCoreFunction *function) {
    return minic_core_function_return_type(function);
}

int main(void) {
    MinicCoreFunction function;
    minic_core_function_initialize(&function);
    return core_type_identity(&function) == MINIC_CORE_TYPE_INVALID ? 0 : 1;
}
