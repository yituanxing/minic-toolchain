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

/* One symbol now has both address roles inside the SAME .head.text function.
 * Physical data writes must use displaced RIP-relative addressing while
 * pointer-to-integer arithmetic must retain the original link VMA. The
 * opposite evaluation orders prevent a producer-order workaround. */
volatile unsigned long kernel_head_mixed_global;

__attribute__((section(".head.text"), noinline))
unsigned long kernel_head_mixed_store_then_vma(unsigned long value) {
    kernel_head_mixed_global = value;
    return (unsigned long)&kernel_head_mixed_global;
}

__attribute__((section(".head.text"), noinline))
unsigned long kernel_head_mixed_vma_then_store(unsigned long value) {
    unsigned long vma = (unsigned long)&kernel_head_mixed_global;
    kernel_head_mixed_global = value;
    return vma;
}

/* An unannotated static helper inherits the address role of its only caller.
 * Linux head64.c has exactly this layout: startup_64_setup_env in .head.text
 * calls startup_64_load_idt in ordinary .text, which passes a global's link
 * VMA into fixup_pointer(ptr, physbase). The helper's global address must
 * remain absolute, even though the machine code executes before paging. */
unsigned long kernel_head_helper_target;
static __attribute__((noinline)) unsigned long
kernel_head_fixup_pointer(unsigned long *pointer, unsigned long physbase) {
    return (unsigned long)pointer -
           (unsigned long)&kernel_link_address_probe_anchor + physbase;
}
static __attribute__((noinline)) unsigned long
kernel_head_only_helper(unsigned long physbase) {
    return kernel_head_fixup_pointer(&kernel_head_helper_target, physbase);
}
__attribute__((section(".head.text"), noinline))
unsigned long kernel_head_helper_entry(unsigned long physbase) {
    return kernel_head_only_helper(physbase);
}

/* The same static helper reached from both normal and early code cannot
 * inherit the head-only role. It keeps ordinary .text PC-relative lowering. */
static __attribute__((noinline)) unsigned long kernel_head_shared_helper(void) {
    return (unsigned long)&kernel_head_helper_target;
}
__attribute__((section(".head.text"), noinline))
unsigned long kernel_head_shared_entry(void) {
    return kernel_head_shared_helper();
}
__attribute__((noinline))
unsigned long kernel_head_shared_normal(void) {
    return kernel_head_shared_helper();
}
