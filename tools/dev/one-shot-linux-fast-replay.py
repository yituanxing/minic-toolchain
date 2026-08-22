from pathlib import Path

replay_path = Path("tests/external/linux/corpus_replay.py")
text = replay_path.read_text()

args_anchor = '''    parser.add_argument("--jobs", required=True, type=int)\n    return parser.parse_args()\n\n\ndef file_sha256(path: Path) -> str:\n'''
args_replacement = '''    parser.add_argument("--jobs", required=True, type=int)\n    parser.add_argument("--offset", type=int, default=0)\n    parser.add_argument("--limit", type=int, default=0)\n    parser.add_argument("--sample-count", type=int, default=0)\n    parser.add_argument("--indices", default="")\n    return parser.parse_args()\n\n\ndef parse_indices(raw: str) -> list[int]:\n    if not raw.strip():\n        return []\n    values: list[int] = []\n    seen: set[int] = set()\n    for token in raw.split(","):\n        token = token.strip()\n        if not token:\n            raise ValueError("indices contains an empty entry")\n        value = int(token)\n        if value < 0:\n            raise ValueError("indices must be non-negative")\n        if value in seen:\n            raise ValueError(f"duplicate configured TU index: {value}")\n        seen.add(value)\n        values.append(value)\n    return values\n\n\ndef select_entries(\n    entries: list[tuple[int, str, str, str]],\n    *,\n    offset: int,\n    limit: int,\n    sample_count: int,\n    indices: str,\n) -> list[tuple[int, str, str, str]]:\n    if offset < 0 or limit < 0 or sample_count < 0:\n        raise ValueError("offset, limit, and sample-count must be non-negative")\n\n    requested = parse_indices(indices)\n    if requested:\n        by_index = {entry[0]: entry for entry in entries}\n        missing = [index for index in requested if index not in by_index]\n        if missing:\n            raise ValueError(\n                "requested configured TU indices are absent from frozen corpus: "\n                + ",".join(str(index) for index in missing)\n            )\n        selected = [by_index[index] for index in requested]\n    else:\n        if offset >= len(entries):\n            selected = []\n        else:\n            stop = len(entries) if limit == 0 else min(len(entries), offset + limit)\n            selected = entries[offset:stop]\n\n    if sample_count == 0 or sample_count >= len(selected):\n        return selected\n    if sample_count == 1:\n        return selected[:1]\n\n    last = len(selected) - 1\n    positions = [sample * last // (sample_count - 1) for sample in range(sample_count)]\n    return [selected[position] for position in positions]\n\n\ndef file_sha256(path: Path) -> str:\n'''
if text.count(args_anchor) != 1:
    raise SystemExit("corpus replay argument anchor is not unique")
text = text.replace(args_anchor, args_replacement, 1)

manifest_anchor = '''    if not entries:\n        raise SystemExit("frozen corpus manifest is empty")\n\n    args.work.mkdir(parents=True, exist_ok=True)\n'''
manifest_replacement = '''    if not entries:\n        raise SystemExit("frozen corpus manifest is empty")\n    try:\n        entries = select_entries(\n            entries,\n            offset=args.offset,\n            limit=args.limit,\n            sample_count=args.sample_count,\n            indices=args.indices,\n        )\n    except (TypeError, ValueError) as error:\n        raise SystemExit(f"invalid frozen corpus selection: {error}") from error\n    if not entries:\n        raise SystemExit("frozen corpus selection is empty")\n    selected_manifest = "".join(\n        f"{index}\\t{obj}\\t{preprocessed}\\t{source}\\n"\n        for index, obj, preprocessed, source in entries\n    )\n\n    args.work.mkdir(parents=True, exist_ok=True)\n'''
if text.count(manifest_anchor) != 1:
    raise SystemExit("corpus replay manifest anchor is not unique")
text = text.replace(manifest_anchor, manifest_replacement, 1)

selected_anchor = '''    (args.work / "batch-results.json").write_text(json.dumps(results, indent=2, sort_keys=True))\n    (args.work / "selected-tus.txt").write_text(manifest.read_text())\n    corpus_manifest = args.corpus / "tu-manifest.txt"\n'''
selected_replacement = '''    (args.work / "batch-results.json").write_text(json.dumps(results, indent=2, sort_keys=True))\n    (args.work / "selected-tus.txt").write_text(selected_manifest)\n    corpus_manifest = args.corpus / "tu-manifest.txt"\n'''
if text.count(selected_anchor) != 1:
    raise SystemExit("corpus replay selected manifest anchor is not unique")
text = text.replace(selected_anchor, selected_replacement, 1)

bytes_anchor = '''    corpus_bytes = sum(path.stat().st_size for path in input_root.rglob("*.i"))\n    replay_seconds = time.monotonic() - started\n    summary_lines = [\n        "LINUX_BATCH_SUMMARY",\n        "corpus=frozen",\n        f"minic_sha256={minic_sha256}",\n        f"corpus_bytes={corpus_bytes}",\n        f"replay_seconds={replay_seconds:.3f}",\n'''
bytes_replacement = '''    corpus_bytes = sum(path.stat().st_size for path in input_root.rglob("*.i"))\n    selected_corpus_bytes = sum((input_root / entry[2]).stat().st_size for entry in entries)\n    replay_seconds = time.monotonic() - started\n    summary_lines = [\n        "LINUX_BATCH_SUMMARY",\n        "corpus=frozen",\n        f"minic_sha256={minic_sha256}",\n        f"corpus_bytes={corpus_bytes}",\n        f"selected_corpus_bytes={selected_corpus_bytes}",\n        f"replay_seconds={replay_seconds:.3f}",\n'''
if text.count(bytes_anchor) != 1:
    raise SystemExit("corpus replay bytes anchor is not unique")
text = text.replace(bytes_anchor, bytes_replacement, 1)

print_anchor = '''    print(\n        f"LINUX_BATCH_CORPUS_REPLAY selected={len(results)} bytes={corpus_bytes} "\n        f"seconds={replay_seconds:.3f} minic_sha256={minic_sha256}"\n    )\n'''
print_replacement = '''    print(\n        f"LINUX_BATCH_CORPUS_REPLAY selected={len(results)} "\n        f"selected_bytes={selected_corpus_bytes} corpus_bytes={corpus_bytes} "\n        f"seconds={replay_seconds:.3f} minic_sha256={minic_sha256}"\n    )\n'''
if text.count(print_anchor) != 1:
    raise SystemExit("corpus replay summary print anchor is not unique")
text = text.replace(print_anchor, print_replacement, 1)
replay_path.write_text(text)

selection_test = '''#!/usr/bin/env python3\nfrom pathlib import Path\nimport sys\n\nsys.path.insert(0, str(Path(__file__).resolve().parent))\nimport corpus_replay\n\nentries = [(index, f"obj{index}", f"tu{index}.i", f"src{index}.c") for index in range(10)]\n\nassert corpus_replay.select_entries(entries, offset=0, limit=0, sample_count=0, indices="") == entries\nassert [entry[0] for entry in corpus_replay.select_entries(\n    entries, offset=2, limit=3, sample_count=0, indices=""\n)] == [2, 3, 4]\nassert [entry[0] for entry in corpus_replay.select_entries(\n    entries, offset=0, limit=0, sample_count=4, indices=""\n)] == [0, 3, 6, 9]\nassert [entry[0] for entry in corpus_replay.select_entries(\n    entries, offset=8, limit=1, sample_count=0, indices="1,7,4"\n)] == [1, 7, 4]\n\nfor invalid in ("1,1", "12"):\n    try:\n        corpus_replay.select_entries(entries, offset=0, limit=0, sample_count=0, indices=invalid)\n    except ValueError:\n        pass\n    else:\n        raise AssertionError(f"expected invalid selection rejection: {invalid}")\n\nprint("PASS linux/corpus-replay-selection")\n'''
Path("tests/external/linux/corpus_replay_selection_test.py").write_text(selection_test)

workflow_path = Path(".github/workflows/linux-batch-pressure.yml")
workflow = workflow_path.read_text()
workflow = workflow.replace(
    "# Replay trigger: promoted static record designator candidate.\n",
    "# PRs replay a deterministic 64-TU sample; workflow_dispatch keeps full/focused qualification available.\n",
    1,
)
path_anchor = "      - 'tests/external/linux/corpus_replay.py'\n"
if workflow.count(path_anchor) != 1:
    raise SystemExit("workflow corpus replay path anchor is not unique")
workflow = workflow.replace(
    path_anchor,
    path_anchor + "      - 'tests/external/linux/corpus_replay_selection_test.py'\n",
    1,
)
input_anchor = '''      minic_jobs:\n        description: Concurrent MiniC processes\n        required: false\n        default: '4'\n      indices:\n'''
input_replacement = '''      minic_jobs:\n        description: Concurrent MiniC processes\n        required: false\n        default: '4'\n      sample_count:\n        description: Deterministic sample count from the selected frozen window; 0 means every selected TU\n        required: false\n        default: '0'\n      indices:\n'''
if workflow.count(input_anchor) != 1:
    raise SystemExit("workflow input anchor is not unique")
workflow = workflow.replace(input_anchor, input_replacement, 1)
workflow = workflow.replace(
    '''      - name: Restore frozen focused Linux corpus\n        if: github.event_name != 'workflow_dispatch'\n        id: linux-corpus-cache\n''',
    '''      - name: Restore frozen focused Linux corpus\n        id: linux-corpus-cache\n''',
    1,
)
build_anchor = '''      - name: Build exact MiniC candidate\n        run: |\n          set -Eeuo pipefail\n          make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/linux-batch-compiler\n'''
build_replacement = '''      - name: Validate frozen replay selector\n        run: python3 tests/external/linux/corpus_replay_selection_test.py\n\n      - name: Build exact MiniC candidate\n        run: |\n          set -Eeuo pipefail\n          make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/linux-batch-compiler\n'''
if workflow.count(build_anchor) != 1:
    raise SystemExit("workflow build anchor is not unique")
workflow = workflow.replace(build_anchor, build_replacement, 1)
env_anchor = '''          BATCH_LIMIT: ${{ github.event_name == 'workflow_dispatch' && inputs.limit || '500' }}\n          BATCH_JOBS: ${{ github.event_name == 'workflow_dispatch' && inputs.minic_jobs || '4' }}\n          BATCH_INDICES: ${{ github.event_name == 'workflow_dispatch' && inputs.indices || '' }}\n'''
env_replacement = '''          BATCH_LIMIT: ${{ github.event_name == 'workflow_dispatch' && inputs.limit || '500' }}\n          BATCH_JOBS: ${{ github.event_name == 'workflow_dispatch' && inputs.minic_jobs || '4' }}\n          BATCH_SAMPLE_COUNT: ${{ github.event_name == 'workflow_dispatch' && inputs.sample_count || '64' }}\n          BATCH_INDICES: ${{ github.event_name == 'workflow_dispatch' && inputs.indices || '' }}\n'''
if workflow.count(env_anchor) != 1:
    raise SystemExit("workflow batch env anchor is not unique")
workflow = workflow.replace(env_anchor, env_replacement, 1)
replay_anchor = '''          if [[ "$GITHUB_EVENT_NAME" != workflow_dispatch && "$CORPUS_CACHE_HIT" == true ]]; then\n            printf '%s\\n' 'LINUX_BATCH_CORPUS hit: replaying frozen preprocessed inputs'\n            python3 tests/external/linux/corpus_replay.py \\\n              --minic "$GITHUB_WORKSPACE/build/linux-batch-compiler/bin/minic" \\\n              --corpus "$GITHUB_WORKSPACE/build/linux-focus-corpus" \\\n              --work "$GITHUB_WORKSPACE/build/linux-batch" \\\n              --jobs "$BATCH_JOBS"\n            batch_status=$?\n'''
replay_replacement = '''          if [[ "$CORPUS_CACHE_HIT" == true ]]; then\n            printf 'LINUX_BATCH_CORPUS hit: replaying frozen inputs offset=%s limit=%s sample=%s indices=%s\\n' \\\n              "$BATCH_OFFSET" "$BATCH_LIMIT" "$BATCH_SAMPLE_COUNT" "$BATCH_INDICES"\n            python3 tests/external/linux/corpus_replay.py \\\n              --minic "$GITHUB_WORKSPACE/build/linux-batch-compiler/bin/minic" \\\n              --corpus "$GITHUB_WORKSPACE/build/linux-focus-corpus" \\\n              --work "$GITHUB_WORKSPACE/build/linux-batch" \\\n              --jobs "$BATCH_JOBS" \\\n              --offset "$BATCH_OFFSET" \\\n              --limit "$BATCH_LIMIT" \\\n              --sample-count "$BATCH_SAMPLE_COUNT" \\\n              --indices "$BATCH_INDICES"\n            batch_status=$?\n'''
if workflow.count(replay_anchor) != 1:
    raise SystemExit("workflow replay anchor is not unique")
workflow = workflow.replace(replay_anchor, replay_replacement, 1)
workflow_path.write_text(workflow)
