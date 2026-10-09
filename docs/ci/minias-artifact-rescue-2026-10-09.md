# MiniAS historical fixture rescue (2026-10-09)

The original 7-shard Linux 3352-TU corpus, the MiniAS Linux sidecar and the
ground-truth C149 input currently depend on **nine Actions artifacts from three
historical runs**. They are not permanent. Do not substitute a fresh producer
run merely because it yields the same filenames.

Pinned inventory: `tools/ci/minias-historical-artifacts-v1.json`. It records
each original GitHub artifact ID, originating run, original **outer artifact ZIP
SHA256** returned by GitHub's artifact API on 2026-10-09, size and expiry.
The seven frozen tar SHA256 values are separately corroborated by
`assembler/corpus/linux-6.6.143-rv64-gcc13.3.0-v1.md`. The sidecar inner
`kernel/config_data.gz` digest is independently pinned. The C149 original
tar digest is only self-reported by the artifact producer: its newly pinned
independent **outer ZIP** digest therefore matters.

- Frozen corpus (7 ZIPs, approximately 1.16 GB total): run `33186855250`;
  expiry 2026-11-26 15:47:26 UTC.
- Linux sidecar: run `33237304382`; expiry 2026-11-27 05:56:34 UTC.
- C149: run `33248985705`; expiry 2026-11-27 10:57:21 UTC.

This is not a completed durable mirror, and M0 does not download gigabytes.
The archived producer workflows
`.github/workflows-disabled/minias-linux-sidecars.yml` and
`.github/workflows-disabled/minias-linux-ground-truth-inventory.yml`
are preserved for reconstruction analysis. Exact old `.i` corpus byte
identity **cannot be inferred** from rerunning Linux Kbuild on a new machine.

## Explicit salvage operation (before expiry)

```bash
python3 tools/ci/minias_artifact_rescue_v1.py --self-test
export GH_TOKEN='<token with actions:read access>'
python3 tools/ci/minias_artifact_rescue_v1.py --download --output-dir ./minias-salvage-2026-10
python3 tools/ci/minias_artifact_rescue_v1.py --verify-dir ./minias-salvage-2026-10
```

The ZIPs are intentionally *not* written into Git, an Actions cache, or a
short-lived Actions upload. Store verified originals in a durable,
access-controlled destination (for example, a release asset store) and retain
the JSON pins alongside them. For a public repository, publishing assets to
a public Release is an additional explicit decision. The rescue tool does not
publish anything or automatically create a costly recurring Workflow.

## Migration acceptance

1. Download the **original nine** artifacts before their recorded expiries,
   check the source-run IDs and verify all ZIP/inner hashes using this tool.
2. Replicate the verified bytes to storage with durable retention, then fetch
   them independently and reverify against the same pinned JSON.
3. Only then modify the MiniAS workflows to read from the new location.
   Preserve source selection, shard counts, sidecar checks and C149/native35
   oracle contracts. Verify real MiniAS runs, not just T0 M0 structure.
4. If an original archive is lost, a regenerated `.i` set must have a
   separately independently checked per-file manifest and a **new** corpus
   identity. Do not claim that passing the regenerated tests retroactively
   proves the old exact inputs.

The existing MiniAS job bodies, trigger tags, artifact IDs, and status
expectations are intentionally unchanged by this provenance/rescue step.
