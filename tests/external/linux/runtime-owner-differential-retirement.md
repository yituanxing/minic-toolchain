# Historical runtime owner differential retirement

The four non-focused runtime owner workflows for timekeeping, vsyscall, notifier, and build_policy were introduced during the September owner-localization phase. They rebuild the cumulative runtime owner chain and preserve before/after object evidence around one selected owner.

Later focused workflows, introduced on October 2, retain the same current-MiniC owner refresh chain, target-specific object/source/disassembly evidence, early relink, and QEMU frontier execution:

- `linux-runtime-timekeeping-focused-v0.yml`
- `linux-runtime-vsyscall-focused-v0.yml`
- `linux-runtime-notifier-focused-v0.yml`
- `linux-runtime-build-policy-focused-v0.yml`

The older non-focused YAML files are therefore archived as historical differential diagnostics. Their byte-for-byte workflow definitions remain available under `.github/workflows-disabled/`, so the original before/after experiments can be reconstructed if ever needed.

The newer focused workflows remain active. The staged `linux-runtime-owner-focused-v1.yml` matrix is a separate future consolidation and is not used as justification for this retirement until it is certified on the canonical runtime cache scope.
