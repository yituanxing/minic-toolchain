# FDT GCC swap retirement contract

`.github/workflows/linux-runtime-fdt-isolation-v1.yml` supersedes the historical `linux-runtime-fdt-ro-gcc-swap-v0.yml` runtime swap experiment.

Both workflows refresh the same 19 current MiniC runtime owner objects before replacing only `lib/fdt_ro.o` with the GCC-built owner. The V1 isolation workflow then performs the same generated-kallsyms relink and extends the runtime evidence with early-boot breadcrumbs and first-die tracing.

The separate `linux-runtime-fdt-ro-codegen-v0.yml` workflow remains active and retains the exact-context MiniC-versus-GCC `fdt_ro.o` static codegen, nm, objdump and readelf comparison.

Therefore the older GCC-swap workflow can be archived without losing either the runtime isolation experiment or the static MiniC/GCC codegen evidence.
