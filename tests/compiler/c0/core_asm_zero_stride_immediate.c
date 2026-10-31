/* GNU accepts zero-size records as an extension. An indexed symbolic ASM
 * immediate over that layout must reject instead of dividing by stride 0. */
struct Empty { };
static struct Empty objects[2];
void core_asm_zero_stride_immediate(void) {
    __asm__ volatile(".dword %0" : : "i"(&objects[1]));
}
