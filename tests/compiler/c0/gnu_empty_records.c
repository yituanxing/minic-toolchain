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

static struct EmptyStruct empty_static_global;

int empty_static_global_address(void) {
    return &empty_static_global != (void *)0;
}

int empty_static_initialized_address(void) {
    static struct EmptyStruct value = {};
    return &value != (void *)0;
}
