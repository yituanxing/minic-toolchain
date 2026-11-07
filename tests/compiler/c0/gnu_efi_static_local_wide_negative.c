typedef unsigned short efi_char16_t;

#if defined(BAD_WIDE_TO_NARROW)
const char *efi_bad_static(void) {
    static const char vendor[] = L"Vendor";
    return vendor;
}
#elif defined(BAD_NARROW_TO_WIDE)
const efi_char16_t *efi_bad_static(void) {
    static const efi_char16_t vendor[] = "Vendor";
    return vendor;
}
#elif defined(BAD_WIDE_TO_WRONG_WIDTH)
const unsigned int *efi_bad_static(void) {
    static const unsigned int vendor[] = L"Vendor";
    return vendor;
}
#elif defined(BAD_WIDE_BRACED_TO_NARROW)
const char *efi_bad_static(void) {
    static const char vendor[] = { L"Vendor" };
    return vendor;
}
#else
#error select a deliberately invalid string initialization
#endif
