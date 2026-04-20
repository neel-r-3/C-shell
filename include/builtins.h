#ifndef BUILTINS_H
#define BUILTINS_H

#include "parser.h"

// Return codes for builtins
#define SHELL_SUCCESS 0
#define SHELL_EXIT    1   // signals main.c to exit the shell

// Core dispatcher (called from main.c)
int run_builtin(parse_result *res);

// Individual builtins
void builtin_hop(char **args, int argc);
int builtin_reveal(char **args, int argc);
int builtin_log(int argc, char **argv);
// in history.c
// Exotic intrinsics
void builtin_activities(char **args, int argc);
void builtin_ping(char **args, int argc);
void builtin_fg(char **args, int argc);
void builtin_bg(char **args, int argc);


#endif // BUILTINS_H
