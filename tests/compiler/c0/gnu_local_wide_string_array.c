/* Linux arch/x86/kernel/kvm.c initializes a local EFI CHAR16 array from L"...". */
typedef unsigned short minic_efi_char16_t;

static int check_efi_name(void) {
    minic_efi_char16_t name[] = L"SevLiveMigrationEnabled";
    minic_efi_char16_t fixed[4] = L"abc";
    return (sizeof(name) / sizeof(name[0]) == 24U &&
            name[0] == 'S' && name[23] == 0 &&
            name[22] == 'd' && fixed[0] == 'a' && fixed[3] == 0);
}

int main(void) { return check_efi_name() ? 0 : 1; }
