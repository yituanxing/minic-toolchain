extern const char *__start_names[];
extern const char *__stop_names[];
extern int array_unqualified[];
extern const int array_const[];
extern int complete_extent[3];
extern int incomplete_extent[];

int array_symbol_boundaries_differ(void) {
    return &__stop_names != &__start_names;
}

int compatible_array_pointer_boundaries(void) {
    return (&array_unqualified != &array_const) +
           (&complete_extent != &incomplete_extent);
}

int main(void) { return 0; }
