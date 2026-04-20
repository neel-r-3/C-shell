#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

// Job states
typedef enum {
    JOB_RUNNING = 0,
    JOB_STOPPED = 1
} job_state_t;

typedef struct jobinfo {
    int jobnum;
    pid_t pid;
    char *cmdline;    // strdup'd
    job_state_t state;
} jobinfo_t;

// Add a background job. `cmdline` will be strdup'd.
// Returns job_number (1-based), or -1 on error.
int jobs_add(pid_t pid, const char *cmdline);

// Non-blocking: check for completed or stopped background jobs.
// Prints notifications per spec:
//  - On normal exit: "<cmdline> with pid <pid> exited normally"
//  - On abnormal exit: "<cmdline> with pid <pid> exited abnormally"
//  - On stop (WIFSTOPPED): prints "[job_number] Stopped <command_name>" and marks job as STOPPED
void jobs_check_completed(void);

// List active jobs for `activities` builtin. Caller should not free returned pointer.
// Returns number of jobs; out array is pointer to internal array (read-only).
int jobs_list(jobinfo_t **out_array);

// Find a job index by job number (-1 if not found)
int jobs_find_by_jobnum(int jobnum);

// Bring job to foreground: send SIGCONT if stopped, wait for it (blocking).
// Returns 0 on success, -1 if job not found.
int jobs_fg(int jobnum);

// Resume a stopped job in background: send SIGCONT, leave as background running.
// Returns 0 on success, -1 if job not found or job already running.
int jobs_bg(int jobnum);

// Get job info by jobnum (returns NULL if none)
jobinfo_t *jobs_get_by_jobnum(int jobnum);

// For signal handlers / executor:
// Set the foreground process group ID (pgid). Pass 0 to clear.
void jobs_set_foreground_pgid(pid_t pgid);
pid_t jobs_get_foreground_pgid(void);

// Send a signal to the current foreground process group (if any). Returns 0 on success or -1.
int jobs_send_signal_to_foreground(int sig);

// Kill all child jobs (used on Ctrl-D). Sends SIGKILL to all tracked pids.
void jobs_kill_all(void);

// Cleanup allocated job structures
void jobs_cleanup(void);

#endif // JOBS_H
