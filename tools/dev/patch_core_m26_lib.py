from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


def insert_before(path: str, anchor: str, payload: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(anchor)
    if count != 1:
        raise SystemExit(f"{path}: expected one insert anchor, found {count}: {anchor[:80]!r}")
    p.write_text(text.replace(anchor, payload + anchor, 1))
