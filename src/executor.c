#define _POSIX_C_SOURCE 200809L
#include "executor.h"
#include "jobs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>

/*
  Implementation notes:

  - exec_pipeline_and_wait(const char *cmdgroup)
      Parse a single cmd_group (which may contain pipes and redirections).
      Fork child processes for pipeline, set up pipes & redirections, set process group,
      wait for them (foreground). If a child stops (SIGTSTP), we add it to jobs list
      and mark stopped.

  - execute_shell_cmd(const char *line)
      Top-level: scans the input line for segments separated by ';' or '&'
      and executes them in order. Foreground segments are waited for. Background
      segments are launched and jobs_add() is used to register them.
*/

// ----------------------------- small helpers -----------------------------

// trim whitespace (malloc'd result)

static char *trim_dup(const char *s) 
{
    while (*s && isspace((unsigned char)*s)) s++;
    const char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) e--;
    size_t len = e - s;
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

// split a pipeline string by '|' into segments; returns malloc'd NULL-terminated array; count returned
// Example: "ls -l | grep txt | wc -l"

// Becomes → ["ls -l", "grep txt", "wc -l"]
static char **split_pipes(const char *s, int *count_out) 
{
    // count
    int cap = 8;
    char **arr = malloc(cap * sizeof(char*));
    int n = 0;

    const char *p = s;
    const char *start = p;
    while (*p) {
        if (*p == '|') {
            size_t len = p - start;
            char *seg = malloc(len + 1);
            if (!seg) { for (int i=0;i<n;i++) free(arr[i]); free(arr); return NULL; }
            memcpy(seg, start, len); seg[len] = '\0';
            char *trimmed = trim_dup(seg);
            free(seg);
            if (!trimmed) { for (int i=0;i<n;i++) free(arr[i]); free(arr); return NULL; }
            if (n >= cap) { cap *= 2; arr = realloc(arr, cap * sizeof(char*)); }
            arr[n++] = trimmed;
            p++;
            start = p;
            continue;
        }
        p++;
    }
    // last
    char *last = trim_dup(start);
    if (!last) { for (int i=0;i<n;i++) free(arr[i]); free(arr); return NULL; }
    if (n >= cap) { cap *= 2; arr = realloc(arr, cap * sizeof(char*)); }
    arr[n++] = last;
    arr = realloc(arr, (n+1)*sizeof(char*));
    arr[n] = NULL;
    *count_out = n;
    return arr;
}

// parse a single pipeline segment into argv, and capture last input/output redirections
// argv_out must be freed (and its elements). last_input/last_output are malloc'd or NULL and must be free'd.
static int parse_segment(const char *seg, char ***argv_out, int *argc_out,
                         char **last_input, char **last_output, int *out_append) 
{
    const char *p = seg;
    int cap = 8, argc = 0;
    char **argv = malloc(cap * sizeof(char*));
    if (!argv) return -1;
    *last_input = NULL;
    *last_output = NULL;
    *out_append = 0;

    while (*p) {
        // skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (*p == '<') 
        {
            p++;
            while (*p && isspace((unsigned char)*p)) p++;
            const char *start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '<' && *p != '>') p++;
            size_t len = p - start;
            if (len == 0) {
                // syntax error: missing filename
                for (int i=0;i<argc;i++) free(argv[i]);
                free(argv);
                return -1;
            }
            char *name = malloc(len+1); memcpy(name, start, len); name[len] = '\0';
            if (*last_input) free(*last_input);
            *last_input = name;
            continue;
        } 
        else if (*p == '>') {
            p++;
            int append = 0;
            if (*p == '>') { append = 1; p++; }
            while (*p && isspace((unsigned char)*p)) p++;
            const char *start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '<' && *p != '>') p++;
            size_t len = p - start;
            if (len == 0) {
                for (int i=0;i<argc;i++) free(argv[i]);
                free(argv);
                return -1;
            }
            char *name = malloc(len+1); memcpy(name, start, len); name[len] = '\0';
            if (*last_output) free(*last_output);
            *last_output = name;
            *out_append = append;
            continue;
        } else {
            // token
            const char *start = p;
            while (*p && !isspace((unsigned char)*p) && *p != '<' && *p != '>') p++;
            size_t len = p - start;
            if (len == 0) continue;
            char *tok = malloc(len+1); memcpy(tok, start, len); tok[len] = '\0';
            if (argc >= cap) { cap *= 2; argv = realloc(argv, cap * sizeof(char*)); }
            argv[argc++] = tok;
        }
    }

    // finalize
    argv = realloc(argv, (argc+1)*sizeof(char*));
    argv[argc] = NULL;
    *argv_out = argv;
    *argc_out = argc;
    return 0;
}

// static void close_pipepair(int p[2]) {
//     close(p[0]);
//     close(p[1]);
// }

// --------------------- pipeline launcher ---------------------
//
// Launch pipeline for a single cmdgroup (may contain multiple segments separated by '|').
// If wait_foreground is non-zero, this function waits for all pipeline processes (foreground).
// If wait_foreground is zero, it does not wait (background).
//
// On success:
//   - if wait_foreground == 1 -> returns 0 after waiting
//   - if wait_foreground == 0 -> returns leader_pid (pgid) > 0  (caller should call jobs_add with that pid)
// On failure returns -1.
//
// Note: For background (wait_foreground==0) the parent configures process groups and returns the leader pid
// immediately after forking pipeline children (without waiting).
static int launch_pipeline(const char *cmdgroup, int wait_foreground) 
    {
    if (!cmdgroup) return -1;

    // split into pipeline segments
    int segcount = 0;
    char **segs = split_pipes(cmdgroup, &segcount);
    if (!segs || segcount == 0) {
        if (segs) { for (int i=0;i<segcount;i++) free(segs[i]); free(segs); }
        return -1;
    }

    // parse each segment for argv and redirs
    char ***argvs = calloc(segcount, sizeof(char**));
    int *argcs = calloc(segcount, sizeof(int));
    char **infiles = calloc(segcount, sizeof(char*));
    char **outfiles = calloc(segcount, sizeof(char*));
    int *out_append = calloc(segcount, sizeof(int));
    if (!argvs || !argcs || !infiles || !outfiles || !out_append) {
        goto cleanup_parse_fail;
    }

    for (int i=0;i<segcount;i++) {
        if (parse_segment(segs[i], &argvs[i], &argcs[i], &infiles[i], &outfiles[i], &out_append[i]) != 0) {
            goto cleanup_parse_fail;
        }
    }

    // if first command has zero argv, nothing to do
    if (argcs[0] == 0) {
        // free and return
        for (int i=0;i<segcount;i++) {
            if (argvs[i]) { for (int j=0;j<argcs[i];j++) free(argvs[i][j]); free(argvs[i]); }
            if (infiles[i]) free(infiles[i]);
            if (outfiles[i]) free(outfiles[i]);
        }
        free(argvs); free(argcs); free(infiles); free(outfiles); free(out_append);
        for (int i=0;i<segcount;i++) 
            free(segs[i]); 
        
        free(segs);
        return -1;
    }

    

    // create pipes
    int (*pipes)[2] = NULL;
    if (segcount > 1) {
        pipes = malloc((segcount-1) * sizeof(int[2]));
        if (!pipes) goto cleanup_parse_fail;
        for (int i=0;i<segcount-1;i++) {
            if (pipe(pipes[i]) == -1) {
                perror("pipe");
                // cleanup pipes created so far
                for (int k=0;k<i;k++) close(pipes[k][0]), close(pipes[k][1]);
                free(pipes);
                goto cleanup_parse_fail;
            }
        }
    }

    pid_t *pids = calloc(segcount, sizeof(pid_t));
    if (!pids) { if (pipes) free(pipes); goto cleanup_parse_fail; }

    pid_t leader_pid = 0;

    // fork children
    for (int i=0;i<segcount;i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            pids[i] = -1;
            // continue attempting other forks? for simplicity, mark error and break
            break;
        } else if (pid == 0) {
            // ---------------- child ----------------
            // Set process group: first child becomes group leader = its pid
            if (i == 0) {
                // create new pgid equal to this child's pid
                if (setpgid(0, 0) == -1) {
                    // ignore
                }
            } else {
                // join leader pgid; leader_pid may not be known here, use getppid? better to use leader pid stored via environment? but typical pattern:
                // setpgid(0, leader_pid) where parent had set leader's pgid already via setpgid after fork.
                // Since parent will setpgid for all children, try to setpgid to leader later; best-effort:
                // We try to setpgid to the pgid stored in pids[0] but in child we don't have access; however setpgid(0, <leader>) will be attempted by parent earlier.
                // For robustness, do nothing here for non-first; parent will setpgid after fork.
                ;
            }

            // redirect stdin/out for pipes
            if (i > 0) {
                if (dup2(pipes[i-1][0], STDIN_FILENO) == -1) {
                    perror("dup2");
                    _exit(1);
                }
            }
            if (i < segcount - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    _exit(1);
                }
            }

            // close all pipe fds in child
            if (pipes) {
                for (int k=0;k<segcount-1;k++) {
                    close(pipes[k][0]);
                    close(pipes[k][1]);
                }
            }

            // handle infile redirection for this segment
            if (infiles[i]) 
            {
                int fd = open(infiles[i], O_RDONLY);
                if (fd == -1) {
                    printf("No such file or directory\n");
                    _exit(1);
                }
                if (dup2(fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    close(fd);
                    _exit(1);
                }
                close(fd);
            }

            // handle outfile redirection for this segment
            if (outfiles[i]) {
                int fd;
                if (out_append[i]) {
                    fd = open(outfiles[i], O_WRONLY | O_CREAT | O_APPEND, 0644);
                } else {
                    fd = open(outfiles[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                }
                if (fd == -1) {
                    printf("Unable to create file for writing\n");
                    _exit(1);
                }
                if (dup2(fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    close(fd);
                    _exit(1);
                }
                close(fd);
            }

            // exec
            execvp(argvs[i][0], argvs[i]);
            // if exec returns, it's an error
            printf("Command not found!\n");
            _exit(127);
        } else {
            // ---------------- parent ----------------
            pids[i] = pid;
            if (i == 0) {
                leader_pid = pid;
                // set pgid for leader
                if (setpgid(pid, pid) == -1) {
                    // ignore - race possible
                }
            } else {
                // set pgid of this child to leader_pg
                if (setpgid(pid, leader_pid) == -1) {
                    // ignore
                }
            }

            // parent closes ends it doesn't need:
            if (i > 0) {
                // close previous read end in parent
                close(pipes[i-1][0]);
            }
            if (i < segcount - 1) {
                // close this write end in parent
                close(pipes[i][1]);
            }
        }
    } // end for fork children

    // parent: close remaining pipe fds
    if (pipes) {
        for (int k=0;k<segcount-1;k++) {
            // some may already be closed; ignore errors
            close(pipes[k][0]);
            close(pipes[k][1]);
        }
    }

    // If background: don't wait, return leader pid
    if (!wait_foreground) {
        // cleanup parse memory (but not child processes)
        free(pids);
        free(pipes);
        for (int i=0;i<segcount;i++) {
            if (argvs[i]) { for (int j=0;j<argcs[i];j++) free(argvs[i][j]); free(argvs[i]); }
            if (infiles[i]) free(infiles[i]);
            if (outfiles[i]) free(outfiles[i]);
        }
        free(argvs); free(argcs); free(infiles); free(outfiles); free(out_append);
        for (int i=0;i<segcount;i++) free(segs[i]);
        free(segs);
        return (int)leader_pid;
    }

    // Foreground: set foreground pgid for signal routing
    jobs_set_foreground_pgid(leader_pid);

    // wait for all children; detect stopped (WIFSTOPPED) and exited statuses
    int overall_status = 0;
int status;
pid_t w;

while ((w = waitpid(-leader_pid, &status, WUNTRACED)) > 0) {
    if (WIFSTOPPED(status)) {
        // Pipeline stopped (Ctrl-Z)
        int jobnum = jobs_add(leader_pid, cmdgroup);
        jobinfo_t *ji = jobs_get_by_jobnum(jobnum);
        if (ji) ji->state = JOB_STOPPED;
        printf("[%d] Stopped %s\n", jobnum, ji ? ji->cmdline : cmdgroup);
        fflush(stdout);
        overall_status = 0;
        break;  // stop handling; job control owns it now
    } else if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != 0)
            overall_status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        overall_status = 128 + WTERMSIG(status);
    }
}

    // clear foreground pgid
    jobs_set_foreground_pgid(0);

    // cleanup
    free(pids);
    if (pipes) free(pipes);
    for (int i=0;i<segcount;i++) {
        if (argvs[i]) { for (int j=0;j<argcs[i];j++) free(argvs[i][j]); free(argvs[i]); }
        if (infiles[i]) free(infiles[i]);
        if (outfiles[i]) free(outfiles[i]);
    }
    free(argvs); free(argcs); free(infiles); free(outfiles); free(out_append);
    for (int i=0;i<segcount;i++) free(segs[i]);
    free(segs);

    (void)overall_status;
    return 0;
    
cleanup_parse_fail:
    // free what we've allocated
    if (pids) free(pids);
    if (pipes) {
        for (int k=0;k<segcount-1;k++) { close(pipes[k][0]); close(pipes[k][1]); }
        free(pipes);
    }
    if (argvs) {
        for (int i=0;i<segcount;i++) {
            if (argvs[i]) { for (int j=0;j<argcs[i];j++) free(argvs[i][j]); free(argvs[i]); }
        }
        free(argvs);
    }
    if (argcs) free(argcs);
    if (infiles) {
        for (int i=0;i<segcount;i++) if (infiles[i]) free(infiles[i]);
        free(infiles);
    }
    if (outfiles) {
        for (int i=0;i<segcount;i++) if (outfiles[i]) free(outfiles[i]);
        free(outfiles);
    }
    if (out_append) free(out_append);
    for (int i=0;i<segcount;i++) if (segs && segs[i]) free(segs[i]);
    if (segs) free(segs);
    return -1;
}

// wrapper used by API: exec_pipeline_and_wait
int exec_pipeline_and_wait(const char *cmdgroup) {
    return launch_pipeline(cmdgroup, 1) == 0 ? 0 : -1;
}

// ----------------- top-level: handle ; and & -----------------

// execute a whole shell line possibly containing multiple cmd_groups separated by ';' or '&'.
// Foreground commands are waited for; background commands are launched and registered via jobs_add.
void execute_shell_cmd(const char *line) {
    if (!line) return;

    const char *p = line;
    while (*p) {
        // skip leading whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        const char *seg_start = p;
        int in_single = 0, in_double = 0;
        const char *seg_end = NULL;
        char sep = 0; // separator char that ended this segment (0, ';' or '&')
        while (*p) {
            if (*p == '\'' && !in_double) { in_single = !in_single; p++; continue; }
            if (*p == '"' && !in_single) { in_double = !in_double; p++; continue; }
            if (!in_single && !in_double && (*p == ';' || *p == '&')) {
                sep = *p;
                seg_end = p;
                p++; // skip separator
                break;
            }
            p++;
        }
        if (!seg_end) seg_end = p;
        // trim trailing whitespace
        const char *tend = seg_end;
        while (tend > seg_start && isspace((unsigned char)*(tend-1))) tend--;
        size_t seg_len = tend - seg_start;
        if (seg_len == 0) {
            // nothing here; continue
            continue;
        }
        char *segment = malloc(seg_len + 1);
        if (!segment) return;
        memcpy(segment, seg_start, seg_len);
        segment[seg_len] = '\0';

        if (sep == '&') {
            // launch in background: create pipeline but do not wait; get leader pid
            int leader = launch_pipeline(segment, 0);
            if (leader > 0) {
                // register job with leader pid (jobs_add prints [jobnum] pid)
                jobs_add((pid_t)leader, segment);
            } else {
                // pipeline failed to launch; per spec, continue with next commands
            }
            free(segment);
            // continue to next segment
            continue;
        } else {
            // foreground: run and wait (exec_pipeline_and_wait)
            exec_pipeline_and_wait(segment);
            free(segment);
            // After a foreground command completes, continue to next segment
            continue;
        }
    }
}
