#include <stdio.h>
#include <string.h>

__attribute__((ms_abi))
long win64_scalar_entry(long, long, long, long, long, long);
__attribute__((ms_abi))
double win64_mixed_entry(long, double, float, long, double, long);
__attribute__((ms_abi)) float win64_float_entry(float);
__attribute__((ms_abi)) long win64_empty_entry(void);

__attribute__((noinline)) long sysv_intrude(long value) {
    /* SysV callees may destroy Win64 callee-preserved RDI/RSI/XMM6. */
    __asm__ volatile (
        "movq $0, %%rdi\n\t"
        "movq $0, %%rsi\n\t"
        "pxor %%xmm6, %%xmm6\n\t"
        : : : "rdi", "rsi", "xmm6");
    return value + 31;
}

static int check_entry_nonvolatiles(void) {
    const unsigned char seed[16] = {
        0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97, 0xa8,
        0xb9, 0xca, 0xdb, 0xec, 0xfd, 0x0e, 0x1f, 0x20};
    unsigned char observed[16] = {0};
    unsigned long preserved_rdi = 0U, preserved_rsi = 0U;
    long result = -1;
    /* GCC's x86 asm sets the actual six Microsoft ABI argument locations:
     * four registers, 32-byte caller home, and slots at rsp+32/rsp+40.
     * The callee calls a SysV function that deliberately clobbers RDI/RSI
     * and XMM6, making missing Win64 saves observable. */
    __asm__ volatile (
        "movq $0x12345, %%rdi\n\t"
        "movq $0x6789a, %%rsi\n\t"
        "movdqu %[input_xmm], %%xmm6\n\t"
        "subq $48, %%rsp\n\t"
        "movq $5, 32(%%rsp)\n\t"
        "movq $6, 40(%%rsp)\n\t"
        "movq $1, %%rcx\n\t"
        "movq $2, %%rdx\n\t"
        "movq $3, %%r8\n\t"
        "movq $4, %%r9\n\t"
        "call win64_scalar_entry\n\t"
        "addq $48, %%rsp\n\t"
        "movq %%rdi, %[output_rdi]\n\t"
        "movq %%rsi, %[output_rsi]\n\t"
        "movdqu %%xmm6, %[output_xmm]\n\t"
        : "=a" (result),
          [output_rdi] "=m" (preserved_rdi),
          [output_rsi] "=m" (preserved_rsi),
          [output_xmm] "=m" (observed)
        : [input_xmm] "m" (seed)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "rdi", "rsi",
          "xmm6", "memory", "cc");
    return result == 192L && preserved_rdi == 0x12345UL &&
           preserved_rsi == 0x6789aUL && memcmp(seed, observed, 16U) == 0;
}

int main(void) {
    if (!check_entry_nonvolatiles() ||
        win64_scalar_entry(1L, 2L, 3L, 4L, 5L, 6L) != 192L ||
        win64_mixed_entry(2L, 1.5, 2.25f, 4L, 3.5, 6L) != 19.25 ||
        win64_float_entry(2.5f) != 3.75f ||
        win64_empty_entry() != 0x1234)
        return 1;
    puts("MS_X64_DEFINITION=PASS incoming=6 mixed_xmm=1 shadow=1 nonvolatile=12");
    return 0;
}
