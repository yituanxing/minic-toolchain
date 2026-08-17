static int __attribute__((__section__(".data.static.init"))) section_initialized = 7;
static int __attribute__((section(".data.static.zero"))) section_zero;
static void *__attribute__((__used__))
__attribute__((__section__(".discard.addressable"))) addressable_shape = (void *)0;
static int __attribute__((aligned(16))) aligned_static = 1;
struct static_metadata_record {
    int value;
};
static struct static_metadata_record record_suffix_metadata __attribute__((__used__))
__attribute__((section(".data.static.record"))) __attribute__((aligned(8))) = {3};
static struct static_metadata_record inferred_record_array_suffix_metadata[]
    __attribute__((section(".data.static.record.array"))) __attribute__((aligned(16))) = {{4}, {5}};
static int scalar_suffix_metadata __attribute__((section(".data.static.scalar")))
__attribute__((aligned(8))) = 2;
static const char linux_setup_string[] __attribute__((section(".init.rodata")))
__attribute__((__aligned__(1))) = "reset_devices";

int read_static_global_sections(void) {
    return section_initialized + section_zero + aligned_static + record_suffix_metadata.value +
           inferred_record_array_suffix_metadata[0].value +
           inferred_record_array_suffix_metadata[1].value + scalar_suffix_metadata +
           linux_setup_string[0] + (addressable_shape == (void *)0);
}
