#!/usr/bin/env python3
import argparse
import os
import re
import subprocess
from pathlib import Path

BAD_SECTIONS = {
    '.con_initcall.init', '.cpuidle.text', '.exit.text', '.exitcall.exit',
    '.init.data', '.init.rodata', '.init.setup', '.init.text',
    '.initcall0.init', '.initcall1.init', '.initcall2.init', '.initcall3.init',
    '.initcall3s.init', '.initcall4.init', '.initcall4s.init', '.initcall5.init',
    '.initcall5s.init', '.initcall6.init', '.initcall7.init', '.initcall7s.init',
    '.initcallearly.init', '.initcallrootfs.init', '.lsm_info.init', '.modinfo',
    '.noinstr.text', '.pci_fixup_early', '.pci_fixup_enable', '.pci_fixup_final',
    '.pci_fixup_header', '.pci_fixup_resume', '.pci_fixup_resume_early',
    '.pci_fixup_suspend', '.pci_fixup_suspend_late', '.ref.data', '.ref.text',
    '.sched.text', '.sdata', '.softirqentry.text', '.spinlock.text',
    '__clk_of_table', '__clk_of_table_end', '__dl_sched_class', '__earlycon_table',
    '__fair_sched_class', '__governor_thermal_table', '__idle_sched_class',
    '__irqchip_acpi_probe_table', '__irqchip_of_table', '__irqchip_of_table_end',
    '__modver', '__param', '__reservedmem_of_table', '__reservedmem_of_table_end',
    '__rt_sched_class', '__stop_sched_class', '__timer_acpi_probe_table',
    '__timer_of_table', '__timer_of_table_end',
}


def bad_sections(path: Path) -> list[str]:
    try:
        text = subprocess.check_output(
            ['riscv64-linux-gnu-readelf', '-SW', str(path)],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (subprocess.CalledProcessError, UnicodeDecodeError):
        return []
    bad = []
    for line in text.splitlines():
        for name in BAD_SECTIONS:
            if not re.search(r'\s' + re.escape(name) + r'\s', line):
                continue
            cols = line.split()
            try:
                idx = cols.index(name)
            except ValueError:
                continue
            rest = cols[idx:]
            flags = rest[6] if len(rest) >= 9 else ''
            if 'A' not in flags:
                bad.append(name)
    return sorted(set(bad))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('out')
    parser.add_argument('--targets-file')
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    out = Path(args.out)

    if args.verify:
        if not args.targets_file:
            parser.error('--verify requires --targets-file')
        failures = []
        for raw in Path(args.targets_file).read_text().splitlines():
            target = raw.strip()
            if not target:
                continue
            bad = bad_sections(out / target)
            if bad:
                failures.append(f"{target}: {','.join(bad)}")
        if failures:
            print('\n'.join(failures))
            return 1
        print('LINUX_SECTION_REFRESH_VERIFY=PASS')
        return 0

    found = []
    for root, _, files in os.walk(out):
        for filename in files:
            if not filename.endswith('.o') or filename == 'vmlinux.o':
                continue
            path = Path(root) / filename
            bad = bad_sections(path)
            if bad:
                target = path.relative_to(out).as_posix()
                found.append((target, bad))
    for target, bad in sorted(found):
        print(target)
        print(f"DETAIL {target}: {','.join(bad)}", file=os.sys.stderr)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
