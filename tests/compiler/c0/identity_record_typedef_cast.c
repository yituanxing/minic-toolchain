typedef struct record_alias_payload {
    long value;
} sockptr_t;

typedef sockptr_t bpfptr_t;

static sockptr_t copy_alias(bpfptr_t source) {
    return (sockptr_t)source;
}

int main(void) {
    bpfptr_t source = {.value = 37};
    return copy_alias(source).value == 37 ? 0 : 1;
}
