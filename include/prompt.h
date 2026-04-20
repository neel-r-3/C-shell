#ifndef PROMPT_H
#define PROMPT_H

#include <limits.h>

extern char start_cwd[PATH_MAX];  // make shell home visible

void prompt_init();
void prompt_print();

#endif
