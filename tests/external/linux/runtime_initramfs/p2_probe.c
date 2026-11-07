#define _GNU_SOURCE

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/dm-ioctl.h>
#include <linux/if_alg.h>
#include <linux/if_link.h>
#include <linux/kvm.h>
#include <linux/netlink.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int passed;
static int failed;
static int skipped;
static int ipv6_passed;

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
    ipv6_passed = 1;
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

static int capability_absent(int error) {
    return error == ENODEV || error == ENOENT || error == ENOPROTOOPT ||
           error == EAFNOSUPPORT || error == EPROTONOSUPPORT ||
           error == EOPNOTSUPP;
}

static void kvm_api_device(void) {
    int fd, api, error;

    /* /dev/kvm is normally created by devtmpfs. The fixed misc minor is
     * also used when the minimal initramfs has no udev. An mknod alone is
     * NEVER sufficient: open and the kernel KVM ioctl must both succeed. */
    fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT) {
        if (mknod("/dev/kvm", S_IFCHR | 0600, makedev(10, 232)) == 0 ||
            errno == EEXIST)
            fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    }
    if (fd < 0) {
        error = errno;
        result("kvm-device-created", capability_absent(error) ? 0 : -1, error);
        return;
    }
    api = ioctl(fd, KVM_GET_API_VERSION, 0);
    error = api < 0 ? errno : EPROTO;
    (void)close(fd);
    result("kvm-device-created", api == KVM_API_VERSION ? 1 : -1, error);
}

static int associated_device_child(const char *directory_path,
                                   const char *child_prefix) {
    DIR *directory = opendir(directory_path);
    struct dirent *entry;
    size_t prefix = strlen(child_prefix);
    int found = 0;

    if (directory == NULL)
        return 0;
    while ((entry = readdir(directory)) != NULL) {
        char fullname[512];
        struct stat info;
        int length;

        if (strncmp(entry->d_name, child_prefix, prefix) != 0 ||
            entry->d_name[prefix] < '0' || entry->d_name[prefix] > '9')
            continue;
        length = snprintf(fullname, sizeof(fullname), "%s/%s", directory_path,
                          entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(fullname))
            continue;
        if (stat(fullname, &info) == 0 && S_ISDIR(info.st_mode)) {
            found = 1;
            break;
        }
    }
    (void)closedir(directory);
    return found;
}

static int bound_driver_and_child(const char *driver_path,
                                  const char *device_base,
                                  const char *device_prefix,
                                  const char *subdirectory,
                                  const char *child_prefix,
                                  int use_parent_device) {
    DIR *directory = opendir(driver_path);
    struct dirent *entry;
    size_t prefix_length = strlen(device_prefix);
    int found = 0;

    if (directory == NULL)
        return -1;
    while ((entry = readdir(directory)) != NULL) {
        char bound_path[512], nested_path[512], real_device[512];
        struct stat info;
        int length;

        if (prefix_length) {
            if (strncmp(entry->d_name, device_prefix, prefix_length) != 0)
                continue;
        } else if (strchr(entry->d_name, ':') == NULL ||
                   strchr(entry->d_name, '.') == NULL) {
            /* PCI BDF (any domain), never "module" or "subsystem". */
            continue;
        }
        length = snprintf(bound_path, sizeof(bound_path), "%s/%s",
                          driver_path, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(bound_path) ||
            lstat(bound_path, &info) != 0 || !S_ISLNK(info.st_mode))
            continue;
        length = snprintf(nested_path, sizeof(nested_path), "%s/%s",
                          device_base, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(nested_path))
            continue;
        if (use_parent_device) {
            char *last, *resolved;

            /* virtio-gpu Linux 6.6 creates the DRM node with
             * drm_dev_alloc(..., vdev->dev.parent), i.e. the parent PCI
             * device, not the virtioN child. Resolve that exact parent. */
            resolved = realpath(nested_path, NULL);
            if (resolved == NULL)
                continue;
            last = strrchr(resolved, '/');
            if (last == NULL) {
                free(resolved);
                continue;
            }
            *last = '\0';
            length = snprintf(nested_path, sizeof(nested_path), "%s",
                              resolved);
            free(resolved);
            if (length < 0 || (size_t)length >= sizeof(nested_path))
                continue;
        }
        length = snprintf(real_device, sizeof(real_device), "%s/%s",
                          nested_path, subdirectory);
        if (length < 0 || (size_t)length >= sizeof(real_device))
            continue;
        if (associated_device_child(real_device, child_prefix)) {
            found = 1;
            break;
        }
    }
    (void)closedir(directory);
    return found;
}

static void virtio_gpu_bound(void) {
    int bound = bound_driver_and_child(
        "/sys/bus/virtio/drivers/virtio_gpu", "/sys/bus/virtio/devices",
        "virtio", "drm", "card", 1);

    if (bound < 0) {
        int error = errno;
        result("virtio-gpu-driver-registered", capability_absent(error) ? 0 : -1,
               error);
    } else {
        /* A bound virtio device's *own* drm/cardN child proves the card is
         * associated with this driver, not an unrelated card0. */
        result("virtio-gpu-driver-registered", bound ? 1 : 0, ENODEV);
    }
}

static void nvme_controller_bound(void) {
    int bound = bound_driver_and_child(
        "/sys/bus/pci/drivers/nvme", "/sys/bus/pci/devices",
        "", "nvme", "nvme", 0);
    if (bound < 0) {
        int error = errno;
        result("nvme-pci-driver-registered",
               capability_absent(error) ? 0 : -1, error);
    } else {
        /* The NVMe class child must belong to the *same bound PCI BDF*. */
        result("nvme-pci-driver-registered", bound ? 1 : 0, ENODEV);
    }
}

/* Minimal route-netlink transactions: exercise the kernel rtnetlink path
 * directly. No external iproute2 binary, printed command or simulation is
 * accepted as evidence of successful bridge/veth operations. */
struct p2_link_request {
    struct nlmsghdr header;
    struct ifinfomsg info;
    unsigned char attrs[1024];
};

static void link_begin(struct p2_link_request *request, int operation,
                       unsigned int flags, int ifindex) {
    (void)memset(request, 0, sizeof(*request));
    request->header.nlmsg_len = NLMSG_LENGTH(sizeof(request->info));
    request->header.nlmsg_type = (unsigned short)operation;
    request->header.nlmsg_flags = (unsigned short)(NLM_F_REQUEST | NLM_F_ACK | flags);
    request->header.nlmsg_seq = 1;
    request->info.ifi_family = AF_UNSPEC;
    request->info.ifi_index = ifindex;
}

static struct rtattr *link_attr(struct p2_link_request *request,
                                int attr_type, const void *value, size_t length) {
    size_t offset = NLMSG_ALIGN(request->header.nlmsg_len);
    size_t data_len = RTA_LENGTH(length);
    size_t next = offset + RTA_ALIGN(data_len);
    struct rtattr *attr;

    if (length > 65531 || next > sizeof(*request)) {
        errno = EMSGSIZE;
        return NULL;
    }
    attr = (struct rtattr *)((char *)request + offset);
    attr->rta_type = (unsigned short)attr_type;
    attr->rta_len = (unsigned short)data_len;
    if (length && value)
        (void)memcpy(RTA_DATA(attr), value, length);
    request->header.nlmsg_len = (unsigned int)next;
    return attr;
}

static void link_nest_end(struct p2_link_request *request, struct rtattr *nest) {
    nest->rta_len = (unsigned short)((char *)request +
                                     request->header.nlmsg_len - (char *)nest);
}

static int link_transaction(struct p2_link_request *request) {
    struct sockaddr_nl destination = {0};
    struct timeval receive_timeout = {.tv_sec = 2};
    char reply[4096];
    int fd, failure = EPROTO;

    fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0)
        return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
                   sizeof(receive_timeout)) != 0) {
        failure = errno;
        goto done;
    }
    destination.nl_family = AF_NETLINK;
    if (sendto(fd, request, request->header.nlmsg_len, 0,
               (struct sockaddr *)&destination, sizeof(destination)) !=
        (ssize_t)request->header.nlmsg_len) {
        failure = errno != 0 ? errno : EIO;
        goto done;
    }
    for (;;) {
        ssize_t received = recv(fd, reply, sizeof(reply), 0);
        struct nlmsghdr *header;
        int remaining;

        if (received < 0) {
            failure = errno;
            break;
        }
        if (received == 0)
            break;
        remaining = (int)received;
        for (header = (struct nlmsghdr *)reply;
             NLMSG_OK(header, remaining); header = NLMSG_NEXT(header, remaining)) {
            struct nlmsgerr *status;

            if (header->nlmsg_seq != request->header.nlmsg_seq ||
                header->nlmsg_type != NLMSG_ERROR)
                continue;
            if (header->nlmsg_len < NLMSG_LENGTH(sizeof(*status)))
                goto done;
            status = (struct nlmsgerr *)NLMSG_DATA(header);
            if (status->error == 0) {
                (void)close(fd);
                return 0;
            }
            failure = -status->error;
            goto done;
        }
    }
done:
    (void)close(fd);
    errno = failure;
    return -1;
}

static int link_create_bridge(void) {
    struct p2_link_request request;
    struct rtattr *linkinfo;

    link_begin(&request, RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL, 0);
    if (!link_attr(&request, IFLA_IFNAME, "p2br0", sizeof("p2br0")))
        return -1;
    linkinfo = link_attr(&request, IFLA_LINKINFO, NULL, 0);
    if (!linkinfo || !link_attr(&request, IFLA_INFO_KIND,
                                "bridge", sizeof("bridge")))
        return -1;
    link_nest_end(&request, linkinfo);
    return link_transaction(&request);
}

static int link_create_veth_pair(void) {
    struct p2_link_request request;
    struct rtattr *linkinfo, *data, *peer;
    struct ifinfomsg peer_info = {0};

    peer_info.ifi_family = AF_UNSPEC;
    link_begin(&request, RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL, 0);
    if (!link_attr(&request, IFLA_IFNAME, "p2v0", sizeof("p2v0")))
        return -1;
    linkinfo = link_attr(&request, IFLA_LINKINFO, NULL, 0);
    if (!linkinfo || !link_attr(&request, IFLA_INFO_KIND,
                                "veth", sizeof("veth")))
        return -1;
    data = link_attr(&request, IFLA_INFO_DATA, NULL, 0);
    peer = data ? link_attr(&request, VETH_INFO_PEER,
                            &peer_info, sizeof(peer_info)) : NULL;
    if (!peer || !link_attr(&request, IFLA_IFNAME,
                            "p2v1", sizeof("p2v1")))
        return -1;
    link_nest_end(&request, peer);
    link_nest_end(&request, data);
    link_nest_end(&request, linkinfo);
    return link_transaction(&request);
}

static int link_set_up(unsigned int index) {
    struct p2_link_request request;

    link_begin(&request, RTM_NEWLINK, 0, (int)index);
    request.info.ifi_flags = IFF_UP;
    request.info.ifi_change = IFF_UP;
    return link_transaction(&request);
}

static int link_set_master(unsigned int child, unsigned int master) {
    struct p2_link_request request;

    link_begin(&request, RTM_NEWLINK, 0, (int)child);
    if (!link_attr(&request, IFLA_MASTER, &master, sizeof(master)))
        return -1;
    return link_transaction(&request);
}

static int link_delete(unsigned int index) {
    struct p2_link_request request;

    link_begin(&request, RTM_DELLINK, 0, (int)index);
    return link_transaction(&request);
}

static int link_is_up(const char *name) {
    struct ifreq data = {0};
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    int success = 0;

    if (fd < 0)
        return 0;
    (void)snprintf(data.ifr_name, sizeof(data.ifr_name), "%s", name);
    if (ioctl(fd, SIOCGIFFLAGS, &data) == 0 && (data.ifr_flags & IFF_UP))
        success = 1;
    (void)close(fd);
    return success;
}

static void br_netfilter_toggle(int bridge_ready) {
    const char *const path = "/proc/sys/net/bridge/bridge-nf-call-ip6tables";
    char previous, desired, readback;
    int fd, error = 0;

    if (!bridge_ready) {
        result("br-netfilter-ipv6-sysctl-toggle", 0, ENODEV);
        return;
    }
    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        error = errno;
        result("br-netfilter-ipv6-sysctl-toggle",
               capability_absent(error) ? 0 : -1, error);
        return;
    }
    if (read(fd, &previous, 1) != 1 ||
        (previous != '0' && previous != '1')) {
        error = EPROTO;
        goto done;
    }
    desired = previous == '0' ? '1' : '0';
    if (lseek(fd, 0, SEEK_SET) < 0 || write(fd, &desired, 1) != 1 ||
        lseek(fd, 0, SEEK_SET) < 0 || read(fd, &readback, 1) != 1 ||
        readback != desired) {
        error = errno != 0 ? errno : EPROTO;
    }
    if (lseek(fd, 0, SEEK_SET) < 0 || write(fd, &previous, 1) != 1 ||
        lseek(fd, 0, SEEK_SET) < 0 || read(fd, &readback, 1) != 1 ||
        readback != previous)
        error = errno != 0 ? errno : EIO;
done:
    (void)close(fd);
    result("br-netfilter-ipv6-sysctl-toggle", error == 0 ? 1 : -1, error);
}

static void bridge_veth_operations(void) {
    unsigned int bridge_index = 0, left = 0, right = 0;
    int bridge_error = 0, veth_error = 0;
    int bridge_ready = 0, veth_ready = 0;
    char master[256] = {0};
    ssize_t length;

    if (link_create_bridge() == 0)
        bridge_index = if_nametoindex("p2br0");
    else
        bridge_error = errno;
    if (link_create_veth_pair() == 0) {
        left = if_nametoindex("p2v0");
        right = if_nametoindex("p2v1");
    } else {
        veth_error = errno;
    }

    if (left && right && left != right &&
        link_set_up(left) == 0 && link_set_up(right) == 0 &&
        link_is_up("p2v0") && link_is_up("p2v1")) {
        veth_ready = 1;
    } else if (!veth_error) {
        veth_error = errno != 0 ? errno : EPROTO;
    }
    if (bridge_index && veth_ready &&
        link_set_master(left, bridge_index) == 0 &&
        link_set_up(bridge_index) == 0 && link_is_up("p2br0")) {
        length = readlink("/sys/class/net/p2v0/master", master,
                          sizeof(master) - 1);
        if (length > 0 && strstr(master, "/p2br0") != NULL)
            bridge_ready = 1;
    }
    if (!bridge_ready && !bridge_error)
        bridge_error = errno != 0 ? errno : EPROTO;

    br_netfilter_toggle(bridge_ready);

    /* Perform cleanup before advertising either device result as PASS. */
    if (left && link_delete(left) != 0) {
        veth_ready = 0;
        veth_error = errno;
    }
    if (bridge_index && link_delete(bridge_index) != 0) {
        bridge_ready = 0;
        bridge_error = errno;
    }
    if (if_nametoindex("p2v0") || if_nametoindex("p2v1")) {
        veth_ready = 0;
        veth_error = EBUSY;
    }
    if (if_nametoindex("p2br0")) {
        bridge_ready = 0;
        bridge_error = EBUSY;
    }
    result("veth-pair-created", veth_ready ? 1 :
           capability_absent(veth_error) ? 0 : -1, veth_error);
    result("bridge-links-up", bridge_ready ? 1 :
           capability_absent(bridge_error) ? 0 : -1, bridge_error);
}

static int conntrack_count(unsigned long *count) {
    const char *const path = "/proc/sys/net/netfilter/nf_conntrack_count";
    char value[64];
    char *end;
    ssize_t length;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0)
        return -1;
    length = read(fd, value, sizeof(value) - 1);
    if (close(fd) != 0 || length < 1) {
        errno = EIO;
        return -1;
    }
    value[length] = '\0';
    errno = 0;
    *count = strtoul(value, &end, 10);
    if (errno != 0 || end == value || (*end != '\n' && *end != '\0')) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int conntrack_udp_in_isolated_netns(void) {
    struct sockaddr_in address = {0};
    struct timeval timeout_value = {.tv_sec = 1};
    socklen_t length = sizeof(address);
    unsigned long before = 0, after = 0;
    int receiver = -1, sender = -1, error = 0;
    char incoming[16] = {0};
    int attempt;
    unsigned int loopback;

    /* Own a fresh net namespace: a global count increase could otherwise
     * originate from unrelated kernel/guest traffic. In this namespace the
     * only generated flow is the UDP five-tuple below, so count 0 -> 1
     * proves this precise flow entered the kernel conntrack table. */
    if (unshare(CLONE_NEWNET) != 0)
        return errno;
    loopback = if_nametoindex("lo");
    if (loopback == 0 || link_set_up(loopback) != 0 ||
        !link_is_up("lo"))
        return errno != 0 ? errno : ENODEV;
    if (conntrack_count(&before) != 0)
        return errno;
    receiver = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (receiver < 0)
        goto failure;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (setsockopt(receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout_value,
                   sizeof(timeout_value)) != 0 ||
        bind(receiver, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(receiver, (struct sockaddr *)&address, &length) != 0)
        goto failure;
    sender = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sender < 0 ||
        sendto(sender, "p2-ct", 5, 0, (struct sockaddr *)&address,
               sizeof(address)) != 5 ||
        recv(receiver, incoming, sizeof(incoming), 0) != 5 ||
        memcmp(incoming, "p2-ct", 5) != 0)
        goto failure;

    for (attempt = 0; attempt < 10; ++attempt) {
        if (conntrack_count(&after) != 0)
            goto failure;
        if (after == 1)
            break;
        (void)usleep(20000);
    }
    printf("P2_CONNTRACK_TRACE before=%lu after=%lu attempts=%d\n",
           before, after, attempt + 1);
    if (before != 0 || after != 1) {
        error = EPROTO;
        goto cleanup;
    }
    goto cleanup;

failure:
    error = errno != 0 ? errno : EPROTO;
cleanup:
    if (sender >= 0)
        (void)close(sender);
    if (receiver >= 0)
        (void)close(receiver);
    return error;
}

static void conntrack_udp_entry(void) {
    int pipefd[2], status, error = EPROTO;
    pid_t child;

    /* Keep namespace isolation strictly inside the child. Bridge/veth and
     * br-netfilter operations that follow must use PID1's original netns,
     * whose mounted /proc and /sys views expose their actual devices. */
    if (pipe2(pipefd, O_CLOEXEC) != 0) {
        result("conntrack-entry-created", -1, errno);
        return;
    }
    child = fork();
    if (child < 0) {
        error = errno;
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        result("conntrack-entry-created", -1, error);
        return;
    }
    if (child == 0) {
        (void)close(pipefd[0]);
        error = conntrack_udp_in_isolated_netns();
        if (write(pipefd[1], &error, sizeof(error)) != (ssize_t)sizeof(error))
            _exit(2);
        (void)close(pipefd[1]);
        _exit(error == 0 ? 0 : 1);
    }
    (void)close(pipefd[1]);
    if (read(pipefd[0], &error, sizeof(error)) != (ssize_t)sizeof(error))
        error = EPROTO;
    (void)close(pipefd[0]);
    if (waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0 || error != 0) {
        result("conntrack-entry-created", capability_absent(error) ? 0 : -1,
               error);
        return;
    }
    result("conntrack-entry-created", 1, 0);
}

int main(void) {
    int total_pass;

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
    conntrack_udp_entry();
    bridge_veth_operations();
    kvm_api_device();
    virtio_gpu_bound();
    nvme_controller_bound();

    /* 17 independently exercised FUNCTION_* checks and one independently
     * exercised RUNTIME_V2_IPV6_TCP. Only this exact 18/18 success can
     * certify the strict full-P2 endpoint; the frontier END alone never can. */
    total_pass = passed + ipv6_passed;
    printf("RUNTIME_V2_P2_END pass=%d fail=%d skip=%d\n",
           total_pass, failed, skipped);
    if (total_pass == 18 && failed == 0 && skipped == 0) {
        puts("RUNTIME_V2_P2_FULL_END pass=18 fail=0 skip=0");
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
