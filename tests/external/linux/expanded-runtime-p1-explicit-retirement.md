# Expanded P1 explicit-initrd retirement contract

The historical `.github/workflows/linux-expanded-runtime-p1-explicit-initrd-v0.yml` is superseded by `.github/workflows/linux-expanded-runtime-p1-v0.yml`.

The dedicated explicit-initrd workflow was introduced first to validate initrd placement. Later on the same day the canonical P1 workflow was updated to use the same `qemu_explicit_initrd_wrapper.py`, the same deterministic P1 initramfs builder, the same Linux Image/cache source, and the same `runtime_v2_log_check.py --profile p1` contract.

The P1 manifest requires all twelve individual syscall PASS markers plus `INIT_RUNTIME_PROBE=PASS`, in addition to the P0 boot/VFS/shutdown contract and fatal-kernel marker rejection. This subsumes the dedicated workflow's aggregate `RUNTIME_V2_SYSCALL_END pass=12 fail=0` and probe checks.

The canonical P1 workflow additionally preserves vmlinux physical-layout and code/rodata inflation diagnostics. Both workflows use the same historical trigger tag `[linux-expanded-runtime-p1]`, so no manual debugging entry point is lost.

The retired YAML is preserved byte-for-byte under `.github/workflows-disabled/`.
