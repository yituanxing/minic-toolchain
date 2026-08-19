typedef unsigned int u32;

int external_address_target;
int global_address_array[10] = {1, 2};
u32 pointer_sign_ids[1];
static int internal_address_target = 7;
static int function_address_target(int value) {
    return value + 1;
}

typedef int (*FunctionAddressType)(int);

static FunctionAddressType conditional_function_address =
    function_address_target == (FunctionAddressType)((void *)0) ? (FunctionAddressType)((void *)0)
                                                                : function_address_target;

struct FunctionAddressHolder {
    void *address;
};

struct PointerSignAddressHolder {
    int *address;
};

static void *external_address = (void *)&external_address_target;
static char *explicit_object_cast_address = (char *)&external_address_target;
static void *function_address = (void *)&function_address_target;
static struct FunctionAddressHolder aggregate_function_address = {
    (void *)&function_address_target,
};
static int *internal_address = &internal_address_target;
static void *parenthesized_address = (void *)(&internal_address_target);
static char *string_literal_address = "/init";
static int *array_decay_address = global_address_array;
static int *array_zero_address = &global_address_array[0];
static int *array_one_address = &global_address_array[1];
static int *pointer_sign_element_address = &pointer_sign_ids[0];
static struct PointerSignAddressHolder aggregate_pointer_sign_element_address = {
    .address = &pointer_sign_ids[0],
};
static int *object_plus_one_address = &internal_address_target + 1;
static struct FunctionAddressHolder aggregate_array_nine = {
    (void *)&global_address_array[9],
};

struct NestedSubobjectAddressTarget {
    char tag;
    char bytes[5];
};

struct SubobjectAddressTarget {
    int prefix;
    int values[4];
    struct NestedSubobjectAddressTarget nested;
};

static struct SubobjectAddressTarget subobject_address_target;
static int *member_array_decay_address = subobject_address_target.values;
static int *member_array_element_address = &subobject_address_target.values[2];
static char *nested_member_array_decay_address = subobject_address_target.nested.bytes;
static char *nested_member_array_element_address = &subobject_address_target.nested.bytes[3];

static void *signed_minus_one_pointer = (void *)-1;
static void *unsigned_32_pointer = (void *)0xffffffffU;
static void *high_unsigned_pointer = (void *)(0xdead000000000000UL + 0x300UL);
static void *gnu_void_pointer_poison = (void *)0x300UL + 0xdead000000000000UL;
static int *scaled_integer_pointer_add = (int *)0x1000UL + 3;
static int *scaled_integer_pointer_subtract = (int *)0x1000UL - 2;
static int *scaled_integer_pointer_reversed = 3 + (int *)0x1000UL;

int read_static_object_addresses(void) {
    return external_address != (void *)0 && explicit_object_cast_address != (void *)0 &&
           function_address != (void *)0 && aggregate_function_address.address != (void *)0 &&
           internal_address != (void *)0 && parenthesized_address != (void *)0 &&
           string_literal_address != (void *)0 && array_decay_address != (void *)0 &&
           array_zero_address != (void *)0 && array_one_address != (void *)0 &&
           pointer_sign_element_address != (void *)0 &&
           aggregate_pointer_sign_element_address.address != (void *)0 &&
           object_plus_one_address != (void *)0 && aggregate_array_nine.address != (void *)0;
}
