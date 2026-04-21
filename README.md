# C-shell

A custom UNIX-like command line interpreter (shell) written entirely in C.

## Features
- **Custom Prompt:** Displays interactive command prompt.
- **Built-in Commands:** Supports essential builtins like `cd`, `echo`, `pwd`, `exit`, etc.
- **Job Control:** Ability to execute commands in the background (using `&`) and manage active jobs.
- **History Tracking:** Maintains a history of executed commands across sessions.
- **Command Execution:** Standard foreground execution of system paths.

## Directory Structure
- `src/` - Source code for parser, execution, builtins, jobs, and history.
- `include/` - C header files defining the interface for the shell.
- `obj/` - Compiled `.o` files.
- `Makefile` - Build instructions.

## Build and Run
1. **Compile the shell:**
   ```bash
   make
   ```
2. **Execute the shell:**
   ```bash
   ./shell.out
   ```
