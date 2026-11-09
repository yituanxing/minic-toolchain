/* Real Linux EFI handoff: input #0 is a 64-bit entry address, and input #1
 * must be live in RSI when the hardware executes the indirect jump. */
void __attribute__((noreturn)) minic_efi_kernel_transfer(
#if MINIC_EFI_TRANSFER_NEGATIVE == 3
    unsigned int entry,
#else
    unsigned long entry,
#endif
    void *boot_params)
{
#if MINIC_EFI_TRANSFER_NEGATIVE == 1
    __asm__("jmp %0" : : "r" (entry), "S" (boot_params));
#elif MINIC_EFI_TRANSFER_NEGATIVE == 2
    __asm__("jmp *%0" : : "r" (entry), "D" (boot_params));
#else
    __asm__("jmp *%0" : : "r" (entry), "S" (boot_params));
#endif
    __builtin_unreachable();
}
