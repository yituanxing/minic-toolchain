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

struct EmptyHolder {
    struct EmptyStruct cookie;
};

static struct EmptyStruct *empty_source(struct EmptyStruct *value) {
    return value;
}

static struct EmptyStruct *empty_target(struct EmptyStruct *value) {
    return value;
}

void empty_record_statement_copy(struct EmptyHolder *holder) {
    holder->cookie = ({
        struct EmptyStruct cookie = {};
        cookie;
    });
}

void empty_record_lvalue_copy(struct EmptyStruct *target, struct EmptyStruct *source) {
    *empty_target(target) = *empty_source(source);
}
