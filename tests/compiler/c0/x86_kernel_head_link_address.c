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

/* The opposite .head.text case: the kernel runs here before the high-half
 * virtual mapping exists. A descriptor actually written via a C lvalue, and
 * subsequently passed to a helper, needs its displaced RIP-relative address.
 * Returning an address as link-time arithmetic above remains absolute. */
struct kernel_head_probe_desc {
    unsigned short limit;
    unsigned long address;
} __attribute__((packed));

struct kernel_head_probe_desc kernel_head_direct_descriptor;
extern void kernel_head_direct_data_consumer(struct kernel_head_probe_desc *);

__attribute__((section(".head.text"), noinline))
void kernel_head_direct_global_probe(unsigned long value) {
    kernel_head_direct_descriptor.address = value;
    kernel_head_direct_data_consumer(&kernel_head_direct_descriptor);
}
