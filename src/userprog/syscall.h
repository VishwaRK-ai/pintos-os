#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stddef.h>
#include <list.h>

struct mmap_entry {
    int mapid;                  
    struct file *file;          
    void *upage;                
    size_t size;                
    struct list_elem elem;      
};

void syscall_init (void);
void sys_munmap (int mapid);


#endif /* userprog/syscall.h */
