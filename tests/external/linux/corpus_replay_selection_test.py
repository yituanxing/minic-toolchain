#!/usr/bin/env python3
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import corpus_replay

entries = [(index, f"obj{index}", f"tu{index}.i", f"src{index}.c") for index in range(10)]

assert corpus_replay.select_entries(entries, offset=0, limit=0, sample_count=0, indices="") == entries
assert [entry[0] for entry in corpus_replay.select_entries(
    entries, offset=2, limit=3, sample_count=0, indices=""
)] == [2, 3, 4]
assert [entry[0] for entry in corpus_replay.select_entries(
    entries, offset=0, limit=0, sample_count=4, indices=""
)] == [0, 3, 6, 9]
assert [entry[0] for entry in corpus_replay.select_entries(
    entries, offset=8, limit=1, sample_count=0, indices="1,7,4"
)] == [1, 7, 4]

for invalid in ("1,1", "12"):
    try:
        corpus_replay.select_entries(entries, offset=0, limit=0, sample_count=0, indices=invalid)
    except ValueError:
        pass
    else:
        raise AssertionError(f"expected invalid selection rejection: {invalid}")

print("PASS linux/corpus-replay-selection")
