#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include "runner.h"


static long long get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


typedef struct {
    pid_t pid;
    int job_index;
    long long start_time;
    long long term_time;
    int state; 
} ActiveProcess;

void run_jobs(Job *jobs, int job_count, int max_parallel, const char *output_dir) {
    ActiveProcess *active = calloc(max_parallel, sizeof(ActiveProcess));
    int running_processes = 0;
    int current_job = 0;

    for (int i = 0; i < job_count; i++) {
        jobs[i].exit_code = -1;
        jobs[i].term_signal = -1;
        jobs[i].duration_ms = 0;
        strcpy(jobs[i].status, "failed"); 
    }

    while (current_job < job_count || running_processes > 0) {
        int status;
        pid_t done_pid;
        
        while ((done_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < max_parallel; i++) {
                if (active[i].state > 0 && active[i].pid == done_pid) {
                    int j_idx = active[i].job_index;
                    jobs[j_idx].duration_ms = (int)(get_time_ms() - active[i].start_time);
                    
                    if (active[i].state == 2) { 
                        strcpy(jobs[j_idx].status, "timeout");
                    } else if (WIFEXITED(status)) {
                        jobs[j_idx].exit_code = WEXITSTATUS(status);
                        if (jobs[j_idx].exit_code == 0) {
                            strcpy(jobs[j_idx].status, "success");
                        } else {
                            strcpy(jobs[j_idx].status, "failed");
                        }
                    } else if (WIFSIGNALED(status)) {
                        jobs[j_idx].term_signal = WTERMSIG(status);
                        strcpy(jobs[j_idx].status, "failed");
                    }
                    
                    active[i].state = 0;
                    running_processes--;
                    break;
                }
            }
        }

    
        long long now = get_time_ms();
        for (int i = 0; i < max_parallel; i++) {
            if (active[i].state == 1) { 
                int j_idx = active[i].job_index;
                if (now - active[i].start_time >= jobs[j_idx].timeout_ms) {
                    kill(active[i].pid, SIGTERM);
                    active[i].state = 2; 
                    active[i].term_time = now;
                }
            } else if (active[i].state == 2) { 
                if (now - active[i].term_time >= 250) {
                    kill(active[i].pid, SIGKILL);
                }
            }
        }

        while (running_processes < max_parallel && current_job < job_count) {
            int slot = -1;
            for (int i = 0; i < max_parallel; i++) {
                if (active[i].state == 0) {
                    slot = i;
                    break;
                }
            }

            pid_t pid = fork();
            if (pid == 0) {
                char out_path[512], err_path[512];
                snprintf(out_path, sizeof(out_path), "%s/%s.out.log", output_dir, jobs[current_job].id);
                snprintf(err_path, sizeof(err_path), "%s/%s.err.log", output_dir, jobs[current_job].id);
                
                int fd_out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                int fd_err = open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                
                if (fd_out >= 0) { dup2(fd_out, STDOUT_FILENO); close(fd_out); }
                if (fd_err >= 0) { dup2(fd_err, STDERR_FILENO); close(fd_err); }

                execl("/bin/sh", "sh", "-c", jobs[current_job].cmd, (char *)NULL);
                exit(127); 
            } else if (pid > 0) {
                active[slot].pid = pid;
                active[slot].job_index = current_job;
                active[slot].start_time = get_time_ms();
                active[slot].state = 1;
                running_processes++;
                current_job++;
            } else {
                perror("fork failed");
                exit(2);
            }
        }

        struct timespec sleep_ts = {0, 10000000L}; 
        nanosleep(&sleep_ts, NULL);; 
    }
    free(active);
}