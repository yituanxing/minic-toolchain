#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mode=${1:-check}
formatter=${CLANG_FORMAT:-clang-format-18}

case "$mode" in
check|write)
    ;;
*)
    printf '%s\n' "usage: $0 [check|write]" >&2
    exit 2
    ;;
esac

if ! command -v "$formatter" >/dev/null 2>&1; then
    printf '%s\n' "FAIL format: missing formatter $formatter" >&2
    exit 1
fi

version=$($formatter --version)
case "$version" in
*"version 18."*)
    ;;
*)
    printf '%s\n' \
        "FAIL format: expected clang-format major 18, found: $version" >&2
    exit 1
    ;;
esac

mapfile -d '' files < <(
    cd "$root"
    find include src tools/minic \
        -type f \( -name '*.c' -o -name '*.h' \) \
        -print0 | sort -z
)

if (( ${#files[@]} == 0 )); then
    printf '%s\n' "FAIL format: no first-party C/header files found" >&2
    exit 1
fi

for index in "${!files[@]}"; do
    files[$index]="$root/${files[$index]}"
done

case "$mode" in
check)
    "$formatter" --dry-run --Werror "${files[@]}"
    ;;
write)
    "$formatter" -i "${files[@]}"
    ;;
esac

printf '%s\n' \
    "PASS format mode=$mode formatter=clang-format-18 files=${#files[@]} scope=include,src,tools/minic"
