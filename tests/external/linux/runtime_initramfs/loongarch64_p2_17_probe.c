/* Linux 6.6.143 LoongArch64: the 17 realizable P2 checks, no invented KVM.
 *
 * Include the canonical P2 implementation directly. Every tested operation
 * uses its exact syscall/ioctl/netlink implementation. Only main is replaced
 * because arch/loongarch/kvm does not exist in Linux 6.6.143.
 *
 * This is deliberately NOT an 18/18 P2 PASS and must never print FULL_END.
 */
#ifndef __loongarch__
#error LoongArch64 structural P2 probe may only target LoongArch
#endif

#define main canonical_p2_full18_main_not_called
#include "p2_probe.c"
#undef main

static int verify_kvm_structurally_absent(void) {
    const char *const paths[] = {"/dev/kvm", "/sys/class/misc/kvm"};
    size_t index;

    for (index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        errno = 0;
        if (access(paths[index], F_OK) == 0 || errno != ENOENT) {
            printf("FUNCTION_FAIL kvm-structural-absence path=%s errno=%d\n",
                   paths[index], errno);
            ++failed;
            return -1;
        }
    }
    puts("STRUCTURAL_BLOCKED kvm-device-created reason=linux-6.6.143-loongarch-no-kvm");
    return 0;
}

int main(void) {
    int total_pass;

    (void)setvbuf(stdout, NULL, _IONBF, 0);
    puts("RUNTIME_V2_P2_BEGIN");

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
    virtio_gpu_bound();
    nvme_controller_bound();

    if (verify_kvm_structurally_absent() != 0)
        return EXIT_FAILURE;
    total_pass = passed + ipv6_passed;
    printf("RUNTIME_V2_P2_PARTIAL_END pass=%d fail=%d skip=%d structural_blocked=1\n",
           total_pass, failed, skipped);
    return total_pass == 17 && failed == 0 && skipped == 0 ?
           EXIT_SUCCESS : EXIT_FAILURE;
}
