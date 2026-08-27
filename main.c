#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "parser.h"
#include "runner.h"
#include "reporter.h"

/* Makes sure the output directory exists. Returns 0 on success, -1 on error. */
static int ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "Error: output path '%s' exists and is not a directory\n", path);
            return -1;
        }
        return 0;
    }
    if (mkdir(path, 0755) != 0) {
        fprintf(stderr, "Error: cannot create output directory '%s': %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char *input_path = NULL;
    int   max_parallel = 0;
    char *output_dir = NULL;

    if (parse_arguments(argc, argv, &input_path, &max_parallel, &output_dir) != 0) {
        return 2;
    }

    int job_count = 0;
    Job *jobs = parse_jobs_file(input_path, &job_count);
    if (!jobs) {
        return 2;
    }

    if (ensure_directory(output_dir) != 0) {
        free(jobs);
        return 2;
    }

    int run_failed = run_jobs(jobs, job_count, max_parallel, output_dir);
    generate_reports(jobs, job_count, output_dir);

    if (run_failed != 0) {
        fprintf(stderr, "Error: the run was aborted before every job could start\n");
        free(jobs);
        return 2;
    }

    int final_exit_code = 0;
    for (int i = 0; i < job_count; i++) {
        if (strcmp(jobs[i].status, "success") != 0) {
            final_exit_code = 1;
            break;
        }
    }

    free(jobs);
    return final_exit_code;
}