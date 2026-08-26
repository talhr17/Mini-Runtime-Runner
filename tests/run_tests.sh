#!/usr/bin/env bash
# Automated test suite for taskrunner.
# Usage: ./tests/run_tests.sh   (or: make test)
# Exits non-zero if any test fails.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/bin/taskrunner"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASS=0
FAIL=0

pass() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
fail() { printf '  [FAIL] %s\n' "$1"; printf '         %s\n' "$2"; FAIL=$((FAIL + 1)); }

check() {
    # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then
        pass "$1"
    else
        fail "$1" "expected '$2', got '$3'"
    fi
}

if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found. Run 'make' first." >&2
    exit 1
fi

# --- Test 1: successful job, stdout captured, exit code 0 ---------------------
echo "Test 1: successful execution and captured stdout"
D="$WORK/t1"
printf 'hello_job\t2000\tprintf %s\n' "'hello world'" > "$WORK/t1.tsv"
"$BIN" --input "$WORK/t1.tsv" --max-parallel 1 --output-dir "$D" > /dev/null 2>&1
check "exit code is 0 when all jobs succeed" "0" "$?"
check "stdout captured to <job_id>.out.log" "hello world" "$(cat "$D/hello_job.out.log" 2>&1)"
check "status is success with exit_code 0 and empty signal" "hello_job,success,0," \
      "$(grep '^hello_job,' "$D/summary.csv" | cut -d, -f1-4)"

# --- Test 2: non-zero exit, stderr captured -----------------------------------
echo "Test 2: non-zero exit code and captured stderr"
D="$WORK/t2"
printf 'bad_job\t2000\techo problem >&2; exit 7\n' > "$WORK/t2.tsv"
"$BIN" --input "$WORK/t2.tsv" --max-parallel 1 --output-dir "$D" > /dev/null 2>&1
check "exit code is 1 when a job fails" "1" "$?"
check "stderr captured to <job_id>.err.log" "problem" "$(cat "$D/bad_job.err.log" 2>&1)"
check "status is failed with exit_code 7" "bad_job,failed,7," \
      "$(grep '^bad_job,' "$D/summary.csv" | cut -d, -f1-4)"

# --- Test 3: timeout ----------------------------------------------------------
echo "Test 3: job exceeding its timeout is terminated"
D="$WORK/t3"
printf 'slow_job\t300\tsleep 5\n' > "$WORK/t3.tsv"
START=$(date +%s%N)
"$BIN" --input "$WORK/t3.tsv" --max-parallel 1 --output-dir "$D" > /dev/null 2>&1
RC=$?
ELAPSED=$(( ($(date +%s%N) - START) / 1000000 ))
check "exit code is 1 when a job times out" "1" "$RC"
check "status is timeout" "slow_job,timeout" \
      "$(grep '^slow_job,' "$D/summary.csv" | cut -d, -f1-2)"
if [ "$ELAPSED" -lt 2000 ]; then
    pass "killed well before the 5s command would finish (${ELAPSED}ms)"
else
    fail "timeout enforced" "took ${ELAPSED}ms, expected under 2000ms"
fi

# --- Test 4: the parallelism limit is respected -------------------------------
# Each job records a marker when it starts and another when it ends. Replaying
# that sequence gives the peak number of jobs that were alive at the same time.
echo "Test 4: parallelism limit is respected"
D="$WORK/t4"
MARKERS="$WORK/markers.txt"
: > "$MARKERS"
for i in 1 2 3 4 5 6; do
    printf 'p%d\t5000\techo S >> %s; sleep 0.4; echo E >> %s\n' "$i" "$MARKERS" "$MARKERS"
done > "$WORK/t4.tsv"
"$BIN" --input "$WORK/t4.tsv" --max-parallel 2 --output-dir "$D" > /dev/null 2>&1
PEAK=$(awk '/^S$/ { c++; if (c > m) m = c } /^E$/ { c-- } END { print m + 0 }' "$MARKERS")
check "6 jobs all ran" "6" "$(grep -c '^S$' "$MARKERS")"
if [ "$PEAK" -le 2 ] && [ "$PEAK" -ge 1 ]; then
    pass "never exceeded --max-parallel 2 (peak concurrency: $PEAK)"
else
    fail "concurrency limit" "peak concurrency was $PEAK, expected at most 2"
fi
check "all 6 jobs reported in summary" "6" "$(($(wc -l < "$D/summary.csv") - 1))"

# --- Test 5: malformed input prevents all jobs from starting ------------------
echo "Test 5: malformed input rejected before any job starts"
run_invalid() {
    # run_invalid <description> <tsv content>
    local desc="$1" content="$2" dir="$WORK/inv_$RANDOM"
    printf '%b' "$content" > "$WORK/invalid.tsv"
    local err
    err=$("$BIN" --input "$WORK/invalid.tsv" --max-parallel 2 --output-dir "$dir" 2>&1 >/dev/null)
    local rc=$?
    if [ "$rc" -ne 2 ]; then
        fail "$desc" "expected exit code 2, got $rc"
    elif [ -d "$dir" ] && [ -n "$(ls -A "$dir" 2>/dev/null)" ]; then
        fail "$desc" "jobs produced output despite invalid input"
    elif [ -z "$err" ]; then
        fail "$desc" "no error message printed"
    else
        pass "$desc"
    fi
}

run_invalid "duplicate job_id rejected"        'dup\t1000\techo a\ndup\t1000\techo b\n'
run_invalid "invalid job_id characters rejected" 'bad id!\t1000\techo a\n'
run_invalid "zero timeout rejected"            'z\t0\techo a\n'
run_invalid "negative timeout rejected"        'n\t-5\techo a\n'
run_invalid "non-numeric timeout rejected"     'n\tabc\techo a\n'
run_invalid "empty command rejected"           'e\t1000\t\n'
run_invalid "missing third field rejected"     'only\t1000\n'

# A valid job placed after a bad line must not run: the whole file is
# validated before anything is launched.
D="$WORK/t5b"
printf 'good\t1000\ttouch %s/ran.marker\nbad id!\t1000\techo x\n' "$WORK" > "$WORK/t5b.tsv"
rm -f "$WORK/ran.marker"
"$BIN" --input "$WORK/t5b.tsv" --max-parallel 2 --output-dir "$D" > /dev/null 2>&1
check "exit code 2 on invalid input" "2" "$?"
if [ -e "$WORK/ran.marker" ]; then
    fail "no job starts when a later line is invalid" "an earlier valid job was executed"
else
    pass "no job starts when a later line is invalid"
fi

# --- Test 6: comments, blank lines and CLI validation -------------------------
echo "Test 6: input hygiene and argument validation"
D="$WORK/t6"
printf '# comment\n\n   \n   # indented comment\nreal\t1000\ttrue\n' > "$WORK/t6.tsv"
"$BIN" --input "$WORK/t6.tsv" --max-parallel 1 --output-dir "$D" > /dev/null 2>&1
check "exit 0 with only comments and one job" "0" "$?"
check "comments and blank lines skipped" "1" "$(($(wc -l < "$D/summary.csv") - 1))"

for bad_arg in "abc" "0" "-1" "2x"; do
    "$BIN" --input "$WORK/t6.tsv" --max-parallel "$bad_arg" --output-dir "$WORK/t6b" >/dev/null 2>&1
    check "--max-parallel '$bad_arg' rejected with exit 2" "2" "$?"
done
"$BIN" --input "$WORK/does_not_exist.tsv" --max-parallel 1 --output-dir "$WORK/t6c" >/dev/null 2>&1
check "missing input file rejected with exit 2" "2" "$?"
"$BIN" --input "$WORK/t6.tsv" --output-dir "$WORK/t6d" >/dev/null 2>&1
check "missing --max-parallel rejected with exit 2" "2" "$?"

# --- Test 7: output directory creation and no leftover children ---------------
echo "Test 7: output directory and process cleanup"
D="$WORK/created/by/run"
rm -rf "$WORK/created"
mkdir -p "$WORK/created/by"
printf 'mk\t2000\ttrue\n' > "$WORK/t7.tsv"
"$BIN" --input "$WORK/t7.tsv" --max-parallel 1 --output-dir "$D" > /dev/null 2>&1
check "missing output directory is created" "0" "$?"
[ -f "$D/summary.csv" ] && pass "summary.csv written into new directory" \
    || fail "summary.csv written into new directory" "file not found"

ZOMBIES=$(ps -o stat= -g $$ 2>/dev/null | grep -c 'Z' || true)
check "no zombie processes left behind" "0" "$ZOMBIES"

# --- Test 8: input order is preserved in the summary --------------------------
echo "Test 8: input order preserved in summary.csv"
D="$WORK/t8"
printf 'zebra\t2000\tsleep 0.3\napple\t2000\ttrue\nmango\t2000\ttrue\n' > "$WORK/t8.tsv"
"$BIN" --input "$WORK/t8.tsv" --max-parallel 3 --output-dir "$D" > /dev/null 2>&1
check "rows follow input order, not completion order" "zebra apple mango" \
      "$(tail -n +2 "$D/summary.csv" | cut -d, -f1 | tr '\n' ' ' | sed 's/ $//')"

# --- Summary ------------------------------------------------------------------
echo
echo "-----------------------------------------"
printf 'Passed: %d   Failed: %d\n' "$PASS" "$FAIL"
echo "-----------------------------------------"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0
