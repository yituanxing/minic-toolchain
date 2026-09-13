#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/local-null-pointer-fact-v0"
toolchain="$work/toolchain"
mkdir -p "$work"

python3 tools/ci/apply-local-integer-assignment-v2.py >"$work/local-integer-patch.log"
python3 tools/ci/apply-local-integer-logical-conditions-v0.py >"$work/logical-conditions-patch.log"
python3 tools/ci/apply-local-null-pointer-facts-v0.py >"$work/null-pointer-patch.log"
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" all >/dev/null

cat >"$work/probe.c" <<'EOF'
extern void must_not_be_referenced(void);

static inline int *always_null(int *input)
{
    return (void *)0;
}

int probe(int *input)
{
    int *value;

    value = always_null(input);
    if (value)
        must_not_be_referenced();
    return 0;
}
EOF

MINIC_LOCAL_FACT_TRACE=1 CORE_FAST_TRACE=1 \
  "$toolchain/bin/minic" -S "$work/probe.c" -o "$work/probe.s" \
  >"$work/probe.stdout" 2>"$work/probe.stderr"

test -s "$work/probe.s"
if grep -q 'must_not_be_referenced' "$work/probe.s"; then
  echo "MINIC_LOCAL_NULL_POINTER_FACT_V0=FAIL dead-call-remains" >&2
  sed -n '1,220p' "$work/probe.stderr" >&2 || true
  grep -n -C 6 'must_not_be_referenced' "$work/probe.s" >&2 || true
  exit 1
fi
if ! grep -q 'null=1' "$work/probe.stderr"; then
  echo "MINIC_LOCAL_NULL_POINTER_FACT_V0=FAIL null-fact-not-observed" >&2
  sed -n '1,220p' "$work/probe.stderr" >&2 || true
  exit 1
fi

echo "MINIC_LOCAL_NULL_POINTER_FACT_V0=PASS"
