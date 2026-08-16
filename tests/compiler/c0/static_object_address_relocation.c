int external_address_target;
int global_address_array[10] = {1, 2};
static int internal_address_target = 7;
static int function_address_target(int value) {
    return value + 1;
}

struct FunctionAddressHolder {
    void *address;
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
static int *object_plus_one_address = &internal_address_target + 1;
static struct FunctionAddressHolder aggregate_array_nine = {
    (void *)&global_address_array[9],
};

int read_static_object_addresses(void) {
    return external_address != (void *)0 && explicit_object_cast_address != (void *)0 &&
           function_address != (void *)0 && aggregate_function_address.address != (void *)0 &&
           internal_address != (void *)0 && parenthesized_address != (void *)0 &&
           string_literal_address != (void *)0 && array_decay_address != (void *)0 &&
           array_zero_address != (void *)0 && array_one_address != (void *)0 &&
           object_plus_one_address != (void *)0 && aggregate_array_nine.address != (void *)0;
}
