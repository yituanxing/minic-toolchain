#!/bin/sh
# Reuse the existing cJSON source/hash/headers/harness gate unmodified.
# The RISC-V-specific runner variables are replaced only at the invocation
# boundary; this is a discovery probe, NOT a claim of complete cJSON support.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built native minic-cc}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in
    x86_64|amd64) ;;
    *) echo 'SKIP cJSON x86-64: native x86-64 host required'; exit 0 ;;
esac
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
scratch=${BUILD_DIR:-"$root/build/debug"}/tests/external/cjson-x86_64-frontier
mkdir -p "$scratch"
# minic-cc already expects -S <input.i> -o <output.s>. The historical
# cJSON gate expects an executable under MINIC; adapt the CLI, not the test.
cat >"$scratch/minic-cc-x86_64" <<'EOF'
#!/bin/sh
exec "${MINIC_CC}" --target=x86_64-linux "$@"
EOF
chmod +x "$scratch/minic-cc-x86_64"
MINIC="$scratch/minic-cc-x86_64" \
RISCV_CC="$HOST_CC" \
QEMU_RISCV64=env \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/external/cjson/probe.sh"
