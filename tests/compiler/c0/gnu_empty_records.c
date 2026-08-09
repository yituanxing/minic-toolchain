struct EmptyStruct {
};

union EmptyUnion {
};

unsigned long empty_struct_size(void) {
    return sizeof(struct EmptyStruct);
}

unsigned long empty_union_size(void) {
    return sizeof(union EmptyUnion);
}

struct EmptyStruct *empty_identity(struct EmptyStruct *value) {
    return value;
}
