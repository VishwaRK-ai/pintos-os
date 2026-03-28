#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

void swap_init (void);

/*Write a memory page to swap disk and return swap slot index*/
size_t swap_out (void *kpage);

/* Reads a page from the swap disk into memoryand frees the slot. */
void swap_in (size_t swap_index, void *kpage);

#endif /* vm/swap.h */