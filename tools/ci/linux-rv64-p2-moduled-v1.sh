#!/usr/bin/env bash
# Recover P2 module prerequisites from the *same* original RV64 P3 archive.
# No byte from that private archive is written into git or uploaded to CI.
# The final verdict is the complete 18-check P2 oracle, not the loader's exit.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
archive=${P2_RV64_LEGACY_CPIO:?set P2_RV64_LEGACY_CPIO to original verified P3 cpio}
image=${P2_RV64_IMAGE:?set P2_RV64_IMAGE to matching GNU or MiniC Image}
reference_log=${P2_RV64_HISTORICAL_LOG:?set P2_RV64_HISTORICAL_LOG to successful 145/203 log}
work=${P2_RV64_WORK:?set P2_RV64_WORK to an empty scoped scratch directory}
cc=${P2_RV64_CC:-riscv64-linux-gnu-gcc}
timeout_seconds=${P2_RV64_TIMEOUT:-1800}

canonical_archive=88c81f1b9eaa3377fbed3f99f29d34c60491eede6ccd57966a609b16b619a51b
test -f "$archive" && test -s "$image" && test -s "$reference_log"
actual_archive=$(sha256sum "$archive" | cut -d' ' -f1)
if [[ "$actual_archive" != "$canonical_archive" ]]; then
    echo "P2_RV64_FIXTURE=FAIL reason=archive_sha actual=$actual_archive" >&2
    exit 2
fi
if [[ -e "$work" ]]; then
    echo "P2_RV64_FIXTURE=FAIL reason=work_must_not_exist path=$work" >&2
    exit 2
fi
mkdir -p "$work/root" "$work/build" "$work/dtb"

# Archive path safety check before extraction. No absolute paths, parent
# escapes, or duplicate member names (except the root entry) are accepted.
cpio -it <"$archive" >"$work/entries.txt" 2>"$work/cpio-list.stderr"
python3 - "$work/entries.txt" <<'PY'
import pathlib
import sys
entries = pathlib.Path(sys.argv[1]).read_text().splitlines()
bad = [name for name in entries if name != "." and (
    name.startswith("/") or ".." in pathlib.PurePosixPath(name).parts)]
if bad or len(set(entries)) != len(entries):
    raise SystemExit("P2_RV64_FIXTURE=FAIL unsafe_or_duplicate_cpio_path")
PY
(
    cd "$work/root"
    cpio -idm --no-absolute-filenames --no-preserve-owner --quiet <"$archive"
)

guest_root=${P2_RV64_PROBE_ROOT:-}
if [[ -z "$guest_root" ]]; then
    ENABLE_P2=1 RISCV_CC="$cc" BUILD_DIR="$work/build" \
    OUTPUT_INITRAMFS="$work/vanilla-p2.cpio.gz" \
        bash "$repo/tests/external/linux/build_runtime_initramfs.sh" \
        >"$work/build-guest.log"
    guest_root="$work/build/root"
else
    echo "P2_RV64_PROBE_PREBUILT=$guest_root" >"$work/build-guest.log"
fi
for binary in init bin/runtime-probe bin/p2-probe bin/sh; do
    test -s "$guest_root/$binary"
done
mkdir -p "$work/root/bin"
cp "$guest_root/init" "$work/root/bin/p2-main-init"
cp "$guest_root/bin/runtime-probe" "$work/root/bin/runtime-probe"
cp "$guest_root/bin/p2-probe" "$work/root/bin/p2-probe"
cp "$guest_root/bin/sh" "$work/root/bin/sh"
"$cc" -static -O2 -Wall -Wextra -Werror \
    "$repo/tests/external/linux/runtime_initramfs/p2_module_preload.c" \
    -o "$work/root/init"

# The historical final module `nouveau` is a P3-only PCI driver, not one of
# the 18 P2 canaries. With an attached virtio GPU, Nouveau's registration
# returns EINVAL; exclude it explicitly from this *P2* fixture, without
# making any claim that the original 145/145 P3 gate was re-run here.
awk '/^MODULE_PASS[ \t]/ {id=$2; sub(/\r$/, "", id); if (id != "nouveau") print id}' \
    "$reference_log" >"$work/order-names.txt"
find "$work/root/lib/modules/6.6.143" -type f -name '*.ko' -print | LC_ALL=C sort \
    >"$work/module-paths.txt"
awk -v root="$work/root" '
    NR==FNR {
        path=$0
        id=path
        sub(/^.*\//, "", id)
        sub(/\.ko$/, "", id)
        canonical=id
        gsub(/-/, "_", canonical)
        if ((id in matches && matches[id] != substr(path, length(root)+1)) ||
            (canonical in matches && matches[canonical] != substr(path, length(root)+1))) {
            print "P2_RV64_FIXTURE=FAIL duplicate-module-basename id=" id > "/dev/stderr"
            exit 2
        }
        matches[id]=substr(path, length(root)+1)
        matches[canonical]=substr(path, length(root)+1)
        next
    }
    {
        if (!($1 in matches)) {
            print "P2_RV64_FIXTURE=FAIL missing-module-name id=" $1 > "/dev/stderr"
            exit 2
        }
        print matches[$1]
    }
' "$work/module-paths.txt" "$work/order-names.txt" >"$work/root/p2-module-order.txt"
test "$(wc -l <"$work/root/p2-module-order.txt")" -eq 144

(
    cd "$work/root"
    find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet \
        >"$work/p2-rv64-archived-modules.cpio"
)
if [[ -n "${P2_RV64_READY_INITRD:-}" ]]; then
    # Required for a strict GNU/MiniC paired run: both boots consume one
    # byte-identical guest archive, not two independently rebuilt archives.
    test -s "$P2_RV64_READY_INITRD"
    cp "$P2_RV64_READY_INITRD" "$work/p2-rv64-archived-modules.cpio"
fi

# Real block devices: an ext4 virtio-blk disk for the persistence probe,
# a PCIe NVMe controller, and a PCIe virtio GPU for device-binding checks.
if [[ -n "${P2_RV64_DISK_TEMPLATE_DIR:-}" ]]; then
    cp --reflink=auto "$P2_RV64_DISK_TEMPLATE_DIR/ext4.baseline.raw" "$work/ext4.raw"
    cp --reflink=auto "$P2_RV64_DISK_TEMPLATE_DIR/nvme.baseline.raw" "$work/nvme.raw"
else
    truncate -s 128M "$work/ext4.raw"
    mkfs.ext4 -F -q "$work/ext4.raw"
    truncate -s 64M "$work/nvme.raw"
fi
cp --reflink=auto "$work/ext4.raw" "$work/ext4.baseline.raw"
cp --reflink=auto "$work/nvme.raw" "$work/nvme.baseline.raw"

sha256sum "$archive" "$image" "$work/p2-rv64-archived-modules.cpio" \
    "$work/ext4.raw" "$work/nvme.raw" >"$work/fixture.sha256"
printf 'P2_RV64_FIXTURE_PREP=PASS scope=P2-only modules=144 original_archive_modules=145 image=%s initrd=%s\n' \
    "$(sha256sum "$image" | cut -d' ' -f1)" \
    "$(sha256sum "$work/p2-rv64-archived-modules.cpio" | cut -d' ' -f1)"

set +e
qemu_start_epoch=$(date +%s)
QEMU_EXPLICIT_DTB_DIR="$work/dtb" QEMU_REAL_SYSTEM_RISCV64=qemu-system-riscv64 \
timeout "$timeout_seconds" python3 "$repo/tests/external/linux/qemu_explicit_initrd_wrapper.py" \
    -M virt -cpu max -m 1024M -smp 1 -nographic -no-reboot -monitor none \
    -bios default -kernel "$image" -initrd "$work/p2-rv64-archived-modules.cpio" \
    -drive "file=$work/ext4.raw,format=raw,if=none,id=p2blk" \
    -device virtio-blk-device,drive=p2blk \
    -drive "file=$work/nvme.raw,format=raw,if=none,id=p2nvme" \
    -device nvme,serial=p2nvme,drive=p2nvme \
    -device virtio-gpu-pci \
    -append 'console=ttyS0 earlycon=uart8250,mmio,0x10000000,115200n8 loglevel=8 rdinit=/init minic_runtime=p2 nowatchdog workqueue.watchdog_thresh=0 hung_task_timeout_secs=0 panic=-1' \
    >"$work/qemu.log" 2>&1
qemu_rc=$?
qemu_elapsed=$(( $(date +%s) - qemu_start_epoch ))
set -e
printf 'P2_RV64_QEMU_EXIT=%d elapsed_seconds=%d timeout_seconds=%d\n' \
    "$qemu_rc" "$qemu_elapsed" "$timeout_seconds" | tee "$work/qemu-rc.txt"
grep -aE '^P2_RV64_MODULE_PRELOAD|^MODULE_FAIL|^RUNTIME_V2_P2_|^INIT_P2_PROBE' \
    "$work/qemu.log" | tail -n 25 || true
python3 "$repo/tests/external/linux/runtime_v2_log_check.py" \
    --profile p2 --log "$work/qemu.log" --qemu-rc "$qemu_rc" \
    >"$work/p2-verdict.stdout" 2>"$work/p2-verdict.stderr" || {
        echo "P2_RV64_FULL=FAIL work=$work" >&2
        sed -n '1,80p' "$work/p2-verdict.stderr" >&2
        exit 1
    }
echo "P2_RV64_FULL=PASS work=$work"
