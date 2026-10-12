#!/usr/bin/env python3
"""Guard MiniAS 3536 semantic oracle consolidation without weakening existing jobs.

Two independent full Git blob archives are the golden contract. The existing
6 focused/window jobs must remain byte-identical. The four semantic jobs keep
everything from their first executable job property onward; only their
job-level dispatch predicates are replaced with a dedicated semantic mode.
"""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".github/workflows/minias-a0-focused-diagnostics-v1.yml"
ARCH = ROOT / ".github/workflows-disabled"
FOCUSED_SHA = "93330555c3913fb78ac1c67d180f2a70dabe951f"
SEMANTIC_SHA = {
    "agent/linux-expanded-kbuild-v0": "519b4b6fa9f3dd6e344761a7e7f6cbbd1de76831",
    "agent/linux-perf-boolean-domain-v1": "96bb3bf93fac928613f944b0042ddbac1c4c0280",
}
FOCUSED = ("real16", "frontier", "vector33", "resolve", "window-shard", "window")
LINUX_SINGLE_ARCHIVE_SHA = "9e2d0a7fa5e1d8cc853c1df33c49cc4030b6a133"
SEMANTIC = ("c-shards", "c149", "native35", "aggregate")

def sha(path):
    return subprocess.check_output(["git", "hash-object", str(path)], text=True).strip()

def jobs(source):
    segment = source.split("\njobs:\n", 1)[1]
    markers = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$", segment))
    return {
        m.group(1): segment[m.start():markers[i+1].start() if i+1<len(markers) else len(segment)].strip()
        for i,m in enumerate(markers)
    }

def executable(body):
    """Ignore only the job ID and its initial if: predicate, including folded if."""
    m = re.search(r"(?m)^    (?:strategy|runs-on|needs):", body)
    if m is None:
        raise AssertionError("job has no nonconditional executable property")
    return body[m.start():].strip()

def main():
    branch = os.environ.get("GITHUB_REF_NAME", "agent/linux-expanded-kbuild-v0")
    if branch not in SEMANTIC_SHA:
        raise AssertionError(f"unexpected branch {branch}")
    old_focus = ARCH / "minias-a0-focused-diagnostics-v1.yml"
    old_semantics = ARCH / "minias-semantic-oracle3536.yml"
    if sha(old_focus) != FOCUSED_SHA or sha(old_semantics) != SEMANTIC_SHA[branch]:
        raise AssertionError("historical MiniAS source Git blob changed")
    current_text = WORK.read_text()
    old_f = jobs(old_focus.read_text())
    old_s = jobs(old_semantics.read_text())
    cur = jobs(current_text)
    if set(old_f) != set(FOCUSED) or set(old_s) != set(SEMANTIC):
        raise AssertionError("old job identities do not match frozen list")
    if set(cur) != set(FOCUSED + SEMANTIC + ("linux-single",)):
        raise AssertionError("lost semantic/focused jobs or introduced unrelated new job")
    old_linux = ARCH / "minias-linux-runtime-gcc-single-variable.yml"
    if sha(old_linux) != LINUX_SINGLE_ARCHIVE_SHA:
        raise AssertionError("historic GNU-only MiniAS runtime source changed")
    expected = jobs(old_linux.read_text())["gcc-minias-linux-runtime"]
    # Independent Linux acceptance reuses its archived A0->Kbuild->QEMU
    # body, with only an explicit canonical mode, both GCC assembler names,
    # and always-preserved success/failure evidence. No GNU as fallback.
    expected = expected.replace("gcc-minias-linux-runtime:", "linux-single:", 1)
    expected = expected.replace(
        "    if: github.event_name == 'workflow_dispatch' || contains(github.event.head_commit.message, '[minias-linux-runtime]')",
        "    if: (github.event_name == 'workflow_dispatch' || github.event_name == 'push') && inputs.mode == 'linux-single'", 1)
    expected = expected.replace(
        '          chmod +x "$bindir/riscv64-linux-gnu-as"',
        '          chmod +x "$bindir/riscv64-linux-gnu-as"\n'
        '          # GCC -B resolves the basename \'as\'; Kbuild AS resolves the prefixed name.\n'
        '          ln -s riscv64-linux-gnu-as "$bindir/as"', 1)
    expected = expected.replace(
        "      - name: Preserve blocker evidence\n        if: failure()",
        "      - name: Preserve MiniAS-only Linux build and guest evidence\n        if: always()", 1)
    expected = expected.replace(
        "          name: minias-linux-gcc-runtime-blocker",
        "          name: minias-linux-gcc-single-cert", 1)
    expected = expected.replace(
        "            build/minias-linux-runtime/out/.config",
        "            build/minias-linux-runtime/out/.config\n"
        "            build/minias-linux-runtime/Image.sha256", 1)
    expected = expected.replace(
        "            build/minias-linux-runtime/qemu-p1/*.log",
        "            build/minias-linux-runtime/qemu-p1/*.log\n"
        "            build/minias-linux-runtime/out/arch/riscv/boot/Image", 1)
    linux_body = cur["linux-single"]
    integrated_path = ROOT / "tests/external/linux/mini_tools_integrated_relink.sh"
    if sha(ROOT / "tests/external/linux/minipp_native_targeted_relink.sh") != "3ecdd7a3db9d8648da463bc7110976c29cabe5e0":
        raise AssertionError("native MiniPP exact producer substitute changed")
    if sha(ROOT / "tests/preprocessor/run_linux_native_full_production.py") != "e7fb9abf889d01685fce9ec3b60ad04aacade125":
        raise AssertionError("native full MiniPP TU inventory or production trace changed")
    if sha(ROOT / "tests/external/linux/minipp_native_full_relink.sh") != "51858d1d76368840690a773391cb4f6e0f24bc2e":
        raise AssertionError("native full MiniPP runtime relink harness changed")
    if sha(integrated_path) != "c89ce99b4d78963d88fde600b651865e36182e1d":
        raise AssertionError("integrated GNU-link MiniAS+MiniAR+MiniObjcopy replay changed")
    approved_integrated = (
        "      - name: Reuse MiniAS Linux objects with MiniAR and MiniObjcopy and run QEMU P0 P1\n"
    )
    diag_begin = "      - name: Diagnose early-boot with cached GNU early objects and incremental relink\n"
    if linux_body.count(approved_integrated) != 1:
        raise AssertionError("integrated native-tool replay step missing")
    integrated_step = linux_body.split(approved_integrated, 1)[1].split(diag_begin, 1)[0]
    for token in (
        "        if: success() && env.MINIAS_INTEGRATED_MODE == 'true'",
        "tests/external/linux/mini_tools_integrated_relink.sh",
        'MINI_TOOLS_WORK="$work"',
    ):
        if token not in integrated_step:
            raise AssertionError(f"integrated native relink contract lost: {token}")
    linux_body = linux_body.split(approved_integrated, 1)[0] + diag_begin + linux_body.split(diag_begin, 1)[1]
    integrated_header = (
        "linux-single:\n"
        "    env:\n"
        "      MINIAS_INTEGRATED_MODE: ${{ inputs.mode == 'linux-integrated' }}\n"
        "    if: (github.event_name == 'workflow_dispatch' || github.event_name == 'push') "
        "&& (inputs.mode == 'linux-single' || inputs.mode == 'linux-integrated')"
    )
    original_header = (
        "linux-single:\n"
        "    if: (github.event_name == 'workflow_dispatch' || github.event_name == 'push') "
        "&& inputs.mode == 'linux-single'"
    )
    if linux_body.count(integrated_header) != 1:
        raise AssertionError("MiniAS integrated mode header changed")
    linux_body = linux_body.replace(integrated_header, original_header, 1)
    integrated_evidence = (
        "            build/minias-linux-runtime/native-integration/*.trace\n"
        "            build/minias-linux-runtime/native-integration/*.log\n"
        "            build/minias-linux-runtime/native-integration/failures/*\n"
        "            build/minias-linux-runtime/native-integration/Image.sha256\n"
        "            build/minias-linux-runtime/native-integration/qemu-p0/*.log\n"
        "            build/minias-linux-runtime/native-integration/qemu-p1/*.log\n"
        "            build/minias-linux-runtime/native-four/*.trace\n"
        "            build/minias-linux-runtime/native-four/*.log\n"
        "            build/minias-linux-runtime/native-four/Image.sha256\n"
        "            build/minias-linux-runtime/native-four/triple-native.Image.sha256\n"
        "            build/minias-linux-runtime/native-four/qemu-p0/*.log\n"
        "            build/minias-linux-runtime/native-four/qemu-p1/*.log\n"
        "            build/minias-linux-runtime/native-pp-two/*.trace\n"
        "            build/minias-linux-runtime/native-pp-two/*.log\n"
        "            build/minias-linux-runtime/native-pp-two/Image.sha256\n"
        "            build/minias-linux-runtime/native-pp-two/selected-command-sources.txt\n"
        "            build/minias-linux-runtime/native-pp-two/production-object-identity.json\n"
        "            build/minias-linux-runtime/native-pp-two/inputs/*.mini.i\n"
        "            build/minias-linux-runtime/native-pp-two/inputs/*.gcc.i\n"
        "            build/minias-linux-runtime/native-pp-two/qemu-p0/*.log\n"
        "            build/minias-linux-runtime/native-pp-two/qemu-p1/*.log\n"
        "            build/minias-linux-runtime/native-pp-full/*.trace\n"
        "            build/minias-linux-runtime/native-pp-full/*.log\n"
        "            build/minias-linux-runtime/native-pp-full/*.tsv\n"
        "            build/minias-linux-runtime/native-pp-full/*.json\n"
        "            build/minias-linux-runtime/native-pp-full/Image.sha256\n"
        "            build/minias-linux-runtime/native-pp-full/stderr/*.txt\n"
        "            build/minias-linux-runtime/native-pp-full/qemu-p0/*.log\n"
        "            build/minias-linux-runtime/native-pp-full/qemu-p1/*.log\n"
    )
    if linux_body.count(integrated_evidence) != 1:
        raise AssertionError("native-integrated relink evidence archive modified")
    linux_body = linux_body.replace(integrated_evidence, "", 1)
    if sha(ROOT / "tests/assembler/minias_linux_incremental_bisect.sh") != (
        "fdf3a07786c2f9500183bc5753cc6a5bb38cb1b3"
    ):
        raise AssertionError("MiniAS incremental diagnostic script differs from reviewed source")
    if linux_body.count("# Capture a small genuine GNU oracle") != 1:
        raise AssertionError("MiniAS dual-reference capture hook missing or duplicated")
    # Only the reviewed 7-owner shadow-GNU reference block may be removed;
    # the actual production artifact is still built with MiniAS alone.
    begin = linux_body.index("            # Capture a small genuine GNU oracle")
    end = linux_body.index("            esac\n", begin) + len("            esac\n")
    golden_capture = linux_body[begin:end]
    for marker in (
        "arch/riscv/kernel/head.o",
        "arch/riscv/kernel/entry.o",
        "arch/riscv/mm/init.o",
        "drivers/of/fdt.o",
        "drivers/firmware/efi/libstub/efi-stub-helper.o",
        '"$real_as" "${gnu_args[@]}"',
        'cp "$input" "${golden%.o}.s"',
        "test -s \"$golden\"",
    ):
        if marker not in golden_capture:
            raise AssertionError(f"MiniAS GNU reference collection lost: {marker}")
    reverted = linux_body[:begin] + linux_body[end:]
    reverted = reverted.replace(
        '          export MINIAS_GNU_ROOT="$work/golden-as"\n', "", 1)
    diag_end = "      - name: Preserve MiniAS-only Linux build and guest evidence\n"
    if reverted.count(diag_begin) != 1 or reverted.count(diag_end) != 1:
        raise AssertionError("MiniAS early-boot incremental diagnostic missing")
    diagnostic = reverted.split(diag_begin, 1)[1].split(diag_end, 1)[0]
    for token in (
        "        if: failure()",
        "tests/assembler/minias_linux_incremental_bisect.sh",
        'MINIAS_GNU_ROOT="$work/golden-as"',
    ):
        if token not in diagnostic:
            raise AssertionError(f"early-boot diagnostic contract lost: {token}")
    reverted = reverted.split(diag_begin, 1)[0] + diag_end + reverted.split(diag_end, 1)[1]
    additions = (
        "            build/minias-linux-runtime/bisect/**/*.log\n"
        "            build/minias-linux-runtime/bisect/*.txt\n"
        "            build/minias-linux-runtime/bisect/*.log\n"
        "            build/minias-linux-runtime/golden-as/arch/riscv/kernel/head.o\n"
        "            build/minias-linux-runtime/golden-as/arch/riscv/kernel/head.s\n"
    )
    if reverted.count(additions) != 1:
        raise AssertionError("MiniAS bisection evidence is missing or altered")
    reverted = reverted.replace(additions, "", 1)
    if reverted != expected:
        raise AssertionError("GNU-only MiniAS runtime job diverges from the approved archived A0->QEMU chain")

    if current_text.count("          - linux-single") != 1 or current_text.count("          - linux-integrated") != 1:
        raise AssertionError("MiniAS opt-in single and integrated modes missing")
    # Explicit reusable push inputs now reach the same manual mode selectors.
    # Preserve every frozen/semantic executable job body byte-for-byte.
    manual_modes = {
        "real16": "real16",
        "frontier": "frontier",
        "vector33": "vector33",
        "resolve": "window",
    }
    for name in FOCUSED:
        if name in manual_modes:
            if executable(cur[name]) != executable(old_f[name]):
                raise AssertionError(f"modified original MiniAS focused execution body: {name}")
            expected = ("    if: (github.event_name == 'workflow_dispatch' || "
                        "github.event_name == 'push') "
                        f"&& inputs.mode == '{manual_modes[name]}'")
            actual_predicates = re.findall(r"(?m)^    if:.*$", cur[name])
            if actual_predicates != [expected]:
                raise AssertionError(f"changed MiniAS manual-only routing: {name}")
        elif cur[name] != old_f[name]:
            raise AssertionError(f"modified preexisting MiniAS focused job: {name}")
    for name in SEMANTIC:
        if executable(cur[name]) != executable(old_s[name]):
            raise AssertionError(f"modified original 3536 executable job: {name}")
        want = ("    if: always() && (github.event_name == 'workflow_dispatch' || github.event_name == 'push') && inputs.mode == 'semantic'"
                if name == "aggregate" else
                "    if: (github.event_name == 'workflow_dispatch' || github.event_name == 'push') && inputs.mode == 'semantic'")
        if want not in cur[name]:
            raise AssertionError(f"changed semantic manual routing: {name}")
    if "on:\n  workflow_call:\n    inputs:" not in current_text or "  workflow_dispatch:\n    inputs:" not in current_text:
        raise AssertionError("MiniAS reusable/manual entrypoint contract missing")
    if "    needs: [c-shards, c149, native35]" not in cur["aggregate"]:
        raise AssertionError("semantic aggregation dependencies changed")
    if "          - semantic" not in current_text or "          - window" not in current_text:
        raise AssertionError("old window / new semantic modes not both available")
    if '  C149_EXPORT_RUN: "33248985705"' not in current_text:
        raise AssertionError("original semantic C149 artifact producer lost")
    for token in ("FROZEN_CORPUS_EXPORT_RUN", "FROZEN_SIDECARS_RUN", "FROZEN_CONFIG_GZ_SHA256"):
        if token not in current_text:
            raise AssertionError(f"missing frozen corpus pin {token}")
    for token in ("  actions: read", "  contents: read"):
        if token not in current_text:
            raise AssertionError(f"missing GitHub Action permission {token}")
    print(f"M0_MINIAS_SEMANTIC_CONVERGENCE=PASS branch={branch} jobs=11 original_focused=6 original_semantic=4 original_blob_shas=3 linux_single_archived_body=1 integrated_same_tree_relink=1")

if __name__ == "__main__":
    main()
