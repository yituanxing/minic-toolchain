static const char setup_name[] = "reset_devices";

static int setup_fn(char *value) {
    return value != 0;
}

struct setup_entry {
    const char *name;
    int (*fn)(char *value);
    int early;
};

static struct setup_entry entry = {
    .name = setup_name,
    .fn = setup_fn,
    .early = 1,
};

int read_static_mixed_symbol_relocations(void) {
    return entry.name[0] + (entry.fn != 0) + entry.early;
}
