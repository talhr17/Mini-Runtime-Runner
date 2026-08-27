#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include "parser.h"

#define MAX_LINE_LEN 1024
#define INITIAL_CAPACITY 16

/* Parses a strictly positive integer. Returns 0 on success, -1 on error. */
static int parse_positive_int(const char *s, long max, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v <= 0 || v > max) {
        return -1;
    }
    *out = v;
    return 0;
}

/* job_id must match [A-Za-z0-9._-]+ */
static int is_valid_job_id(const char *id) {
    if (*id == '\0') {
        return 0;
    }
    for (const char *p = id; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '.' && *p != '_' && *p != '-') {
            return 0;
        }
    }
    return 1;
}

int parse_arguments(int argc, char *argv[], char **input_path, int *max_parallel, char **output_dir) {
    static struct option long_options[] = {
        {"input",        required_argument, 0, 'i'},
        {"max-parallel", required_argument, 0, 'p'},
        {"output-dir",   required_argument, 0, 'o'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;

    while ((opt = getopt_long(argc, argv, "i:p:o:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'i':
                *input_path = optarg;
                break;
            case 'p': {
                long v;
                if (parse_positive_int(optarg, INT_MAX, &v) != 0) {
                    fprintf(stderr, "Error: --max-parallel must be a positive integer, got '%s'\n", optarg);
                    return -1;
                }
                *max_parallel = (int)v;
                break;
            }
            case 'o':
                *output_dir = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s --input <file> --max-parallel <num> --output-dir <dir>\n", argv[0]);
                return -1;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "Error: unexpected argument '%s'\n", argv[optind]);
        return -1;
    }
    if (!*input_path || !*output_dir || *max_parallel <= 0) {
        fprintf(stderr, "Usage: %s --input <file> --max-parallel <num> --output-dir <dir>\n", argv[0]);
        return -1;
    }
    return 0;
}

Job *parse_jobs_file(const char *filepath, int *out_count) {
    FILE *file = fopen(filepath, "r");
    if (!file) {
        fprintf(stderr, "Error: cannot open input file '%s': %s\n", filepath, strerror(errno));
        return NULL;
    }

    int capacity = INITIAL_CAPACITY;
    Job *jobs = malloc((size_t)capacity * sizeof(Job));
    if (!jobs) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(file);
        return NULL;
    }

    int count = 0;
    int line_num = 0;
    char line[MAX_LINE_LEN];

    while (fgets(line, sizeof(line), file)) {
        line_num++;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] != '\n' && !feof(file)) {
            fprintf(stderr, "Error: line %d exceeds the %d byte limit\n", line_num, MAX_LINE_LEN - 1);
            goto fail;
        }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        /* Skip blank lines and comments (first non-space character is '#'). */
        const char *scan = line;
        while (*scan == ' ' || *scan == '\t') {
            scan++;
        }
        if (*scan == '\0' || *scan == '#') {
            continue;
        }

        /* Split into exactly three tab-separated fields. */
        char *tab1 = strchr(line, '\t');
        if (!tab1) {
            fprintf(stderr, "Error: line %d: expected 3 tab-separated fields\n", line_num);
            goto fail;
        }
        *tab1 = '\0';
        char *tab2 = strchr(tab1 + 1, '\t');
        if (!tab2) {
            fprintf(stderr, "Error: line %d: expected 3 tab-separated fields\n", line_num);
            goto fail;
        }
        *tab2 = '\0';

        const char *id      = line;
        const char *timeout = tab1 + 1;
        const char *command = tab2 + 1;   /* may itself contain tabs */

        if (!is_valid_job_id(id)) {
            fprintf(stderr, "Error: line %d: job_id '%s' must match [A-Za-z0-9._-]+\n", line_num, id);
            goto fail;
        }
        if (strlen(id) >= sizeof(jobs[0].id)) {
            fprintf(stderr, "Error: line %d: job_id exceeds %zu characters\n", line_num, sizeof(jobs[0].id) - 1);
            goto fail;
        }
        for (int i = 0; i < count; i++) {
            if (strcmp(jobs[i].id, id) == 0) {
                fprintf(stderr, "Error: line %d: duplicate job_id '%s'\n", line_num, id);
                goto fail;
            }
        }

        long timeout_value;
        if (parse_positive_int(timeout, INT_MAX, &timeout_value) != 0) {
            fprintf(stderr, "Error: line %d: timeout_ms must be a positive integer, got '%s'\n", line_num, timeout);
            goto fail;
        }

        if (*command == '\0') {
            fprintf(stderr, "Error: line %d: command must not be empty\n", line_num);
            goto fail;
        }
        if (strlen(command) >= sizeof(jobs[0].cmd)) {
            fprintf(stderr, "Error: line %d: command exceeds %zu characters\n", line_num, sizeof(jobs[0].cmd) - 1);
            goto fail;
        }

        if (count == capacity) {
            int new_capacity = capacity * 2;
            Job *grown = realloc(jobs, (size_t)new_capacity * sizeof(Job));
            if (!grown) {
                fprintf(stderr, "Error: out of memory\n");
                goto fail;
            }
            jobs = grown;
            capacity = new_capacity;
        }

        memset(&jobs[count], 0, sizeof(jobs[count]));
        memcpy(jobs[count].id,  id,      strlen(id) + 1);
        memcpy(jobs[count].cmd, command, strlen(command) + 1);
        jobs[count].timeout_ms = (unsigned int)timeout_value;
        count++;
    }

    if (ferror(file)) {
        fprintf(stderr, "Error: failed to read '%s': %s\n", filepath, strerror(errno));
        goto fail;
    }

    fclose(file);
    *out_count = count;
    return jobs;

fail:
    free(jobs);
    fclose(file);
    return NULL;
}