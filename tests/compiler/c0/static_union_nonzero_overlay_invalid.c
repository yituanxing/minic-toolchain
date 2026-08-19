union reader_special {
    long l;
    int s;
};
struct task_state {
    long before;
    union reader_special special;
    long after;
};
static struct task_state init_task = {
    .before = 3,
    .after = 5,
    .special.s = 1,
};
int main(void) {
    return init_task.special.s;
}
