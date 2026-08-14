int external_address_target;
int global_address_array[2] = {1, 2};
static int internal_address_target = 7;

static void *external_address = (void *)&external_address_target;
static int *internal_address = &internal_address_target;
static void *parenthesized_address = (void *)(&internal_address_target);
static char *string_literal_address = "/init";
static int *array_decay_address = global_address_array;
static int *array_zero_address = &global_address_array[0];

int read_static_object_addresses(void) {
    return external_address != (void *)0 && internal_address != (void *)0 &&
           parenthesized_address != (void *)0 && string_literal_address != (void *)0 &&
           array_decay_address != (void *)0 && array_zero_address != (void *)0;
}
