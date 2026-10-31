/* Keep canonical P0/P1 init operations, but do not advertise 18/18 P2. */
#ifndef __loongarch__
#error LoongArch64 structural P2 init may only target LoongArch
#endif

#define main canonical_runtime_init_main_not_called
#define run_p2_probe canonical_runtime_p2_runner_not_called
#include "init.c"
#undef run_p2_probe
#undef main

static int run_loongarch64_partial_p2_probe(void) {
    pid_t child;
    int status;

    child = fork();
    if (child == 0) {
        execl("/bin/p2-probe", "p2-probe", (char *)NULL);
        _exit(127);
    }
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        (void)fprintf(stderr, "INIT_P2_PROBE=FAIL errno=%d\n", errno);
        return 1;
    }
    puts("INIT_P2_PROBE=STRUCTURAL_BLOCKED");
    return 0;
}

int main(void) {
    int failures = 0;

    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    puts("MINIC_LINUX_RUNTIME_INIT_BEGIN");
    failures += mount_runtime_fs("proc", "/proc", "proc", "INIT_PROC_MOUNT=PASS");
    failures += mount_runtime_fs("sysfs", "/sys", "sysfs", "INIT_SYSFS_MOUNT=PASS");
    failures += mount_runtime_fs("devtmpfs", "/dev", "devtmpfs", "INIT_DEVTMPFS_MOUNT=PASS");
    failures += show_cmdline();
    failures += tmpfs_roundtrip();
    failures += run_runtime_probe();
    failures += run_loongarch64_partial_p2_probe();
    if (failures == 0) {
        puts("USER_SHELL_OK");
        puts("DONE_COMMANDS");
    } else {
        printf("INIT_FAST_FAIL count=%d\n", failures);
    }
    printf("MINIC_LINUX_RUNTIME_INIT_END fail=%d\n", failures);

    sync();
    (void)sleep(1);
    if (reboot(RB_POWER_OFF) != 0)
        (void)fprintf(stderr, "INIT_POWEROFF_ERROR errno=%d\n", errno);
    for (;;) pause();
}
