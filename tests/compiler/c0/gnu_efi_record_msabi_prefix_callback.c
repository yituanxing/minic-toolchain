typedef unsigned short efi_char16_t;
typedef unsigned long efi_status_t;
typedef unsigned int efi_u32_t;

struct efi_device_path;
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

efi_char16_t *efi_convert(
    union efi_device_path_to_text_protocol *protocol,
    const struct efi_device_path *path) {
    return protocol->convert_device_node_to_text(path, 0, 1);
}
