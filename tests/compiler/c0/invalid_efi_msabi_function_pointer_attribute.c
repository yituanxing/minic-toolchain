typedef unsigned long efi_status_t;

#if defined(BAD_TYPEDEF_ARGUMENT)
typedef efi_status_t (__attribute__((ms_abi(1))) *efi_bad_callback_t)(void *);
#elif defined(BAD_RECORD_ARGUMENT)
struct efi_bad_record {
    efi_status_t (__attribute__((ms_abi(1))) *handler)(void *);
};
#elif defined(BAD_PARAMETER_ARGUMENT)
int efi_bad_parameter(
    efi_status_t (__attribute__((ms_abi(1))) *callback)(void *));
#else
#error select an invalid EFI ABI attribute case
#endif
