#include "userprog/syscall.h"
#include <stdio.h>
#include <list.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

struct semaphore rw_sema;

static void syscall_handler (struct intr_frame *);
static void copy_in (void *dst_, const void *usrc_, size_t size);
static char* copy_in_string (const char *us);
static inline bool get_user (uint8_t *dst, const uint8_t *usrc);
static bool verify_user (const void *uaddr);
void exit (int status);
int write (int fd, const void *buffer, unsigned length);
bool create (const char *file, unsigned initial_size);
int open (const char *file);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  sema_init (&rw_sema, 1);  
}

static void
syscall_handler (struct intr_frame *f) 
{
  unsigned callNum;
  int args[3];

  if (!verify_user (f->esp))
    exit (-1); 

  copy_in (&callNum, f->esp, sizeof callNum);

  switch (callNum)
  {
    case SYS_HALT:
    {
      shutdown_power_off();
    }
    case SYS_EXIT:
    {
      copy_in (args, (uint32_t *) f->esp + 1, sizeof *args);
      exit (args[0]);
      break;
    }
    case SYS_CREATE:
    {
      copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 2);
      f->eax = create (args[0], args[1]);
      break; 
    }
    case SYS_WRITE:
    {
      copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 3);
      f->eax = write (args[0], args[1], args[2]);
      break;
    }
    case SYS_OPEN:
    {
      copy_in (args, (uint32_t *) f->esp + 1, sizeof *args);
      f->eax = open (args[0]);
      break;
    }
  }
}

/* Copies SIZE bytes from user address USRS to kernel address
   DST.
   Call thread_exit() if any of the user accesses are invalid. */
static void
copy_in (void *dst_, const void *usrc_, size_t size) 
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;
 
  for (; size > 0; size--, dst++, usrc++) 
    if (usrc >= (uint8_t *) PHYS_BASE || !get_user (dst, usrc)) 
      exit (-1);
}



/* Creates a copy of user string US in kernel memory
   and returns it as a page that must be freed with
   palloc_free_page().
   Truncates the string at PGSIZE bytes in size.
   Call thread_exit() if any of the user accesses are invalid. */
static char *
copy_in_string (const char *us) 
{
  char *ks;
  size_t length;
 
  ks = palloc_get_page (0);
  if (ks == NULL) 
    thread_exit ();
 
  for (length = 0; length < PGSIZE; length++)
    {
      if (us >= (char *) PHYS_BASE || !get_user (ks + length, us++)) 
        {
          palloc_free_page (ks);
          thread_exit (); 
        }
       
      if (ks[length] == '\0')
        return ks;
    }
  ks[PGSIZE - 1] = '\0';
  return ks;
}

/* Copies a byte from user address USRC to kernel address DST.
   USRC must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static inline bool
get_user (uint8_t *dst, const uint8_t *usrc)
{
  int eax;
  asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
       : "=m" (*dst), "=&a" (eax) : "m" (*usrc));
  return eax != 0;
}

/* Returns true if UADDR is a valid, mapped user address,
   false otherwise. */
static bool
verify_user (const void *uaddr) 
{
  return (uaddr < PHYS_BASE
          && pagedir_get_page (thread_current ()->pagedir, uaddr) != NULL);
}

/* Find and return the file that has FD from within
   the current thread's files list.

   RW_SEMA should be down before calling. */
struct file *get_file (int fd)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct file_helper *f;

  for (e = list_begin (&cur->files); e != list_end (&cur->files);
       e = list_next (e)){

    f = list_entry (e, struct file_helper, file_elem);
    if (f->fd == fd)
      return f->file;
  }
  return NULL;
}

/* System calls. */
void exit (int status)
{
  struct thread *cur = thread_current();
  cur->exit_status = status;
  
  thread_exit();
}

int write (int fd, const void *buffer, unsigned length)
{
  if (fd == 1) {
    putbuf (buffer, length);
    return length;
  }

  sema_down (&rw_sema);
  struct file *write_file = get_file (fd);

  if (!write_file){
    sema_up (&rw_sema);
    return 0;
  }

  if (!verify_user (buffer))
    exit (-1);

  int bytes_written = file_write (write_file, buffer, length);
  sema_up (&rw_sema);  

  return bytes_written;   
}

bool create (const char *file, unsigned initial_size)
{
  bool success = false;

  if (!file || !verify_user (file))
    exit (-1);
  
  sema_down (&rw_sema); 
  success =  filesys_create (file, initial_size);
  sema_up (&rw_sema);

  return success;
}

int open (const char *file)
{
  struct thread *cur = thread_current ();
  struct file *open_file;
  static struct file_helper fh;

  if (!file) 
    return -1;

  if (!verify_user (file))
    exit (-1);
  
  sema_down (&rw_sema);
  open_file = filesys_open (file);

  if (open_file) {
    fh.fd = cur->fd_next;
    cur->fd_next++;

    fh.file = open_file;
    list_push_front (&cur->files, &fh.file_elem);
    sema_up (&rw_sema);

    return fh.fd;
  }
  sema_up (&rw_sema);
  return -1;
}
