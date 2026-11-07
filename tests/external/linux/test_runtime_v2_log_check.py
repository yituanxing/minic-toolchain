#!/usr/bin/env python3
"""Synthetic negative/positive tests of the runtime-v2 *log oracle*.

These fixtures test failure semantics only. They are not Linux/QEMU evidence.
"""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


CHECKER = Path(__file__).with_name("runtime_v2_log_check.py")
P0_P1 = [
    "Linux version 6.6.143",
    "Run /init as init process",
    "INIT_PROC_MOUNT=PASS",
    "INIT_SYSFS_MOUNT=PASS",
    "INIT_DEVTMPFS_MOUNT=PASS",
    "INIT_CMDLINE_READ=PASS",
    "INIT_TMPFS_RW=PASS",
    "MINIC_LINUX_RUNTIME_INIT_END fail=0",
    "RUNTIME_V2_SYSCALL_PASS eventfd-rw",
    "RUNTIME_V2_SYSCALL_PASS epoll-eventfd",
    "RUNTIME_V2_SYSCALL_PASS timerfd-read",
    "RUNTIME_V2_SYSCALL_PASS signalfd-read",
    "RUNTIME_V2_SYSCALL_PASS inotify-create",
    "RUNTIME_V2_SYSCALL_PASS socketpair-rw",
    "RUNTIME_V2_SYSCALL_PASS ipv6-tcp-loopback",
    "RUNTIME_V2_SYSCALL_PASS memfd-rw",
    "RUNTIME_V2_SYSCALL_PASS pidfd-open",
    "RUNTIME_V2_SYSCALL_PASS futex-wake",
    "RUNTIME_V2_SYSCALL_PASS io-uring-setup",
    "RUNTIME_V2_SYSCALL_PASS mmap-mprotect",
    "INIT_RUNTIME_PROBE=PASS",
]
P2 = [
    "RUNTIME_V2_ROOTFS_RW=PASS",
    "FUNCTION_PASS overlay-copy-up",
    "FUNCTION_PASS iptables-get-info hooks=0x1f",
    "FUNCTION_PASS ip6tables-get-info hooks=0x1f",
    "FUNCTION_PASS conntrack-entry-created",
    "FUNCTION_PASS bridge-links-up",
    "FUNCTION_PASS br-netfilter-ipv6-sysctl-toggle",
    "FUNCTION_PASS veth-pair-created",
    "RUNTIME_V2_IPV6_TCP=PASS",
    "FUNCTION_PASS afalg-sha256-abc",
    "FUNCTION_PASS afalg-sha512-abc",
    "FUNCTION_PASS afalg-sha3-256-abc",
    "FUNCTION_PASS afalg-blake2b-512-abc",
    "FUNCTION_PASS btrfs-filesystem-registered",
    "FUNCTION_PASS kvm-device-created",
    "FUNCTION_PASS dm-control-device",
    "FUNCTION_PASS virtio-gpu-driver-registered",
    "FUNCTION_PASS nvme-pci-driver-registered",
]
FULL_END = "RUNTIME_V2_P2_FULL_END pass=18 fail=0 skip=0"


class P2StrictOracleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "qemu.log"
        self.lines = P0_P1 + P2 + [FULL_END, "reboot: Power down"]

    def run_checker(self, *, lines=None, rc="0", timeout=False, profile="p2"):
        self.path.write_text("\n".join(self.lines if lines is None else lines) + "\n")
        command = [sys.executable, str(CHECKER), "--profile", profile,
                   "--log", str(self.path)]
        if rc is not None:
            command += ["--qemu-rc", str(rc)]
        if timeout:
            command.append("--qemu-timeout")
        return subprocess.run(command, text=True, capture_output=True, check=False)

    def assert_rejected(self, **kwargs):
        actual = self.run_checker(**kwargs)
        self.assertNotEqual(actual.returncode, 0, actual.stdout)
        return actual.stderr

    def test_synthetic_eighteen_distinct_checks(self):
        actual = self.run_checker()
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertIn("checks=40 fatal=0", actual.stdout)

    def test_kernel_timestamped_powerdown_is_accepted(self):
        changed = ["[    1.787892] reboot: Power down"
                   if line == "reboot: Power down" else line for line in self.lines]
        actual = self.run_checker(lines=changed)
        self.assertEqual(actual.returncode, 0, actual.stderr)

    def test_missing_qemu_rc_is_not_pass(self):
        self.assertIn("missing-qemu-rc", self.assert_rejected(rc=None))

    def test_nonzero_qemu_rc_is_not_pass(self):
        self.assertIn("qemu-rc=124", self.assert_rejected(rc=124))

    def test_timeout_is_not_pass_even_with_zero_rc(self):
        self.assertIn("qemu-timeout", self.assert_rejected(timeout=True))

    def test_missing_shutdown_is_not_pass(self):
        self.assertIn("powerdown", self.assert_rejected(
            lines=[line for line in self.lines if line != "reboot: Power down"],
        ))

    def test_missing_p2_feature_is_not_pass(self):
        self.assertIn("p2-bridge-occurrences=0", self.assert_rejected(
            lines=[line for line in self.lines if line != "FUNCTION_PASS bridge-links-up"],
        ))

    def test_duplicate_p2_feature_is_not_pass(self):
        self.assertIn("p2-bridge-occurrences=2", self.assert_rejected(
            lines=self.lines + ["FUNCTION_PASS bridge-links-up"],
        ))

    def test_prefix_spoof_does_not_match(self):
        changed = [line.replace("FUNCTION_PASS bridge-links-up",
                                "FUNCTION_PASS bridge-links-up-extra")
                   for line in self.lines]
        self.assertIn("p2-bridge-occurrences=0", self.assert_rejected(lines=changed))

    def test_skip_is_not_pass_even_when_success_marker_present(self):
        self.assertIn("bad-marker=FUNCTION_SKIP", self.assert_rejected(
            lines=self.lines + ["FUNCTION_SKIP btrfs-filesystem-registered errno=19"],
        ))

    def test_fail_is_not_pass_even_when_success_marker_present(self):
        self.assertIn("bad-marker=FUNCTION_FAIL", self.assert_rejected(
            lines=self.lines + ["FUNCTION_FAIL kvm-device-created errno=19"],
        ))

    def test_module_skip_is_not_pass(self):
        self.assertIn("bad-marker=MODULE_SKIP", self.assert_rejected(
            lines=self.lines + ["MODULE_SKIP kvm reason=unsupported"],
        ))

    def test_unavailable_is_not_pass(self):
        self.assertIn("bad-marker=FUNCTION_UNAVAILABLE", self.assert_rejected(
            lines=self.lines + ["FUNCTION_UNAVAILABLE afalg-sha3-256-abc"],
        ))

    def test_four_canary_summary_cannot_substitute_full_eighteen(self):
        changed = ["RUNTIME_V2_P2_END pass=4 fail=0 skip=0"
                   if line == FULL_END else line for line in self.lines]
        self.assertIn("missing-or-duplicate-p2-full-endpoint",
                      self.assert_rejected(lines=changed))

    def test_partial_p2_result_with_skips_does_not_pass(self):
        self.assertIn("bad-marker=RUNTIME_V2_P2_END", self.assert_rejected(
            lines=self.lines + ["RUNTIME_V2_P2_END pass=1 fail=0 skip=3"],
        ))

    def test_duplicate_full_endpoint_is_not_pass(self):
        self.assertIn("missing-or-duplicate-p2-full-endpoint",
                      self.assert_rejected(lines=self.lines + [FULL_END]))

    def test_p0_p1_existing_contract_still_works_without_qemu_rc(self):
        actual = self.run_checker(lines=self.lines, rc=None, profile="p1")
        self.assertEqual(actual.returncode, 0, actual.stderr)
        self.assertIn("checks=22", actual.stdout)


if __name__ == "__main__":
    unittest.main()
