union StackLike {
    int scalar;
    struct {
        signed char tag;
        unsigned short delta;
    } linked;
};

unsigned read_delta(union StackLike *value) {
    return value->linked.delta;
}

int main(void) {
    union StackLike value;
    value.linked.tag = -1;
    value.linked.delta = 513;
    return sizeof(value) == 4 && read_delta(&value) == 513 ? 0 : 1;
}
