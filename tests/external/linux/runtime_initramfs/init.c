#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

static int mount_runtime_fs(const char *source,
                            const char *target,
                            const char *filesystem,
                            const char *pass_marker) {
    (void)mkdir(target, 0755);
    if (mount(source, target, filesystem, 0UL, NULL) != 0 && errno != EBUSY) {
        (void)fprintf(stderr,
                      "INIT_MOUNT_ERROR fs=%s target=%s errno=%d\n",
                      filesystem,
                      target,
                      errno);
        return 1;
    }
    (void)printf("%s\n", pass_marker);
    return 0;
}

static int tmpfs_roundtrip(void) {
    static const char expected[] = "minic-linux-runtime-v2";
    char buffer[64];
    FILE *file;
    size_t count;

    if (mount_runtime_fs("tmpfs", "/tmp", "tmpfs", "INIT_TMPFS_MOUNT=PASS") != 0) {
        return 1;
    }

    file = fopen("/tmp/runtime-v2.txt", "w");
    if (file == NULL) {
        (void)fprintf(stderr, "INIT_TMPFS_WRITE_ERROR errno=%d\n", errno);
        return 1;
    }
    if (fputs(expected, file) < 0 || fclose(file) != 0) {
        (void)fprintf(stderr, "INIT_TMPFS_WRITE_ERROR errno=%d\n", errno);
        return 1;
    }

    memset(buffer, 0, sizeof(buffer));
    file = fopen("/tmp/runtime-v2.txt", "r");
    if (file == NULL) {
        (void)fprintf(stderr, "INIT_TMPFS_READ_ERROR errno=%d\n", errno);
        return 1;
    }
    count = fread(buffer, 1U, sizeof(buffer) - 1U, file);
    if (ferror(file) != 0 || fclose(file) != 0) {
        (void)fprintf(stderr, "INIT_TMPFS_READ_ERROR errno=%d\n", errno);
        return 1;
    }
    buffer[count] = '\0';
    if (strcmp(buffer, expected) != 0) {
        (void)fprintf(stderr,
                      "INIT_TMPFS_MISMATCH expected=%s actual=%s\n",
                      expected,
                      buffer);
        return 1;
    }

    (void)puts("INIT_TMPFS_RW=PASS");
    return 0;
}

static int show_cmdline(void) {
    FILE *file;
    char buffer[1024];

    file = fopen("/proc/cmdline", "r");
    if (file == NULL) {
        (void)fprintf(stderr, "INIT_CMDLINE_ERROR errno=%d\n", errno);
        return 1;
    }
    (void)fputs("INIT_CMDLINE=", stdout);
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        (void)fputs(buffer, stdout);
    }
    (void)fclose(file);
    (void)fflush(stdout);
    (void)puts("INIT_CMDLINE_READ=PASS");
    return 0;
}

int main(void) {
    int failures = 0;

    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);

    (void)puts("MINIC_LINUX_RUNTIME_INIT_BEGIN");

    failures += mount_runtime_fs("proc", "/proc", "proc", "INIT_PROC_MOUNT=PASS");
    failures += mount_runtime_fs("sysfs", "/sys", "sysfs", "INIT_SYSFS_MOUNT=PASS");
    failures += mount_runtime_fs("devtmpfs", "/dev", "devtmpfs", "INIT_DEVTMPFS_MOUNT=PASS");
    failures += show_cmdline();
    failures += tmpfs_roundtrip();

    if (failures == 0) {
        (void)puts("USER_SHELL_OK");
        (void)puts("DONE_COMMANDS");
    } else {
        (void)printf("INIT_FAST_FAIL count=%d\n", failures);
    }
    (void)printf("MINIC_LINUX_RUNTIME_INIT_END fail=%d\n", failures);

    sync();
    (void)sleep(1);

    if (reboot(RB_POWER_OFF) != 0) {
        (void)fprintf(stderr, "INIT_POWEROFF_ERROR errno=%d\n", errno);
    }
    for (;;) {
        pause();
    }
}
