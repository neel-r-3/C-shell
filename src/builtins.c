#include "builtins.h"
#include "prompt.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include "history.h"
#include "executor.h"

// previous working directory for "hop -" and "reveal -"
char prev_cwd[PATH_MAX] = "";

// ------------------------- HOP -------------------------

void builtin_hop(char **args, int argc) 
{
    char cwd[PATH_MAX];
    char target[PATH_MAX];

    // Get current working directory
    if (getcwd(cwd, sizeof(cwd)) == NULL) 
    {
        perror("getcwd");
        return;
    }

    // Case 1: No args OR "~" → shell’s home dir
    if (argc == 1 || strcmp(args[1], "~") == 0) {
        strncpy(target, start_cwd, sizeof(target));

    // Case 2: "." → do nothing
    } else if (strcmp(args[1], ".") == 0) {
        return;

    // Case 3: ".." → parent dir
    } else if (strcmp(args[1], "..") == 0) {
        strncpy(target, cwd, sizeof(target));
        char *slash = strrchr(target, '/');
        if (slash && slash != target) {
            *slash = '\0';
        } else {
            // already at root
            return;
        }

    // Case 4: "-" → previous cwd
    } else if (strcmp(args[1], "-") == 0) {
        if (prev_cwd[0] == '\0') {
            printf("hop: No previous directory!\n");
            return;
        }
        strncpy(target, prev_cwd, sizeof(target));

    // Case 5: relative/absolute name
    } else {
        strncpy(target, args[1], sizeof(target));
    }

    target[sizeof(target) - 1] = '\0';

    // Save old cwd before hopping
    char old_cwd[PATH_MAX];
    strncpy(old_cwd, cwd, sizeof(old_cwd));

    // Try to hop
    if (chdir(target) != 0) {
        printf("No such directory!\n");
        return;
    }

    // Update prev_cwd (to old cwd before hop)
    strncpy(prev_cwd, old_cwd, sizeof(prev_cwd));
    prev_cwd[sizeof(prev_cwd) - 1] = '\0';

    // ⚠️ Removed prompt_print() — main loop handles it
}



// ------------------------- REVEAL -------------------------

int builtin_reveal(char **argv, int argc) 
{
    int show_all = 0;
    int line_format = 0;
    char *target = NULL;

    // Parse args
    for (int i = 1; i < argc; i++) 
    {
        // Special case: exactly "-" (previous directory)
        if (strcmp(argv[i], "-") == 0) {
            if (target) {
                fprintf(stderr, "reveal: Invalid Syntax!\n");
                return 1;
            }
            target = argv[i];
            continue;
        }

        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j] != '\0'; j++) 
            {
                if (argv[i][j] == 'a') show_all = 1;
                else if (argv[i][j] == 'l') line_format = 1;
                else {
                    // ignore unknown flags, per spec only a/l
                }
            }
        } else {
            if (target) {
                fprintf(stderr, "reveal: Invalid Syntax!\n");
                return 1;
            }
            target = argv[i];
        }
    }

    // Resolve directory path
    char path[PATH_MAX];
    if (!target) {
        strcpy(path, ".");
    } else if (strcmp(target, "~") == 0) {
        char *home = getenv("HOME");
        if (!home) home = ".";
        strcpy(path, home);
    } else if (strcmp(target, "-") == 0) {
        if (prev_cwd[0] == '\0') {
            printf("No such directory!\n");
            return 1;
        }
        strcpy(path, prev_cwd);
    } else {
        strcpy(path, target);
    }

    // Open dir
    DIR *dir = opendir(path);
    if (!dir) {
        printf("No such directory!\n");
        return 1;
    }

    // Collect entries
    struct dirent *entry;
    char **names = NULL;
    size_t count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (!show_all && entry->d_name[0] == '.') continue; // skip hidden
        names = realloc(names, sizeof(char *) * (count + 1));
        names[count++] = strdup(entry->d_name);
    }
    closedir(dir);

    // Sort
    int cmpstr(const void *a, const void *b) {
        return strcmp(*(char **)a, *(char **)b);
    }

    qsort(names, count, sizeof(char *), cmpstr);

    // Print
    for (size_t i = 0; i < count; i++) {
        if (line_format) {
            printf("%s\n", names[i]);
        } else {
            printf("%s", names[i]);
            if (i != count - 1) printf(" ");
        }
        free(names[i]);
    }
    if (!line_format && count > 0) printf("\n");
    free(names);

    return 0;
}


int builtin_log(int argc, char **argv) {
    if (argc == 1) {
        history_print();
        return 0;
    } else if (argc == 2 && strcmp(argv[1], "purge") == 0) {
        history_purge();
        return 0;
    } 
    else if (argc >= 3 && strcmp(argv[1], "execute") == 0) {
        int idx = atoi(argv[2]);
        if (idx <= 0) {
            printf("log: Invalid Syntax!\n");
            return 0;
        }
        const char *cmd = history_get(idx);
        if (!cmd) {
            printf("log: Invalid Syntax!\n");
            return 0;
        }
    
        // Rebuild command string: history command + any extra tokens after argv[2]
        size_t bufsize = strlen(cmd) + 1;
        for (int i = 3; i < argc; i++) bufsize += strlen(argv[i]) + 1;
    
        char *full_cmd = malloc(bufsize);
        strcpy(full_cmd, cmd);
    
        for (int i = 3; i < argc; i++) {
            strcat(full_cmd, " ");
            strcat(full_cmd, argv[i]);
        }
    
        // Execute the combined command string
        execute_shell_cmd(full_cmd);
    
        free(full_cmd);
        return 0;
    }
    else 
    {
        printf("log: Invalid Syntax!\n");
        return 0;
    }
}



int run_builtin(parse_result *res) 
{
    if (res->argc == 0) {
        return SHELL_SUCCESS; // nothing to do
    }

    if (strcmp(res->argv[0], "hop") == 0) {
        builtin_hop(res->argv, res->argc);
        return SHELL_SUCCESS;
    }
    else if (strcmp(res->argv[0], "reveal") == 0) {
        builtin_reveal(res->argv, res->argc);
        return SHELL_SUCCESS;
    }
    else if (strcmp(res->argv[0], "activities") == 0) {
        builtin_activities(res->argv, res->argc);
        return SHELL_SUCCESS;
    }
    else if (strcmp(res->argv[0], "fg") == 0) {
        builtin_fg(res->argv, res->argc);
        return SHELL_SUCCESS;
    }
    else if (strcmp(res->argv[0], "bg") == 0) {
        builtin_bg(res->argv, res->argc);
        return SHELL_SUCCESS;
    }
    else if (strcmp(res->argv[0], "ping") == 0) {
        builtin_ping(res->argv, res->argc);
        return SHELL_SUCCESS;
    }
    else if (strcmp(res->argv[0], "exit") == 0) {
        return SHELL_EXIT; // tell main.c to quit
    }
    else if (strcmp(res->argv[0], "log") == 0) 
    {
        return builtin_log(res->argc, res->argv);
    }

    // Not a recognized builtin
    fprintf(stderr, "Unknown builtin: %s\n", res->argv[0]);
    return SHELL_SUCCESS;
}





#include "jobs.h"
#include <signal.h>
#include <ctype.h>

// activities: prints [pid] : command_name - State sorted lexicographically by command_name (ASCII)
void builtin_activities(char **args, int argc) {
    jobinfo_t *arr = NULL;
    int n = jobs_list(&arr);
    if (n == 0) return;

    // Make array of pointers to jobinfo to sort by cmdline (ASCII)
    jobinfo_t *cpy = malloc(n * sizeof(jobinfo_t));
    if (!cpy) return;
    for (int i = 0; i < n; ++i) cpy[i] = arr[i];

    // simple qsort using ASCII strcmp on cmdline
    int cmp(const void *A, const void *B) {
        const jobinfo_t *a = A, *b = B;
        return strcmp(a->cmdline, b->cmdline);
    }
    qsort(cpy, n, sizeof(jobinfo_t), cmp);

    for (int i = 0; i < n; ++i) {
        const char *state = (cpy[i].state == JOB_RUNNING) ? "Running" : "Stopped";
        printf("[%d] : %s - %s\n", (int)cpy[i].pid, cpy[i].cmdline, state);
    }
    free(cpy);
}

// ping <pid> <signal_number>
void builtin_ping(char **args, int argc) {
    if (argc != 3) {
        printf("Invalid syntax!\n");
        return;
    }
    // validate pid numeric
    char *end;
    long pidl = strtol(args[1], &end, 10);
    if (*end != '\0' || pidl <= 0) {
        printf("No such process found\n");
        return;
    }
    long sig = strtol(args[2], &end, 10);
    if (*end != '\0') {
        printf("Invalid syntax!\n");
        return;
    }
    int actual = (int)(sig % 32);
    if (actual <= 0) actual = (actual + 32) % 32; // map negative properly (but kill with 0 is special)
    if (kill((pid_t)pidl, actual) == -1) {
        if (errno == ESRCH) {
            printf("No such process found\n");
        } else {
            perror("ping");
        }
        return;
    }
    printf("Sent signal %ld to process with pid %ld\n", sig, pidl);
}

// fg [jobnum]
void builtin_fg(char **args, int argc) {
    int jobnum = -1;
    if (argc == 1) {
        // find most recent job
        // jobs_list and pick highest jobnum
        jobinfo_t *arr = NULL;
        int n = jobs_list(&arr);
        if (n == 0) {
            printf("No such job\n");
            return;
        }
        int recent = -1;
        for (int i = 0; i < n; ++i) {
            if (arr[i].jobnum > recent) recent = arr[i].jobnum;
        }
        jobnum = recent;
    } else {
        char *end;
        long j = strtol(args[1], &end, 10);
        if (*end != '\0' || j <= 0) { printf("No such job\n"); return; }
        jobnum = (int)j;
    }
    // get job
    jobinfo_t *ji = jobs_get_by_jobnum(jobnum);
    if (!ji) {
        printf("No such job\n");
        return;
    }
    // print entire command when bringing to foreground:
    printf("%s\n", ji->cmdline);
    fflush(stdout);
    // call jobs_fg
    if (jobs_fg(jobnum) != 0) {
        printf("No such job\n");
    }
}

// bg [jobnum]
void builtin_bg(char **args, int argc) {
    int jobnum = -1;
    if (argc == 1) {
        // find most recent job
        jobinfo_t *arr = NULL;
        int n = jobs_list(&arr);
        if (n == 0) {
            printf("No such job\n");
            return;
        }
        int recent = -1;
        for (int i = 0; i < n; ++i) {
            if (arr[i].jobnum > recent) recent = arr[i].jobnum;
        }
        jobnum = recent;
    } else {
        char *end;
        long j = strtol(args[1], &end, 10);
        if (*end != '\0' || j <= 0) { printf("No such job\n"); return; }
        jobnum = (int)j;
    }

    int r = jobs_bg(jobnum);
    if (r == -1) {
        printf("No such job\n");
    } else if (r == 1) {
        printf("Job already running\n");
    } else {
        // jobs_bg prints the "[job_number] command &" line
    }
}
