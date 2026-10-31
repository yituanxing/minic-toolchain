/* x86 Linux FPU state save contracts: both architecturally different pointer
 * encodings must preserve a 512-byte, 16-byte-aligned memory output. These
 * instructions can execute on supported user CPUs but tests only assemble. */
struct minic_test_fx_area {
    unsigned char image[512];
} __attribute__((aligned(16)));

static struct minic_test_fx_area minic_test_static_fx_area;

void minic_test_fxsave32(struct minic_test_fx_area *area) {
    __asm__ volatile("fxsave %0" : "+m"(*area));
}

void minic_test_fxsave64(struct minic_test_fx_area *area) {
    __asm__ volatile("fxsaveq %[fx]" : [fx] "=m"(*area));
}

void minic_test_fxsave_static(void) {
    __asm__ volatile("fxsave %0" : "+m"(minic_test_static_fx_area));
}
