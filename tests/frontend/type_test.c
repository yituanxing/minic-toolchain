#include "frontend/type.h"

#include <limits.h>
#include <stdio.h>

static int fail(const char *message)
{
    (void)fprintf(stderr, "FAIL frontend/type: %s\n", message);
    return 1;
}

static int test_builtin_declaration_roles(void) {
    static const struct {
        MinicTypeBaseKind kind;
        MinicBuiltinDeclarationRole role;
    } cases[] = {
        {MINIC_TYPE_BASE_FLOAT, MINIC_BUILTIN_DECLARATION_STANDARD_FLOAT},
        {MINIC_TYPE_BASE_DOUBLE, MINIC_BUILTIN_DECLARATION_STANDARD_FLOAT},
        {MINIC_TYPE_BASE_LONG_DOUBLE, MINIC_BUILTIN_DECLARATION_STANDARD_FLOAT},
        {MINIC_TYPE_BASE_FLOAT128, MINIC_BUILTIN_DECLARATION_EXTENDED_FLOAT},
        {MINIC_TYPE_BASE_FLOAT32, MINIC_BUILTIN_DECLARATION_EXTENDED_FLOAT},
        {MINIC_TYPE_BASE_FLOAT64, MINIC_BUILTIN_DECLARATION_EXTENDED_FLOAT},
        {MINIC_TYPE_BASE_FLOAT32X, MINIC_BUILTIN_DECLARATION_EXTENDED_FLOAT},
        {MINIC_TYPE_BASE_FLOAT64X, MINIC_BUILTIN_DECLARATION_EXTENDED_FLOAT},
        {MINIC_TYPE_BASE_AARCH64_FLOAT32X4, MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE},
        {MINIC_TYPE_BASE_AARCH64_FLOAT64X2, MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE},
        {MINIC_TYPE_BASE_AARCH64_SVE_FLOAT32, MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE},
        {MINIC_TYPE_BASE_AARCH64_SVE_FLOAT64, MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE},
        {MINIC_TYPE_BASE_AARCH64_SVE_BOOL, MINIC_BUILTIN_DECLARATION_HEADER_ONLY_OPAQUE},
        {MINIC_TYPE_BASE_X86_VA_LIST_CURSOR, MINIC_BUILTIN_DECLARATION_ABI_CURSOR}
    };
    size_t index;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        if (minic_type_builtin_declaration_role(cases[index].kind) != cases[index].role) {
            return fail("extended C builtin declaration trait classification");
        }
    }
    if (minic_type_builtin_declaration_role(MINIC_TYPE_BASE_VOID) !=
            MINIC_BUILTIN_DECLARATION_INVALID ||
        minic_type_builtin_declaration_role(MINIC_TYPE_BASE_INT) !=
            MINIC_BUILTIN_DECLARATION_INVALID ||
        !minic_type_is_void(minic_type_aarch64_header_builtin(MINIC_TYPE_BASE_FLOAT128)) ||
        minic_type_aarch64_header_builtin(MINIC_TYPE_BASE_AARCH64_SVE_BOOL).base_kind !=
            MINIC_TYPE_BASE_AARCH64_SVE_BOOL) {
        return fail("builtin declaration trait rejects non-vector ISA identity");
    }
    return 0;
}

int main(void)
{
    MinicType void_type;
    MinicType void_pointer_type;
    MinicType unsigned_char_type;
    MinicType unsigned_char_pointer_type;
    MinicType integer_type;
    MinicType unsigned_integer_type;
    MinicType signed_long_type;
    MinicType unsigned_long_type;
    MinicType const_long_type;
    MinicType double_type;
    MinicType const_double_type;
    MinicType double_pointer_type;
    MinicType unsigned_pointer_type;
    MinicType const_integer_type;
    MinicType const_pointer_type;
    MinicType pointer_type;
    MinicType const_pointer_object_type;
    MinicType pointer_to_pointer_type;
    MinicType pointer_to_const_pointer_type;
    MinicType const_pointer_to_pointer_type;
    MinicType const_void_type;
    MinicType const_void_pointer_type;
    MinicType volatile_void_type;
    MinicType volatile_void_pointer_type;
    MinicType volatile_pointer_object_type;
    MinicType pointer_to_volatile_pointer_type;
    MinicType record_type;
    MinicType other_record_type;
    MinicType record_pointer_type;
    MinicType recovered_type;
    MinicType unqualified_type;
    MinicType malformed_type;
    MinicType overflow_type;

    if (test_builtin_declaration_roles() != 0) {
        return 1;
    }

    void_type = minic_type_void();
    if (!minic_type_is_void(void_type) ||
        minic_type_is_const(void_type) ||
        minic_type_is_integer(void_type) ||
        minic_type_is_double(void_type) ||
        minic_type_is_char_integer(void_type) ||
        minic_type_is_signed_integer(void_type) ||
        minic_type_is_unsigned_integer(void_type) ||
        minic_type_is_record(void_type) ||
        minic_type_is_pointer(void_type)) {
        return fail("void classification");
    }
    if (!minic_type_pointer_to(void_type, &void_pointer_type) ||
        !minic_type_is_pointer(void_pointer_type) ||
        minic_type_is_void(void_pointer_type) ||
        !minic_type_pointee(void_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, void_type)) {
        return fail("void pointer construction");
    }

    integer_type = minic_type_int();
    if (!minic_type_is_integer(integer_type) ||
        minic_type_is_double(integer_type) ||
        minic_type_is_char_integer(integer_type) ||
        !minic_type_is_signed_integer(integer_type) ||
        minic_type_is_unsigned_integer(integer_type) ||
        minic_type_is_const(integer_type) ||
        minic_type_is_void(integer_type) ||
        minic_type_is_record(integer_type) ||
        minic_type_is_pointer(integer_type)) {
        return fail("signed int classification");
    }

    unsigned_integer_type = minic_type_unsigned_int();
    if (!minic_type_is_integer(unsigned_integer_type) ||
        minic_type_is_double(unsigned_integer_type) ||
        minic_type_is_char_integer(unsigned_integer_type) ||
        minic_type_is_signed_integer(unsigned_integer_type) ||
        !minic_type_is_unsigned_integer(unsigned_integer_type) ||
        minic_type_is_const(unsigned_integer_type) ||
        minic_type_is_void(unsigned_integer_type) ||
        minic_type_is_record(unsigned_integer_type) ||
        minic_type_is_pointer(unsigned_integer_type) ||
        minic_type_equal(integer_type, unsigned_integer_type)) {
        return fail("unsigned int identity");
    }

    signed_long_type = minic_type_long();
    unsigned_long_type = minic_type_unsigned_long();
    if (!minic_type_is_integer(signed_long_type) ||
        !minic_type_is_long_integer(signed_long_type) ||
        !minic_type_is_signed_integer(signed_long_type) ||
        minic_type_is_unsigned_integer(signed_long_type) ||
        minic_type_is_double(signed_long_type) ||
        !minic_type_is_integer(unsigned_long_type) ||
        !minic_type_is_long_integer(unsigned_long_type) ||
        minic_type_is_signed_integer(unsigned_long_type) ||
        !minic_type_is_unsigned_integer(unsigned_long_type) ||
        minic_type_is_double(unsigned_long_type) ||
        minic_type_equal(signed_long_type, integer_type) ||
        minic_type_equal(unsigned_long_type, unsigned_integer_type) ||
        minic_type_equal(signed_long_type, unsigned_long_type)) {
        return fail("long integer identity");
    }

    if (!minic_type_add_const(signed_long_type, &const_long_type) ||
        !minic_type_is_const(const_long_type) ||
        !minic_type_is_long_integer(const_long_type)) {
        return fail("const long identity");
    }

    double_type = minic_type_double();
    if (!minic_type_is_double(double_type) || minic_type_is_integer(double_type) ||
        minic_type_is_char_integer(double_type) || minic_type_is_long_integer(double_type) ||
        minic_type_is_signed_integer(double_type) || minic_type_is_unsigned_integer(double_type) ||
        minic_type_is_void(double_type) || minic_type_is_record(double_type) ||
        minic_type_is_array(double_type) || minic_type_is_pointer(double_type) ||
        minic_type_equal(double_type, signed_long_type) ||
        minic_type_equal(double_type, unsigned_long_type)) {
        return fail("double identity");
    }
    if (!minic_type_add_const(double_type, &const_double_type) ||
        !minic_type_is_const(const_double_type) || !minic_type_is_double(const_double_type) ||
        minic_type_equal(double_type, const_double_type) ||
        !minic_type_assignment_compatible(double_type, double_type) ||
        !minic_type_assignment_compatible(const_double_type, double_type) ||
        minic_type_assignment_compatible(double_type, integer_type) ||
        minic_type_assignment_compatible(integer_type, double_type) ||
        !minic_type_cast_compatible(double_type, integer_type) ||
        !minic_type_cast_compatible(integer_type, double_type)) {
        return fail("bounded double conversions");
    }
    if (!minic_type_pointer_to(double_type, &double_pointer_type) ||
        !minic_type_is_pointer(double_pointer_type) || minic_type_is_double(double_pointer_type) ||
        !minic_type_pointee(double_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, double_type)) {
        return fail("double pointer preservation");
    }

    unsigned_char_type = minic_type_unsigned_char();
    if (!minic_type_is_integer(unsigned_char_type) ||
        !minic_type_is_char_integer(unsigned_char_type) ||
        minic_type_is_signed_integer(unsigned_char_type) ||
        !minic_type_is_unsigned_integer(unsigned_char_type) ||
        minic_type_is_double(unsigned_char_type) ||
        minic_type_is_const(unsigned_char_type) ||
        minic_type_is_void(unsigned_char_type) ||
        minic_type_is_record(unsigned_char_type) ||
        minic_type_is_pointer(unsigned_char_type) ||
        minic_type_equal(unsigned_char_type, unsigned_integer_type) ||
        minic_type_equal(unsigned_char_type, integer_type)) {
        return fail("unsigned char identity");
    }
    if (!minic_type_assignment_compatible(integer_type, unsigned_integer_type) ||
        !minic_type_assignment_compatible(unsigned_integer_type, integer_type) ||
        !minic_type_assignment_compatible(unsigned_char_type, integer_type) ||
        !minic_type_assignment_compatible(integer_type, unsigned_char_type) ||
        !minic_type_assignment_compatible(integer_type, integer_type) ||
        minic_type_assignment_compatible(integer_type, void_type) ||
        !minic_type_cast_compatible(unsigned_char_type, integer_type) ||
        !minic_type_cast_compatible(integer_type, unsigned_char_type) ||
        !minic_type_cast_compatible(void_pointer_type, integer_type) ||
        !minic_type_cast_compatible(integer_type, void_pointer_type)) {
        return fail("integer assignment and cast conversions");
    }
    if (!minic_type_pointer_to(unsigned_integer_type, &unsigned_pointer_type) ||
        !minic_type_is_pointer(unsigned_pointer_type) ||
        minic_type_is_integer(unsigned_pointer_type) ||
        !minic_type_pointee(unsigned_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, unsigned_integer_type)) {
        return fail("unsigned int pointer preservation");
    }
    if (!minic_type_pointer_to(unsigned_char_type, &unsigned_char_pointer_type) ||
        !minic_type_is_pointer(unsigned_char_pointer_type) ||
        minic_type_is_integer(unsigned_char_pointer_type) ||
        !minic_type_pointee(unsigned_char_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, unsigned_char_type) ||
        !minic_type_cast_compatible(unsigned_char_pointer_type, unsigned_pointer_type)) {
        return fail("unsigned char pointer preservation");
    }

    if (!minic_type_add_const(integer_type, &const_integer_type) ||
        !minic_type_is_const(const_integer_type) ||
        !minic_type_is_integer(const_integer_type) ||
        !minic_type_is_signed_integer(const_integer_type) ||
        minic_type_equal(integer_type, const_integer_type) ||
        !minic_type_assignment_compatible(const_integer_type, integer_type) ||
        !minic_type_pointer_to(const_integer_type, &const_pointer_type) ||
        minic_type_is_const(const_pointer_type) ||
        !minic_type_pointee(const_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, const_integer_type)) {
        return fail("const int pointee preservation");
    }
    if (!minic_type_pointer_to(integer_type, &pointer_type) ||
        !minic_type_is_pointer(pointer_type) ||
        minic_type_is_const(pointer_type) ||
        minic_type_is_void(pointer_type) ||
        minic_type_is_integer(pointer_type) ||
        minic_type_is_record(pointer_type) ||
        minic_type_equal(integer_type, pointer_type) ||
        minic_type_equal(pointer_type, const_pointer_type) ||
        minic_type_equal(pointer_type, unsigned_pointer_type) ||
        minic_type_equal(pointer_type, double_pointer_type) ||
        minic_type_assignment_compatible(pointer_type, unsigned_pointer_type) ||
        minic_type_assignment_compatible(pointer_type, double_pointer_type) ||
        !minic_type_assignment_compatible(const_pointer_type, pointer_type) ||
        minic_type_assignment_compatible(pointer_type, const_pointer_type)) {
        return fail("int pointer construction and pointee qualification");
    }
    if (!minic_type_add_const(pointer_type, &const_pointer_object_type) ||
        !minic_type_is_const(const_pointer_object_type) ||
        !minic_type_is_pointer(const_pointer_object_type) ||
        minic_type_equal(pointer_type, const_pointer_object_type) ||
        !minic_type_assignment_compatible(pointer_type, const_pointer_object_type) ||
        !minic_type_assignment_compatible(const_pointer_object_type, pointer_type) ||
        !minic_type_unqualified(const_pointer_object_type, &unqualified_type) ||
        !minic_type_equal(unqualified_type, pointer_type) ||
        !minic_type_pointee(const_pointer_object_type, &recovered_type) ||
        !minic_type_equal(recovered_type, integer_type)) {
        return fail("top-level pointer const qualification");
    }
    if (!minic_type_pointer_to(pointer_type, &pointer_to_pointer_type) ||
        pointer_to_pointer_type.pointer_depth != 2U ||
        !minic_type_pointer_to(const_pointer_object_type, &pointer_to_const_pointer_type) ||
        minic_type_is_const(pointer_to_const_pointer_type) ||
        !minic_type_pointee(pointer_to_const_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, const_pointer_object_type) ||
        !minic_type_add_const(pointer_to_pointer_type, &const_pointer_to_pointer_type) ||
        !minic_type_is_const(const_pointer_to_pointer_type) ||
        minic_type_equal(pointer_to_const_pointer_type, const_pointer_to_pointer_type) ||
        !minic_type_unqualified(const_pointer_to_pointer_type, &unqualified_type) ||
        !minic_type_equal(unqualified_type, pointer_to_pointer_type) ||
        !minic_type_assignment_compatible(pointer_to_pointer_type, const_pointer_to_pointer_type) ||
        !minic_type_assignment_compatible(const_pointer_to_pointer_type, pointer_to_pointer_type)) {
        return fail("nested pointer qualifier identity");
    }
    if (!minic_type_pointee(pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, integer_type) ||
        !minic_type_pointee(pointer_to_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, pointer_type)) {
        return fail("pointee recovery");
    }
    if (!minic_type_add_const(void_type, &const_void_type) ||
        !minic_type_pointer_to(const_void_type, &const_void_pointer_type) ||
        !minic_type_add_volatile(void_type, &volatile_void_type) ||
        !minic_type_pointer_to(volatile_void_type, &volatile_void_pointer_type) ||
        !minic_type_add_volatile(pointer_type, &volatile_pointer_object_type) ||
        !minic_type_pointer_to(volatile_pointer_object_type,
                               &pointer_to_volatile_pointer_type) ||
        !minic_type_assignment_compatible(void_pointer_type, pointer_to_pointer_type) ||
        minic_type_assignment_compatible(void_pointer_type,
                                         pointer_to_const_pointer_type) ||
        minic_type_assignment_compatible(void_pointer_type,
                                         pointer_to_volatile_pointer_type) ||
        !minic_type_assignment_compatible(const_void_pointer_type,
                                          pointer_to_const_pointer_type) ||
        !minic_type_assignment_compatible(volatile_void_pointer_type,
                                          pointer_to_volatile_pointer_type) ||
        minic_type_assignment_compatible(pointer_to_pointer_type,
                                         const_void_pointer_type) ||
        minic_type_assignment_compatible(pointer_to_pointer_type,
                                         volatile_void_pointer_type)) {
        return fail("void pointer conversion preserves immediate pointee qualifiers");
    }

    record_type = minic_type_record(3U);
    other_record_type = minic_type_record(4U);
    if (!minic_type_is_record(record_type) ||
        minic_type_is_const(record_type) ||
        minic_type_is_void(record_type) ||
        minic_type_is_integer(record_type) ||
        minic_type_is_double(record_type) ||
        minic_type_is_char_integer(record_type) ||
        minic_type_is_signed_integer(record_type) ||
        minic_type_is_unsigned_integer(record_type) ||
        minic_type_is_pointer(record_type) ||
        !minic_type_equal(record_type, record_type) ||
        minic_type_equal(record_type, other_record_type) ||
        minic_type_equal(record_type, integer_type)) {
        return fail("record identity");
    }
    if (!minic_type_pointer_to(record_type, &record_pointer_type) ||
        !minic_type_is_pointer(record_pointer_type) ||
        minic_type_is_const(record_pointer_type) ||
        minic_type_is_void(record_pointer_type) ||
        minic_type_is_record(record_pointer_type) ||
        !minic_type_pointee(record_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, record_type)) {
        return fail("record pointer construction");
    }

    malformed_type = integer_type;
    malformed_type.pointer_qualifiers = 1U;
    if (minic_type_is_pointer(malformed_type) || minic_type_is_const(malformed_type) ||
        minic_type_add_const(malformed_type, &recovered_type) ||
        minic_type_unqualified(malformed_type, &recovered_type)) {
        return fail("out-of-depth pointer qualifier accepted");
    }
    if (minic_type_add_const(integer_type, NULL) || minic_type_unqualified(integer_type, NULL) ||
        minic_type_pointer_to(integer_type, NULL)) {
        return fail("NULL type output accepted");
    }
    if (minic_type_pointee(integer_type, &recovered_type) ||
        minic_type_pointee(pointer_type, NULL)) {
        return fail("invalid pointee request accepted");
    }
    overflow_type = integer_type;
    overflow_type.pointer_depth = UINT_MAX;
    if (minic_type_pointer_to(overflow_type, &pointer_type)) {
        return fail("pointer-depth overflow accepted");
    }

    (void)printf("PASS frontend/type\n");
    return 0;
}
