static const char setup_name[] = "reset_devices";

static int setup_fn(char *value) {
    return value != 0;
}

struct setup_entry {
    const char *name;
    int (*fn)(char *value);
    int early;
};

static struct setup_entry entry = {setup_name, setup_fn, 0};

int read_static_mixed_symbol_relocations(void) {
    return entry.name[0] + (entry.fn != 0) + entry.early;
}
