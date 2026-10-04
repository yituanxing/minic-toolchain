# Branch consolidation archive — 2026-10-04

This archive branch preserves the history that was previously spread across the repository's old branch refs before branch consolidation.

## Immutable snapshot

- Snapshot source workflow run: `37198279835`
- Snapshot artifact ID: `11302075865`
- Original branch-ref rows: `929`
- Unique tip commits: `890`
- Original `branches-before.tsv` SHA-256: `1b27df763c66c825c0f816f13958764e5eeee50ce403fc420cbb97b9532952a1`
- Synthetic archive DAG root/head before this metadata commit: `d3ee757ea81db25de31afee6064858a9f9e94381`

The synthetic archive DAG was built by making the old unique branch-tip commits parents of a short chain of archive commits. This preserves each old tip and its complete reachable history without trying to merge conflicting working trees.

## Branch-name to tip-SHA manifest

The exact original mapping is stored in:

`archive/branch-tips-2026-10-04.tsv.gz.b64`

Recover it with:

```sh
base64 -d archive/branch-tips-2026-10-04.tsv.gz.b64 | gzip -dc > /tmp/branch-tips-2026-10-04.tsv
sha256sum /tmp/branch-tips-2026-10-04.tsv
wc -l /tmp/branch-tips-2026-10-04.tsv
```

Expected results:

```text
1b27df763c66c825c0f816f13958764e5eeee50ce403fc420cbb97b9532952a1  /tmp/branch-tips-2026-10-04.tsv
929 /tmp/branch-tips-2026-10-04.tsv
```

To restore any deleted branch, look up its SHA in the manifest and create a new branch at that SHA.

## Canonical branches after consolidation

- `main`
- `agent/linux-expanded-kbuild-v0`
- `archive/all-progress-2026-10-04`

Do not delete the archive branch unless another permanent ref is first made to contain its full history.
