# Linux 6.6.143: one-target current MiniC replay from immutable GNU inputs

This is an explicit, one-shot development workflow, **not a three-ISA Linux
acceptance gate**. It removes the dependency on interactive `workflow_dispatch`
while retaining the historical original GNU `.i/.o.cmd` SHA contract.

## Preconditions

1. Verify that the earlier source-only workflow run completed successfully.
   For example, source run `42366441631` must be verified as successful before
   being used here; its creation alone does not certify the inputs.
2. Confirm the selected architecture has all seven
   `minic-linux-seven-input-${target}-{0..6}` artifacts, all carrying the
   exact original Linux 6.6.143 owner identities and historical `.i` SHA.
   The producer uses historical fixtures for **all 9,589 owners** and protects
   the Kconfig and GNU C-owner manifest SHA.
3. Leave `tools/ci/linux-three-isa-seven-source-refresh-trigger.txt`
   untouched. Its purpose is the independent source-only refresh.

## Trigger

In one new **last HEAD commit**, create or modify
`tools/ci/linux-three-isa-seven-candidate-trigger.txt` with exactly four lines:

```text
target=aarch64
input_run=42366441631
nonce=20261107-2
mode=candidate-only
```

Replace `target` with exactly one of `x86_64`, `aarch64`, `loongarch64`, and
replace `input_run` with a verified successful source-export Actions run ID.
Change `nonce` for any later replay. Push or PR workflows check that **this
marker changed in the exact HEAD**; ordinary CI changes cannot activate the
expensive runner. An invalid marker or simultaneous source-regeneration,
candidate-object reuse or legacy marker causes hard failure.

The gate launches only seven current-source MiniC C/GNU-AS workers for the
selected target. Each worker rechecks its source run's full historical
`owner -> input_sha256` pairs, configuration SHA, owner-manifest SHA, and shard
identity before even installing its GNU assembler. Existing cache keys remain
bound to exact source bytes and compiler identity. The one matching GNU Kbuild
receiver runs only after all seven genuine C-object artifacts are present.

The result stage deliberately refuses to describe one architecture as full
three-ISA acceptance. `-O*`, stack protector and other original GNU options
remain unqualified until independently certified, even if QEMU P0/P1 boots.
