#!/usr/bin/env bash
set -Eeuo pipefail

host_cc=${1:-cc}
root=$(cd "$(dirname "$0")/../.." && pwd)
core_sources=("$root"/src/core/*.c)
core_headers=("$root"/src/core/*.h)

# Source language declaration identities and C importer dependencies belong
# exclusively to compiler/c_lower. Check both spelled references and the
# compiler's actual transitive include closure, including indirect includes.
for source in "${core_sources[@]}" "${core_headers[@]}"; do
    if LC_ALL=C grep -En \
        'MinicType(BaseKind)?|Minic(Enum|Record|FunctionType)Id|Minic(Expression|Program)|MinicCoreSource(Draft|Builder)|source_draft|frontend/|compiler/c_lower/' \
        "$source"; then
        printf 'CORE_LANGUAGE_BOUNDARY=FAIL source_identity=%s\n' "$source" >&2
        exit 1
    fi
done

for source in "${core_sources[@]}"; do
    dependencies=$("$host_cc" -std=c11 -MM -I"$root/include" -I"$root/src" "$source")
    case "$dependencies" in
        *'/frontend/'*|*'/compiler/c_lower/'*|*'/target/source_policy/'*)
            printf 'CORE_LANGUAGE_BOUNDARY=FAIL include_closure=%s\n' "$source" >&2
            exit 1 ;;
    esac
done

"$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror \
    -I"$root/include" -I"$root/src" \
    -fsyntax-only "$root/tests/core/core_language_boundary_test.c"

printf 'CORE_LANGUAGE_BOUNDARY=PASS pure_core_sources=%s pure_core_headers=%s forbidden_source_identity=0\n' \
    "${#core_sources[@]}" "${#core_headers[@]}"
