#!/usr/bin/env bash
# Certify two MiniPP-produced translation units, preserving the same
# 4-native GNU-GCC object tree and its original Kbuild commands.
set -Eeuo pipefail
work=${MINI_TOOLS_WORK:?}
bins=${MINI_TOOLS_BIN:?}
src="$work/linux-6.6.143"
out="$work/out"
stage="$work/native-pp-two"
native_stage="$work/native-integration"
four="$work/native-four"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$stage"
test -s "$four/Image.sha256"
test -x "$bins/minic-cpp"
test -x "$four/ld-final"
test -s "$out/arch/riscv/boot/Image"
test -s "$work/runtime-initramfs.cpio.gz"
: "${MINIAS_AS_TRACE:?}"
: "${MINILD_TRACE:?}"
: "${MINILD:?}"
: "${MINIAR:?}"
: "${MINIOBJCOPY:?}"
: "${REAL_AS:?}"
: >"$stage/minipp.trace"
export MINIPP="$bins/minic-cpp"
export MINIPP_LINUX_TRACE="$stage/minipp.trace"
export MINIPP_LINUX_WORK="$stage/inputs"
export MINIPP_LINUX_SRC="$src"
# MiniAS wrapper also builds shadow GNU early-boot object references for
# selected owner paths, even when producing actual MiniAS .o files.
export MINIAS_GNU_ROOT="$work/golden-as"
export MINIPP_LINUX_SELECT_FILE="$stage/selected-command-sources.txt"
export MINIPP_LINUX_SUBSTITUTE=true
export REAL_CC=riscv64-linux-gnu-gcc
export REFERENCE_CPP=riscv64-linux-gnu-gcc

# Keep full Kbuild's CC identity unchanged. Reuse the exact saved GNU GCC
# command for two existing output objects, injecting MiniPP .i only there.
# No whole-kernel recompilation from a global CC command change.
python3 - "$out" "$root/tests/preprocessor/linux_kbuild_pp_cc.sh" "$stage" <<'PY'
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

out = Path(sys.argv[1]).resolve()
wrapper = Path(sys.argv[2]).resolve()
stage = Path(sys.argv[3]).resolve()
specs = [("init/main.o", "init/main.c"),
         ("kernel/configs.o", "kernel/configs.c")]
commands = []
for obj, source in specs:
    cmdfile = out / Path(obj).parent / ("." + Path(obj).name + ".cmd")
    assert cmdfile.is_file(), f"Kbuild command missing: {cmdfile}"
    text = cmdfile.read_text(errors="replace")
    match = re.search(r"(?m)^(?:savedcmd|cmd)_" + re.escape(obj)
                      + r"\s*:=\s*(.*?)(?=\n(?:source_|deps_|cmd_|savedcmd_|\n)|\Z)",
                      text, re.S)
    assert match, f"original Kbuild compiler command unavailable: {obj}"
    argv = shlex.split(match.group(1).replace("\\\n", " ").strip())
    assert argv and "gcc" in Path(argv[0]).name, (obj, argv[:3])
    sources = [a for a in argv[1:] if a.endswith(".c") and not a.startswith("-")]
    assert len(sources) == 1, f"ambiguous TU for {obj}: {sources}"
    resolved = (out / sources[0]).resolve()
    assert resolved.as_posix().endswith("/" + source), (obj, resolved)
    assert "-c" in argv and "-o" in argv, obj
    assert (out / obj).is_file(), f"baseline .o missing: {obj}"
    commands.append((obj, argv[1:], sources[0]))

selector = stage / "selected-command-sources.txt"
# Wrapper normalizes absolute Linux source paths to src-relative names.
# A raw absolute selector would silently delegate straight to GNU GCC.
selector.write_text("".join(source + "\n" for _, source in specs))
assert len(set(x[2] for x in commands)) == 2
for obj, args, source in commands:
    print(f"MINIPP_PRODUCTION_TU_BEGIN object={obj} source={source}", flush=True)
    subprocess.run(["bash", str(wrapper), *args],
                   cwd=out, env=os.environ.copy(), check=True)
    assert (out / obj).stat().st_size > 0, obj
print("MINIPP_NATIVE_OBJECTS=PASS exact=2 fallback=0", flush=True)
PY

# Snapshot exact production objects before calling Kbuild again.
# This prevents a false PASS if Kbuild silently recompiles our MiniPP .o
# with the original GNU preprocessing command.
python3 - "$out" "$stage/production-object-identity.json" <<'PY'
import hashlib, json, pathlib, sys
out=pathlib.Path(sys.argv[1])
targets=["init/main.o", "kernel/configs.o"]
obj={}
for name in targets:
    path=out/name
    st=path.stat()
    obj[name]={"size":st.st_size, "mtime_ns":st.st_mtime_ns,
               "sha256":hashlib.sha256(path.read_bytes()).hexdigest()}
pathlib.Path(sys.argv[2]).write_text(json.dumps(obj,sort_keys=True,indent=2)+"\n")
print("MINIPP_NATIVE_OBJECT_IDENTITY=RECORDED objects=2")
PY

test "$(grep -c '^SUBSTITUTE ' "$stage/minipp.trace")" -eq 2
test "$(grep -c 'status=EXACT' "$stage/minipp.trace")" -eq 2
test "$(grep -Ec 'status=DIFF|status=MINIPP_FAIL' "$stage/minipp.trace" || true)" -eq 0
before_as=$(grep -c '^pass ' "$MINIAS_AS_TRACE" || true)
: >"$stage/minild.trace"
export MINILD_TRACE="$stage/minild.trace"
start=$(date +%s)
if ! make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
  CC="riscv64-linux-gnu-gcc -B$work/as-override/" \
  AS="$work/as-override/riscv64-linux-gnu-as" \
  AR="$native_stage/native-ar" \
  OBJCOPY="$native_stage/native-objcopy" \
  LD="$four/ld-final" \
  -j4 Image >"$stage/kbuild.log" 2>&1; then
  echo "MINIPP_NATIVE_RELINK=FAIL reason=kbuild" >&2
  tail -n 100 "$stage/kbuild.log" >&2
  exit 1
fi
python3 - "$out" "$stage/production-object-identity.json" <<'PY'
import hashlib, json, pathlib, sys
out=pathlib.Path(sys.argv[1])
before=json.loads(pathlib.Path(sys.argv[2]).read_text())
for name,want in before.items():
    path=out/name
    st=path.stat()
    observed={"size":st.st_size, "mtime_ns":st.st_mtime_ns,
              "sha256":hashlib.sha256(path.read_bytes()).hexdigest()}
    if observed != want:
        raise SystemExit(f"MINIPP_NATIVE_OBJECT_IDENTITY=FAIL reverted_or_rebuilt={name} before={want} after={observed}")
print("MINIPP_NATIVE_OBJECT_IDENTITY=PASS objects=2 no_kbuild_reversion=1")
PY
elapsed=$(( $(date +%s) - start ))
native_ld=$(grep -c '^minild ' "$stage/minild.trace" || true)
test "$native_ld" -eq 3 || {
  echo "MINIPP_NATIVE_RELINK=FAIL reason=expected_3_native_final_links actual=$native_ld" >&2
  exit 1
}
test -s "$out/arch/riscv/boot/Image"
sha256sum "$out/arch/riscv/boot/Image" >"$stage/Image.sha256"
printf 'MINIPP_NATIVE_IMAGE=PASS selected=2 exact=2 native_final_ld=%s new_minias=%s elapsed_s=%s sha256=%s\n' \
  "$native_ld" "$(( $(grep -c '^pass ' "$MINIAS_AS_TRACE" || true) - before_as ))" \
  "$elapsed" "$(cut -d' ' -f1 "$stage/Image.sha256")"
LINUX_IMAGE="$out/arch/riscv/boot/Image" \
INITRAMFS="$work/runtime-initramfs.cpio.gz" \
BUILD_DIR="$stage/qemu-p0" \
LINUX_RELEASE=6.6.143 \
QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
QEMU_TIMEOUT_SECONDS=90 \
  bash "$root/tests/external/linux/runtime_boot.sh"
LINUX_IMAGE="$out/arch/riscv/boot/Image" \
INITRAMFS="$work/runtime-initramfs.cpio.gz" \
BUILD_DIR="$stage/qemu-p1" \
LINUX_RELEASE=6.6.143 \
LINUX_RUNTIME_PROFILE=p1 \
QEMU_SYSTEM_RISCV64=qemu-system-riscv64 \
QEMU_TIMEOUT_SECONDS=90 \
  bash "$root/tests/external/linux/runtime_boot.sh"
python3 "$root/tests/external/linux/runtime_v2_log_check.py" \
  --profile p1 --log "$stage/qemu-p1/rdinit-init.log"
echo "MINIPP_NATIVE_TWO_LINUX_RUNTIME=PASS pp=MiniPP-exact-two gcc=GNU as=MiniAS ar=MiniAR ld=MiniLD-final objcopy=MiniObjcopy p0=2/2 p1=12/12"
