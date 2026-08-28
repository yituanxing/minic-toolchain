# Linux Runtime Validation V2

Status: active design for the post-Compiler-V1 Linux integration phase.

## Goal

Preserve the failure-detection quality of the Python-era Linux validation while
reducing repeated work.  The rule is:

> Build an Image once, then reuse that exact Image across increasingly deep
> runtime profiles.  Keep the historical full gate as a release/freeze oracle,
> but do not pay its cost on every iteration.

## Historical evidence recovered

The Python MiniC line already established several useful validation shapes:

- hard userspace reachability: a tiny static PID1 printed a marker and powered
  off; guest poweroff was PASS, panic/hang timed out;
- M37 five gates: boot, smoke, rootfs, ipv6tcp and more2;
- a focused bridge/overlay/conntrack gate reached its endpoint at about 48 s;
- final cumulative module gate: 145 MODULE_PASS + 203 FUNCTION_PASS, 0 FAIL;
- strong semantic canaries included:
  - overlay mount/read/copy-up;
  - dummy interface + bridge;
  - iptables / conntrack;
  - br-netfilter sysctls;
  - AF_ALG SHA-256, SHA-512, SHA3-256 and BLAKE2b-512 known-answer tests;
  - Btrfs filesystem registration;
  - KVM registration and /dev/kvm;
  - Radeon/Nouveau PCI-driver registration;
- the full cumulative QEMU endpoint was observed around 442 s in one run and
  around 1345 s in a slower run.

The old suite therefore already proved an important fact: most correctness
coverage can be exercised inside one guest boot.  The expensive part is heavy
module loading under TCG, not the number of assertions.

## V2 profiles

### P0 — hard boot canary

Purpose: fastest possible kernel reachability and panic detector.

One QEMU boot:

1. boot Linux with initramfs;
2. run a tiny static PID1;
3. print a deterministic marker;
4. power off.

Required negatives:

- no panic;
- no Oops;
- no BUG;
- no lockup;
- no invalid execution status.

Use: every Image-producing iteration and bisection.

### P1 — fast semantic canary

Purpose: high information density in one boot.

Target budget: approximately 60–120 s under normal TCG conditions.

One guest boot should combine:

- /proc, /sys and /dev availability;
- command-line/PID1 sanity;
- writable storage/rootfs round-trip when a disk image is attached;
- overlay lower/read/copy-up;
- dummy network interface;
- bridge attach/up;
- conntrack loopback traffic;
- IPv4/IPv6/TCP sanity;
- br-netfilter/iptables registration when configured;
- crypto known-answer tests:
  - SHA-256("abc");
  - SHA-512("abc");
  - SHA3-256("abc");
  - BLAKE2b-512("abc");
- explicit PASS/FAIL counters;
- clean poweroff.

These canaries are deliberately chosen because historical compiler defects
escaped static/module-registration checks but were caught by real operations.
The BLAKE2b gate, for example, detected a 64-bit enum truncation bug.

### P2 — heavy subsystem canary

Purpose: exercise expensive kernel subsystems without loading the entire
historical module pool.

One boot, selected high-value checks:

- Btrfs registered and visible in /proc/filesystems;
- KVM registered and /dev/kvm present;
- device-mapper control path;
- one representative DRM/virtio path;
- selected storage/network dependency chains;
- repeat module load/unload where meaningful.

Radeon/Nouveau remain valuable release checks but are not required in every
iteration because their TCG relocation/load cost dominates runtime.

### P3 — full historical acceptance

Purpose: release/freeze oracle.

Retain the historical quality bar:

- 145/145 module load gates;
- 203/203 functional gates;
- 0 failures;
- normal guest powerdown;
- no Unknown symbol;
- no invalid module format;
- no Oops / BUG / kernel panic;
- no soft/workqueue lockup.

This gate is not deleted.  It is moved to milestone/release frequency.

## Build/runtime separation

Runtime profiles must consume an Image artifact rather than rebuilding Linux.

Pipeline:

```
compiler
  -> one Kbuild
  -> vmlinux / Image artifact
       -> P0
       -> P1
       -> P2
       -> P3
```

This lets P0/P1/P2/P3 run independently or in parallel without recompiling the
kernel.

## Host MiniC versus self-hosted Stage2

The current self-hosted Stage2 compiler is much slower under qemu-user.  B3
measurements for 16 spread Linux TUs are already roughly 769 s, 1102 s and
1326 s on completed shards, while GNU assembly of those 16 outputs takes only
about 3–6 s.

Therefore:

### Iteration lane

Use native-host MiniC for full Kbuild -> Image -> P0/P1.

This remains the fastest way to find Linux correctness defects in the compiler
and backend.

### Self-host bridge

Use frozen Stage2 for:

- B1 fixed point;
- B2 real-project/runtime;
- B3 spread Linux samples.

This proves that the self-hosted compiler behaves correctly on diverse inputs
without paying a second full-kernel build on every iteration.

### Self-host release lane

Run a full Stage2 Kbuild -> Image -> runtime once at the Linux/self-host
milestone boundary.

Do not require both:

1. Stage2 frozen-corpus all3352 replay; and
2. Stage2 full Kbuild

as mandatory back-to-back gates.  Full Kbuild is strictly stronger because it
re-exercises compilation and continues through object generation, linking and
runtime.  The all3352 replay remains available as a diagnostic/freeze tool.

## Promotion policy

Routine Linux integration:

```
host MiniC full Image
  -> P0
  -> P1
```

Subsystem-sensitive change:

```
host MiniC full Image
  -> P0
  -> P1
  -> affected P2 canaries
```

Compiler/Linux milestone:

```
Stage2 full Image
  -> P0
  -> P1
  -> P2
  -> P3 full 145/203
```

This changes test frequency, not the final quality bar.


## 2026-08-28 implemented status

The design above is now implemented far enough to freeze the validation
architecture.

- B3 self-hosted Stage2 Linux spread sample: **112/112 PASS** across 7/7
  shards (run `33177107948`).
- P0 one-boot init canary is implemented and checks proc, sysfs, devtmpfs,
  cmdline, tmpfs read/write, PID1 and clean poweroff.
- P1 is intentionally capped at a **single boot** containing 12 high-value
  syscall canaries:
  - eventfd read/write;
  - epoll + eventfd readiness;
  - timerfd;
  - signalfd;
  - inotify;
  - Unix socketpair;
  - IPv6 TCP loopback;
  - memfd;
  - pidfd;
  - futex;
  - io_uring setup;
  - mmap + mprotect.
- P1 also proves fork/exec/wait by executing the probe as a child of PID1.
- The P1 boundary is machine-readable in
  `tests/external/linux/runtime_v2_checks.tsv`; older rootfs/module/netfilter/
  crypto checks are P2, not P1.
- GCC Linux 6.6.143 calibration run `33182572335` passed both:
  - historical full runtime contract: **2/2 lanes PASS**;
  - P1: **12/12 syscall canaries PASS in one boot**.
- The calibrated P1 QEMU phase itself completes in roughly two seconds on the
  current hosted runner; therefore it is suitable for routine Image
  qualification.
- `Linux Runtime V2 P1 Reuse` consumes an already-built MiniC Image artifact
  so P1 does not trigger a second kernel build.

P2 and P3 are deliberately not expanded further during compiler V1 closure.
The historical 145-module / 203-function gate remains the P3 release oracle.

The remaining compiler-phase integration action is only to qualify a
MiniC-built Image through P1.  No further runtime-test architecture work is
required before starting the native preprocessor module.
