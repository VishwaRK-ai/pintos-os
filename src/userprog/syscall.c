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

//new headers for file handling and management
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/free-map.h"

#include "vm/page.h"
#include "threads/malloc.h"

#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "devices/serial.h"

void sys_exit (int status);
static void syscall_handler (struct intr_frame *);

struct lock filesys_lock;

/*hlper to safely retrieve a file from the current threads fd_table*/
static struct file *
get_file_by_fd (int fd)
{
  if (fd < 2 || fd >= 128) 
    return NULL;
    
  return thread_current ()->fd_table[fd];
}

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
    if(vaddr == NULL || !is_user_vaddr(vaddr))
    {
      // printf("%s: exit(-1)\n",thread_current ()->name);
      thread_current()->exit_status = -1;
      thread_exit(); //exit thread if pointer is wrong

    }

    struct thread *t = thread_current ();

    if(t->pagedir == NULL)
    {
      t->exit_status = -1;
      thread_exit();
    }
    /*check if already mapped to physical frame*/
    if(pagedir_get_page(t->pagedir, vaddr) == NULL)
    {
      struct page_entry *p = spt_lookup(&t->spt, pg_round_down(vaddr));

      if(p!= NULL)
      {
        /*if page not loaded, load it*/
        if(!p->is_loaded)
        {
          if(!handle_mm_fault(p))
          {
            t->exit_status = -1;
            thread_exit();
          }
        }
      }
      else
      {
        t->exit_status = -1;
        thread_exit ();
      }
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



int
sys_mmap (int fd, void *addr)
{
  /*validate add:page aligned, non zero, in user space*/
  if (addr == NULL || pg_ofs(addr) != 0 || !is_user_vaddr(addr)) return -1;
  if (fd == 0 || fd ==1) return -1;


  struct thread *t = thread_current ();

  if(fd <2 || fd >= 128 || t->fd_table[fd] == NULL)
  {
    return -1;
  }
  //get the file
  struct file *f = t->fd_table[fd];
  if(f == NULL) return -1;

  /*reopen file so it has indep offset*/
  struct file *reopened_file = file_reopen(f);
  if(reopened_file == NULL) return -1;

  size_t length = file_length (reopened_file);
  if(length == 0) return -1;

  /*check overlaps in spt*/

  for(size_t offset = 0; offset <length; offset +=PGSIZE)
  {
    if(spt_lookup (&t->spt, addr + offset) != NULL)
    {
      file_close (reopened_file);
      return -1;
    }
  }

  /*Map the pages into spt , lazily*/
  for(size_t offset =0; offset < length; offset += PGSIZE)
  {
    size_t read_bytes = (offset +PGSIZE <length)?PGSIZE :length -offset;
    size_t zero_bytes =PGSIZE - read_bytes;
    spt_insert_file (&t->spt, addr+offset, reopened_file, offset,read_bytes, zero_bytes, true);
  }
  /*track map in the thread*/
  struct mmap_entry *mapping = malloc (sizeof *mapping);
  mapping->mapid = t->next_mapid++;
  mapping->file = reopened_file;
  mapping->upage = addr;
  mapping->size = length;
  list_push_back (&t->mmap_list, &mapping->elem);

  return mapping->mapid;
}

void
sys_munmap (int mapid)
{
  struct thread *t = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&t->mmap_list); e != list_end (&t->mmap_list); e = list_next (e)) {
        struct mmap_entry *mapping = list_entry (e, struct mmap_entry, elem);
        
        if (mapping->mapid == mapid) {
            /* Unmap all pages associated with this file */
            for (size_t offset = 0; offset <mapping->size; offset += PGSIZE) {
                size_t bytes = (offset + PGSIZE <mapping->size) ? PGSIZE : mapping->size - offset;
                spt_unmap (&t->spt, t->pagedir, mapping->upage + offset, mapping->file, offset, bytes);
            }
            // bool held = lock_held_by_current_thread(&filesys_lock);
            // if(!held)
              //lock_acquire(&filesys_lock);
            file_close (mapping->file);
            // if(!held)
              //lock_release(&filesys_lock);
            
            list_remove (&mapping->elem);
            free (mapping);
            return;
        }
    }
}


void
sys_exit (int status)
{
  struct thread *t = thread_current ();
  t->exit_status = status;
  thread_exit ();
}

/*ADD ALL SYSCALLS INSIDE THIS*/
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  thread_current()->saved_esp = f->esp;//save user stack ptr for stack growth checks
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
        
        putbuf(buffer, size); 
        f->eax = size;      
      }
      else if(fd >= 2 && fd < 128 && t->fd_table[fd] != NULL)
      {
        /*writing to file*/
        //lock_acquire(&filesys_lock);
        f->eax = file_write(t->fd_table[fd], buffer, size);
        //lock_release(&filesys_lock);
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
        printf("DEBUG: SYS_OPEN failed to open -> '%s'\n", file); 
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
        //lock_acquire(&filesys_lock);
        f->eax = file_length(t->fd_table[fd]);
        //lock_release(&filesys_lock);
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

      if(fd == 0 )
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
      else if(fd<2 || fd>= 128)
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
        /*Reading from file*/
        //lock_acquire(&filesys_lock);
        f->eax = file_read(t->fd_table[fd], (void*)buffer, size);
        //lock_release(&filesys_lock);
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
        //lock_acquire(&filesys_lock);
        file_seek (t->fd_table[fd], pos);
        //lock_release(&filesys_lock);
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
        //lock_acquire(&filesys_lock);
        f->eax = file_tell(t->fd_table[fd]);//taking only one arg
        //lock_release(&filesys_lock);
      }
      break;
    }

    case SYS_EXIT:
    {
      /*Sys_exit required 1 arg: the exit status code, it is in the next 4 bytes after the syscall number*/
      check_valid_ptr(f->esp+4);
      int exit_status = *(int *)(f->esp + 4);
      sys_exit (exit_status);          //save it for receipt
      /*Current pintos exit format*/
      // printf("%s: exit(%d)\n",t->name , exit_status);
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

    case SYS_MMAP:
    {
      check_valid_buffer(f->esp +4, 8);
      int fd = *((int*)f->esp +1);
      void *addr = (void *)(*((uint32_t *)f->esp +2));
      f->eax = sys_mmap(fd,addr);
      break;
    }
    case SYS_MUNMAP:
    {
      check_valid_buffer(f->esp+4 ,4);
      int mapid = *((int *)f->esp +1);
      sys_munmap(mapid);
      break;
    }

#ifdef FILESYS

    case SYS_CHDIR:
    {
        const char *dir = (const char *)*((uint32_t *)f->esp + 1);
        if (dir == NULL || !is_user_vaddr (dir)) 
          {
            sys_exit (-1);
          }
        
        char basename[NAME_MAX + 1];
        struct dir *target_dir = dir_resolve_path (dir, basename);
        
        if (target_dir == NULL) 
          {
            f->eax = false;
            break;
          }

        /* If the path was exactly "/", just go to root */
        if (strcmp (basename, ".") == 0)
          {
            if (thread_current ()->cwd != NULL)
              dir_close (thread_current ()->cwd);
            thread_current ()->cwd = target_dir;
            f->eax = true;
            break;
          }

        /* Look up the final directory name to enter */
        struct inode *inode = NULL;
        if (dir_lookup (target_dir, basename, &inode))
          {
            if (inode_is_dir (inode))
              {
                if (thread_current ()->cwd != NULL)
                  dir_close (thread_current ()->cwd);
                thread_current ()->cwd = dir_open (inode);
                f->eax = true;
              }
            else
              {
                /* It was a regular file, not a directory */
                inode_close (inode);
                f->eax = false;
              }
          }
        else
          {
            f->eax = false;
          }
          
        dir_close (target_dir);
        break;
      }

   case SYS_MKDIR:
      {
        const char *dir = (const char *)*((uint32_t *)f->esp + 1);
        if (dir == NULL || !is_user_vaddr (dir)) 
          {
            sys_exit (-1);
          }
          
        char basename[NAME_MAX + 1];
        struct dir *target_dir = dir_resolve_path (dir, basename);
        
        if (target_dir == NULL || strcmp (basename, ".") == 0)
          {
            f->eax = false;
            if (target_dir != NULL) dir_close (target_dir);
            break;
          }

        block_sector_t sector = 0;
        bool success = false;

        /*allocate a free sector for the new directory */
        if (free_map_allocate (1, &sector)) 
          {
            /*format that sector as a directory */
            if (dir_create (sector, 16)) 
              {
                /*open the new directory to inject the hidden files */
                struct dir *new_dir = dir_open (inode_open (sector));
                if (new_dir != NULL) 
                  {
                    /*add '.' (Points to itself) */
                    dir_add (new_dir, ".", sector);
                    
                    /*add '..' (Points to the parent) */
                    block_sector_t parent_sector = inode_get_inumber (dir_get_inode (target_dir));
                    dir_add (new_dir, "..", parent_sector);
                    
                    dir_close (new_dir);
                    
                    /*link the new directory to the parent directory */
                    success = dir_add (target_dir, basename, sector);
                  }
              }
            
            /* uf anything failed along the way, release the sector to prevent memory leaks */
            if (!success) 
              free_map_release (sector, 1);
          }
          
        f->eax = success;
        dir_close (target_dir);
        break;
      }

      case SYS_READDIR:
      {
        int fd = *((int *)f->esp + 1);
        char *name = (char *)*((uint32_t *)f->esp + 2);
        check_valid_buffer(name, NAME_MAX+1); 

        struct file *f_obj = get_file_by_fd (fd); 
        if (f_obj == NULL) { f->eax = false; break; }

        struct inode *inode = file_get_inode (f_obj);
        if (!inode_is_dir (inode)) { f->eax = false; break; }

        struct dir *dir = dir_open (inode_reopen (inode));
        if (dir == NULL) { f->eax = false; break; }

        /* THE FIX: Use your helper functions instead of accessing dir->pos directly! */
        dir_seek (dir, file_tell (f_obj));
        bool success = dir_readdir (dir, name);
        file_seek (f_obj, dir_tell (dir));
        
        dir_close (dir);

        f->eax = success;
        break;
      }

    case SYS_ISDIR:
    {
      int fd = *((int *)f->esp + 1);
      struct file *f_obj = get_file_by_fd (fd); 
      if (f_obj == NULL) { f->eax = false; break; }
          
      //lock_acquire(&filesys_lock);
      f->eax = inode_is_dir (file_get_inode (f_obj));
      //lock_release(&filesys_lock);
      break;
    }

    case SYS_INUMBER:
    {
      int fd = *((int *)f->esp + 1);
      struct file *f_obj = get_file_by_fd (fd); 
       if (f_obj == NULL)
        {
         f->eax = -1;
          break;
        }
          
        /* Get the inode, then get the sector number (inumber) */
      f->eax = inode_get_inumber (file_get_inode (f_obj));
      break;
    }

    #endif

  
    default:
    {
      printf("The required syscall ,%d is not implemented",syscall_number);
      thread_exit();
      break;
    }
  }
}
