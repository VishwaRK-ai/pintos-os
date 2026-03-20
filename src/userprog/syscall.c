#include "userprog/syscall.h"
#include <stdio.h>
#include <stdarg.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
//added following headers
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include <syscall-nr.h>
#include "userprog/process.h"

//New headers for file handling and management
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "devices/input.h"


static void syscall_handler (struct intr_frame *);

struct lock filesys_lock;

void
syscall_init (void) 
{
  lock_init(&filesys_lock); //initialize lock when os boots
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*HElper function: Check if provided user memory pointer is valid, else kill user process*/
static void
check_valid_ptr(const void * vaddr)
{
  /*If it is NUll or,
    if it is above PHYS_BASE, ie trying to access Kernel Memory or,
    If it is unmapped memory*/
    if(vaddr == NULL || !is_user_vaddr(vaddr) || pagedir_get_page(thread_current () ->pagedir, vaddr) == NULL )
    {
      // printf("%s: exit(-1)\n",thread_current ()->name);
      thread_current()->exit_status = -1;
      thread_exit(); //exit thread if pointer is wrong

    }
}
static void
check_valid_buffer(const void * buffer, unsigned size)
{
  unsigned i;
  char* ptr =(char *)buffer;

  for(i =0;i<size;i++){
    check_valid_ptr((const void *)ptr);
    ptr++;
  }
}

static void
check_valid_string(const void * str)
{
  char *ptr = (char *)str;
  check_valid_ptr((const void *)ptr);
  while (*ptr != '\0')
  {
    ptr++;
    check_valid_ptr((const void *)ptr);
  }
}


/*ADD ALL SYSCALLS INSIDE THIS*/
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //check if esp is valid
  check_valid_buffer(f->esp,4);
  /**/
  //get syscall number(the first 4 bytes of sp)
  int syscall_number = *(int *)(f->esp);
  // printf("Syscall requested, syscall numer: %d\n",syscall_number);
  struct thread *t = thread_current();

  switch(syscall_number)
  {
    case SYS_WRITE:
    {
      check_valid_buffer(f->esp+4, 12);
      /*takes 3 arguments:(int fd, void* buffer, unsigned size)*/
      int fd = *((int *)f->esp + 1);
      const void *buffer = (const void *)*((uint32_t *)f->esp+2);
      unsigned size = *((unsigned *)f->esp +3);
      //validate buffer pointer
      check_valid_buffer(buffer, size);

      if (fd == 1) {
        /*fd 1 is Console Output (STDOUT)*/
        putbuf(buffer, size); 
        f->eax = size;      
      }
      else if(fd >= 2 && fd < 128 && t->fd_table[fd] != NULL)
      {
        /*writing to file*/
        lock_acquire(&filesys_lock);
        f->eax = file_write(t->fd_table[fd], buffer, size);
        lock_release(&filesys_lock);
      }
      else
      {
        f->eax = 0; /*Couldnt write anything*/
      }
      break;
    }
    
    case SYS_CREATE:
    {
      check_valid_buffer(f->esp+4, 8);
      const char* file = (const char *)*((uint32_t *)f->esp + 1);
      unsigned size = *((unsigned *)f->esp +2);
      //validate file pointer
      check_valid_string(file);

      lock_acquire(&filesys_lock);
      f->eax = filesys_create(file, size);
      lock_release(&filesys_lock);
      break;
    }

    
    case SYS_REMOVE:
    {
      check_valid_buffer(f->esp+4, 4);
      const char* file = (const char *)*((uint32_t *)f->esp + 1);
      //validate file pointer
      check_valid_string(file);

      lock_acquire(&filesys_lock);
      f->eax = filesys_remove(file);
      lock_release(&filesys_lock);
      break;
    }

    case SYS_OPEN:
    {
      check_valid_buffer(f->esp+4, 4);
      const char* file = (const char *)*((uint32_t *)f->esp + 1);
      //validate file pointer
      check_valid_string(file);
      lock_acquire(&filesys_lock);

      struct file* opened_file = filesys_open(file);
      lock_release(&filesys_lock);
      
      if(opened_file == NULL)
      {
        f->eax = -1; //File couldnt be opened
      }
      else
      {
        //Add file to threads
        int fd = t->next_fd;
        t->fd_table[fd] = opened_file;
        t->next_fd++;
        f->eax = fd;//return user program the file descripter int
      }
      break;
    }

    case SYS_CLOSE:
    {
      check_valid_buffer(f->esp+4, 4);
      int fd = *((int *)f->esp + 1);
      //validate fd pointer(needs to be in limits and is properly opened)
      if(fd<2 || fd>= 128)
      {
        // printf("%s, thread killed due to invalid range.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else if(t->fd_table[fd] == NULL)
      {
        // printf("%s, thread killed due to invalid fd.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else
      {
        //valid fd
        lock_acquire(&filesys_lock);
        file_close(t->fd_table[fd]);
        lock_release(&filesys_lock);
        t->fd_table[fd] = NULL;//clear it from threads
      }
      break;
    }

    case SYS_FILESIZE:
    {
      check_valid_buffer(f->esp+4, 4);
      int fd = *((int *)f->esp + 1);
      //validate fd pointer(needs to be in limits and is properly opened)
      if(fd<2 || fd>= 128)
      {
        // printf("%s, thread killed due to invalid range.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else if(t->fd_table[fd] == NULL)
      {
        // printf("%s, thread killed due to invalid fd.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else
      {
        //valid fd
        lock_acquire(&filesys_lock);
        f->eax = file_length(t->fd_table[fd]);
        lock_release(&filesys_lock);
      }
      break;
    }

    case SYS_READ:
    {
      check_valid_buffer(f->esp+4, 12);
      int fd= *((int *)f->esp + 1);
      const void *buffer = (const void *)*((uint32_t *)f->esp + 2);
      unsigned size = *((unsigned *)f->esp+3);
      //validate buffer pointer
      check_valid_buffer(buffer, size);

      if(fd<2 || fd>= 128)
      {
        // printf("%s, thread killed due to invalid range.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else if(t->fd_table[fd] == NULL)
      {
        // printf("%s, thread killed due to invalid fd.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else if(fd == 0 )
      {
        /*read from keyboard (fd == 0 for STDIN)*/
        unsigned i ;
        uint8_t *buffer_ptr = (uint8_t *)buffer;
        for(i = 0; i < size; i++)
        {
          buffer_ptr[i] = input_getc();//save into array
        }
        f->eax = size;
      }
      else
      {
        /*Reading from file*/
        lock_acquire(&filesys_lock);
        f->eax = file_read(t->fd_table[fd], (void*)buffer, size);
        lock_release(&filesys_lock);
      }
      break;
    }

    case SYS_SEEK:
    {
      check_valid_buffer(f->esp+4, 8);
      int fd = *((int *)f->esp +1);
      unsigned pos = *((unsigned * )f->esp + 2);


      if(fd<2 || fd>= 128)
      {
        // printf("%s, thread killed due to invalid range.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else if(t->fd_table[fd] == NULL)
      {
        // printf("%s, thread killed due to invalid fd.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else
      {
        //valid fd
        lock_acquire(&filesys_lock);
        file_seek (t->fd_table[fd], pos);
        lock_release(&filesys_lock);
      }
      break;

    }

    case SYS_TELL:
    {
      check_valid_buffer(f->esp+4, 4);
      int fd = *((int *)f->esp +1);

      if(fd<2 || fd>= 128)
      {
        // printf("%s, thread killed due to invalid range.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else if(t->fd_table[fd] == NULL)
      {
        // printf("%s, thread killed due to invalid fd.\n", thread_current ()-> name);
        f->eax = -1;
      }
      else
      {
        //valid fd
        lock_acquire(&filesys_lock);
        f->eax = file_tell(t->fd_table[fd]);//taking only one arg
        lock_release(&filesys_lock);
      }
      break;
    }

    case SYS_EXIT:
    {
      /*Sys_exit required 1 arg: the exit status code, it is in the next 4 bytes after the syscall number*/
      check_valid_ptr(f->esp+4);
      int exit_status = *(int *)(f->esp + 4);
      t->exit_status = exit_status;           //save it for receipt
      /*Current pintos exit format*/
      // printf("%s: exit(%d)\n",t->name , exit_status);
      thread_exit();
      break;
    }

    case SYS_EXEC:
    {
      check_valid_buffer(f->esp+4, 4);
      const char *cmd_line = (const char *)*((uint32_t *)f->esp+1);
      check_valid_string(cmd_line);
      f->eax = process_execute(cmd_line);
      break;
    }

    case SYS_WAIT:
    {
      check_valid_buffer(f->esp+4, 4);
      tid_t pid = *((tid_t *)f->esp +1);
      f->eax = process_wait(pid);
      break;
    }
  
    default:
    {
      printf("The required syscall ,%d is not implemented",syscall_number);
      thread_exit();
      break;
    }
  }
}
