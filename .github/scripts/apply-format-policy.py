from pathlib import Path
import subprocess


def replace_once(path: Path, old: str, new: str) -> None:
    content = path.read_text(encoding="utf-8")
    count = content.count(old)
    if count != 1:
        raise RuntimeError(
            f"{path}: expected one replacement, found {count}: {old!r}"
        )
    path.write_text(content.replace(old, new), encoding="utf-8")


replace_once(
    Path("Makefile"),
    "format:\n\t@printf '%s\\n' \"format: formatter policy is not automated yet\"\n\n"
    "format-check:\n\t@printf '%s\\n' \"format-check: formatter policy is not automated yet\"\n",
    "format:\n\tCLANG_FORMAT=\"$${CLANG_FORMAT:-clang-format-18}\" \\\n"
    "\t\tbash tools/maintenance/run-format.sh write\n\n"
    "format-check:\n\tCLANG_FORMAT=\"$${CLANG_FORMAT:-clang-format-18}\" \\\n"
    "\t\tbash tools/maintenance/run-format.sh check\n",
)

full_gate = Path(".github/scripts/compiler-c0-full-gate.sh")
replace_once(
    full_gate,
    "source_inventory() {\n"
    "    sh tools/maintenance/check-production-source-inventory.sh\n"
    "}\n\n",
    "source_inventory() {\n"
    "    sh tools/maintenance/check-production-source-inventory.sh\n"
    "}\n\n"
    "format_check() {\n"
    "    CLANG_FORMAT=clang-format-18 bash tools/maintenance/run-format.sh check\n"
    "}\n\n",
)
replace_once(
    full_gate,
    "printf '%s\\n' 'Phase 1: source inventory, tool preparation, and three host configurations'\n"
    "start_gate source-inventory source_inventory\n",
    "printf '%s\\n' 'Phase 1: source inventory, format policy, tool preparation, and three host configurations'\n"
    "start_gate source-inventory source_inventory\n"
    "start_gate format-check format_check\n",
)

subprocess.run(
    ["bash", "tools/maintenance/run-format.sh", "write"],
    check=True,
)

for temporary in (
    Path(".github/scripts/apply-format-policy.py"),
    Path(".github/workflows/apply-format-policy.yml"),
    Path(".github/format-policy.ready"),
):
    temporary.unlink(missing_ok=True)
