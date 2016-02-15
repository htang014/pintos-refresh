#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

struct exec_helper
{
  char *file_name;
  struct semaphore ld_sema;
  bool ld_success;
  struct thread *parent;
  //struct list children;  
};

static void *push (uint8_t *kpage, size_t *offset, const void *buf, size_t size);
tid_t process_execute (const char *file_name);
static void start_process (void *file_name_);//, struct exec_helper *parent_exec);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool load (const char *cmd_line, void (**eip) (void), void **esp);
#endif /* userprog/process.h */
