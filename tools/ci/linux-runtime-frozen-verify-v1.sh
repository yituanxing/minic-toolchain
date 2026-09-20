#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <frozen-fixture-dir>" >&2
  exit 64
fi

frozen=$(realpath "$1")
cd "$frozen"

cat >.expected.sha256 <<'EOF'
8391ac85b062382189185c301d5bb24406213fce175c3cea41afb1c554468072  asm-args.txt
8c46c0b533714ce44c66b0eca12ba8d62f9d23edd852afcfa34bfacdd890f3f1  fixture.txt
67f10ee81526fa1d67de77a41dd613c764cffb885c4d5a951d7d4904f332ec75  lib__idr.i
81a39e7da7a7bafd3c1a81a44963fa21bbda52de264913f27426dc6c70464732  lib__xarray.i
02cee6b8e5f1b6c998d1186288a981746d55dd5d14d07e39b793c80ec27b93b9  src/arch/riscv/tools/relocs_check.sh
5b27a6f8a21c903c9825471b77db5710e106c7c441e465fb61435ac53a7ba056  src/scripts/head-object-list.txt
EOF
sha256sum -c .expected.sha256
rm -f .expected.sha256

grep -Fxq 'linux_version=6.6.143' fixture.txt
grep -Fxq 'config_sha256=e538a6ad42ec49c5a667cab5de9bb943f82ca702ff2b7749a020e38ff7ddaea7' fixture.txt
grep -Fxq 'targets=lib/idr.o lib/xarray.o' fixture.txt
mapfile -t asm_args <asm-args.txt
[[ ${#asm_args[@]} -eq 6 ]]
printf 'FROZEN_FIXTURE_VERIFY=PASS bytes=%s\n' "$(du -sb . | awk '{print $1}')"
