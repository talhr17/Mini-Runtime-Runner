#pragma once

#include "job.h"


void run_jobs(Job *jobs, int job_count, int max_parallel, const char *output_dir);

