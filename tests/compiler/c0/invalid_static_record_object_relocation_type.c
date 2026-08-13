static int target;
static int setup_fn(char *value) {
    return value != 0;
}
struct setup_entry {
    char *name;
    int (*fn)(char *value);
    int early;
};
static struct setup_entry entry = {&target, setup_fn, 0};
