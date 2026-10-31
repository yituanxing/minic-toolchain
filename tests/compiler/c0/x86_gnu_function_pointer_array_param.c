/* Linux arch/x86/kvm/vmx/vmx.i:58019:
 * nested_vmx_hardware_setup(int (*exit_handlers[])(struct kvm_vcpu *));
 * An array parameter of function pointers adjusts to pointer-to-function-
 * pointer, without leaving an ownerless incomplete array descriptor. */
typedef int (*exit_handler_t)(int);

extern int invoke_handlers(int (*exit_handlers[])(int), int value);

__attribute__((noinline))
int invoke_handlers(int (*exit_handlers[])(int), int value) {
    return exit_handlers[1](value);
}

__attribute__((noinline))
int invoke_typedef_handlers(exit_handler_t exit_handlers[], int value) {
    return exit_handlers[0](value);
}
