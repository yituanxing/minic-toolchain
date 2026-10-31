typedef unsigned short efi_char16_t;

const efi_char16_t *efi_vendor_name(void) {
    static const efi_char16_t ami[] = L"American Megatrends";
    return ami;
}

const efi_char16_t *efi_vendor_concat(void) {
    static const efi_char16_t vendor[] = L"Vendor" L"-" L"B";
    return vendor;
}

#ifndef EFI_SKIP_BRACED
const efi_char16_t *efi_vendor_braced(void) {
    static const efi_char16_t vendor[] = { L"AB" };
    return vendor;
}
#endif

const efi_char16_t *efi_vendor_escapes(void) {
    static const efi_char16_t vendor[] = L"A\x42\103";
    return vendor;
}

const char *efi_ascii_vendor(void) {
    static const char vendor[] = "AMI";
    return vendor;
}
