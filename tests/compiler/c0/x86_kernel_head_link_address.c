/* x86's pre-page-table .head.text needs the link-time kernel VMA for a
 * C global address; regular code and PIE code must keep their own policy. */
unsigned long kernel_link_address_probe_anchor = 0x195ac3UL;

__attribute__((section(".head.text"), noinline))
unsigned long kernel_head_link_address_probe(void) {
    return (unsigned long)&kernel_link_address_probe_anchor;
}

__attribute__((noinline))
unsigned long kernel_regular_link_address_probe(void) {
    return (unsigned long)&kernel_link_address_probe_anchor;
}
