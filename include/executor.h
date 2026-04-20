#ifndef EXECUTOR_H
#define EXECUTOR_H

// Execute a single pipeline/cmd_group and wait for it (blocking).
// This is internal but declared for clarity if needed.
int exec_pipeline_and_wait(const char *cmdgroup);

// Top-level shell command executor that handles sequences and background markers.
// E.g. "cmd1 ; cmd2 & cmd3" — this will run cmd1 (foreground), then cmd2 in background, then cmd3 ...
// This function returns after the entire line's commands have been scheduled/executed
// (foreground commands complete; background ones are launched).
void execute_shell_cmd(const char *line);

#endif // EXECUTOR_H
