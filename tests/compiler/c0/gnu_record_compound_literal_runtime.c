extern void *malloc(unsigned long size);
extern void free(void *pointer);

struct FlexibleRuntime {
    int tag;
    unsigned long count;
    unsigned char payload[];
};

void assign_flexible_prefix(struct FlexibleRuntime *out, int tag, unsigned long count);
void assign_flexible_prefix_positional(struct FlexibleRuntime *out, int tag, unsigned long count);
void clear_flexible_prefix(struct FlexibleRuntime *out);

static int payload_unchanged(const struct FlexibleRuntime *value) {
    return value->payload[0] == 0xA5U && value->payload[1] == 0x5AU && value->payload[2] == 0x3CU &&
           value->payload[3] == 0xC3U;
}

static void seed_payload(struct FlexibleRuntime *value) {
    value->payload[0] = 0xA5U;
    value->payload[1] = 0x5AU;
    value->payload[2] = 0x3CU;
    value->payload[3] = 0xC3U;
}

int main(void) {
    struct FlexibleRuntime *value;

    value = (struct FlexibleRuntime *)malloc(sizeof(*value) + 4U);
    if (value == (void *)0) {
        return 90;
    }

    seed_payload(value);
    value->tag = -1;
    value->count = 99U;
    assign_flexible_prefix(value, 7, 41U);
    if (value->tag != 7 || value->count != 41U || !payload_unchanged(value)) {
        free(value);
        return 1;
    }

    seed_payload(value);
    value->tag = -2;
    value->count = 98U;
    assign_flexible_prefix_positional(value, 8, 42U);
    if (value->tag != 8 || value->count != 42U || !payload_unchanged(value)) {
        free(value);
        return 2;
    }

    seed_payload(value);
    value->tag = 9;
    value->count = 43U;
    clear_flexible_prefix(value);
    if (value->tag != 0 || value->count != 0U || !payload_unchanged(value)) {
        free(value);
        return 3;
    }

    free(value);
    return 0;
}
