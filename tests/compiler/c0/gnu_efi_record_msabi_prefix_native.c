typedef unsigned short efi_char16_t;
typedef unsigned int efi_u32_t;

struct efi_device_path {
    int marker;
};
union efi_device_path_to_text_protocol {
    struct {
        efi_char16_t *(__attribute__((ms_abi)) *convert_device_node_to_text)(
            const struct efi_device_path *, int, int);
        efi_char16_t *(__attribute__((ms_abi)) *convert_device_path_to_text)(
            const struct efi_device_path *, int, int);
    };
    struct {
        efi_u32_t convert_device_node_to_text;
        efi_u32_t convert_device_path_to_text;
    } mixed_mode;
};

extern efi_char16_t *efi_convert(
    union efi_device_path_to_text_protocol *, const struct efi_device_path *);

static efi_char16_t witness[2] = { 0x0045, 0x0046 };

static efi_char16_t *__attribute__((ms_abi)) efi_callback(
    const struct efi_device_path *path, int flag_a, int flag_b) {
    if (path->marker == 77 && flag_a == 0 && flag_b == 1)
        return witness;
    return (efi_char16_t *)0;
}

int main(void) {
    struct efi_device_path path = { 77 };
    union efi_device_path_to_text_protocol table = { 0 };

    table.convert_device_node_to_text = efi_callback;
    return efi_convert(&table, &path) != witness;
}
