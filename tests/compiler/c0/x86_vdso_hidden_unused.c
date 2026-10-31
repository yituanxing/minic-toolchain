/* A Linux vDSO translation unit may declare more hidden externs than its
 * emitted functions actually use.  An unused hidden declaration must not
 * become an undefined ELF symbol merely because it was parsed. */
extern long vdso_hidden_unused_data __attribute__((visibility("hidden")));
extern long vdso_hidden_required_data __attribute__((visibility("hidden")));
extern long vdso_default_preemptible_data;
__attribute__((visibility("hidden")))
extern long vdso_hidden_required_function(long);

long minic_x86_vdso_hidden_probe(long input) {
    long (*function)(long) = vdso_hidden_required_function;
    return vdso_hidden_required_data + vdso_default_preemptible_data +
           function(input);
}
