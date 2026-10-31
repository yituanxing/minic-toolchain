#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/dm-ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int passed;
static int failed;
static int skipped;

static void result(const char *name, int verdict, int error) {
    if (verdict > 0) {
        printf("FUNCTION_PASS %s\n", name);
        passed++;
    } else if (verdict == 0) {
        printf("FUNCTION_SKIP %s errno=%d\n", name, error);
        skipped++;
    } else {
        printf("FUNCTION_FAIL %s errno=%d\n", name, error);
        failed++;
    }
}

static int write_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    size_t len = strlen(value);
    int ok = fd >= 0 && write(fd, value, len) == (ssize_t)len;

    if (fd >= 0 && close(fd) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

static int file_equals(const char *path, const char *expected) {
    char data[64] = {0};
    int fd = open(path, O_RDONLY);
    ssize_t bytes;

    if (fd < 0)
        return 0;
    bytes = read(fd, data, sizeof(data));
    if (close(fd) != 0 || bytes < 0)
        return 0;
    return bytes == (ssize_t)strlen(expected) &&
           memcmp(data, expected, (size_t)bytes) == 0;
}

static void overlay_copy_up(void) {
    static const char *const directories[] = {
        "/tmp/p2-lower", "/tmp/p2-upper", "/tmp/p2-work", "/tmp/p2-merged"
    };
    static const char options[] =
        "lowerdir=/tmp/p2-lower,upperdir=/tmp/p2-upper,workdir=/tmp/p2-work";
    size_t i;
    int error;

    for (i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        if (mkdir(directories[i], 0755) != 0 && errno != EEXIST) {
            result("overlay-copy-up", -1, errno);
            return;
        }
    }
    if (write_file("/tmp/p2-lower/origin", "before") != 0) {
        result("overlay-copy-up", -1, errno);
        return;
    }
    if (mount("overlay", "/tmp/p2-merged", "overlay", 0, options) != 0) {
        error = errno;
        result("overlay-copy-up", error == ENODEV ? 0 : -1, error);
        return;
    }
    if (!file_equals("/tmp/p2-merged/origin", "before") ||
        write_file("/tmp/p2-merged/origin", "after") != 0 ||
        !file_equals("/tmp/p2-merged/origin", "after") ||
        !file_equals("/tmp/p2-upper/origin", "after") ||
        !file_equals("/tmp/p2-lower/origin", "before")) {
        error = errno;
        (void)umount("/tmp/p2-merged");
        result("overlay-copy-up", -1, error);
        return;
    }
    if (umount("/tmp/p2-merged") != 0) {
        result("overlay-copy-up", -1, errno);
        return;
    }
    result("overlay-copy-up", 1, 0);
}

static void btrfs_registration(void) {
    FILE *file = fopen("/proc/filesystems", "r");
    char line[256];
    int registered = 0;

    if (file == NULL) {
        result("btrfs-filesystem-registered", -1, errno);
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, "\tbtrfs\n") != NULL)
            registered = 1;
    }
    (void)fclose(file);
    result("btrfs-filesystem-registered", registered ? 1 : 0, ENODEV);
}

static void dm_control(void) {
    struct dm_ioctl ctl = {0};
    int fd;
    int error;

    if (mkdir("/dev/mapper", 0755) != 0 && errno != EEXIST) {
        result("dm-control-device", -1, errno);
        return;
    }
    if (mknod("/dev/mapper/control", S_IFCHR | 0600, makedev(10, 236)) != 0 &&
        errno != EEXIST) {
        result("dm-control-device", -1, errno);
        return;
    }
    fd = open("/dev/mapper/control", O_RDWR);
    if (fd < 0) {
        error = errno;
        result("dm-control-device", error == ENODEV || error == ENOENT ? 0 : -1,
               error);
        return;
    }
    ctl.version[0] = DM_VERSION_MAJOR;
    ctl.version[1] = DM_VERSION_MINOR;
    ctl.version[2] = DM_VERSION_PATCHLEVEL;
    ctl.data_size = sizeof(ctl);
    ctl.data_start = sizeof(ctl);
    if (ioctl(fd, DM_VERSION, &ctl) != 0) {
        error = errno;
        (void)close(fd);
        result("dm-control-device", -1, error);
        return;
    }
    if (close(fd) != 0) {
        result("dm-control-device", -1, errno);
        return;
    }
    result("dm-control-device", 1, 0);
}

static void ext4_block_roundtrip(void) {
    const char *const device = "/dev/vda";
    const char *const target = "/tmp/p2-block";
    const char *const payload = "minic-p2-ext4-persist";
    int error;

    /* This profile is meaningful only when QEMU attached an ext4 disk. */
    if (access(device, F_OK) != 0) {
        result("ext4-block-persist", 0, ENODEV);
        return;
    }
    if (mkdir(target, 0755) != 0 && errno != EEXIST) {
        result("ext4-block-persist", -1, errno);
        return;
    }
    if (mount(device, target, "ext4", 0, NULL) != 0) {
        result("ext4-block-persist", -1, errno);
        return;
    }
    if (write_file("/tmp/p2-block/roundtrip.txt", payload) != 0) {
        error = errno;
        (void)umount(target);
        result("ext4-block-persist", -1, error);
        return;
    }
    sync();
    if (umount(target) != 0) {
        result("ext4-block-persist", -1, errno);
        return;
    }
    if (mount(device, target, "ext4", 0, NULL) != 0) {
        result("ext4-block-persist", -1, errno);
        return;
    }
    if (!file_equals("/tmp/p2-block/roundtrip.txt", payload)) {
        error = errno;
        (void)umount(target);
        result("ext4-block-persist", -1, error);
        return;
    }
    if (umount(target) != 0) {
        result("ext4-block-persist", -1, errno);
        return;
    }
    puts("RUNTIME_V2_ROOTFS_RW=PASS");
    result("ext4-block-persist", 1, 0);
}

int main(void) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    printf("RUNTIME_V2_P2_BEGIN\n");
    overlay_copy_up();
    btrfs_registration();
    dm_control();
    ext4_block_roundtrip();
    printf("RUNTIME_V2_P2_END pass=%d fail=%d skip=%d\n", passed, failed, skipped);
    return failed != 0 || passed == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
