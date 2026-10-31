enum simple_mode { SIMPLE_A, SIMPLE_B };
static const enum simple_mode read_only_mode = SIMPLE_A;

/* Even a same-representation underlying int does not justify dropping
 * the source object's const qualification. */
unsigned int *minic_bad_enum_qualifier_drop(void) {
    return &read_only_mode;
}
