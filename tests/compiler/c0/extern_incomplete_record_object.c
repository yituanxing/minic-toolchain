struct opaque_record;
extern struct opaque_record opaque_object;

static struct opaque_record *opaque_address(void) {
    return &opaque_object;
}

int main(void) {
    return opaque_address() == &opaque_object ? 0 : 1;
}
