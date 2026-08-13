int external_address_target;
static int internal_address_target = 7;

static void *external_address = (void *)&external_address_target;
static int *internal_address = &internal_address_target;
static void *parenthesized_address = (void *)(&internal_address_target);

int read_static_object_addresses(void) {
    return external_address != (void *)0 && internal_address != (void *)0 &&
           parenthesized_address != (void *)0;
}
