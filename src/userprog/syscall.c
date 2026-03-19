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

static void syscall_handler (struct intr_frame *);
/*HElper function: Check if provided user memory pointer is valid, else kill user process*/
static void
check_valid_ptr(const void * vaddr)
{
  /*If it is NUll or,
    if it is above PHYS_BASE, ie trying to access Kernel Memory or,
    If it is unmapped memory*/
    if(vaddr == NULL || !is_user_vaddr(vaddr) || pagedir_get_page(thread_current () ->pagedir, vaddr) == NULL )
    {
      printf("%s:Thread killed due to its invalidity\n",thread_current ()->name);
      printf("Pointer Address: %p\n",vaddr);
      thread_exit(); //exit thread if pointer is wrong

    }
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*ADD ALL SYSCALLS INSIDE THIS*/
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //check if esp is valid
  check_valid_ptr(f->esp);
  /**/
  //get syscall number(the first 4 bytes of sp)
  int syscall_number = *(int *)(f->esp);
  printf("Syscall requested, syscall numer: %d\n",syscall_number);

  switch(syscall_number)
  {
    case SYS_EXIT:
    {
      /*Sys_exit required 1 arg: the exit status code, it is in the next 4 bytes after the syscall number*/
      check_valid_ptr(f->esp + 4);
      int exit_status = *(int *)(f->esp + 4);
      /*Current pintos exit format*/
      printf("%s: exit(%d)\n", thread_current()->name, exit_status);
      thread_exit();
      break;
    }

    case SYS_WRITE:
    {
      /*takes 3 arguments:(int fd, void* buffer, unsigned size)*/
      int fd = *((int *)f->esp + 1);
      const void *buffer = (const void *)*((uint32_t *)f->esp+2);
      unsigned size = *((unsigned *)f->esp +3);
      //validate buffer pointer
      check_valid_ptr(buffer);

      if(fd == 1)
      {
        putbuf(buffer, size); //Print the string onto terminal
        f->eax = size;      //Store return value in register eaxx
      }
      break;
    }

    default:
    {
      printf("The required syscall %d is not implemented",syscall_number);
      thread_exit();
      break;
    }
  }
}
