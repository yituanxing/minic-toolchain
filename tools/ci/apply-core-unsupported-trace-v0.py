#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
needle = "return MINIC_CORE_LOWER_UNSUPPORTED;"
lines = text.splitlines(keepends=True)
count = sum(line.count(needle) for line in lines)
if count == 0:
    raise SystemExit("no Core unsupported returns found")

site = 0
out = []
manifest = []
for line_no, line in enumerate(lines, 1):
    occurrences = line.count(needle)
    if occurrences > 1:
        raise SystemExit(f"multiple unsupported returns on source line {line_no}")
    if occurrences == 1:
        site += 1
        indent = line[: len(line) - len(line.lstrip())]
        replacement = (
            "do {\n"
            f'{indent}    (void)fprintf(stderr, "CORE_UNSUPPORTED_TRACE helper=%s site={site} source_line={line_no}\\n", __func__);\n'
            f"{indent}    return MINIC_CORE_LOWER_UNSUPPORTED;\n"
            f"{indent}}} while (0);"
        )
        line = line.replace(needle, replacement, 1)
        lo = max(0, line_no - 4)
        hi = min(len(lines), line_no + 3)
        context = "".join(lines[lo:hi]).rstrip()
        manifest.append(f"SITE {site} SOURCE_LINE {line_no}\n{context}\n---\n")
    out.append(line)

if site != count:
    raise SystemExit(f"trace site mismatch: expected {count}, wrote {site}")
p.write_text("".join(out))
Path("/tmp/core-unsupported-sites-v0.txt").write_text("".join(manifest))
print(f"MINIC_CORE_UNSUPPORTED_TRACE_V0=APPLIED returns={site}")
