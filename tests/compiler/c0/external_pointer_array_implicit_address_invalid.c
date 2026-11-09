/* Negative GCC/MiniC parity: integer global names must not be silently
 * rewritten to their addresses in external pointer-array initializers. */
int external_pointer_array_scalar;
int *external_pointer_array_bad[1] = {external_pointer_array_scalar};
