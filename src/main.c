#include "prompt.h"
#include "executor.h"
#include "parser.h"
#include "builtins.h"
#include "jobs.h"
#include <history.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>   // for fork/exec, exit handling
#include <signal.h>
#include <string.h>

static void handle_sigint(int sig) 
{
    // forward SIGINT to current foreground pgid
    jobs_send_signal_to_foreground(SIGINT);
    // do not exit
}

static void handle_sigtstp(int sig) 
{
    // forward SIGTSTP to current foreground pgid
    jobs_send_signal_to_foreground(SIGTSTP);
    // shell should not stop
}

int main(void) 
{
    prompt_init();
    history_init();

    char *line = NULL; // will store user input
    size_t len = 0;

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = handle_sigtstp;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &sa, NULL);

    // registers handlers for sigint and sigtstp    
    
    while (1) 
    {
        prompt_print();

        ssize_t read = getline(&line, &len, stdin);
        if (read == -1) // -1 particularly corresponds to 
        { 
            // Ctrl-D (EOF) detected
            jobs_kill_all();           // kill all child jobs
            printf("logout\n");        // required by spec
            free(line);
            exit(0);                   // exit with status 0
        }

        if (read > 0 && line[read - 1] == '\n') 
        {
            line[read - 1] = '\0'; // strip \n character
        }

        if (line[0] != '\0') // skip blank commands
        {
            // check background jobs before parsing input (per spec)
            jobs_check_completed(); // if completed print their o/p or wtv
    
            parse_result res = parse_shell_cmd(line);
            if (!res.valid) 
            {
                printf("Invalid Syntax!\n");
            } 
            else if (res.is_builtin) 
            {
                int code = run_builtin(&res);
                if (code == SHELL_EXIT) { free_parse_result(&res); break; }
            } 
            else 
            {
                // run external commands (supports pipes/redirs/sequences/background)
                execute_shell_cmd(line);
            }

            if (res.valid && !res.is_builtin) 
            {
                history_add(line);
            }
            else if (res.is_builtin && strcmp(res.argv[0], "log") != 0) 
            {
                history_add(line);
            }

            free_parse_result(&res);
        }
    }

    free(line);
    return 0;
}
