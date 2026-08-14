struct EmptyStruct {
    ;
};

union EmptyUnion {
    ;
};

struct EmptyMemberRecord {
    void *lock;
    ;
};

unsigned long empty_struct_size(void) {
    return sizeof(struct EmptyStruct);
}

unsigned long empty_union_size(void) {
    return sizeof(union EmptyUnion);
}

unsigned long empty_member_record_size(void) {
    return sizeof(struct EmptyMemberRecord);
}

struct EmptyStruct *empty_identity(struct EmptyStruct *value) {
    return value;
}
