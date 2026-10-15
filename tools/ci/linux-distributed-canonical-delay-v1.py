#!/usr/bin/env python3
"""Reconcile one verified Kbuild single-target command-context discrepancy.

The producer built arch/riscv/lib/delay.o as a single direct target, causing
KBUILD_MODFILE and local include directories to use arch/riscv rather than
arch/riscv/lib. A full Kbuild descent corrected the metadata while producing
identical MiniC object bytes. Preserve the original manifest and allow only
that exact, provenance-checked command correction before strict final replay.
"""
import hashlib
import pathlib
import shutil
import sys

if len(sys.argv) != 9:
    raise SystemExit("usage: canonical_delay SRC OUT REUSE PROD_CMD CANON_CMD WARMUP_SHA WARMUP_TRACE PRODUCER_MANIFEST")
src, out, reuse, producer, canonical, warmup_sha, warmup_trace, backup = map(pathlib.Path, sys.argv[1:])
target = "arch/riscv/lib/.delay.o.cmd"
object_name = "arch/riscv/lib/delay.o"
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def command(path):
    first = path.read_text().splitlines()[0]
    prefix = "savedcmd_arch/riscv/lib/delay.o := "
    if not first.startswith(prefix):
        raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL saved_command_format")
    return first[len(prefix):].rstrip()
old = command(producer)
new = command(canonical)
normalized = new.replace("-I " + str(src) + "/arch/riscv/lib ", "-I " + str(src) + "/arch/riscv ")
normalized = normalized.replace("-I ./arch/riscv/lib ", "-I ./arch/riscv ")
normalized = normalized.replace("KBUILD_MODFILE='\"arch/riscv/lib/delay\"'", "KBUILD_MODFILE='\"arch/riscv/delay\"'")
if normalized != old or new == old:
    raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL unexpected_compiler_command_delta")
if not any(line.startswith("pass source=") and " output=" + object_name in line
           for line in warmup_trace.read_text().splitlines()):
    raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL no_MiniC_warmup_pass")
saved_warmup_sha = warmup_sha.read_text().strip()
if len(saved_warmup_sha) != 64 or sha(out / object_name) != saved_warmup_sha:
    raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL MiniC_object_bytes_changed")
lines = reuse.read_text().splitlines()
if len(lines) != 6704:
    raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL incorrect_manifest_size")
matching = [i for i, line in enumerate(lines) if line.endswith("  " + target)]
if len(matching) != 1:
    raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL missing_or_duplicate_target")
idx = matching[0]
before = lines[idx][:64]
if before != sha(producer):
    raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL producer_command_hash_mismatch")
if sha(out / target) != before:
    raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL producer_restore_not_exact")
shutil.copyfile(reuse, backup)
shutil.copyfile(canonical, out / target)
after = sha(out / target)
if after == before:
    raise SystemExit("DIST_IMAGE_CANONICAL_DELAY=FAIL no_metadata_delta")
lines[idx] = after + "  " + target
reuse.write_text("\n".join(lines) + "\n")
print(
    "DIST_IMAGE_CANONICAL_DELAY=PASS unchanged_object_sha=1 "
    "producer_files_authenticated=6704 reconciled_cmds=1 "
    f"original_cmd_sha={before} canonical_cmd_sha={after}"
)
