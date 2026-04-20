#include "history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *history[HISTORY_MAX];
static int history_count = 0;
static int history_start = 0;

static char history_path[1024];
static int initializing = 0;  // flag to avoid saving during init

void history_init() {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(history_path, sizeof(history_path), "%s/%s", home, HISTORY_FILE);

    FILE *fp = fopen(history_path, "r");
    if (!fp) return;

    initializing = 1;  // suppress saves during init
    char *line = NULL;
    size_t len = 0;
    while (getline(&line, &len, fp) != -1) {
        if (line[strlen(line) - 1] == '\n')
            line[strlen(line) - 1] = '\0';
        history_add(line);
    }
    free(line);
    fclose(fp);
    initializing = 0;
}

void history_save() {
    FILE *fp = fopen(history_path, "w");
    if (!fp) return;

    int idx = history_start;
    for (int i = 0; i < history_count; i++) {
        fprintf(fp, "%s\n", history[idx]);
        idx = (idx + 1) % HISTORY_MAX;
    }
    fclose(fp);
}

void history_add(const char *cmd) {
    if (!cmd || !*cmd) return;
    if (strncmp(cmd, "log", 3) == 0 &&
       (cmd[3] == '\0' || cmd[3] == ' ')) return;

    // skip duplicate
    if (history_count > 0) {
        int last_idx = (history_start + history_count - 1) % HISTORY_MAX;
        if (strcmp(history[last_idx], cmd) == 0) return;
    }

    char *copy = strdup(cmd);
    if (history_count < HISTORY_MAX) {
        history[(history_start + history_count) % HISTORY_MAX] = copy;
        history_count++;
    } else {
        free(history[history_start]);
        history[history_start] = copy;
        history_start = (history_start + 1) % HISTORY_MAX;
    }

    if (!initializing) {
        history_save();
    }
}

void history_print() {
    int idx = history_start;
    for (int i = 0; i < history_count; i++) {
        printf("%s\n", history[idx]);
        idx = (idx + 1) % HISTORY_MAX;
    }
}

void history_purge() {
    for (int i = 0; i < history_count; i++) {
        int idx = (history_start + i) % HISTORY_MAX;
        free(history[idx]);
        history[idx] = NULL;
    }
    history_count = 0;
    history_start = 0;
    history_save();
}

const char *history_get(int index) {
    if (index <= 0 || index > history_count) return NULL;
    int idx = (history_start + history_count - index) % HISTORY_MAX;
    return history[idx];
}
