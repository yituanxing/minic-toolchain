#!/usr/bin/env python3
"""Finish enum semantic ownership from the already-landed enum-id identity slice.

v1 proved the identity change but its productizer shell could continue after a failed
materialization because errexit is suppressed for functions executed under `if ! ...`.
This v2 deliberately reuses only v1's stage-2+ transformations and refuses to succeed
unless the complete ownership slice appears in the working-tree manifest.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
v1_path = ROOT / "tools/dev/materialize-enum-type-ownership-v1.py"
source = v1_path.read_text()

stage2_marker = "# 2. Delete whole-program enum repair and add one canonical semantic query."
prelude_marker = "# 1. Enum MinicType is identity only. Compatible integer facts live on MinicEnum."
if stage2_marker not in source or prelude_marker not in source:
    raise SystemExit("v1 materializer markers are unavailable")

# v1 was authored against the equivalent multi-line enum accessor. Current canonical ast.c
# compacted it to a conditional expression. Normalize that spelling before reusing stage 2 so the
# semantic transformation is insensitive to this formatting-only difference.
ast_path = ROOT / "src/frontend/ast.c"
ast_text = ast_path.read_text()
compact_accessor = """const MinicEnum *minic_c0_program_enum(const MinicC0Program *program, MinicEnumId enum_id) {
    return program != NULL && enum_id < program->enum_count ? &program->enums[enum_id] : NULL;
}
"""
expanded_accessor = """const MinicEnum *minic_c0_program_enum(const MinicC0Program *program, MinicEnumId enum_id) {
    if (program == NULL || enum_id >= program->enum_count) {
        return NULL;
    }
    return &program->enums[enum_id];
}
"""
if compact_accessor in ast_text:
    ast_path.write_text(ast_text.replace(compact_accessor, expanded_accessor, 1))
elif expanded_accessor not in ast_text:
    raise SystemExit("ast.c: unsupported enum accessor spelling")

# Reuse helper functions/imports, skip stage 1 because current canonical head already has it.
prelude = source.split(prelude_marker, 1)[0]
stage2_and_later = stage2_marker + source.split(stage2_marker, 1)[1]
exec(compile(prelude + stage2_and_later, str(v1_path) + ":stage2", "exec"), globals(), globals())

# Identity half must already be canonical before this slice starts.
type_h = (ROOT / "src/frontend/type.h").read_text()
type_c = (ROOT / "src/frontend/type.c").read_text()
parser_enum = (ROOT / "src/frontend/parser_enum.c").read_text()
if "MinicType minic_type_enum(MinicEnumId enum_id);" not in type_h:
    raise SystemExit("enum-id identity prerequisite missing from type.h")
if "MinicType minic_type_enum(MinicEnumId enum_id)" not in type_c:
    raise SystemExit("enum-id identity prerequisite missing from type.c")
if "minic_type_enum(enum_id," in parser_enum:
    raise SystemExit("parser still embeds compatible integer facts in enum type")

# A coherent replacement may create new focused fixtures. Plain `git diff --name-only` only sees
# tracked paths, so include untracked, non-ignored paths in the manifest as well.
changed = set(
    subprocess.check_output(
        ["git", "diff", "--name-only", "--", "src", "tests/compiler/c0"],
        cwd=ROOT,
        text=True,
    ).splitlines()
)
changed.update(
    subprocess.check_output(
        [
            "git",
            "ls-files",
            "--others",
            "--exclude-standard",
            "--",
            "src",
            "tests/compiler/c0",
        ],
        cwd=ROOT,
        text=True,
    ).splitlines()
)
required = {
    "src/frontend/ast.c",
    "src/frontend/ast.h",
    "src/frontend/ast_verifier.c",
    "src/frontend/const_eval.c",
    "src/frontend/expression_semantics.c",
    "src/frontend/parser_expression.c",
    "src/frontend/parser_statement.c",
    "src/target/data_layout.c",
    "src/target/target_info.c",
    "src/target/target_info.h",
    "src/target/riscv64/codegen_internal.h",
    "src/target/riscv64/codegen_support.c",
    "src/target/riscv64/codegen_expression.c",
    "src/target/riscv64/codegen_statement.c",
    "src/target/riscv64/core_codegen.c",
    "tests/compiler/c0/enum_forward_completion.c",
    "tests/compiler/c0/run-enum-forward-completion.sh",
    "tests/compiler/c0/run.sh",
}
missing = sorted(required - changed)
if missing:
    raise SystemExit("enum ownership materialization incomplete; missing changed paths: " + ", ".join(missing))

ast_c = (ROOT / "src/frontend/ast.c").read_text()
if "minic_refresh_program_enum_types" in ast_c or "minic_refresh_enum_type" in ast_c:
    raise SystemExit("whole-program enum repair survived v2")
if "minic_c0_type_effective_integer_type" not in ast_c:
    raise SystemExit("canonical effective integer query was not materialized")

print("ENUM_TYPE_OWNERSHIP_V2_MANIFEST_OK")
for path in sorted(changed):
    print(path)
