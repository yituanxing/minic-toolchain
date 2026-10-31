#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <linux/dm-ioctl.h>
#include <linux/if_alg.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
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

static void afalg_digest(const char *name, const char *algorithm,
                         const char *expected_hex) {
    struct sockaddr_alg address = {0};
    unsigned char digest[64];
    size_t digest_len = strlen(expected_hex) / 2;
    static const char hex[] = "0123456789abcdef";
    int fd = -1, operation = -1, error = 0;
    size_t i;

    if (digest_len > sizeof(digest) || strlen(expected_hex) != digest_len * 2) {
        result(name, -1, EINVAL);
        return;
    }
    fd = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (fd < 0)
        goto failure;
    address.salg_family = AF_ALG;
    (void)memcpy(address.salg_type, "hash", 5);
    if (strlen(algorithm) >= sizeof(address.salg_name)) {
        error = EINVAL;
        goto out;
    }
    (void)memcpy(address.salg_name, algorithm, strlen(algorithm) + 1);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
        goto failure;
    operation = accept(fd, NULL, NULL);
    if (operation < 0)
        goto failure;
    if (write(operation, "abc", 3) != 3 ||
        read(operation, digest, digest_len) != (ssize_t)digest_len) {
        error = errno != 0 ? errno : EIO;
        goto out;
    }
    for (i = 0; i < digest_len; ++i) {
        if (digest[i] != (unsigned char)(
                ((strchr(hex, expected_hex[i * 2]) - hex) << 4) |
                (strchr(hex, expected_hex[i * 2 + 1]) - hex))) {
            error = EBADMSG;
            goto out;
        }
    }
    result(name, 1, 0);
    goto out_close;

failure:
    error = errno;
out:
    result(name, error == ENOENT || error == EAFNOSUPPORT ||
                 error == EPROTONOSUPPORT || error == EOPNOTSUPP ? 0 : -1,
           error);
out_close:
    if (operation >= 0)
        (void)close(operation);
    if (fd >= 0)
        (void)close(fd);
}

static void netfilter_table_info(void) {
    struct ipt_getinfo v4 = {0};
    struct ip6t_getinfo v6 = {0};
    socklen_t length;
    int fd, error;

    fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (fd < 0) {
        error = errno;
        result("iptables-get-info", -1, error);
    } else {
        (void)memcpy(v4.name, "filter", 7);
        length = sizeof(v4);
        if (getsockopt(fd, IPPROTO_IP, IPT_SO_GET_INFO, &v4, &length) != 0) {
            error = errno;
            result("iptables-get-info", error == ENOPROTOOPT ? 0 : -1, error);
        } else {
            result("iptables-get-info",
                   length == sizeof(v4) && v4.valid_hooks != 0 &&
                   v4.num_entries != 0 ? 1 : -1, EPROTO);
        }
        (void)close(fd);
    }

    fd = socket(AF_INET6, SOCK_RAW, IPPROTO_RAW);
    if (fd < 0) {
        error = errno;
        result("ip6tables-get-info", -1, error);
    } else {
        (void)memcpy(v6.name, "filter", 7);
        length = sizeof(v6);
        if (getsockopt(fd, IPPROTO_IPV6, IP6T_SO_GET_INFO, &v6, &length) != 0) {
            error = errno;
            result("ip6tables-get-info", error == ENOPROTOOPT ? 0 : -1, error);
        } else {
            result("ip6tables-get-info",
                   length == sizeof(v6) && v6.valid_hooks != 0 &&
                   v6.num_entries != 0 ? 1 : -1, EPROTO);
        }
        (void)close(fd);
    }
}

static void ipv6_tcp_loopback(void) {
    struct sockaddr_in6 address = {0};
    socklen_t addr_len = sizeof(address);
    char received[8];
    size_t received_len = 0;
    int server = -1, client = -1, accepted = -1, error = 0;

    server = socket(AF_INET6, SOCK_STREAM, 0);
    if (server < 0)
        goto failure;
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server, 1) != 0 ||
        getsockname(server, (struct sockaddr *)&address, &addr_len) != 0)
        goto failure;
    client = socket(AF_INET6, SOCK_STREAM, 0);
    if (client < 0 ||
        connect(client, (struct sockaddr *)&address, addr_len) != 0)
        goto failure;
    accepted = accept(server, NULL, NULL);
    if (accepted < 0 || write(client, "p2-ipv6", 7) != 7)
        goto failure;
    while (received_len < 7) {
        ssize_t amount = read(accepted, received + received_len,
                              7 - received_len);
        if (amount <= 0) {
            error = amount == 0 ? EIO : errno;
            goto out;
        }
        received_len += (size_t)amount;
    }
    if (memcmp(received, "p2-ipv6", 7) != 0) {
        error = EBADMSG;
        goto out;
    }
    puts("RUNTIME_V2_IPV6_TCP=PASS");
    goto out;

failure:
    error = errno;
out:
    if (accepted >= 0)
        (void)close(accepted);
    if (client >= 0)
        (void)close(client);
    if (server >= 0)
        (void)close(server);
    if (error != 0)
        result("ipv6-tcp-loopback-p2", -1, error);
}

int main(void) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    printf("RUNTIME_V2_P2_BEGIN\n");
    overlay_copy_up();
    btrfs_registration();
    dm_control();
    ext4_block_roundtrip();
    netfilter_table_info();
    ipv6_tcp_loopback();
    afalg_digest("afalg-sha256-abc", "sha256",
                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    afalg_digest("afalg-sha512-abc", "sha512",
                 "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    afalg_digest("afalg-sha3-256-abc", "sha3-256",
                 "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
    afalg_digest("afalg-blake2b-512-abc", "blake2b-512",
                 "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923");
    printf("RUNTIME_V2_P2_END pass=%d fail=%d skip=%d\n", passed, failed, skipped);
    /* This is an eleven-check frontier, not the full 18-check P2 contract.
     * IPv6 TCP emits its own marker, not FUNCTION_PASS. Never certify a skip. */
    return failed != 0 || skipped != 0 || passed != 10
               ? EXIT_FAILURE : EXIT_SUCCESS;
}
