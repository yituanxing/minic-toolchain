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
    MinicType integer_type;
    MinicType pointer_type;
    MinicType pointer_to_pointer_type;
    MinicType overflow_type;

    integer_type = minic_type_int();
    if (!minic_type_is_integer(integer_type) ||
        minic_type_is_pointer(integer_type)) {
        return fail("int classification");
    }
    if (!minic_type_pointer_to(integer_type, &pointer_type) ||
        !minic_type_is_pointer(pointer_type) ||
        minic_type_is_integer(pointer_type) ||
        minic_type_equal(integer_type, pointer_type)) {
        return fail("int pointer construction");
    }
    if (!minic_type_pointer_to(pointer_type, &pointer_to_pointer_type) ||
        pointer_to_pointer_type.pointer_depth != 2U ||
        !minic_type_equal(pointer_type, pointer_type) ||
        minic_type_equal(pointer_type, pointer_to_pointer_type)) {
        return fail("nested pointer construction");
    }
    if (minic_type_pointer_to(integer_type, NULL)) {
        return fail("NULL output accepted");
    }
    overflow_type = integer_type;
    overflow_type.pointer_depth = UINT_MAX;
    if (minic_type_pointer_to(overflow_type, &pointer_type)) {
        return fail("pointer-depth overflow accepted");
    }

    (void)printf("PASS frontend/type\n");
    return 0;
}
