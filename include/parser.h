#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>

// Parsed command structure
typedef struct 
{
    bool valid;
    bool is_builtin;
    char **argv;    // argument vector (NULL-terminated)
    int argc;       // number of arguments
} parse_result;

parse_result parse_shell_cmd(const char *input);
void free_parse_result(parse_result *res);

#endif
