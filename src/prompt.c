// prompt.c - code to produce <user@host:cwd_with_tilde> prompt

#define _POSIX_C_SOURCE 200809L
#include "prompt.h"
#include <pwd.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>

static char username[128];         // user's login name
static char hostname[128];         // machine host name
char start_cwd[PATH_MAX];          // directory where shell is started

void prompt_init()
{
    struct passwd *pw = getpwuid(geteuid());

    if (pw && pw->pw_name) {
        strncpy(username, pw->pw_name, sizeof(username) - 1);
        username[sizeof(username) - 1] = '\0';
    } else {
        snprintf(username, sizeof(username), "%d", (int)geteuid());
    }

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(hostname, "unknown", sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }

    getcwd(start_cwd, sizeof(start_cwd));    // shell "home"
}

void prompt_print()
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) 
    {
        printf("<%s@%s:? >", username, hostname);
        fflush(stdout);
        return;
    }

    size_t shellhome_length = strlen(start_cwd);
    if (shellhome_length > 0 && strncmp(cwd, start_cwd, shellhome_length) == 0) 
    {
        if (cwd[shellhome_length] == '\0') 
        {
            // exactly at shell home
            printf("<%s@%s:~> ", username, hostname);
        } 
        else if (cwd[shellhome_length] == '/') 
        {
            // inside shell home
            printf("<%s@%s:~%s> ", username, hostname, cwd + shellhome_length);
        } 
        else 
        {
            // fallback (shouldn’t usually hit, but safe)
            printf("<%s@%s:%s> ", username, hostname, cwd);
        }
    } 
    else 
    {
        // cwd is outside shell home → print absolute path
        printf("<%s@%s:%s> ", username, hostname, cwd);
    }

    fflush(stdout);
}
