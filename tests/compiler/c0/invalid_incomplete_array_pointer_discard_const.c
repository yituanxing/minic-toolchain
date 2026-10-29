struct Entry { unsigned id; };
static const struct Entry entries[2];
struct Holder { struct Entry (*discard_const)[]; };
static struct Holder holder = { .discard_const = &entries };
