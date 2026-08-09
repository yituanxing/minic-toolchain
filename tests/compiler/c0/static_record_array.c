typedef unsigned char MiniByte;

static const struct MiniPriority {
    MiniByte left;
    MiniByte right;
} priority[] = {
    {10, 10},
    {6, 6},
    {4}
};

int read_priority(int index) {
    return (int)priority[index].left + (int)priority[index].right;
}
