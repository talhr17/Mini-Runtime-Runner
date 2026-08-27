#pragma once

#include "job.h"


/*
 * Runs every job, never exceeding max_parallel concurrent children, and fills
 * in each job's status, exit_code, term_signal and duration_ms.
 *
 * On a setup failure (allocation or fork) the run is aborted, but every child
 * already started is still signalled and reaped before returning, so no zombie
 * is left behind. Jobs that never started keep their initial "failed" status.
 *
 * Returns 0 if the whole batch was launched, or -1 on a setup failure.
 */
int run_jobs(Job *jobs, int job_count, int max_parallel, const char *output_dir);

