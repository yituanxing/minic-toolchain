enum signed_mode { SIGNED_BELOW_ZERO = -1, SIGNED_ZERO = 0 };
static enum signed_mode signed_state;

/* Signed enum is compatible with int, not unsigned int. */
unsigned int *minic_bad_signed_enum_pointer(void) {
    return &signed_state;
}
