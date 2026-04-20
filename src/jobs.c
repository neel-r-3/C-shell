#define _POSIX_C_SOURCE 200809L
#include "jobs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

typedef struct bgjob {
    int jobnum;
    pid_t pid;
    char *cmdline;
    job_state_t state;
} bgjob_t;
    
static bgjob_t *jobs = NULL;
static int jobs_cap = 0;
static int jobs_len = 0;
static int next_jobnum = 1;
static pid_t current_fg_pgid = 0; // foreground process group id

static int ensure_cap(void) {
    if (jobs_len >= jobs_cap) {
        int newcap = jobs_cap == 0 ? 16 : jobs_cap * 2;
        bgjob_t *n = realloc(jobs, newcap * sizeof(bgjob_t));
        if (!n) return -1;
        jobs = n;
        jobs_cap = newcap;
    }
    return 0;
}

int jobs_add(pid_t pid, const char *cmdline) {
    if (ensure_cap() != 0) return -1;
    int idx = jobs_len++;
    jobs[idx].jobnum = next_jobnum++;
    jobs[idx].pid = pid;
    jobs[idx].cmdline = strdup(cmdline ? cmdline : "");
    jobs[idx].state = JOB_RUNNING;
    // print start message per spec
    printf("[%d] %d\n", jobs[idx].jobnum, (int)pid);
    fflush(stdout);
    return jobs[idx].jobnum;
}

static int find_index_by_pid(pid_t pid) {
    for (int i = 0; i < jobs_len; ++i) if (jobs[i].pid == pid) return i;
    return -1;
}

int jobs_list(jobinfo_t **out_array) {
    if (jobs_len == 0) {
        *out_array = NULL;
        return 0;
    }
    // create an array of jobinfo_t that points to copies (or internal entries) — spec requires lexical sorting by command name.
    // We'll build a temporary array of pointers to bgjob_t, sort by cmdline ASCII, and print in builtin.
    *out_array = (jobinfo_t*)malloc(sizeof(jobinfo_t) * jobs_len);
    if (!*out_array) return 0;
    for (int i = 0; i < jobs_len; ++i) {
        (*out_array)[i].jobnum = jobs[i].jobnum;
        (*out_array)[i].pid = jobs[i].pid;
        (*out_array)[i].cmdline = jobs[i].cmdline;
        (*out_array)[i].state = jobs[i].state;
    }
    return jobs_len;
}

jobinfo_t *jobs_get_by_jobnum(int jobnum) {
    for (int i = 0; i < jobs_len; ++i) {
        if (jobs[i].jobnum == jobnum) {
            // return pointer into heap array as jobinfo_t (we need to provide a stable pointer)
            // To keep API simple, allocate a small struct to return; caller must not free.
            // But simpler: convert index to pointer by returning cast; user must not modify.
            return (jobinfo_t *)&jobs[i];
        }
    }
    return NULL;
}

int jobs_find_by_jobnum(int jobnum) {
    for (int i = 0; i < jobs_len; ++i) if (jobs[i].jobnum == jobnum) return i;
    return -1;
}

static void remove_index(int idx) {
    if (idx < 0 || idx >= jobs_len) return;
    free(jobs[idx].cmdline);
    // replace with last
    if (idx != jobs_len - 1) jobs[idx] = jobs[jobs_len - 1];
    jobs_len--;
}

void jobs_check_completed(void) {
    int status;
    pid_t pid;
    // Use WNOHANG | WUNTRACED so we can see stopped children
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        int idx = find_index_by_pid(pid);
        if (idx == -1) {
            // not a background job tracked here; ignore
            continue;
        }
        if (WIFSTOPPED(status)) {
            // mark stopped and print "[job_number] Stopped command_name"
            jobs[idx].state = JOB_STOPPED;
            printf("[%d] Stopped %s\n", jobs[idx].jobnum, jobs[idx].cmdline);
            fflush(stdout);
            continue;
        }
        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code == 0) {
                printf("%s with pid %d exited normally\n", jobs[idx].cmdline, (int)pid);
            } else {
                printf("%s with pid %d exited abnormally\n", jobs[idx].cmdline, (int)pid);
            }
            fflush(stdout);
            remove_index(idx);
            continue;
        }
        if (WIFSIGNALED(status)) {
            // terminated by signal = abnormal
            printf("%s with pid %d exited abnormally\n", jobs[idx].cmdline, (int)pid);
            fflush(stdout);
            remove_index(idx);
            continue;
        }
        if (WIFCONTINUED(status)) {
            // resumed — mark running
            jobs[idx].state = JOB_RUNNING;
            continue;
        }
    }
}

void jobs_set_foreground_pgid(pid_t pgid) {
    current_fg_pgid = pgid;
}
pid_t jobs_get_foreground_pgid(void) {
    return current_fg_pgid;
}

int jobs_send_signal_to_foreground(int sig) {
    if (current_fg_pgid <= 0) return -1;
    // send to process group
    if (kill(-current_fg_pgid, sig) == -1) {
        return -1;
    }
    return 0;
}

int jobs_fg(int jobnum) {
    int idx = jobs_find_by_jobnum(jobnum);
    if (idx == -1) return -1;
    pid_t pid = jobs[idx].pid;
    // Bring to foreground: send SIGCONT if stopped
    if (jobs[idx].state == JOB_STOPPED) {
        if (kill(pid, SIGCONT) == -1) {
            return -1;
        }
        jobs[idx].state = JOB_RUNNING;
    }
    // Make this job the foreground pgid for signal routing (the job's pgid is the pid)
    jobs_set_foreground_pgid(pid);
    // Wait for it to finish or stop again
    int status;
    pid_t w = waitpid(pid, &status, WUNTRACED);
    // After wait, clear fg pgid
    jobs_set_foreground_pgid(0);
    if (w == -1) return -1;
    // handle state transitions: if stopped, mark stopped; if exited, remove job
    if (WIFSTOPPED(status)) {
        jobs[idx].state = JOB_STOPPED;
        printf("[%d] Stopped %s\n", jobs[idx].jobnum, jobs[idx].cmdline);
        fflush(stdout);
    } else {
        // treat exit as removal
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // normal exit — remove
            remove_index(idx);
        } else {
            // abnormal exit remove too
            remove_index(idx);
        }
    }
    return 0;
}

int jobs_bg(int jobnum) {
    int idx = jobs_find_by_jobnum(jobnum);
    if (idx == -1) return -1;
    if (jobs[idx].state == JOB_RUNNING) {
        // Already running
        return 1; // special code: job already running
    }
    pid_t pid = jobs[idx].pid;
    if (kill(pid, SIGCONT) == -1) return -1;
    jobs[idx].state = JOB_RUNNING;
    printf("[%d] %s &\n", jobs[idx].jobnum, jobs[idx].cmdline);
    fflush(stdout);
    return 0;
}

void jobs_kill_all(void) {
    for (int i = 0; i < jobs_len; ++i) {
        if (jobs[i].pid > 0) {
            kill(jobs[i].pid, SIGKILL);
        }
    }
}

void jobs_cleanup(void) {
    for (int i = 0; i < jobs_len; ++i) free(jobs[i].cmdline);
    free(jobs);
    jobs = NULL;
    jobs_cap = jobs_len = 0;
}
