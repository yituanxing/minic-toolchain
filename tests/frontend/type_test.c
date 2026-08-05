#include "frontend/type.h"

#include <limits.h>
#include <stdio.h>

static int fail(const char *message)
{
    (void)fprintf(stderr, "FAIL frontend/type: %s\n", message);
    return 1;
}

int main(void)
{
    MinicType void_type;
    MinicType void_pointer_type;
    MinicType integer_type;
    MinicType const_integer_type;
    MinicType const_pointer_type;
    MinicType pointer_type;
    MinicType pointer_to_pointer_type;
    MinicType record_type;
    MinicType other_record_type;
    MinicType record_pointer_type;
    MinicType recovered_type;
    MinicType overflow_type;

    void_type = minic_type_void();
    if (!minic_type_is_void(void_type) ||
        minic_type_is_const(void_type) ||
        minic_type_is_integer(void_type) ||
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
        minic_type_is_const(integer_type) ||
        minic_type_is_void(integer_type) ||
        minic_type_is_record(integer_type) ||
        minic_type_is_pointer(integer_type)) {
        return fail("int classification");
    }
    if (!minic_type_add_const(integer_type, &const_integer_type) ||
        !minic_type_is_const(const_integer_type) ||
        !minic_type_is_integer(const_integer_type) ||
        minic_type_equal(integer_type, const_integer_type) ||
        !minic_type_pointer_to(const_integer_type, &const_pointer_type) ||
        !minic_type_is_const(const_pointer_type) ||
        !minic_type_pointee(const_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, const_integer_type)) {
        return fail("const int pointer preservation");
    }
    if (!minic_type_pointer_to(integer_type, &pointer_type) ||
        !minic_type_is_pointer(pointer_type) ||
        minic_type_is_const(pointer_type) ||
        minic_type_is_void(pointer_type) ||
        minic_type_is_integer(pointer_type) ||
        minic_type_is_record(pointer_type) ||
        minic_type_equal(integer_type, pointer_type) ||
        minic_type_equal(pointer_type, const_pointer_type)) {
        return fail("int pointer construction");
    }
    if (!minic_type_pointer_to(pointer_type, &pointer_to_pointer_type) ||
        pointer_to_pointer_type.pointer_depth != 2U ||
        !minic_type_equal(pointer_type, pointer_type) ||
        minic_type_equal(pointer_type, pointer_to_pointer_type)) {
        return fail("nested pointer construction");
    }
    if (!minic_type_pointee(pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, integer_type) ||
        !minic_type_pointee(pointer_to_pointer_type, &recovered_type) ||
        !minic_type_equal(recovered_type, pointer_type)) {
        return fail("pointee recovery");
    }

    record_type = minic_type_record(3U);
    other_record_type = minic_type_record(4U);
    if (!minic_type_is_record(record_type) ||
        minic_type_is_const(record_type) ||
        minic_type_is_void(record_type) ||
        minic_type_is_integer(record_type) ||
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

    if (minic_type_add_const(integer_type, NULL) ||
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
