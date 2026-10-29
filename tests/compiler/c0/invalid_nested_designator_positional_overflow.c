struct inner { int first; int second; };
struct outer { struct inner nested; int tail; };

static struct outer invalid = { .nested.first = 1, 2, 3, 4 };
int main(void) { return invalid.tail; }
