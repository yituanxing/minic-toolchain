/* Positive GCC/MiniC parity: static pointer-array initializers must accept
 * explicit addresses, array-to-pointer decay and null pointer constants. */
int external_pointer_array_target = 17;
int external_pointer_array_second = 29;
int external_pointer_array_data[2] = {41, 43};
int *external_pointer_array_values[4] = {
    &external_pointer_array_target,
    0,
    external_pointer_array_data,
    &external_pointer_array_second
};
/* This unbounded spelling is the only path that calls the older
 * parse_external_integer_array_definition entry point. */
int *external_pointer_array_inferred[] = {
    &external_pointer_array_second, external_pointer_array_data,
    0, &external_pointer_array_target
};
char external_pointer_array_text[] = "ok";

int main(void) {
    return external_pointer_array_values[0] == &external_pointer_array_target &&
                   *external_pointer_array_values[0] == 17 &&
                   external_pointer_array_values[1] == 0 &&
                   external_pointer_array_values[2] == external_pointer_array_data &&
                   external_pointer_array_values[2][1] == 43 &&
                   external_pointer_array_values[3] == &external_pointer_array_second &&
                   sizeof(external_pointer_array_inferred) == 4 * sizeof(int *) &&
                   *external_pointer_array_inferred[0] == 29 &&
                   external_pointer_array_inferred[1][1] == 43 &&
                   external_pointer_array_inferred[2] == 0 &&
                   *external_pointer_array_inferred[3] == 17 &&
                   sizeof(external_pointer_array_text) == 3 &&
                   external_pointer_array_text[0] == 'o'
               ? 0
               : 1;
}
