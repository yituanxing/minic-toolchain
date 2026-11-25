typedef unsigned long efi_status_t;
typedef void *efi_handle_t;
typedef struct efi_system_table efi_system_table_t;

efi_status_t __attribute__((ms_abi)) efi_pe_entry(efi_handle_t handle,
                                                   efi_system_table_t *sys_table_arg);

efi_status_t call_efi_entry(efi_handle_t handle,
                           efi_system_table_t *sys_table_arg) {
    return efi_pe_entry(handle, sys_table_arg);
}
