#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

static void cat_cmdline(void) {
    FILE *file;
    char buffer[1024];

    file = fopen("/proc/cmdline", "r");
    if (file == NULL) {
        (void)fprintf(stderr, "cat: /proc/cmdline: errno=%d\n", errno);
        return;
    }
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        (void)fputs(buffer, stdout);
    }
    (void)fclose(file);
    (void)fflush(stdout);
}

int main(void) {
    char line[1024];

    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    (void)puts("MINIC_LINUX_RUNTIME_SH_READY");

    while (fgets(line, sizeof(line), stdin) != NULL) {
        size_t length = strlen(line);
        while (length != 0U && (line[length - 1U] == '\n' || line[length - 1U] == '\r')) {
            line[--length] = '\0';
        }

        if (strcmp(line, "mount -t proc proc /proc") == 0) {
            (void)mkdir("/proc", 0555);
            if (mount("proc", "/proc", "proc", 0UL, NULL) != 0 && errno != EBUSY) {
                (void)fprintf(stderr, "mount: proc: errno=%d\n", errno);
            }
        } else if (strncmp(line, "echo ", 5U) == 0) {
            (void)puts(line + 5U);
        } else if (strcmp(line, "cat /proc/cmdline") == 0) {
            cat_cmdline();
        } else if (strcmp(line, "sync") == 0) {
            sync();
        } else if (strcmp(line, "poweroff -f") == 0) {
            sync();
            (void)sleep(1);
            if (reboot(RB_POWER_OFF) != 0) {
                (void)fprintf(stderr, "poweroff: errno=%d\n", errno);
            }
            for (;;) {
                pause();
            }
        } else if (line[0] != '\0') {
            (void)fprintf(stderr, "minish: unsupported command: %s\n", line);
        }
    }
    return 0;
}
