typedef unsigned long efi_status_t;
typedef void *efi_event_t;
typedef efi_status_t (__attribute__((ms_abi)) *efi_event_notify_t)(efi_event_t, void *);

struct efi_notify_table {
    efi_event_notify_t notify;
};

efi_status_t dispatch_notification(
    struct efi_notify_table *table, efi_event_t event, void *context) {
    return table->notify(event, context);
}
