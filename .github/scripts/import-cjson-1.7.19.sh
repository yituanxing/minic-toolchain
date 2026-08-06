#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

target=tests/vendor/cjson/upstream
upstream_commit=c859b25da02955fef659d658b8f324b5cde87be3
raw_base="https://raw.githubusercontent.com/DaveGamble/cJSON/$upstream_commit"
branch=tests/cjson-baseline

mkdir -p "$target"

fetch_and_verify() {
    local name=$1
    local expected_blob=$2
    local destination="$target/$name"

    curl --fail --location --silent --show-error \
        "$raw_base/$name" \
        --output "$destination"

    local actual_blob
    actual_blob=$(git hash-object "$destination")
    if [[ "$actual_blob" != "$expected_blob" ]]; then
        printf 'FAIL import-cjson: %s blob=%s expected=%s\n' \
            "$name" "$actual_blob" "$expected_blob" >&2
        exit 1
    fi
    printf 'PASS import-cjson file=%s blob=%s\n' "$name" "$actual_blob"
}

fetch_and_verify cJSON.c 6e4fb0dd369cd905923da515be87ab06db6c1ee0
fetch_and_verify cJSON.h cab5feb427725f8e5c82287f7fe59481b609b9b5
fetch_and_verify LICENSE 78deb0406d713ab9730e3c2447be1abdbd70b9a2

# The importer and its workflow are bootstrap-only and must not survive in the final branch.
git rm -- .github/scripts/import-cjson-1.7.19.sh .github/workflows/import-cjson-1.7.19.yml

git config user.name github-actions[bot]
git config user.email 41898282+github-actions[bot]@users.noreply.github.com

git add "$target"
git commit -m "tests: vendor pinned cJSON 1.7.19 sources

Vendor byte-identical cJSON.c, cJSON.h, and LICENSE from official release 1.7.19 at commit c859b25da02955fef659d658b8f324b5cde87be3.

Verify all three upstream Git Blob identities during the one-time import and remove the temporary importer and workflow before committing.

中文说明：
从官方 cJSON 1.7.19 发布提交逐字节导入 cJSON.c、cJSON.h 和 LICENSE。

一次性导入期间校验三份上游 Git Blob 身份，并在提交前删除临时导入器与工作流。"

git push origin "HEAD:$branch"
