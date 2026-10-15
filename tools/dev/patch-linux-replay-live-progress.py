#!/usr/bin/env python3
"""Make frozen Linux replay report each TU as soon as it completes."""

from pathlib import Path

PATH = Path("tests/external/linux/corpus_replay.py")
MARKER = "LINUX_BATCH_PROGRESS"
TRACE_MARKER = "LINUX_FAST_MINIC_STDERR"


def main() -> int:
    text = PATH.read_text()

    if "import os\n" not in text:
        import_anchor = "import json\nimport re\n"
        if text.count(import_anchor) != 1:
            raise SystemExit(f"live-progress import anchor count={text.count(import_anchor)}")
        text = text.replace(import_anchor, "import json\nimport os\nimport re\n", 1)

    if TRACE_MARKER not in text:
        stderr_anchor = '''        stderr_path.write_text(proc.stderr)\n        if proc.returncode == 0 and asm_path.is_file() and asm_path.stat().st_size > 0:\n'''
        stderr_replacement = '''        stderr_path.write_text(proc.stderr)\n        if os.environ.get("GITHUB_JOB") == "fast-frontier" and proc.stderr:\n            for stderr_line in proc.stderr.splitlines():\n                if "CORE_FAST_TRACE" in stderr_line:\n                    print(\n                        f"LINUX_FAST_MINIC_STDERR index={index} {stderr_line}",\n                        flush=True,\n                    )\n        if proc.returncode == 0 and asm_path.is_file() and asm_path.stat().st_size > 0:\n'''
        if text.count(stderr_anchor) != 1:
            raise SystemExit(f"fast-stderr anchor count={text.count(stderr_anchor)}")
        text = text.replace(stderr_anchor, stderr_replacement, 1)

    if MARKER not in text:
        old = '''    results: list[dict[str, object]] = []\n    with ThreadPoolExecutor(max_workers=args.jobs) as pool:\n        futures = [pool.submit(compile_one, entry) for entry in entries]\n        for future in as_completed(futures):\n            results.append(future.result())\n    results.sort(key=lambda row: int(row["index"]))\n'''
        new = '''    results: list[dict[str, object]] = []\n    with ThreadPoolExecutor(max_workers=args.jobs) as pool:\n        futures = [pool.submit(compile_one, entry) for entry in entries]\n        for future in as_completed(futures):\n            row = future.result()\n            results.append(row)\n            message = str(row["message"]).replace("\\t", " ").replace("\\n", " ")\n            total_lines = "-" if row["total_lines"] is None else str(row["total_lines"])\n            first_error_line = "-" if row["first_error_line"] is None else str(row["first_error_line"])\n            locator = "-" if row["first_error_source"] is None else str(row["first_error_source"])\n            progress = "-" if row["progress_ratio"] is None else f'{float(row["progress_ratio"]) * 100.0:.2f}%'\n            print(\n                "LINUX_BATCH_PROGRESS "\n                f'index={row["index"]} status={row["status"]} '\n                f'input={row["input"]} total_lines={total_lines} '\n                f'first_error_line={first_error_line} progress={progress} locator={locator} '\n                f'seconds={float(row["seconds"]):.3f} message={message}',\n                flush=True,\n            )\n    results.sort(key=lambda row: int(row["index"]))\n'''
        count = text.count(old)
        if count != 1:
            raise SystemExit(f"live-progress anchor count={count}")
        text = text.replace(old, new, 1)

    PATH.write_text(text)
    print("Linux replay live progress + first-error metrics + fast stderr applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
