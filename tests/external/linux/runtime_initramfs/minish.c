#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <unistd.h>

static int contains_shell_rdinit(const char *command_line) {
    static const char needle[] = "rdinit=/bin/sh";
    const char *cursor = command_line;

    while ((cursor = strstr(cursor, needle)) != NULL) {
        const char before = cursor == command_line ? ' ' : cursor[-1];
        const char after = cursor[sizeof(needle) - 1U];
        if ((before == ' ' || before == '\t') &&
            (after == '\0' || after == '\n' || after == ' ' || after == '\t')) {
            return 1;
        }
        ++cursor;
    }
    return 0;
}

static void cat_cmdline(void) {
    FILE *file;
    char buffer[4096];
    size_t count;
    int read_failed;
    int truncated = 0;
    int close_failed;

    file = fopen("/proc/cmdline", "r");
    if (file == NULL) {
        (void)fprintf(stderr, "minish: proc-cmdline-open-failed errno=%d\n", errno);
        return;
    }

    count = fread(buffer, 1U, sizeof(buffer) - 1U, file);
    read_failed = ferror(file);
    if (!read_failed && count == sizeof(buffer) - 1U) {
        int extra = fgetc(file);
        truncated = extra != EOF;
        read_failed = ferror(file);
    }
    close_failed = fclose(file) != 0;
    if (read_failed || truncated || close_failed) {
        (void)fprintf(stderr, "minish: proc-cmdline-read-failed read=%d truncated=%d close=%d\n",
                      read_failed, truncated, close_failed);
        return;
    }
    buffer[count] = '\0';
    if (!contains_shell_rdinit(buffer)) {
        (void)fprintf(stderr, "minish: proc-cmdline-missing-shell-rdinit\n");
        return;
    }
    if (fwrite(buffer, 1U, count, stdout) != count || fflush(stdout) != 0) {
        (void)fprintf(stderr, "minish: proc-cmdline-output-failed errno=%d\n", errno);
        return;
    }
    (void)puts("MINISH_PROC_CMDLINE_READ=PASS");
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
