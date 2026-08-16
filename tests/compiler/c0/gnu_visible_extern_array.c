__attribute__((visibility("internal"))) extern const char *const names[3];

__attribute__((visibility("internal"))) const char *const names[3] = {
    "one",
    "two",
    "three",
};

const char *read_name(int index) {
    return names[index];
}

struct hidden_item {
    int value;
};

extern struct hidden_item hidden_items[2] __attribute__((visibility("hidden")));
struct hidden_item hidden_items[2] = {{1}, {2}};
