#!/usr/bin/env python3
"""Make frozen Linux replay report each TU as soon as it completes."""

from pathlib import Path

PATH = Path("tests/external/linux/corpus_replay.py")
MARKER = "LINUX_BATCH_PROGRESS"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("Linux replay live progress already applied")
        return 0

    old = '''    results: list[dict[str, object]] = []\n    with ThreadPoolExecutor(max_workers=args.jobs) as pool:\n        futures = [pool.submit(compile_one, entry) for entry in entries]\n        for future in as_completed(futures):\n            results.append(future.result())\n    results.sort(key=lambda row: int(row["index"]))\n'''
    new = '''    results: list[dict[str, object]] = []\n    with ThreadPoolExecutor(max_workers=args.jobs) as pool:\n        futures = [pool.submit(compile_one, entry) for entry in entries]\n        for future in as_completed(futures):\n            row = future.result()\n            results.append(row)\n            message = str(row["message"]).replace("\\t", " ").replace("\\n", " ")\n            print(\n                "LINUX_BATCH_PROGRESS "\n                f'index={row["index"]} status={row["status"]} '\n                f'input={row["input"]} seconds={float(row["seconds"]):.3f} '\n                f"message={message}",\n                flush=True,\n            )\n    results.sort(key=lambda row: int(row["index"]))\n'''
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"live-progress anchor count={count}")
    PATH.write_text(text.replace(old, new, 1))
    print("Linux replay live progress applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
