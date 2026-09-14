#!/usr/bin/env python3
import argparse
import os
import subprocess
from concurrent.futures import ThreadPoolExecutor
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

# These objects were outside the historical final-link failure pool and can be
# restored from a mixed cache produced before all-internal Core reachability was
# fixed. Rebuild this tiny known set alongside section refreshes so final-link
# validation never measures stale lockdep_is_held references.
STALE_SEMANTIC_OBJECTS = (
    'net/ipv4/tcp_output.o',
    'net/ipv4/tcp_timer.o',
    'net/ipv4/tcp_ipv4.o',
)


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
        cols = line.split()
        names = BAD_SECTIONS.intersection(cols)
        if not names:
            continue
        for name in names:
            idx = cols.index(name)
            rest = cols[idx:]
            flags = rest[6] if len(rest) >= 9 else ''
            if 'A' not in flags:
                bad.append(name)
    return sorted(set(bad))


def inspect(out: Path, path: Path):
    bad = bad_sections(path)
    if not bad:
        return None
    return path.relative_to(out).as_posix(), bad


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
        targets = [x.strip() for x in Path(args.targets_file).read_text().splitlines() if x.strip()]
        with ThreadPoolExecutor(max_workers=16) as pool:
            results = list(pool.map(lambda target: (target, bad_sections(out / target)), targets))
        for target, bad in results:
            if bad:
                failures.append(f"{target}: {','.join(bad)}")
        if failures:
            print('\n'.join(failures))
            return 1
        print(f'LINUX_SECTION_REFRESH_VERIFY=PASS targets={len(targets)}')
        return 0

    paths = []
    for root, _, files in os.walk(out):
        for filename in files:
            if filename.endswith('.o') and filename != 'vmlinux.o':
                paths.append(Path(root) / filename)

    workers = min(24, max(8, (os.cpu_count() or 4) * 4))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        results = pool.map(lambda path: inspect(out, path), paths)
        found = [result for result in results if result is not None]

    refresh = {target: ','.join(bad) for target, bad in found}
    for target in STALE_SEMANTIC_OBJECTS:
        if (out / target).is_file():
            refresh.setdefault(target, 'semantic-cache-stale')

    for target in sorted(refresh):
        print(target)
        print(f"DETAIL {target}: {refresh[target]}", file=os.sys.stderr)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
