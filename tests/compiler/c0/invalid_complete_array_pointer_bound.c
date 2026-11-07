struct Entry { unsigned id; };
static struct Entry entries[2];
struct Holder { struct Entry (*wrong_extent)[3]; };
static struct Holder holder = { .wrong_extent = &entries };
