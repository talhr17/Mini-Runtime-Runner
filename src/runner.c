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


static long long get_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        return 0;
    }
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


/* Slot states. */
#define SLOT_FREE     0  /* no child in this slot                         */
#define SLOT_RUNNING  1  /* child running, within its timeout             */
#define SLOT_TERMED   2  /* past deadline, SIGTERM sent, in grace period  */
#define SLOT_KILLED   3  /* grace period expired, SIGKILL already sent    */

typedef struct {
    pid_t pid;
    int job_index;
    long long start_time;
    long long term_time;
    int state; 
} ActiveProcess;

int run_jobs(Job *jobs, int job_count, int max_parallel, const char *output_dir) {
    int running_processes = 0;
    int current_job = 0;
    int setup_failed = 0;

    for (int i = 0; i < job_count; i++) {
        jobs[i].exit_code = -1;
        jobs[i].term_signal = -1;
        jobs[i].duration_ms = 0;
        strcpy(jobs[i].status, "failed"); 
    }

    ActiveProcess *active = calloc((size_t)max_parallel, sizeof(ActiveProcess));
    if (!active) {
        fprintf(stderr, "Error: out of memory allocating %d job slots\n", max_parallel);
        return -1;
    }

    /* Stop launching once setup fails, but keep looping until every child
     * already started has been reaped. */
    while ((current_job < job_count && !setup_failed) || running_processes > 0) {
        int status;
        pid_t done_pid;
        
        while ((done_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < max_parallel; i++) {
                if (active[i].state != SLOT_FREE && active[i].pid == done_pid) {
                    int j_idx = active[i].job_index;
                    jobs[j_idx].duration_ms = (int)(get_time_ms() - active[i].start_time);
                    
                    if (active[i].state >= SLOT_TERMED) { 
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
                    
                    active[i].state = SLOT_FREE;
                    running_processes--;
                    break;
                }
            }
        }

    
        long long now = get_time_ms();
        for (int i = 0; i < max_parallel; i++) {
            if (active[i].state == SLOT_RUNNING) { 
                int j_idx = active[i].job_index;
                if (now - active[i].start_time >= jobs[j_idx].timeout_ms) {
                    if (kill(active[i].pid, SIGTERM) != 0) {
                        perror("kill(SIGTERM)");
                    }
                    active[i].state = SLOT_TERMED; 
                    active[i].term_time = now;
                }
            } else if (active[i].state == SLOT_TERMED) { 
                /* Grace period expired: escalate once, not on every poll. */
                if (now - active[i].term_time >= 250) {
                    if (kill(active[i].pid, SIGKILL) != 0) {
                        perror("kill(SIGKILL)");
                    }
                    active[i].state = SLOT_KILLED;
                }
            }
        }

        while (!setup_failed && running_processes < max_parallel && current_job < job_count) {
            int slot = -1;
            for (int i = 0; i < max_parallel; i++) {
                if (active[i].state == SLOT_FREE) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                break;  /* defensive: should not happen while running < max */
            }

            pid_t pid = fork();
            if (pid == 0) {
                char out_path[512], err_path[512];
                snprintf(out_path, sizeof(out_path), "%s/%s.out.log", output_dir, jobs[current_job].id);
                snprintf(err_path, sizeof(err_path), "%s/%s.err.log", output_dir, jobs[current_job].id);
                
                /* Redirection must succeed: a job whose output cannot be
                 * captured is a failure, not something to run silently.
                 * _exit() is used throughout so the stdio buffers this child
                 * inherited from the parent are never flushed into a log. */
                int fd_out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0 || dup2(fd_out, STDOUT_FILENO) < 0) {
                    perror("cannot open stdout log");
                    _exit(126);
                }
                close(fd_out);

                int fd_err = open(err_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_err < 0 || dup2(fd_err, STDERR_FILENO) < 0) {
                    perror("cannot open stderr log");
                    _exit(126);
                }
                close(fd_err);

                execl("/bin/sh", "sh", "-c", jobs[current_job].cmd, (char *)NULL);
                perror("execl");
                _exit(127); 
            } else if (pid > 0) {
                active[slot].pid = pid;
                active[slot].job_index = current_job;
                active[slot].start_time = get_time_ms();
                active[slot].state = SLOT_RUNNING;
                running_processes++;
                current_job++;
            } else {
                /* Do not exit here: children are still running and must be
                 * terminated and reaped before this function returns. */
                perror("fork");
                setup_failed = 1;
                for (int i = 0; i < max_parallel; i++) {
                    if (active[i].state == SLOT_RUNNING) {
                        kill(active[i].pid, SIGTERM);
                        active[i].state = SLOT_TERMED;
                        active[i].term_time = get_time_ms();
                    }
                }
                break;
            }
        }

        struct timespec sleep_ts = {0, 10000000L}; 
        nanosleep(&sleep_ts, NULL);
    }
    free(active);
    return setup_failed ? -1 : 0;
}