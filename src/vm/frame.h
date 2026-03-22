#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/thread.h"


/*A Physical frame of memory*/

struct frame_entry {
    void *kpage;        /*Physical Addres(Kernel page)*/
    void *upage;        /*Virtual Address(User page)*/

    struct thread *owner;   /*The theread which owns this frame*/
    struct list_elem elem;  /*for storing in global frame list*/
};

void frame_init (void);
void *frame_allocate (enum palloc_flags flags, void *upage);
void frame_free (void *kpage);


#endif
