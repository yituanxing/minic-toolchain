/* Calling-convention identity must survive typedef, field and parameter parsing. */
typedef long (__attribute__((ms_abi)) *efi_callback_t)(long);

struct efi_callback_holder {
    long (__attribute__((ms_abi)) *callback)(long);
};

static long invoke_efi_parameter(
    long (__attribute__((ms_abi)) *parameter_callback)(long), long input) {
    return parameter_callback(input);
}

static long __attribute__((ms_abi)) ms_increment(long input) {
    return input + 1L;
}

int main(void) {
    efi_callback_t from_typedef = ms_increment;
    struct efi_callback_holder from_record = { ms_increment };
    return invoke_efi_parameter(from_record.callback, 40L) == 41L &&
                   invoke_efi_parameter(from_typedef, 41L) == 42L
               ? 0 : 1;
}
