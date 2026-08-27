# Mini Runtime Runner

A small Linux command-line tool that reads a list of jobs from a TSV file, runs
them with a configurable parallelism limit, enforces a per-job timeout, captures
each job's output to disk, and reports the results.

## Build

Requires Linux, GCC and GNU Make. No third-party libraries.    

```sh
make            # builds bin/taskrunner
make test       # runs the automated test suite
make clean      # removes objects, bin/ and run/
```

The code compiles cleanly with `-Wall -Wextra -Wpedantic -std=c11`.

## Usage

```sh
./bin/taskrunner --input examples/jobs.tsv --max-parallel 2 --output-dir run
```

| Flag | Meaning |
| --- | --- |
| `--input` | Path to the job file (TSV). |
| `--max-parallel` | Positive integer; maximum number of jobs running at once. |
| `--output-dir` | Directory for logs and the summary. Created if missing. |

### Input format

```
job_id<TAB>timeout_ms<TAB>command
```

Blank lines and lines whose first non-space character is `#` are ignored.
`job_id` must be unique and match `[A-Za-z0-9._-]+`, `timeout_ms` must be a
positive integer, and `command` must be non-empty. The command is passed to
`/bin/sh -c`, so shell syntax such as redirection and `&&` works.

### Output

- `<output-dir>/<job_id>.out.log` — the job's standard output.
- `<output-dir>/<job_id>.err.log` — the job's standard error.
- `<output-dir>/summary.csv` — one row per job, in input order:

```
job_id,status,exit_code,signal,duration_ms
```

`status` is `success` for exit code 0, `failed` for a non-zero exit or an
unexpected signal, and `timeout` when the job was killed by the timeout path.
`exit_code` and `signal` are left empty when they do not apply.

### Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Every job succeeded. |
| 1 | At least one job failed or timed out. |
| 2 | Usage, input validation, or setup error. |

## Design choices

**Modules.** `parser` handles argument and file parsing, `runner` owns the
process lifecycle, `reporter` writes the summary, and `main` wires them together
and decides the exit code. Each module has a header exposing only what callers
need.

**Validate everything up front.** `parse_jobs_file` reads and checks the whole
file before `run_jobs` is called, so an error on the last line means no job runs
at all. Errors always name the offending line number.

**Fields are split with `strchr`, not `sscanf`.** Only the first two tabs are
separators; everything after them is the command. This means a command may
itself contain tab characters without breaking the record.

**No threads.** The runner keeps a slot table of active children and polls in a
loop with a 10 ms `nanosleep`. Each pass reaps finished children with
`waitpid(WNOHANG)`, checks running jobs against their deadlines, and fills any
free slots in input order. Sleeping between passes keeps CPU usage near zero,
which a tight busy loop would not.

**Timeouts use a monotonic clock.** Elapsed time comes from
`clock_gettime(CLOCK_MONOTONIC)`, so a wall-clock adjustment during a run cannot
shorten or extend a timeout. On expiry the job gets `SIGTERM`, and `SIGKILL`
after a 250 ms grace period.

**Redirection happens in the child.** After `fork`, the child opens its two log
files, `dup2`s them onto stdout and stderr, and calls
`execl("/bin/sh", "sh", "-c", cmd)`. The parent never touches the job's output.

**Errors return, they do not exit.** Library functions report a diagnostic and
return an error value; only `main` decides the process exit code. This keeps
the modules testable and makes cleanup paths explicit.

## Testing approach

`make test` runs `tests/run_tests.sh`, a Bash script with no network access and
no interactive input. It builds temporary job files in a `mktemp -d` directory,
runs the binary, and asserts on exit codes, log contents, and `summary.csv`
rows. It prints a per-assertion PASS/FAIL line and exits non-zero if anything
fails.

Coverage (33 assertions):

1. A successful job: exit code 0, stdout captured, `success` row in the CSV.
2. A failing job: exit code 1, stderr captured, `failed` with exit code 7.
3. A timed-out job: `timeout` status, and the run finishes far sooner than the
   5-second command would have.
4. The parallelism limit: six jobs each append a marker on start and on finish;
   replaying that sequence shows peak concurrency never exceeded the limit.
5. Malformed input: duplicate IDs, illegal ID characters, zero, negative and
   non-numeric timeouts, empty commands and short rows all exit 2 — and a valid
   job listed *before* a bad line is confirmed never to have run.
6. Comment and blank-line handling, plus `--max-parallel` and missing-file
   argument validation.
7. Output directory creation and absence of zombie processes.
8. Summary rows follow input order rather than completion order.

## Assumptions

- The input file is trusted, as stated in the assignment. Commands are passed to
  `/bin/sh -c` without sanitising.
- Job IDs are used directly as log file names. The `[A-Za-z0-9._-]+` rule
  already excludes path separators, so no additional escaping is needed.
- A job that exits on its own during the 250 ms grace period is still reported
  as `timeout`, because it was already past its deadline.
- Files from a previous run with the same names are overwritten.

## Known limitations

- Job IDs are capped at 63 characters and commands at 255, and input lines at
  1023 bytes. All three are checked and reported rather than silently truncated,
  but a heap-allocated string would be more flexible.
- `--output-dir` is created one level deep only; `mkdir -p` behaviour for a
  nested path such as `a/b/c` is not implemented.
- Timeout resolution is bounded by the 10 ms poll interval, so a job may run up
  to roughly 10 ms past its deadline before `SIGTERM` is sent.
- Duplicate-ID detection is a linear scan, so parsing is O(n²) in the number of
  jobs. Fine for thousands of jobs, not for millions.
- If `fork` fails partway through a run, the program reports the error and
  exits without waiting for children already running.
- Only the direct child is signalled. A command that spawns its own background
  grandchildren may leave them running after a timeout; process groups would be
  the fix.

## What I would improve with more time

- Replace the fixed-size `id` and `cmd` buffers with dynamically allocated
  strings and drop the length limits entirely.
- Put each job in its own process group and signal the whole group, so timeouts
  reliably clean up grandchildren.
- Replace the polling loop with `signalfd`/`ppoll` on `SIGCHLD` plus a timer, to
  remove the 10 ms granularity and the periodic wakeups.
- Use a hash set for duplicate ID detection.
- Add a `--verbose` flag and a machine-readable JSON summary alongside the CSV.
- Run the test suite under Valgrind and ASan in CI.

## Time spent

ive spent approximately 10 hours, 50% of the time was dedicated to understand the assignment to create the enviroment and planning the architecture.
the rest of the time was dedicated to write the logic with gemini write the code and check with claude.



## AI use disclosure

**Tool used:** Claude, Gemini.

At first i sent the assignment file to gemini to help me understand which program i need to develop than used him to build the architecture.

Second part i sent the file also to claude telling to build me a guide that will help me develop stage by stage.

Used the guide to write the code with gemini.

Every stage i made was tested before i moved on to the next one.

Then used Claude as a reviewer against the assignment specification, and it
found real defects I had missed:

- `parse_jobs_file` read the fields in the wrong order (`id, command, timeout`
  instead of `id, timeout, command`), so the sample input from the assignment
  was rejected.
- `run_jobs` was being called twice from `main`, so every job executed twice.
- The output directory was never created, and `mkdir`'s return value was later
  ignored, so a failed setup still reported success.
- `atoi` accepted trailing garbage such as `2abc`, and overflows are undefined
  behaviour; this was replaced with `strtol` plus an end-pointer check.
- Duplicate IDs, the `[A-Za-z0-9._-]+` rule, and positive-timeout validation
  were missing entirely.

Claude also drafted the `tests/run_tests.sh` scaffolding and a first pass of
this README, both of which I reviewed and edited.

**Representative prompts.**

- "Go over the assignment PDF and tell me whether my implementation meets the
  requirements."
- "Is this Makefile ready to apply?"
- "Review my `parse_arguments` for argument validation edge cases."
- "Write the five required automated tests and make `make test` fail loudly."

**How I verified the output.** I did not accept any change on description alone.
Every fix was compiled with `-Wall -Wextra -Wpedantic` and run against the
assignment's own sample input, checking `summary.csv`, both log files, and the
process exit code. The parallelism limit was verified with start/finish markers
rather than by trusting timing. I confirmed the double-execution bug by having a
job append to a counter file and checking the line count, and confirmed no
zombies with `ps`.

One instructive case: at one point the program still failed on my machine after
the fixes were applied. The cause turned out to be a stale binary left in the
project root from an earlier build, being run instead of `bin/taskrunner` — a
reminder that a passing description is not a passing run.

I have read and understood every line in this repository and I am responsible
for all of it.
