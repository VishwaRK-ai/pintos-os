#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/thread.h"


void frame_init (void);
void *frame_allocate (enum palloc_flags flags, void *upage);
void frame_free (void *kpage);
void frame_unpin (void *kpage);
void frame_free_thread (void);
#endif
