/* Exact essential GNU -E fs/exec.c BUILD_BUG_ON condition, stripped of
 * unrelated Linux headers.  Runtime-dependent VM_EXEC is strictly disjoint
 * from VM_STACK_INCOMPLETE_SETUP and cannot reach the diagnostic call. */
typedef unsigned int u32;
struct task_snapshot { u32 personality; };
static volatile int observed_side_effects;

extern void __compiletime_assert_707(void)
    __attribute__((__noreturn__, __error__("VM_STACK_FLAGS & VM_STACK_INCOMPLETE_SETUP")));

static int linux_stack_flags(struct task_snapshot *task) {
    if (!(!((0x00000100 | (0x00000001 | 0x00000002 |
                    ((task->personality & 0x00200000) ? 0x00000004 : 0) |
                    0x00000010 | 0x00000020 | 0x00000040) |
               0x00100000) & (0x00010000 | 0x00008000 | 0)))) {
        __compiletime_assert_707();
    }
    return task->personality & 1U ? 42 : 41;
}

static u32 snapshot_personality(struct task_snapshot *task) {
    ++observed_side_effects;
    return task->personality;
}

static void linux_stack_flags_effectful(struct task_snapshot *task) {
    if (!(!((0x00000100 | (0x00000001 | 0x00000002 |
                    ((snapshot_personality(task) & 0x00200000) ? 0x00000004 : 0) |
                    0x00000010 | 0x00000020 | 0x00000040) |
               0x00100000) & (0x00010000 | 0x00008000 | 0)))) {
        __compiletime_assert_707();
    }
}

int main(int argc, char **argv) {
    struct task_snapshot task;
    (void)argv;
    task.personality = (u32)argc;
    linux_stack_flags_effectful(&task);
    return observed_side_effects == 1 ? linux_stack_flags(&task) : 1;
}
