#pragma once

#include "job.h"

/*
 * מדפיס סיכום לטרמינל ומייצר את הקובץ summary.csv 
 */
void generate_reports(Job *jobs, int job_count, const char *output_dir);

