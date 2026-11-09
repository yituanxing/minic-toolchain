/* Genuine GNU ELF declaration-merging cases: a weak undefined symbol may
 * acquire its binding on a later declaration, before code uses its address.
 * In kernel non-PIC code, address materialization must still use the GOT. */
extern long early_strong_object;
extern long early_strong_object __attribute__((weak));

extern long early_weak_object __attribute__((weak));
extern long early_weak_object;

extern long weak_function(void) __attribute__((weak));
extern long weak_function(void);

long weak_address_sum(void) {
    return (long)&early_strong_object + (long)&early_weak_object +
           (long)weak_function;
}
