struct item { int value; };
extern struct item __start_items[];
extern struct item __stop_items[];

int has_items(void) {
    return &__stop_items > &__start_items;
}

extern const void __start_blob;
extern const void __stop_blob;
int has_blob(void) {
    return &__stop_blob > &__start_blob;
}
