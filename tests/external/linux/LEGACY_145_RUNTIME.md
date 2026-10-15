# Reusing the Python V218 Linux runtime oracle

This is an adapter for an **existing, historically passing** guest-side test,
not a reimplementation of its 145 module and 203 functional assertions.

## Authenticated external test asset

Original asset (user-owned archival Library; **not** stored in GitHub):

- Filename: natural145-v024-cold-overlay.cpio
- SHA256: 88c81f1b9eaa3377fbed3f99f29d34c60491eede6ccd57966a609b16b619a51b
- 67,667,456 bytes (uncompressed newc cpio).
- 145 ELF64 RISC-V .ko modules.
- All 145 modules contain vermagic: `6.6.143 SMP mod_unload riscv`.
- /init: static RISC-V ELF, 187,520 bytes.
- Original V218 result: 145 MODULE_PASS, 203 FUNCTION_PASS, 0 FAIL,
  endpoint `M38_FINAL145_V028_END pass=203 fail=0`, normal guest powerdown.
- Historical QEMU suite was run with `-m 1024M` and took roughly 300–1345
  guest/host elapsed seconds across reported runner conditions. Do not use a 30–45-second syscall timeout for this gate.

The recovered V218 Python source archive is separate from this guest asset.
Do not confuse it with the modules/functional test executable.

## Preflight before real QEMU

```bash
python3 tests/external/linux/legacy_145_runtime_bridge.py \
  --archive /path/to/natural145-v024-cold-overlay.cpio \
  --output-dir build/legacy145 \
  --label inspect
```

The preflight verifies the archive hash, confirms precisely 145 modules,
checks their vermagic, validates the static RISC-V /init and checks the
historical test endpoint. It does not claim a Linux runtime PASS.

Optional replay of a preserved *actual* Python-era QEMU log:

```bash
python3 tests/external/linux/legacy_145_runtime_bridge.py \
  --archive /path/to/natural145-v024-cold-overlay.cpio \
  --historical-log /path/to/qemu-v218-final145.log \
  --output-dir build/legacy145 --label historical
```

## Real paired runtime validation

**First** run the original cpio unchanged against the certified, same-config
pure-GCC 6.6.143 Linux Image. **Then** reuse the same cpio with the candidate
MiniC Image, keeping Linux .config, toolchain-supported ABI, QEMU and cmdline
constant. Do not infer module compatibility solely from Linux version:

```bash
python3 tests/external/linux/legacy_145_runtime_bridge.py \
  --archive /path/to/natural145-v024-cold-overlay.cpio \
  --image /path/to/certified-gnu/arch/riscv/boot/Image \
  --label gcc --output-dir build/legacy145 --timeout 1800

python3 tests/external/linux/legacy_145_runtime_bridge.py \
  --archive /path/to/natural145-v024-cold-overlay.cpio \
  --image /path/to/repaired-minic/arch/riscv/boot/Image \
  --label minic --output-dir build/legacy145 --timeout 1800
```

The helper invokes the existing `qemu_explicit_initrd_wrapper.py`, with
`-m 1024M`, an explicit initramfs relocation away from the oversized
current MiniC Image, unchanged historical /init, and the current RV64 UART
early console (which works with this configuration). The wrapper now derives
DTB memory size and initrd bounds from the requested QEMU memory rather than
hardcoding 512 MiB.

No historical test is silently dropped. An ordinary QEMU exit or the presence
of a Linux banner is **not** considered PASS. PASS requires *all* of:
145 module PASS markers, 203 function PASS markers, the original endpoint,
zero module/functional FAIL markers, no kernel fatal markers, no unknown
symbols/module compatibility error, QEMU exit 0, and normal powerdown.

Possible results:

- `PASS`: full historical oracle satisfied.
- `ENVIRONMENT_OR_MODULE_ABI_MISMATCH`: unknown symbol, bad module
  version, etc.; first fix the fixture/config, not the MiniC compiler.
- `FAIL_RUNTIME`: kernel BUG/Oops/lockup or a functional assertion.
- `INCONCLUSIVE`: missing markers, timeout, load error not yet classified.

Each run preserves `qemu.log` and a structured `<label>-legacy145.json`
with the last module/function marker and first failure.

## Limitation and promotion

The 67 MB source cpio remains in the user's private archival Library and
is **not available to ordinary public GitHub-hosted runners**. Do not add a
workflow that claims to execute this gate until the artifact is transported
to the runner by an approved, verified mechanism. Do not publish the
historical user-owned binary/module archive without explicit approval.

The exact original V024 Image plus unchanged cpio has now been independently
replayed under QEMU 8.2.2 with 145/145 module and 203/203 functional PASS,
matching the historical unique-ID fingerprints; QEMU rc=0 and clean shutdown.
The current 215,502,848-byte MiniC Image (SHA256
`791e2ec22f9f6a5cf02eb7f437c6cf25e5e8e89e6fa8230ce6573f0749beb056`)
has also entered this unchanged suite, with 142/145 modules reached before
full certification. Distinguish slow heavy module relocation from true stalling;
the suite deliberately has no fixed small QEMU timeout.

The historical V218 M37 profile consists of five independent tests
(boot, smoke, ipv6tcp, ext4 rootfs, more2). Its original gate logs and
run_one.sh have also been recovered, but the complete input initramfs and
ext4 disk assets must be checked separately before claiming the M37 gate has
been ported. The all145 cpio is one concrete executable oracle, not a
replacement for all five M37 test assets.

Prioritize this exact-runtime acceptance gate for milestone validation.
Keep the existing fast P0/P1 contract for cheap routine object debugging.
