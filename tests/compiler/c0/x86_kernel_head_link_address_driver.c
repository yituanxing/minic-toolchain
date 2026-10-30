#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

extern unsigned long kernel_link_address_probe_anchor;
extern unsigned long kernel_head_link_address_probe(void);
extern unsigned long kernel_regular_link_address_probe(void);

int main(void) {
    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    unsigned long (*relocated)(void);
    unsigned long linked = (unsigned long)&kernel_link_address_probe_anchor;
    if (page == MAP_FAILED) return 4;
    if (kernel_head_link_address_probe() != linked ||
        kernel_regular_link_address_probe() != linked) return 5;
    /* The displacement-to-RIP representation is wrong for this function:
     * a relocated copy must still produce the original link-time symbol VMA. */
    memcpy(page, (const void *)(uintptr_t)&kernel_head_link_address_probe, 256);
    memcpy(&relocated, &page, sizeof(relocated));
    if (relocated() != linked) return 6;
    if (munmap(page, 4096) != 0) return 7;
    printf("%lx\n", linked);
    return 0;
}
