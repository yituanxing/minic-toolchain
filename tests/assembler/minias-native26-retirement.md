# MiniAS native26 retirement contract

The historical `.github/workflows/minias-a0-native26.yml` workflow is retired from active GitHub Actions.

## Why it is no longer a valid active gate

The workflow consumes the historical `minias-a0-native26-progress` artifact from run `33245348705`.
That run no longer exposes any artifacts, so the workflow cannot be replayed from its own declared inputs.

It is not an artifact producer or a unique semantic oracle; it is an acceptance consumer for the old frozen native26 fixture.

## Replacement

`.github/workflows/minias-a0-gate-v1.yml` owns the canonical native-assembly hard gate.
Its `native35` job reconstructs the certified Linux 6.6.143 RISC-V native assembly workload in the same run from exact targeted Kbuild object commands, builds the current MiniAS, and requires 35/35 successful ELF64 RISC-V ET_REL outputs.

The canonical MiniAS contract requires the full 3536 workload at one repository HEAD:
3352 frozen C translation units + 149 ground-truth C translation units + 35 native assembly inputs.

## Cleanup consequence

`minias-a0-native26.yml` may be archived without deleting its YAML history.
Focused diagnostics, window replay, semantic-oracle, census, inventory, and sidecar workflows remain active until separately audited.
