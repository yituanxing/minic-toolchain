#!/bin/sh
# Two-ISA SQLite 3.54.1 original amalgamation frontier.
# Same official sqlite3.c GNU-preprocessed input for MiniC and GNU. NEVER
# interpret a rejected/timeout TU as an accepted SQLite build. The complete
# GNU/QEMU SQL differential is strict when each MiniC object is available.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc binary}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
release=3.54.1
source_id=3540100
expected_sha3=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
if ! timeout 120s curl -fsSL --retry 2 --retry-delay 1 \
        "https://www.sqlite.org/2026/sqlite-amalgamation-$source_id.zip" \
        -o "$work/upstream.zip"; then
    echo "FOUR_BACKEND_SQLITE_ORACLE=FAIL stage=download" >&2
    exit 1
fi
python3 - "$work/upstream.zip" "$expected_sha3" <<'PY'
import hashlib, sys
with open(sys.argv[1], "rb") as f:
    actual = hashlib.file_digest(f, "sha3_256").hexdigest()
if actual != sys.argv[2]:
    raise SystemExit("FOUR_BACKEND_SQLITE_ORACLE=FAIL sha3_256=" + actual)
print("FOUR_BACKEND_SQLITE_SOURCE=VERIFIED release=3.54.1 sha3_256=" + actual)
PY
unzip -q "$work/upstream.zip" -d "$work"
src="$work/sqlite-amalgamation-$source_id"
test -s "$src/sqlite3.c" && test -s "$src/sqlite3.h"
cat > "$work/workload.c" <<'SRC'
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
static int result(void *data, int n, char **values, char **names) {
    int i;
    (void)data;
    for (i = 0; i < n; ++i)
        printf("%s=%s%s", names[i], values[i] ? values[i] : "NULL",
               i + 1 == n ? "\n" : "|");
    return 0;
}
static void run(sqlite3 *db, const char *sql) {
    char *error = NULL;
    int rc = sqlite3_exec(db, sql, result, NULL, &error);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "sqlite failure rc=%d %s\n", rc, error ? error : "");
        sqlite3_free(error);
        exit(3);
    }
}
int main(void) {
    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;
    run(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, x INT);"
            "INSERT INTO t VALUES(1,11),(2,13);"
            "CREATE INDEX t_x ON t(x);"
            "SELECT SUM(x) AS sum FROM t;");
    run(db, "BEGIN; UPDATE t SET x=99 WHERE id=1; ROLLBACK;");
    run(db, "SELECT x FROM t WHERE id=1;");
    run(db, "PRAGMA integrity_check;");
    printf("sqlite-version=%s\n", sqlite3_libversion());
    return sqlite3_close(db) == SQLITE_OK ? 0 : 2;
}
SRC
passed=0
blocked=0
failed=0
for target in aarch64 loongarch64; do
    case "$target" in
        aarch64) gcc=aarch64-linux-gnu-gcc; qemu=qemu-aarch64 ;;
        loongarch64) gcc=loongarch64-linux-gnu-gcc-13; qemu=qemu-loongarch64 ;;
    esac
    command -v "$gcc" >/dev/null
    command -v "$qemu" >/dev/null
    mkdir -p "$work/$target"
    input="$work/$target/sqlite3.i"
    asm="$work/$target/sqlite3.s"
    define='-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION'
    # Cross-preprocess each target's real GNU libc. Never feed x86 headers.
    if ! "$gcc" -E -P -std=gnu11 $define -I"$src" "$src/sqlite3.c" \
            -o "$input" >"$work/$target/pp.out" 2>"$work/$target/pp.err"; then
        echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=GNU_preprocess" >&2
        sed -n '1,18p' "$work/$target/pp.err" >&2
        failed=$((failed + 1))
        continue
    fi
    echo "FOUR_BACKEND_SQLITE_INPUT target=$target version=$release preprocessed_lines=$(wc -l < "$input") preprocessed_bytes=$(wc -c < "$input")"
    set +e
    /usr/bin/time -f 'MINIC_SQLITE_SECONDS=%e MINIC_SQLITE_MAXRSS_KB=%M' \
        timeout 120s env MINIC_M1_TRACE=1 MINIC_BACKEND_CENSUS=1 \
        "$MINIC_CC" --target="$target-linux" -S "$input" -o "$asm" \
        >"$work/$target/mini.out" 2>"$work/$target/mini.err"
    status=$?
    set -e
    if [ "$status" -ne 0 ]; then
        if [ "$status" -ge 128 ] && [ "$status" -ne 124 ]; then
            echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=MiniC_crash status=$status" >&2
            failed=$((failed + 1))
        else
            echo "FOUR_BACKEND_SQLITE=BLOCKED target=$target stage=MiniC status=$status"
            blocked=$((blocked + 1))
        fi
        # One full SQLite compilation collects ALL rejected functions.
        # Group the reject census by stage/opcode and print representative
        # function names, rather than one CI cycle per individual rejection.
        echo "FOUR_BACKEND_SQLITE_FIRST_FAILURE target=$target"
        grep -m 2 -E 'MINIC_BACKEND_REJECT|MINIC_M4_GLOBAL_FRONTIER|MINIC_BACKEND_PREFLIGHT|error:' \
            "$work/$target/mini.err" || true
        awk '
            /MINIC_BACKEND_REJECT/ {
                stage="none"; opcode="none"; function_name="none";
                for (i=1;i<=NF;i++) {
                    if ($i ~ /^stage=/) stage=$i;
                    if ($i ~ /^kind=/) opcode=$i;
                    if ($i ~ /^function=/) function_name=$i;
                }
                key=stage " " opcode
                count[key]++
                if (!(key in example)) example[key]=function_name
            }
            END {
                for (key in count)
                    printf "%06d %s example=%s\n",count[key],key,example[key]
            }
        ' "$work/$target/mini.err" | sort -rn | head -n 20 |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_ERROR_CLASS target=$target $line"
            done
        grep -m 12 'MINIC_M4_GLOBAL_FRONTIER' \
            "$work/$target/mini.err" |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_GLOBAL_CLASS target=$target $line"
            done
        grep -m 12 'MINIC_SQLITE_CALL_ARGUMENT' \
            "$work/$target/mini.err" |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_CALL_ARGUMENT target=$target $line"
            done
        grep -m 12 'MINIC_BACKEND_CALL_GATE' \
            "$work/$target/mini.err" |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_CALL_CLASS target=$target $line"
            done

        tail -n 5 "$work/$target/mini.err"
        lineno=$(sed -n 's/.*sqlite3\.i:\([0-9][0-9]*\):[0-9][0-9]*: error:.*/\1/p' \
                 "$work/$target/mini.err" | head -n 1)
        if [ -n "$lineno" ]; then
            start=$((lineno > 4 ? lineno - 4 : 1))
            end=$((lineno + 4))
            echo "FOUR_BACKEND_SQLITE_CONTEXT target=$target input_lines=$start-$end"
            sed -n "${start},${end}p" "$input" |
                nl -ba -v "$start"
        fi
        continue
    fi
    echo "FOUR_BACKEND_SQLITE_COMPILE=PASS target=$target source=sqlite3.c"
    if ! "$gcc" -c -x assembler "$asm" -o "$work/$target/mini.o" \
            >"$work/$target/as.out" 2>"$work/$target/as.err"; then
        echo "FOUR_BACKEND_SQLITE=BLOCKED target=$target stage=GNU_AS"
        sed -n '1,14p' "$work/$target/as.err"
        blocked=$((blocked + 1))
        continue
    fi
    echo "FOUR_BACKEND_SQLITE_ASSEMBLE=PASS target=$target source=sqlite3.c"
    if ! "$gcc" -O0 -std=gnu11 -c -x c "$input" -o "$work/$target/gnu.o" ||
       ! "$gcc" -static -I"$src" "$work/workload.c" "$work/$target/gnu.o" \
             -lm -ldl -lpthread -o "$work/$target/gnu" ||
       ! "$gcc" -static -I"$src" "$work/workload.c" "$work/$target/mini.o" \
             -lm -ldl -lpthread -o "$work/$target/mini"; then
        echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=GNU_or_MiniC_link" >&2
        failed=$((failed + 1))
        continue
    fi
    runtime_ok=1
    for cc in gnu mini; do
        if ! timeout 60s "$qemu" "$work/$target/$cc" \
                >"$work/$target/$cc.stdout" 2>"$work/$target/$cc.stderr"; then
            echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=$cc-QEMU" >&2
            sed -n '1,15p' "$work/$target/$cc.stderr" >&2
            runtime_ok=0
        fi
    done
    if [ "$runtime_ok" -ne 1 ] ||
       ! cmp -s "$work/$target/gnu.stdout" "$work/$target/mini.stdout" ||
       ! cmp -s "$work/$target/gnu.stderr" "$work/$target/mini.stderr"; then
        echo "FOUR_BACKEND_SQLITE=FAIL target=$target stage=GNU_QEMU_differential" >&2
        diff -u "$work/$target/gnu.stdout" "$work/$target/mini.stdout" || true
        failed=$((failed + 1))
        continue
    fi
    grep -F "sqlite-version=$release" "$work/$target/mini.stdout" >/dev/null
    echo "FOUR_BACKEND_SQLITE=PASS target=$target source=original_sqlite3.c GNU_QEMU_byte_exact=1"
    passed=$((passed + 1))
done
echo "FOUR_BACKEND_SQLITE_FRONTIER=COMPLETE architectures=2 passed=$passed blocked=$blocked failed=$failed"
if [ "$failed" -ne 0 ]; then exit 1; fi
if [ "${FOUR_BACKEND_SQLITE_REQUIRE:-0}" = 1 ] && { [ "$passed" -ne 2 ] || [ "$blocked" -ne 0 ]; }; then
    echo "FOUR_BACKEND_SQLITE_ACCEPT=FAIL passed=$passed blocked=$blocked" >&2
    exit 1
fi
if [ "$passed" -eq 2 ]; then
    echo "FOUR_BACKEND_SQLITE_ACCEPT=PASS targets=2 original_sqlite3=1 GNU_QEMU=1"
fi
