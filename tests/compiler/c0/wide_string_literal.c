typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long efi_status_t;
typedef u16 efi_char16_t;

typedef struct {
    u8 bytes[16];
} efi_guid_t;

typedef efi_status_t efi_get_variable_t(
    efi_char16_t *name, efi_guid_t *vendor, u32 *attr, unsigned long *data_size, void *data);

_Static_assert(sizeof(L"A") == 4, "-fshort-wchar wide literal size");

static efi_status_t wide_efi_call(efi_get_variable_t *get_var) {
    unsigned long size = 1;
    u8 secboot = 0;

    return get_var(L"SecureBoot", (efi_guid_t *)0, (u32 *)0, &size, &secboot);
}

int Lvalue_boundary(int Lvalue) {
    return Lvalue + (int)wide_efi_call;
}
