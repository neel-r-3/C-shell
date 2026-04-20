#ifndef HISTORY_H
#define HISTORY_H

#define HISTORY_FILE ".my_shell_history"
#define HISTORY_MAX 15

void history_init();
void history_save();
void history_add(const char *cmd);
void history_print();
void history_purge();
const char *history_get(int index); // 1-indexed from newest

#endif
