#pragma once

#include "job.h"

/*
 * Parses command line arguments to extract input file path,
 * max parallel jobs, and output directory.
 * Returns 0 on success, or non-zero on error.
 */
int parse_arguments(int argc, char *argv[], char **input_path, int *max_parallel, char **output_dir);

/*
 * Reads the jobs TSV file, parses the jobs, and allocates an array of Job structs.
 * Updates job_count with the number of jobs read.
 * Returns a pointer to the jobs array on success, or NULL on failure.
 */
Job* parse_jobs_file(const char *file_path, int *job_count);