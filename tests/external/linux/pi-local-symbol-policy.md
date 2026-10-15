# PI local-symbol semantic policy

`.github/workflows/linux-runtime-pi-local-symbol-v0.yml` validates PI address lowering against the active MiniC runtime profile.

The local assembler symbol must always lower with `lla`.

For external/global symbols the expected pseudo-instruction depends on the profile:

- selective PI-local-symbol policy: extern/global remains `la`;
- Linux static-PCREL policy already installed: the stronger global-address emitter is intentionally preserved, so extern/global also uses `lla`.

The workflow reads the applied profile marker instead of assuming that the selective policy is always active. This avoids treating the intentional `static-pcrel-preserved` mode as a compiler regression.
