#ifndef SHOCKER_SIGNALS_H
#define SHOCKER_SIGNALS_H

#include <sys/types.h>

void signals_init(void);
void signals_set_active_child(pid_t pid);
void signals_clear_active_child(void);
int signals_exit_requested(void);

#endif