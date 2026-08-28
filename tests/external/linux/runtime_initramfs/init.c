#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

static void show_cmdline(void) {
    FILE *file;
    char buffer[1024];

    file = fopen("/proc/cmdline", "r");
    if (file == NULL) {
        (void)fprintf(stderr, "INIT_CMDLINE_ERROR errno=%d\n", errno);
        return;
    }
    (void)fputs("INIT_CMDLINE=", stdout);
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        (void)fputs(buffer, stdout);
    }
    (void)fclose(file);
    (void)fflush(stdout);
}

int main(void) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);

    (void)mkdir("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0UL, NULL) != 0 && errno != EBUSY) {
        (void)fprintf(stderr, "INIT_PROC_MOUNT_ERROR errno=%d\n", errno);
    }

    (void)puts("MINIC_LINUX_RUNTIME_INIT_BEGIN");
    show_cmdline();
    (void)puts("USER_SHELL_OK");
    (void)puts("DONE_COMMANDS");
    sync();
    (void)sleep(1);

    if (reboot(RB_POWER_OFF) != 0) {
        (void)fprintf(stderr, "INIT_POWEROFF_ERROR errno=%d\n", errno);
    }
    for (;;) {
        pause();
    }
}
