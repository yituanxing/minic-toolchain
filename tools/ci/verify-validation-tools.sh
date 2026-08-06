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

version_line() {
    "$@" --version | sed -n '1p'
}

require_command cc
require_command clang-format-18
require_command riscv64-linux-gnu-gcc
require_command riscv64-linux-gnu-ld
require_command riscv64-linux-gnu-objdump
require_command qemu-riscv64
require_command dpkg-query

printf '%s\n' 'PROFILE validation=ubuntu-24.04-apt'
printf 'TOOL host-cc=%s\n' "$(version_line cc)"
printf 'TOOL clang-format=%s\n' "$(version_line clang-format-18)"
printf 'TOOL target-gcc=%s\n' "$(version_line riscv64-linux-gnu-gcc)"
printf 'TOOL target-ld=%s\n' "$(version_line riscv64-linux-gnu-ld)"
printf 'TOOL target-objdump=%s\n' "$(version_line riscv64-linux-gnu-objdump)"
printf 'TOOL qemu-riscv64=%s\n' "$(version_line qemu-riscv64)"

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
    printf 'PACKAGE %s=%s\n' "$package" "$version"
done <"$packages"

printf '%s\n' 'PASS validation-tools profile=ubuntu-24.04-apt'
