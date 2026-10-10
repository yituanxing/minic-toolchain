#!/usr/bin/env python3
"""Read-only RISC-V Linux startup boundary probe using QEMU's GDB stub.

NOT a complete stack unwind, compiler certificate, or PID1 boot test.
Hardware breakpoints mark entry to a few known Linux 6.6.143 functions.
A confirmed mnt_init breakpoint is required as the positive control.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time

TARGETS = ("mnt_init", "alloc_large_system_hash", "kernfs_init", "sysfs_init")
CMDLINE = ("console=ttyS0 earlycon=uart8250,mmio,0x10000000,115200n8 "
           "loglevel=8 ignore_loglevel panic=-1")

def collect_symbols(binary):
    nm=subprocess.run(["riscv64-linux-gnu-nm", "-n", "--defined-only", str(binary)],
                      capture_output=True, text=True, check=True)
    found={}
    for row in nm.stdout.splitlines():
        parts=row.split(None,2)
        if len(parts)==3 and parts[1] in ("t","T","w","W") and parts[2] in TARGETS:
            found[parts[2]]=int(parts[0],16)
    return found

def terminate(group):
    if group.poll() is None:
        os.killpg(group.pid,signal.SIGTERM)
        try: group.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(group.pid,signal.SIGKILL)
            group.wait(timeout=3)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--image",required=True,type=Path)
    p.add_argument("--vmlinux",required=True,type=Path)
    p.add_argument("--output-dir",required=True,type=Path)
    p.add_argument("--timeout-seconds",type=int,default=34)
    a=p.parse_args()
    if not 12<=a.timeout_seconds<=90: p.error("timeout-seconds must be 12..90")
    for executable in ("gdb-multiarch","riscv64-linux-gnu-nm","qemu-system-riscv64"):
        if shutil.which(executable) is None: raise SystemExit(f"GDB_BOUNDARY=INCONCLUSIVE tool_missing={executable}")
    a.output_dir.mkdir(parents=True,exist_ok=True)
    symbols=collect_symbols(a.vmlinux)
    for key in TARGETS:
        if key not in symbols:
            print(f"GDB_BOUNDARY=INCONCLUSIVE missing_symbol={key}",flush=True)
    if "mnt_init" not in symbols or "kernfs_init" not in symbols:
        return 8
    sock=Path(f"/tmp/minic-boundary-{os.getpid()}.sock")
    sock.unlink(missing_ok=True)
    qlog=a.output_dir/"qemu-uart.log"
    glog=a.output_dir/"gdb.log"
    script=a.output_dir/"breakpoints.gdb"
    lines=[
        "set confirm off",
        "set pagination off",
        "set print thread-events off",
        "set architecture riscv:rv64",
        f"file {a.vmlinux.resolve()}",
        f"target remote {sock}",
    ]
    for key in TARGETS:
        if key not in symbols: continue
        lines.extend([
            f"hbreak *0x{symbols[key]:x}",
            "commands",
            "silent",
            f'printf "GDB_BOUNDARY_HIT function={key} pc=0x%lx\\n", $pc',
            "continue",
            "end",
        ])
    lines.append("continue")
    script.write_text("\n".join(lines)+"\n")
    state="INCONCLUSIVE"
    with qlog.open("w") as qfile:
        qemu=subprocess.Popen([
            "qemu-system-riscv64","-S",
            "-chardev",f"socket,path={sock},server=on,wait=off,id=gdb0",
            "-gdb","chardev:gdb0",
            "-M","virt","-cpu","max","-m","512M","-smp","1",
            "-nographic","-no-reboot","-bios","default",
            "-kernel",str(a.image.resolve()),"-append",CMDLINE,
        ],stdin=subprocess.DEVNULL,stdout=qfile,stderr=subprocess.STDOUT,
           start_new_session=True)
        try:
            for _ in range(100):
                if sock.exists(): break
                if qemu.poll() is not None: raise RuntimeError(f"QEMU exited rc={qemu.returncode}")
                time.sleep(.05)
            else: raise RuntimeError("GDB socket not created")
            try:
                result=subprocess.run(["gdb-multiarch","--nx","--quiet","--batch","-x",str(script)],
                    capture_output=True,text=True,errors="replace",timeout=a.timeout_seconds)
                glog.write_text(result.stdout+"\n"+result.stderr)
                print(f"GDB_BOUNDARY_GDB_EXIT rc={result.returncode}",flush=True)
            except subprocess.TimeoutExpired as exc:
                out=exc.stdout or b"";err=exc.stderr or b""
                if isinstance(out,bytes):out=out.decode(errors="replace")
                if isinstance(err,bytes):err=err.decode(errors="replace")
                glog.write_text(out+"\n"+err)
                print("GDB_BOUNDARY_GDB_EXIT timeout_expected=true",flush=True)
            evidence=glog.read_text(errors="replace")
            for key in TARGETS:
                count=evidence.count("GDB_BOUNDARY_HIT function="+key+" ")
                print(f"GDB_BOUNDARY_COUNT function={key} hits={count}",flush=True)
            uart=qlog.read_text(errors="replace")
            banner="Linux version 6.6.143" in uart
            mount="Mountpoint-cache hash table entries" in uart
            print(f"GDB_BOUNDARY_SERIAL kernel_banner={str(banner).lower()} mountpoint_log={str(mount).lower()}",flush=True)
            if "GDB_BOUNDARY_HIT function=mnt_init " in evidence and banner:
                state="POSITIVE_CONTROL_PASS"
            print(f"GDB_BOUNDARY={state} runtime_certificate=false",flush=True)
        except (OSError,RuntimeError) as exc:
            glog.write_text(f"GDB_BOUNDARY_ERROR={exc}\n")
            print(f"GDB_BOUNDARY=INCONCLUSIVE reason={exc}",flush=True)
        finally:
            terminate(qemu)
            sock.unlink(missing_ok=True)
    return 0 if state=="POSITIVE_CONTROL_PASS" else 8

if __name__=="__main__":
    raise SystemExit(main())
