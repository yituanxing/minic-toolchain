/* RV64-only P2 fixture bridge: load the byte-identical archived Linux
 * 6.6.143 modules before executing the normal runtime-v2 /init.
 *
 * This program is a fixture loader, not a test oracle. A loaded module only
 * enables subsequent guest operations; it is never reported as FUNCTION_PASS.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static int load_module(const char *path) {
    struct stat info;
    unsigned char *bytes;
    const char *params = "";
    size_t offset = 0;
    int fd;
    int error = 0;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno;
    if (fstat(fd, &info) != 0 || info.st_size <= 0 || info.st_size > 16 * 1024 * 1024) {
        error = EINVAL;
        goto done;
    }
    bytes = malloc((size_t)info.st_size);
    if (bytes == NULL) {
        error = ENOMEM;
        goto done;
    }
    while (offset < (size_t)info.st_size) {
        ssize_t n = read(fd, bytes + offset, (size_t)info.st_size - offset);
        if (n <= 0) {
            error = n < 0 ? errno : EIO;
            break;
        }
        offset += (size_t)n;
    }
    /* nf_conntrack defaults to *not* registering packet hooks. The old
     * Python-era P3 oracle exercised real UDP tracking after enabling them;
     * this P2 fixture must activate that real kernel path as well. */
    if (strstr(path, "/nf_conntrack.ko") != NULL)
        params = "enable_hooks=1";
    if (error == 0 && syscall(SYS_init_module, bytes, (size_t)info.st_size,
                              params) != 0)
        error = errno;
    free(bytes);
done:
    (void)close(fd);
    return error;
}

int main(void) {
    char *path = NULL;
    size_t capacity = 0;
    ssize_t length;
    FILE *order;
    unsigned loaded = 0;
    unsigned failed = 0;

    (void)setvbuf(stdout, NULL, _IONBF, 0);
    puts("P2_RV64_MODULE_PRELOAD_BEGIN");
    order = fopen("/p2-module-order.txt", "r");
    if (order == NULL) {
        printf("P2_RV64_MODULE_PRELOAD_ERROR stage=manifest errno=%d\n", errno);
        failed++;
    } else {
        while ((length = getline(&path, &capacity, order)) != -1) {
            int error;
            if (length > 0 && path[length - 1] == '\n')
                path[length - 1] = '\0';
            if (path[0] != '/' || strstr(path, "..") != NULL) {
                printf("P2_RV64_MODULE_PRELOAD_ERROR stage=path\n");
                failed++;
                break;
            }
            printf("P2_RV64_MODULE_LOAD_BEGIN index=%u module=%s\n", loaded + 1,
                   path);
            error = load_module(path);
            if (error != 0) {
                printf("P2_RV64_MODULE_PRELOAD_ERROR stage=init_module module=%s errno=%d\n",
                       path, error);
                failed++;
                break;
            }
            loaded++;
            printf("P2_RV64_MODULE_LOAD_DONE index=%u\n", loaded);
        }
        if (ferror(order)) {
            puts("P2_RV64_MODULE_PRELOAD_ERROR stage=read_manifest");
            failed++;
        }
        free(path);
        (void)fclose(order);
    }
    printf("P2_RV64_MODULE_PRELOAD_END loaded=%u fail=%u\n", loaded, failed);
    if (failed != 0)
        puts("MODULE_FAIL p2-rv64-preload");

    /* Keep running the normal P0/P1/P2 guest to expose precisely which
     * operations succeed. The host gate must reject any preload failure. */
    execl("/bin/p2-main-init", "p2-main-init", (char *)NULL);
    printf("P2_RV64_MODULE_PRELOAD_ERROR stage=exec errno=%d\n", errno);
    return 127;
}
