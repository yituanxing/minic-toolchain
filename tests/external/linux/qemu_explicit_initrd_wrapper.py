#!/usr/bin/env python3
import os
import pathlib
import shutil
import subprocess
import sys

REAL_QEMU = os.environ.get("QEMU_REAL_SYSTEM_RISCV64", "qemu-system-riscv64")
RAM_BASE = 0x80000000
RAM_SIZE = 512 * 1024 * 1024
KERNEL_LOAD = 0x80200000
ALIGN = 2 * 1024 * 1024
GUARD = 2 * 1024 * 1024


def value_after(args, flag):
    try:
        i = args.index(flag)
    except ValueError:
        return None
    return args[i + 1] if i + 1 < len(args) else None


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def main():
    args = sys.argv[1:]
    initrd = value_after(args, "-initrd")
    kernel = value_after(args, "-kernel")
    if not initrd or not kernel:
        os.execvp(REAL_QEMU, [REAL_QEMU, *args])

    initrd_path = pathlib.Path(initrd).resolve()
    kernel_path = pathlib.Path(kernel).resolve()
    image_size = kernel_path.stat().st_size
    initrd_size = initrd_path.stat().st_size
    initrd_addr = align_up(KERNEL_LOAD + image_size + GUARD, ALIGN)
    initrd_end = initrd_addr + initrd_size
    ram_end = RAM_BASE + RAM_SIZE
    if initrd_end >= ram_end:
        raise SystemExit(
            f"QEMU_EXPLICIT_INITRD_ERROR image={image_size} initrd={initrd_size} "
            f"range=0x{initrd_addr:x}-0x{initrd_end:x} ram_end=0x{ram_end:x}"
        )

    work = pathlib.Path(
        os.environ.get("QEMU_EXPLICIT_DTB_DIR", str(initrd_path.parent / "explicit-dtb"))
    )
    work.mkdir(parents=True, exist_ok=True)
    dtb = work / "virt-explicit-initrd.dtb"
    if dtb.exists():
        dtb.unlink()

    subprocess.run(
        [
            REAL_QEMU,
            "-M", f"virt,dumpdtb={dtb}",
            "-cpu", "max",
            "-m", "512M",
            "-smp", "1",
            "-nographic",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if not dtb.is_file() or dtb.stat().st_size == 0:
        raise SystemExit("QEMU_EXPLICIT_INITRD_ERROR failed to dump virt DTB")

    if subprocess.run(["fdtget", "-p", str(dtb), "/chosen"],
                      stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode != 0:
        subprocess.run(["fdtput", "-c", str(dtb), "/chosen"], check=True)

    start_hi, start_lo = (initrd_addr >> 32) & 0xFFFFFFFF, initrd_addr & 0xFFFFFFFF
    end_hi, end_lo = (initrd_end >> 32) & 0xFFFFFFFF, initrd_end & 0xFFFFFFFF
    subprocess.run(
        ["fdtput", "-t", "x", str(dtb), "/chosen", "linux,initrd-start",
         f"{start_hi:x}", f"{start_lo:x}"],
        check=True,
    )
    subprocess.run(
        ["fdtput", "-t", "x", str(dtb), "/chosen", "linux,initrd-end",
         f"{end_hi:x}", f"{end_lo:x}"],
        check=True,
    )

    rewritten = []
    i = 0
    while i < len(args):
        if args[i] == "-initrd":
            i += 2
            continue
        if args[i] == "-dtb":
            i += 2
            continue
        rewritten.append(args[i])
        i += 1
    rewritten += [
        "-dtb", str(dtb),
        "-device", f"loader,file={initrd_path},addr=0x{initrd_addr:x},force-raw=on",
    ]

    print(
        f"QEMU_EXPLICIT_INITRD=PASS image_bytes={image_size} initrd_bytes={initrd_size} "
        f"start=0x{initrd_addr:x} end=0x{initrd_end:x} dtb={dtb}",
        file=sys.stderr,
        flush=True,
    )
    os.execvp(REAL_QEMU, [REAL_QEMU, *rewritten])


if __name__ == "__main__":
    main()
