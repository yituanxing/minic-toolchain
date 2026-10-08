# Linux distributed Image: two independently verified blockers (2026-10-08)

This document preserves **actionable, bounded evidence**, not a claim of a bootable
MiniC-built Image. Do not rerun a 50-minute Image merely to rediscover these
issues; retain the independently certified 3352-object artifacts from
[run 37767282927](https://github.com/yituanxing/minic-toolchain/actions/runs/37767282927)
until their published expiry.

## A. Kbuild rebuilds restored objects despite matching commands

- [Receiver run 37769783557](https://github.com/yituanxing/minic-toolchain/actions/runs/37769783557)
  restored 3352 real C objects **and 3352 `.cmd` metadata files**, passed
  6704 content SHA checks, but its `make Image` took **3087 s** and reran
  many C compilations. Final link did not pass.
- The producer's saved command in `out/init/.main.o.cmd` matches the receiver's
  logged `init/main.o` command **byte for byte**. Another sampled
  `arch/riscv/kernel/alternative.o` command also matches; changing compiler
  flags is not the demonstrated cause for those samples.
- Source artifacts retain producer timestamps. Receiver `prepare` regenerates
  local headers at a later time. A generated dependency newer than its
  restored `.o` makes Kbuild correctly consider it stale, even when the
  command string and object bytes match.
- The [focused mtime experiment 37779016387](https://github.com/yituanxing/minic-toolchain/actions/runs/37779016387)
  **PASSED**. With original object timestamp a direct
  `make init/main.o` incurred `minic_calls=4` in the observed trace.
  Restoring the **same content** and only touching `init/main.o`
  reduced the observed call count to **0**.
  This proves an mtime-sensitive incremental-build failure for that object;
  it does not yet prove 3352/3352 no-rebuild in a complete Image build.

**Recommended fix and gate**

1. Fresh receiver performs complete source/config/compiler/manifest identity
   verification and restores every `.o` **plus its original `.cmd`**.
2. Verify **all 6704 SHA256 hashes** before changing filesystem metadata.
3. Refresh `.o` modification times *after* generating all local Kbuild headers,
   preserving all bytes and `.cmd` content, then verify hashes again.
   Do not touch kernel source or mutate config to fake identity.
4. Check sample objects across modules/built-in directories using real
   `make -n` and/or focused `make` with identical wrapper flags;
   if a transferred sample is rebuilt, fail before full Image.
5. For eventual full Image, compare every transferred `.o/.cmd` hash after
   Kbuild; **zero unexpected transferred-object rebuilds** is the gate.
   Do not call the 3087-second failed run a performance result for a
   correctly cached distributed Image.

## B. Five residual undefined compiletime assertions

The last `vmlinux` link in 37769783557 failed on exactly these reported
unresolved symbols:

| Object | Symbol | Macro family |
| --- | --- | --- |
| `kernel/bpf/bpf_lru_list.o` | `__compiletime_assert_76` | `clamp()` |
| `fs/nfs/dir.o` | `__compiletime_assert_638` | `min()` |
| `drivers/scsi/sr_ioctl.o` | `__compiletime_assert_363` | `clamp()` |
| `net/core/page_pool.o` | `__compiletime_assert_489` | `min()` |
| `net/ipv4/tcp_output.o` | `__compiletime_assert_879` | `min()` |

- The [five-object reference differential
  37778193835](https://github.com/yituanxing/minic-toolchain/actions/runs/37778193835)
  **PASSED**: each producer object and same-profile MiniC replay retained
  its precise undefined symbol, whereas a genuine Kbuild GCC compile of
  the same target retained **none** of these five.
- The captured **preprocessed inputs** show the common source pattern:
  Linux `min/clamp` signedness guards combine `typeof`, integer size,
  `__builtin_constant_p((long long)local >= 0)`, and positive literal
  `__auto_type` locals. Examples:
  `min(pool->p.pool_size, 16384)`, `min(NFS_SERVER(dir)->dtsize, 0x00010000)`,
  and `clamp(speed, 0, 0xffff / 177)`.
- **Hypothesis, not proven root cause:** MiniC's local-integer facts or
  constant-p/conditional/bitwise lowering fails to prove a positive
  initialized `__auto_type` bound nonnegative. GCC removes the conditional
  error-call. Avoid hardcoding these five symbol names or silently
  suppressing all compile-time errors.

**Suggested minimal semantic regression before changing Core**

```c
int check_positive_auto(void) {
    __auto_type limit = 16384;
    return __builtin_constant_p((long long)limit >= 0) &&
           ((long long)limit >= 0);
}
int check_signedness(unsigned input) {
    __auto_type bound = 16384;
    return (((typeof(input))-1) < (typeof(input))1) ? 2 :
           (1 + 2 * (sizeof(input) < 4));
}
```

Compare GNU GCC and MiniC for the `constant_p` query itself, nested
`?:` / bitwise signedness masks and the full macro at the same optimization
mode. Extend to all five exact source objects; any proposed patch must
retain valid nonconstant behavior and satisfy 3352/3352 + existing
runtime correctness gates. A five-GCC-object swap may serve as a labeled
**hybrid diagnostic** for locating the *next* Image/QEMU blocker; it is
**not** proof of a fully MiniC-built kernel.

## Exit criteria

- All 3352 `.o` compile and restore with exact identity, and final Kbuild
  performs no unexpected recompilation of transferred C objects.
- The five GNU-vs-MiniC assertion differences disappear because MiniC emits
  GCC-compatible semantics—not because link checks were removed.
- Complete `vmlinux`, `System.map`, `Image` and real QEMU marker all pass.
- Report real end-to-end wall time separately from aggregate per-TU CPU time
  and from one-time artifact downloads/setup.


## Follow-up profile differential: 55-patch candidate also fails 5/5

[Run 37779305823](https://github.com/yituanxing/minic-toolchain/actions/runs/37779305823)
completed **SUCCESS as a diagnostic workflow** at 2026-10-08 12:49 UTC,
but the result for the compiler feature is negative:

```text
PERF_FIVE_ASSERT=REMAINS target=kernel/bpf/bpf_lru_list.o
PERF_FIVE_ASSERT=REMAINS target=fs/nfs/dir.o
PERF_FIVE_ASSERT=REMAINS target=drivers/scsi/sr_ioctl.o
PERF_FIVE_ASSERT=REMAINS target=net/core/page_pool.o
PERF_FIVE_ASSERT=REMAINS target=net/ipv4/tcp_output.o
PERF_FIVE_SUMMARY objects=5 unresolved=5 compiler=55-patch
```

Thus switching the Linux Image producer from the canonical runtime profile
to the 55-patch performance candidate would **not resolve** the five linker
blockers. The underlying `min/clamp` signedness/constant-p semantics need a
focused compiler fix. The same diagnostic reran the controlled mtime probe and
reconfirmed `original=4`, `touched=0` MiniC calls on `init/main.o`.
This workflow deliberately did **not** run full Image or QEMU; its green status
must not be interpreted as a green Linux kernel.
