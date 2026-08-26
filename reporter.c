#include <stdio.h>
#include <string.h>
#include "reporter.h"

void generate_reports(Job *jobs, int job_count, const char *output_dir) {
    char csv_path[512];
    snprintf(csv_path, sizeof(csv_path), "%s/summary.csv", output_dir);

    FILE *csv_file = fopen(csv_path, "w");
    if (!csv_file) {
        perror("Error creating summary.csv");
        return;
    }

    // כתיבת כותרת ה-CSV לפי דרישות המטלה
    fprintf(csv_file, "job_id,status,exit_code,signal,duration_ms\n");

    printf("\n=== Execution Summary ===\n");

    for (int i = 0; i < job_count; i++) {
        char exit_str[16] = "";
        char sig_str[16] = "";

        // המרת המספרים למחרוזות, או השארתם ריקים אם הערך הוא -1
        if (jobs[i].exit_code != -1) {
            snprintf(exit_str, sizeof(exit_str), "%d", jobs[i].exit_code);
        }
        if (jobs[i].term_signal != -1) {
            snprintf(sig_str, sizeof(sig_str), "%d", jobs[i].term_signal);
        }

        // כתיבת השורה ל-CSV
        fprintf(csv_file, "%s,%s,%s,%s,%d\n", 
                jobs[i].id, jobs[i].status, exit_str, sig_str, jobs[i].duration_ms);

        // הדפסה קריאה לטרמינל
        printf("Job %s:\tStatus: %s\tDuration: %d ms\n", 
               jobs[i].id, jobs[i].status, jobs[i].duration_ms);
    }

    fclose(csv_file);
    printf("=========================\n");
    printf("CSV Summary written to: %s\n", csv_path);
}