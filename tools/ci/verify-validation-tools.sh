#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
packages="$root/tools/ci/ubuntu-24.04-packages.txt"

require_command() {
    command_name=$1
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf '%s\n' \
            "FAIL validation-tools: missing command $command_name" >&2
        exit 1
    fi
}

require_command riscv64-linux-gnu-gcc
require_command riscv64-linux-gnu-ld
require_command riscv64-linux-gnu-objdump
require_command qemu-riscv64
require_command dpkg-query

printf '%s\n' 'Validation profile: ubuntu-24.04-apt / 验证配置：ubuntu-24.04-apt'
riscv64-linux-gnu-gcc --version | sed -n '1p'
riscv64-linux-gnu-ld --version | sed -n '1p'
riscv64-linux-gnu-objdump --version | sed -n '1p'
qemu-riscv64 --version | sed -n '1p'

while IFS= read -r package || test -n "$package"; do
    case "$package" in
    ''|'#'*)
        continue
        ;;
    esac

    version=$(dpkg-query -W -f='${Version}' "$package" 2>/dev/null) || {
        printf '%s\n' \
            "FAIL validation-tools: package is not installed: $package" >&2
        exit 1
    }
    printf 'package %s=%s\n' "$package" "$version"
done <"$packages"

printf '%s\n' 'PASS validation-tools profile=ubuntu-24.04-apt'
