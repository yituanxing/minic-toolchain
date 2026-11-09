/* Linux x86's full exact privileged zero-operand ISA inventory.
 * Never execute these functions on the host: compile + GNU AS only. */
void linux_priv_cli(void) {
    asm volatile("cli" : : : "memory");
}

void linux_priv_sti(void) {
    asm volatile("sti" : : : "memory");
}

void linux_priv_sti_hlt(void) {
    asm volatile("sti; hlt" : : : "memory");
}

void linux_priv_hlt(void) {
    asm volatile("hlt");
}

void linux_priv_wbinvd(void) {
    asm volatile("wbinvd" : : : "memory");
}

void linux_priv_swapgs(void) {
    asm volatile("swapgs" : : : "memory");
}

void linux_priv_serialize(void) {
    asm volatile(".byte 0xf, 0x1, 0xe8" : : : "memory");
}

void linux_priv_io_delay_80(void) {
    asm volatile("outb %al, $0x80");
}

void linux_priv_io_delay_ed(void) {
    asm volatile("outb %al, $0xed");
}

void linux_priv_int3(void) {
    asm volatile("int3");
}
