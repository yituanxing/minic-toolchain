#!/usr/bin/env python3
"""Bounded, content-exact provenance audit for distributed Linux Image linkage.

All 3352 producer .o and Kbuild .cmd files must initially pass SHA256 in the
receiver.  After a successful make Image, Linux may regenerate SELinux headers,
ASN.1 tables, vDSO metadata, etc. and rebuild a small set of already restored
objects.  This audit never silently accepts changed bytes: each changed file
must be part of the pinned object manifest, and its corresponding object must
have an explicit successful MiniC wrapper 'pass' record from this receiver.
An absolute rebuild limit fails closed on unexpected broad invalidation.
"""
import hashlib
import pathlib
import sys

if len(sys.argv) != 7:
    raise SystemExit(
        "usage: audit.py OUT EXPECTED_SHA IMAGE_TRACE MANIFEST MAX_REBUILDS REPORT"
    )
out, sha_file, trace_file, manifest_file, max_rebuilds, report_file = sys.argv[1:]
out = pathlib.Path(out)
max_rebuilds = int(max_rebuilds)
if max_rebuilds < 0 or max_rebuilds > 100:
    raise SystemExit("DIST_IMAGE_AUDIT=FAIL invalid_rebuild_limit")

plan = set()
for line in pathlib.Path(manifest_file).read_text().splitlines():
    fields = line.split("\t")
    if len(fields) != 4 or not fields[2].endswith(".o"):
        raise SystemExit("DIST_IMAGE_AUDIT=FAIL malformed_manifest")
    plan.add(fields[2])
if len(plan) < 2500:
    raise SystemExit("DIST_IMAGE_AUDIT=FAIL incomplete_manifest")

compiled = set()
for line in pathlib.Path(trace_file).read_text(errors="replace").splitlines():
    if line.startswith("pass source=") and " output=" in line:
        compiled.add(line.rsplit(" output=", 1)[1].strip())
recompiled = compiled & plan

expected = {}
for line in pathlib.Path(sha_file).read_text().splitlines():
    if len(line) < 67 or line[64:66] not in ("  ", " *"):
        raise SystemExit("DIST_IMAGE_AUDIT=FAIL malformed_sha_record")
    checksum = line[:64]
    name = line[66:]
    if any(ch not in "0123456789abcdef" for ch in checksum):
        raise SystemExit("DIST_IMAGE_AUDIT=FAIL malformed_sha_hex")
    if name in expected:
        raise SystemExit("DIST_IMAGE_AUDIT=FAIL duplicate_sha_record")
    expected[name] = checksum
if len(expected) != 2 * len(plan):
    raise SystemExit("DIST_IMAGE_AUDIT=FAIL incomplete_sha_manifest")

def expected_object(name):
    if name.endswith(".o"):
        return name
    parent, sep, basename = name.rpartition("/")
    if not sep or not basename.startswith(".") or not basename.endswith(".o.cmd"):
        return None
    return parent + "/" + basename[1:-4]

changed = []
changed_objects = set()
for name, before in expected.items():
    relative = pathlib.PurePosixPath(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise SystemExit("DIST_IMAGE_AUDIT=FAIL unsafe_path")
    filename = out / name
    if not filename.is_file():
        raise SystemExit(f"DIST_IMAGE_AUDIT=FAIL missing_file={name}")
    h = hashlib.sha256()
    with filename.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    after = h.hexdigest()
    obj = expected_object(name)
    if obj is None or obj not in plan:
        raise SystemExit(f"DIST_IMAGE_AUDIT=FAIL unexpected_manifest_path={name}")
    if before != after:
        changed.append((name, obj, before, after))
        changed_objects.add(obj)

violations = sorted(changed_objects - recompiled)
if violations:
    raise SystemExit("DIST_IMAGE_AUDIT=FAIL changed_without_minic_pass=" + ",".join(violations))
if len(recompiled) > max_rebuilds:
    raise SystemExit(
        f"DIST_IMAGE_AUDIT=FAIL recompiles={len(recompiled)} limit={max_rebuilds}"
    )
if len(changed_objects) > max_rebuilds:
    raise SystemExit("DIST_IMAGE_AUDIT=FAIL changed_object_limit")
report = [
    f"DIST_IMAGE_PROVENANCE=PASS manifest_objects={len(plan)} "
    f"changed_files={len(changed)} changed_objects={len(changed_objects)} "
    f"traced_recompiles={len(recompiled)} reused_objects={len(plan)-len(recompiled)} "
    f"rebuild_limit={max_rebuilds}"
]
for name, obj, before, after in sorted(changed):
    report.append(f"REBUILT_VERIFIED object={obj} changed_file={name} before={before} after={after}")
pathlib.Path(report_file).write_text("\n".join(report) + "\n")
print(report[0])
