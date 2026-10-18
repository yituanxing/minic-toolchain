#!/bin/sh
# Independent source-driven x86-64 pressure test: official SQLite 3.54.1
# *unmodified* sqlite3.c amalgamation, same genuine GNU preprocessed input
# for MiniC vs GCC. This project is deliberately much larger than Lua/zlib.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc binary}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "SKIP SQLite x86-64: native x86 host required"; exit 0 ;;
esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
release=3.54.1
source_id=3540100
expected_sha3=92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30
curl -fsSL --retry 3 "https://www.sqlite.org/2026/sqlite-amalgamation-$source_id.zip" \
  -o "$work/upstream.zip"
python3 - "$work/upstream.zip" "$expected_sha3" <<'PY'
import hashlib, sys
with open(sys.argv[1], 'rb') as f:
    actual = hashlib.file_digest(f, 'sha3_256').hexdigest()
if actual != sys.argv[2]:
    raise SystemExit("SQLITE_X86_64_SOURCE=FAIL SHA3-256 mismatch: " + actual)
print("SQLITE_X86_64_SOURCE=PASS sha3_256=" + actual)
PY
unzip -q "$work/upstream.zip" -d "$work"
src="$work/sqlite-amalgamation-$source_id"
test -s "$src/sqlite3.c" && test -s "$src/sqlite3.h"
# Preserve upstream feature selection and host headers exactly; this is
# not a faked glibc or manually rewritten SQLite preprocessed file.
defines="-DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION"
"$HOST_CC" -E -P -std=gnu11 $defines -I"$src" "$src/sqlite3.c" -o "$work/sqlite3.i"
printf 'SQLITE_X86_64_INPUT=PASS version=%s preprocessed_bytes=%s preprocessed_lines=%s\n' \
  "$release" "$(wc -c < "$work/sqlite3.i")" "$(wc -l < "$work/sqlite3.i")"
# Discovery is intentionally a non-certifying mode. CI remains green when
# correctly reporting a blocked frontier; only PASS counts as coverage.
discovery=${SQLITE_X86_64_DISCOVERY:-0}
case "$discovery" in 0|1) ;; *) echo "Invalid SQLITE_X86_64_DISCOVERY" >&2; exit 2;; esac
if /usr/bin/time -f 'SQLITE_X86_64_COMPILE_ELAPSED=%e maxrss_kb=%M' \
     timeout 180s "$MINIC_CC" --target=x86_64-linux -S \
     "$work/sqlite3.i" -o "$work/sqlite3.s" \
     >"$work/minic.stdout" 2>"$work/minic.stderr"; then
  echo "SQLITE_X86_64_COMPILER=PASS source=sqlite3.c"
else
  status=$?
  echo "SQLITE_X86_64_BLOCKER stage=MiniC status=$status source=sqlite3.c" >&2
  head -n 35 "$work/minic.stderr" >&2
  tail -n 8 "$work/minic.stderr" >&2
  # Parse line only as guidance, never modify the upstream TU.
  line=$(sed -n 's/.*sqlite3\.i:\([0-9][0-9]*\):.*/\1/p' "$work/minic.stderr" | head -n 1)
  if test -n "$line"; then
    start=$((line > 6 ? line - 6 : 1))
    end=$((line + 6))
    echo "SQLITE_X86_64_SOURCE_CONTEXT preprocessed_lines=$start-$end" >&2
    nl -ba "$work/sqlite3.i" | sed -n "${start},${end}p" >&2
  fi
  if test "$discovery" -eq 1; then
    echo "SQLITE_X86_64_FRONTIER=BLOCKED at=MiniC (not a pass)"
    exit 0
  fi
  exit 1
fi
if ! "$HOST_CC" -c -x assembler "$work/sqlite3.s" -o "$work/sqlite3-minic.o" \
        >"$work/assembler.stdout" 2>"$work/assembler.stderr"; then
  echo "SQLITE_X86_64_BLOCKER stage=GNU-assembler" >&2
  head -n 50 "$work/assembler.stderr" >&2
  if test "$discovery" -eq 1; then echo "SQLITE_X86_64_FRONTIER=BLOCKED at=assembler"; exit 0; fi
  exit 1
fi
echo "SQLITE_X86_64_OBJECT=PASS source=sqlite3.c"

cat > "$work/sqlite_workload.c" <<'EOF'
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int row(void *opaque, int n, char **values, char **names) {
    int j;
    (void)opaque;
    for (j = 0; j < n; ++j) {
        printf("%s=%s%s", names[j], values[j] ? values[j] : "NULL",
               j + 1 == n ? "\n" : "|");
    }
    return 0;
}
static void sql(sqlite3 *db, const char *text) {
    char *error = NULL;
    int rc = sqlite3_exec(db, text, row, NULL, &error);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLITE_QUERY_FAIL rc=%d detail=%s\n", rc,
                error ? error : sqlite3_errmsg(db));
        sqlite3_free(error);
        exit(2);
    }
}
int main(int argc, char **argv) {
    sqlite3 *db = NULL;
    int rc, i;
    sqlite3_stmt *stmt = NULL;
    if (argc != 2) return 1;
    rc = sqlite3_open(argv[1], &db);
    if (rc != SQLITE_OK) return 2;
    sql(db, "BEGIN; CREATE TABLE records(id INTEGER PRIMARY KEY,"
            "category TEXT NOT NULL, value INTEGER NOT NULL);"
            "INSERT INTO records VALUES(1,'alpha',7),(2,'beta',11),"
            "(3,'alpha',13),(4,'beta',17); COMMIT;");
    sql(db, "CREATE INDEX by_category ON records(category, value);");
    sql(db, "SELECT category,COUNT(*) AS n,SUM(value) AS total "
            "FROM records GROUP BY category ORDER BY category;");
    sql(db, "BEGIN; UPDATE records SET value=value+100 WHERE id=1; ROLLBACK;");
    sql(db, "SELECT value FROM records WHERE id=1;");
    rc = sqlite3_prepare_v2(db, "INSERT INTO records VALUES(?1, ?2, ?3)",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 3;
    for (i = 5; i <= 12; ++i) {
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_bind_text(stmt, 2, "gamma", -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, i * i);
        if (sqlite3_step(stmt) != SQLITE_DONE) return 4;
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);
    sql(db, "SELECT COUNT(*) AS n, SUM(value) AS total "
            "FROM records WHERE category='gamma';");
    sql(db, "SELECT name FROM sqlite_master WHERE type='index' ORDER BY name;");
    sql(db, "PRAGMA integrity_check;");
    printf("sqlite-version=%s\n", sqlite3_libversion());
    if (sqlite3_close(db) != SQLITE_OK) return 5;
    return 0;
}
EOF
# Build the independent oracle from the exact same preprocessed sqlite3.i,
# not from a system sqlite3 library or a different distro version.
"$HOST_CC" -O0 -std=gnu11 -c -x c "$work/sqlite3.i" -o "$work/sqlite3-gcc.o"
for compiler in gcc minic; do
  "$HOST_CC" -std=gnu11 -I"$src" "$work/sqlite_workload.c" \
    "$work/sqlite3-$compiler.o" -lm -ldl -lpthread \
    -o "$work/sqlite-$compiler.bin"
  "$work/sqlite-$compiler.bin" "$work/$compiler.sqlite" \
    >"$work/$compiler.stdout" 2>"$work/$compiler.stderr"
done
if ! cmp -s "$work/gcc.stdout" "$work/minic.stdout" ||
   ! cmp -s "$work/gcc.stderr" "$work/minic.stderr"; then
  echo "SQLITE_X86_64_DIFFERENTIAL=FAIL" >&2
  diff -u "$work/gcc.stdout" "$work/minic.stdout" >&2 || true
  diff -u "$work/gcc.stderr" "$work/minic.stderr" >&2 || true
  exit 1
fi
grep -F "sqlite-version=$release" "$work/minic.stdout" >/dev/null
echo "SQLITE_X86_64_RUNTIME=PASS source=sqlite3.c oracle=GCC byte-exact sql=transactions,index,prepared,rollback,integrity_check"
