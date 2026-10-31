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
extern volatile unsigned long kernel_head_mixed_global;
extern unsigned long kernel_head_mixed_store_then_vma(unsigned long);
extern unsigned long kernel_head_mixed_vma_then_store(unsigned long);
static int head_direct_consumer_ok;

void kernel_head_direct_data_consumer(struct kernel_head_probe_desc *desc) {
    head_direct_consumer_ok = (desc == &kernel_head_direct_descriptor &&
                               desc->address == 0x19ac321UL);
}

/* The early function and its data are displaced by an identical amount.
 * PC-relative stores must therefore update the cloned physical object, but
 * VMA arithmetic must still produce the ORIGINAL link-time object address.
 * Merely executing the functions at their normal link location would not
 * distinguish the two representations. */
static int check_displaced_mixed_use(
    unsigned long (*function)(unsigned long), unsigned long value) {
    const uintptr_t code = (uintptr_t)(void *)function;
    const uintptr_t data = (uintptr_t)&kernel_head_mixed_global;
    const uintptr_t begin = code < data ? code : data;
    const uintptr_t code_end = code + 256U;
    const uintptr_t data_end = data + sizeof(unsigned long);
    const uintptr_t end = code_end > data_end ? code_end : data_end;
    const size_t span = (size_t)(end - begin);
    unsigned char *copy;
    uintptr_t relocated_address;
    unsigned long (*relocated)(unsigned long);
    unsigned long result, cloned_data = 0U;
    int ok;
    if (span > (1U << 20)) return 0;
    copy = mmap(NULL, span, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (copy == MAP_FAILED) return 0;
    memcpy(copy + (code - begin), (const void *)code, 256U);
    kernel_head_mixed_global = 0x7733UL;
    relocated_address = (uintptr_t)(copy + (code - begin));
    memcpy(&relocated, &relocated_address, sizeof(relocated));
    result = relocated(value);
    memcpy(&cloned_data, copy + (data - begin), sizeof(cloned_data));
    ok = result == (unsigned long)data && cloned_data == value &&
         kernel_head_mixed_global == 0x7733UL;
    if (munmap(copy, span) != 0) return 0;
    return ok;
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
    if (!check_displaced_mixed_use(
            kernel_head_mixed_store_then_vma, 0x195ac3UL)) return 9;
    if (!check_displaced_mixed_use(
            kernel_head_mixed_vma_then_store, 0x8f71UL)) return 10;
    /* The displacement-to-RIP representation is wrong for this function:
     * a relocated copy must still produce the original link-time symbol VMA. */
    memcpy(page, (const void *)(uintptr_t)&kernel_head_link_address_probe, 256);
    memcpy(&relocated, &page, sizeof(relocated));
    if (relocated() != linked) return 6;
    if (munmap(page, 4096) != 0) return 7;
    printf("%lx\n", linked);
    return 0;
}
