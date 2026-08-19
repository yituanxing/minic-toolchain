static unsigned char init_stack[128];

struct thread_state {
    unsigned long sp;
};

struct task_state {
    struct thread_state thread;
};

static struct task_state init_task = {
    .thread = {
        .sp = sizeof(init_stack) + (unsigned long)&init_stack,
    },
};

int main(void) {
    return init_task.thread.sp == 0UL;
}
