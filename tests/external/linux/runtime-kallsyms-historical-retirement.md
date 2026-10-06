# Historical kallsyms runtime diagnostic retirement

This cleanup retires two runtime workflows whose diagnostic questions have been superseded by the maintained kallsyms first-fault chain.

## linux-runtime-kallsyms-breakpoint-v0.yml

This workflow compared the certified cached `kernel/kallsyms.o` against a freshly rebuilt MiniC `kernel/kallsyms.o`, relinked both variants, and compared the resulting runtime frontier.

Its final revision was commit `8388ea442d4497707fe1e74597b60009ebc692fd`. GitHub Actions run `37197958008` completed SUCCESS.

That run showed:

- fresh kallsyms rebuild: `KALLSYMS_REFRESH=PASS`
- baseline runtime: `FRONTIER_VERDICT=MOVED_LATER`, highest progress `vfs-caches-init`
- refreshed MiniC runtime: `FRONTIER_VERDICT=MOVED_LATER`, highest progress `net-ns-init`
- neither variant reported a current first fault

The maintained `linux-runtime-first-die-context-v0.yml` now performs a stronger three-way experiment. Run `37209311496` rebuilt and recorded cached, GCC, and current MiniC kallsyms objects, their hashes and relevant relocations, then independently relinked and executed all three variants through QEMU first-die capture.

The older two-way breakpoint workflow therefore no longer needs to remain an active Actions entry point.

## linux-runtime-kallsyms-rest-init-v0.yml

This workflow was a September frontier diagnostic that refreshed the then-current runtime owner set through libfdt, crc32, and kallsyms, linked generated kallsyms data, and ran the old arch-rest-init frontier classifier.

Its final revision was commit `cf28ed289bef613348508fce4b7d7b937b5a95bf`. GitHub Actions run `36660131870` ended FAILURE after successfully rebuilding its owner set and generated kallsyms image because its runtime classifier still observed the historical fault:

`FRONTIER_FAULT=load_page_fault:crc32_body+0xdd4`

That exact crc32 fault has since been retired as a historical frontier. On synchronized cleanup/runtime HEAD `86da8ee4bc029431939ca0e6206126dc3ac15840`, current generated-kallsyms run `37483502587` completed SUCCESS and reached first die at:

- `FIRST_DIE_HIT=YES`
- `EPC_NEAREST=fdt32_ld+0x94`
- `CAUSE_NAME=load-page-fault`
- `GET_SYMBOL_POS_FAULT=NO`
- `GENERATED_KALLSYMS_FIRST_DIE=PASS`

The maintained `linux-runtime-generated-kallsyms-first-die-v0.yml` therefore owns the current generated-kallsyms runtime question, while `linux-runtime-fdt-isolation-v1.yml` and `linux-runtime-fdt-ro-codegen-v0.yml` remain active because the current frontier is now in FDT code.

## Retirement decision

The following workflow files are preserved byte-for-byte under `.github/workflows-disabled/` and removed from the active Actions set:

- `linux-runtime-kallsyms-breakpoint-v0.yml`
- `linux-runtime-kallsyms-rest-init-v0.yml`

The following kallsyms diagnostics remain active:

- `linux-runtime-first-die-context-v0.yml` — cached/GCC/MiniC three-way first-fault comparison
- `linux-runtime-generated-kallsyms-first-die-v0.yml` — current generated-kallsyms first-die certification
- `linux-runtime-kallsyms-object-diagnose-v0.yml` — detailed object-level GCC/MiniC codegen and relocation evidence

No historical YAML, commits, Actions runs, or uploaded artifacts are deleted.
