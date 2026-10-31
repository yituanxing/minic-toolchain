#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

extern unsigned long kernel_link_address_probe_anchor;
extern unsigned long kernel_head_link_address_probe(void);
extern unsigned long kernel_regular_link_address_probe(void);
struct kernel_head_probe_desc {
    unsigned short limit;
    unsigned long address;
} __attribute__((packed));
extern struct kernel_head_probe_desc kernel_head_direct_descriptor;
extern void kernel_head_direct_global_probe(unsigned long value);
static int head_direct_consumer_ok;

void kernel_head_direct_data_consumer(struct kernel_head_probe_desc *desc) {
    head_direct_consumer_ok = (desc == &kernel_head_direct_descriptor &&
                               desc->address == 0x19ac321UL);
}

int main(void) {
    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned long (*relocated)(void);
    unsigned long linked = (unsigned long)&kernel_link_address_probe_anchor;
    if (page == MAP_FAILED) return 4;
    if (kernel_head_link_address_probe() != linked ||
        kernel_regular_link_address_probe() != linked) return 5;
    kernel_head_direct_global_probe(0x19ac321UL);
    if (!head_direct_consumer_ok ||
        kernel_head_direct_descriptor.address != 0x19ac321UL) return 8;
    /* The displacement-to-RIP representation is wrong for this function:
     * a relocated copy must still produce the original link-time symbol VMA. */
    memcpy(page, (const void *)(uintptr_t)&kernel_head_link_address_probe, 256);
    memcpy(&relocated, &page, sizeof(relocated));
    if (relocated() != linked) return 6;
    if (munmap(page, 4096) != 0) return 7;
    printf("%lx\n", linked);
    return 0;
}
