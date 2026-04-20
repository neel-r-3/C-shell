#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Simple tokenizer: split by whitespace
static char **tokenize(const char *input, int *count) // static implies private to this file
{
    char *copy = strdup(input); // duplicate string 
    char *tok = strtok(copy, " \t\r\n"); // splits by whitespace
    int cap = 8, n = 0;
    char **arr = malloc(cap * sizeof(char*));

    while (tok) 
    {
        if (n >= cap) 
        {
            cap *= 2;
            arr = realloc(arr, cap * sizeof(char*)); // 
        }
        arr[n++] = strdup(tok);
        tok = strtok(NULL, " \t\r\n");
    }
    arr = realloc(arr, (n+1)*sizeof(char*));
    arr[n] = NULL;
    *count = n;
    free(copy);

    return arr;
}

parse_result parse_shell_cmd(const char *input) 
{
    parse_result res = { .valid = false, .is_builtin = false, .argv = NULL, .argc = 0 };

    int argc;
    char **argv = tokenize(input, &argc);
    if (argc == 0) 
    {
        return res;
    }

    // Grammar validation can be extended. For now: disallow empty pipes/semicolons.
    for (int i=0;i<argc;i++) 
    {
        if ((strcmp(argv[i], "|")==0 || strcmp(argv[i], ";")==0) && (i==0 || i==argc-1)) 
        {
            // Pipe/semicolon at beginning or end
            goto cleanup;
        }
    }

    res.valid = true;
    res.argv = argv;
    res.argc = argc;

    // Builtins detection
    if (strcmp(argv[0], "hop") == 0 ||
    strcmp(argv[0], "reveal") == 0 ||
    strcmp(argv[0], "exit") == 0 ||
    strcmp(argv[0], "activities") == 0 ||
    strcmp(argv[0], "fg") == 0 ||
    strcmp(argv[0], "bg") == 0 ||
    strcmp(argv[0], "ping") == 0 || strcmp(argv[0],"log")==0) 
    {
        res.is_builtin = true;
    }


    return res;

cleanup:
    for (int i=0;i<argc;i++) free(argv[i]);
    free(argv);
    return res;
}

void free_parse_result(parse_result *res) 
{
    if (!res->argv) 
        return;

    for (int i=0;i<res->argc;i++) 
        free(res->argv[i]);
        
    free(res->argv);
    res->argv = NULL;
    res->argc = 0;
}

