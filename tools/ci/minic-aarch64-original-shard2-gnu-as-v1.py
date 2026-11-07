#!/usr/bin/env python3
"""Exact original AArch64 Linux 6.6.143 shard2 MiniC -> GNU AS proof.

Consumes only the immutable original-failure artifact from GitHub Actions
#42345808682; does not rebuild Linux, alter .i inputs, or invoke GCC fallback.
"""

import argparse
import hashlib
import json
import pathlib
import subprocess
import sys


EXPECTED = {
    "arch/arm64/kernel/proton-pack.o": "dedc80f6995afa27f2fb13f1d23ce8b4b906c9d5c0c79d61b40b19a24352acf1",
    "arch/arm64/kvm/arch_timer.o": "addb1f5d79a53dce6dd77bb362daf7eaa5dbf21ea37ff26324ac8ed906f830a7",
    "arch/arm64/kernel/perf_callchain.o": "7f6f5e4eb55981ecea449fa7384f178b9bb39f9392fbcaa37e20bbf86bb2c886",
    "arch/arm64/kernel/signal32.o": "248ae48c0bacf1b5ceced1acda74d41f55467cf11e0edf40b257f6d268f52bd0",
    "arch/arm64/kvm/hyp/nvhe/hyp-main.nvhe.o": "58392c1fa0b05ce9c7bdafea842010d8549956572967b4a6dc9221dd05695900",
    "arch/arm64/kvm/hyp/nvhe/setup.nvhe.o": "3ddcf02eadd3dce1f97dd495a3b35a136cb9620d547ccf7bd99d58b470d6196f",
    "arch/arm64/kvm/hyp/vhe/switch.o": "9a771d928b3f314bde938bb8ce4c7b976c814ce10971225508acbaa33df086a7",
    "arch/arm64/kvm/mmu.o": "520bb81944a0ed1f055d3397f1830b6f5a53facd7f4cd0ffd62aeb053de9df2c",
}
EXPECTED_COMMAND = {
    "arch/arm64/kernel/proton-pack.o": "f77150b6ecc272a29f6cf9f1bb76487e8c900d080422f9016f957fd63a83033c",
    "arch/arm64/kvm/arch_timer.o": "d4c61a269c4d873f6fac06f664ea37c1150bf72b8ff9e9c32b087e923690661a",
    "arch/arm64/kernel/perf_callchain.o": "379a9d36c0fa691fc3ee092c18efcdd5aa5cb8c8c8d9ed7f22bec76978eb49b6",
    "arch/arm64/kernel/signal32.o": "4de37cf6db1387393b1e94bb33666f32e91f2b2ec9accfe7b8ebd4517fc2f476",
    "arch/arm64/kvm/hyp/nvhe/hyp-main.nvhe.o": "b443e1be18e9b930e7dcc8b0466a6b868566d930d0db0bbb14b1a45b1746bbee",
    "arch/arm64/kvm/hyp/nvhe/setup.nvhe.o": "62b19c03e3ba168eac1a9c07336c22bc1e815b5763b8f4aa4cc891c42fe6bd24",
    "arch/arm64/kvm/hyp/vhe/switch.o": "7a7c0e68be51e0f04b4bc5f38ae34587bbed7db7d26d8447ffc280c952c9ce63",
    "arch/arm64/kvm/mmu.o": "5e537fef37eeba2d47f152d9d9d1dc17e9e6659e7899786addaed6a5f36433ee",
}
ORIGINAL_FLAGS = ("-fshort-wchar", "-funsigned-char", "-fno-PIE", "-mabi=lp64", "-march=armv8.5-a")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def execute(argv, cwd, prefix):
    result = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=240, check=False)
    pathlib.Path(str(prefix) + ".stdout").write_text(result.stdout)
    pathlib.Path(str(prefix) + ".stderr").write_text(result.stderr)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-root", type=pathlib.Path, required=True)
    parser.add_argument("--minic", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = args.artifact_root.resolve() / "failed-owner-diagnostics"
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((root / "index.json").read_text())
    owners = {sample["owner"]: sample for sample in manifest["samples"]}
    if set(owners) != set(EXPECTED) or len(owners) != 8:
        raise ValueError(f"expected precisely eight frozen original failed owners: got {sorted(owners)}")

    results = []
    for owner, expected in EXPECTED.items():
        sample = owners[owner]
        stem = owner[:-2]
        source_dir = root / stem
        original = source_dir / "original.gnu.i"
        command = source_dir / "original.gnu.o.cmd"
        name = stem.replace("/", "__")
        prefix = out / name
        asm = out / (name + ".s")
        obj = out / (name + ".o")
        receipt = {"owner": owner, "input_sha256": sha256(original), "status": "NOT_RUN"}
        results.append(receipt)
        if receipt["input_sha256"] != expected or sample["files"]["original.gnu.i"]["sha256"] != expected:
            receipt["status"] = "INPUT_SHA_MISMATCH"
            continue
        cmd_data = command.read_text()
        if any(flag not in cmd_data for flag in ORIGINAL_FLAGS):
            receipt["status"] = "ORIGINAL_GNU_FLAGS_MISMATCH"
            continue
        if (sha256(command) != EXPECTED_COMMAND[owner] or
                sample["files"]["original.gnu.o.cmd"]["sha256"] != EXPECTED_COMMAND[owner]):
            receipt["status"] = "ORIGINAL_COMMAND_SHA_MISMATCH"
            continue

        # .i is already exactly GNU-preprocessed; MiniC does not accept
        # AArch64 -march/-mabi options (these are AArch64 GNU AS arguments).
        # Preserve the source-language ABI/signedness flags from .cmd.
        minic = execute([str(args.minic.resolve()), "-S", str(original), "--target=aarch64-linux",
                         "-fshort-wchar", "-funsigned-char", "-fno-pie", "-o", str(asm)],
                        out, out / (name + ".minic"))
        if minic.returncode:
            receipt["status"] = "MINIC_S_FAIL"
            receipt["returncode"] = minic.returncode
            continue
        receipt["asm_sha256"] = sha256(asm)

        gnu_as = execute(["aarch64-linux-gnu-as", "-march=armv8.5-a", "-o", str(obj), str(asm)],
                         out, out / (name + ".as"))
        if gnu_as.returncode:
            receipt["status"] = "GNU_AS_FAIL"
            receipt["returncode"] = gnu_as.returncode
            continue
        elf = execute(["aarch64-linux-gnu-readelf", "-h", str(obj)],
                      out, out / (name + ".readelf"))
        if elf.returncode or "AArch64" not in elf.stdout or "REL (Relocatable file)" not in elf.stdout:
            receipt["status"] = "ELF_MACHINE_OR_TYPE_FAIL"
            continue
        receipt["obj_sha256"] = sha256(obj)
        receipt["status"] = "MINIC_S_GNU_AS_PASS"
        print(f"AARCH_ORIGINAL_SHARD2_OWNER=PASS owner={owner} input_sha={expected} asm_sha={receipt['asm_sha256']} obj_sha={receipt['obj_sha256']}", flush=True)

    (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    passing = sum(row["status"] == "MINIC_S_GNU_AS_PASS" for row in results)
    print(f"AARCH_ORIGINAL_SHARD2_GNU_AS={'PASS' if passing == 8 else 'FAIL'} originals={passing}/8 source_run=42345808682 gcc_fallback=NO full_Linux_Image_P1=NOT_RUN", flush=True)
    return 0 if passing == 8 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        print(f"AARCH_ORIGINAL_SHARD2_GNU_AS=FAIL error={exc}", file=sys.stderr)
        sys.exit(1)
