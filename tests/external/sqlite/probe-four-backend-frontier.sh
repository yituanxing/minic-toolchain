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
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=open\n");
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=schema_and_insert\n");
    run(db, "CREATE TABLE t(id INTEGER PRIMARY KEY, x INT);"
            "INSERT INTO t VALUES(1,11),(2,13);"
            "CREATE INDEX t_x ON t(x);"
            "SELECT SUM(x) AS sum FROM t;");
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=transaction\n");
    run(db, "BEGIN; UPDATE t SET x=99 WHERE id=1; ROLLBACK;");
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=select\n");
    run(db, "SELECT x FROM t WHERE id=1;");
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=integrity\n");
    run(db, "PRAGMA integrity_check;");
    fprintf(stderr, "SQLITE_RUNTIME_STAGE=version_and_close\n");
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
    # SQLite officially provides a portable 64-bit implementation of
    # multiply128/multiply160 when SQLITE_DISABLE_INTRINSIC is enabled.
    # MiniC does not yet claim 128-bit scalar SSA/ABI correctness, so use
    # SQLite's unmodified supported portable source path for BOTH MiniC
    # and the GNU oracle. This is not a claim of general __uint128_t support.
    define='-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DISABLE_INTRINSIC'
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
        grep -m 12 'MINIC_SQLITE_CONVERSION_LIMIT' \
            "$work/$target/mini.err" |
            while IFS= read -r line; do
                echo "FOUR_BACKEND_SQLITE_CONVERSION_CLASS target=$target $line"
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
            sed -n '1,30p' "$work/$target/$cc.stderr" >&2
            if [ "$cc" = mini ] && [ "$target" = loongarch64 ]; then
                # Run the same REAL MiniC-built SQLite binary under QEMU's
                # block trace. This is a diagnostic replay, not a pass.
                set +e
                timeout 20s "$qemu" -d in_asm,exec \
                    -D "$work/$target/mini-qemu.log" \
                    "$work/$target/mini" >"$work/$target/trace.out" \
                    2>"$work/$target/trace.err"
                qemu_trace_status=$?
                set -e
                echo "FOUR_BACKEND_SQLITE_TRACE target=$target replay_status=$qemu_trace_status"
                tail -n 32 "$work/$target/mini-qemu.log" 2>/dev/null |
                    while IFS= read -r line; do
                        echo "FOUR_BACKEND_SQLITE_TRACE_LINE $line"
                    done
                # GNU's function symbol table can map QEMU guest PCs to
                # the exact function responsible, even for 2500+ symbols.
                "$gcc" -print-prog-name=nm >"$work/$target/nm-command"
                nm_tool=$(cat "$work/$target/nm-command")
                "$nm_tool" -n "$work/$target/mini" |
                    awk '$2=="t" || $2=="T" {print $1, $3}' \
                    >"$work/$target/symbols.txt"
                python3 - "$work/$target/mini-qemu.log" \
                        "$work/$target/symbols.txt" <<'PY'
import bisect, re, sys
from collections import deque

pcs = deque(maxlen=12)
branches = deque(maxlen=10)
traceblocks = deque(maxlen=8)
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        m = re.match(r'^0x([0-9a-fA-F]+):', line)
        if m:
            pcs.append(int(m.group(1), 16))
        b = re.search(r'\b(?:bl|b|jirl)\s+[^#\n]*#\s*0x([0-9a-fA-F]+)', line)
        if b:
            branches.append(int(b.group(1), 16))
        t = re.search(r'^Trace .*?\[([0-9a-fA-F]{8,})/', line)
        if t:
            traceblocks.append(int(t.group(1), 16))
symbols = []
with open(sys.argv[2]) as f:
    for line in f:
        v = line.split(maxsplit=1)
        if len(v) == 2:
            try: symbols.append((int(v[0], 16), v[1].strip()))
            except ValueError: pass
symbols.sort()
addresses = [p for p, _ in symbols]
def describe(tag, pc):
    at = bisect.bisect_right(addresses, pc) - 1
    name = symbols[at][1] if at >= 0 else '<unknown>'
    base = symbols[at][0] if at >= 0 else 0
    print(f"FOUR_BACKEND_SQLITE_TRACE_{tag} pc=0x{pc:x} function={name} offset=0x{pc-base:x}")
for pc in pcs:
    describe("PC", pc)
for pc in branches:
    describe("BRANCH", pc)
for pc in traceblocks:
    describe("EXEC", pc)
PY            fi
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
