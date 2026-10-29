#include "core/core_ir.h"
#include "frontend/type.h"

#include <stdio.h>

/* Pure Core metadata exercise: no target backend is allowed to infer that
 * MS_X64 implies SysV machine argument locations. */
int main(void) {
    MinicCoreFunction fn;
    MinicCoreCalleeId direct, repeated_direct, distinct_direct;
    MinicCoreCallSignatureId indirect, repeated_indirect, distinct_indirect;
    const MinicType scalar = minic_type_int();
    const MinicFunctionCallingConvention invalid =
        (MinicFunctionCallingConvention)99;

    minic_core_function_initialize(&fn);
    if (!minic_core_function_set_signature(
            &fn, "caller", 6U, scalar, &scalar, 1U) ||
        fn.calling_convention != MINIC_CALLING_CONVENTION_DEFAULT ||
        !minic_core_function_set_calling_convention(
            &fn, MINIC_CALLING_CONVENTION_MS_X64) ||
        fn.calling_convention != MINIC_CALLING_CONVENTION_MS_X64 ||
        minic_core_function_set_calling_convention(
            &fn, MINIC_CALLING_CONVENTION_DEFAULT) ||
        minic_core_function_set_calling_convention(&fn, invalid))
        return 2;

    if (!minic_core_function_add_callee(
            &fn, "efi_method", 10U, scalar, &scalar, 1U, false,
            MINIC_CALLING_CONVENTION_MS_X64, &direct) ||
        !minic_core_function_add_callee(
            &fn, "efi_method", 10U, scalar, &scalar, 1U, false,
            MINIC_CALLING_CONVENTION_MS_X64, &repeated_direct) ||
        direct != repeated_direct || fn.callee_count != 1U ||
        fn.callees[direct].calling_convention != MINIC_CALLING_CONVENTION_MS_X64)
        return 3;

    /* Same ELF symbol must never be interned with conflicting ABI metadata. */
    if (minic_core_function_add_callee(
            &fn, "efi_method", 10U, scalar, &scalar, 1U, false,
            MINIC_CALLING_CONVENTION_DEFAULT, &distinct_direct) ||
        minic_core_function_add_callee(
            &fn, "efi_method", 10U, scalar, &scalar, 1U, false,
            invalid, &distinct_direct) ||
        fn.callee_count != 1U)
        return 4;

    if (!minic_core_function_add_callee(
            &fn, "sysv_helper", 11U, scalar, &scalar, 1U, false,
            MINIC_CALLING_CONVENTION_DEFAULT, &distinct_direct) ||
        distinct_direct == direct || fn.callee_count != 2U ||
        fn.callees[distinct_direct].calling_convention != MINIC_CALLING_CONVENTION_DEFAULT)
        return 5;

    if (!minic_core_function_add_call_signature(
            &fn, 7U, scalar, &scalar, 1U, false,
            MINIC_CALLING_CONVENTION_MS_X64, &indirect) ||
        !minic_core_function_add_call_signature(
            &fn, 7U, scalar, &scalar, 1U, false,
            MINIC_CALLING_CONVENTION_MS_X64, &repeated_indirect) ||
        indirect != repeated_indirect || fn.call_signature_count != 1U ||
        fn.call_signatures[indirect].calling_convention != MINIC_CALLING_CONVENTION_MS_X64)
        return 6;

    /* Defensive Core interning checks convention even when a malformed
     * producer reuses one function-type ID for two ABI signatures. */
    if (!minic_core_function_add_call_signature(
            &fn, 7U, scalar, &scalar, 1U, false,
            MINIC_CALLING_CONVENTION_DEFAULT, &distinct_indirect) ||
        distinct_indirect == indirect || fn.call_signature_count != 2U ||
        minic_core_function_add_call_signature(
            &fn, 7U, scalar, &scalar, 1U, false,
            invalid, &distinct_indirect))
        return 7;

    minic_core_function_destroy(&fn);
    puts("CORE_CALLING_CONVENTION_METADATA=PASS direct indirect conflict invalid");
    return 0;
}
