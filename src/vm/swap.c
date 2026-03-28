#include "vm/swap.h"
#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include <debug.h>

static struct block *swap_block;
static struct bitmap *swap_map;
static struct lock swap_lock;

static const size_t SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

/*Setup swap disk ant tracking bitmap*/
void
swap_init (void)
{
    swap_block = block_get_role (BLOCK_SWAP);
    if (swap_block == NULL)
        return; //no swap disk found

    /*Create a bitmat with 1 bit for each swap slot(1 bit/memory page)*/
    swap_map = bitmap_create(block_size(swap_block)/SECTORS_PER_PAGE);
    lock_init(&swap_lock);
}

/*Write page to an empty slot in disk and return slot index*/
size_t
swap_out (void *kpage)
{
    lock_acquire(&swap_lock);

    /*Find the first available 0 bit and flip to 1*/
    size_t free_index = bitmap_scan_and_flip (swap_map, 0, 1, false);

    if(free_index == BITMAP_ERROR)
        PANIC("Swap disk is completely full");

    /*Else write all 8 sectors to disk*/
    size_t i;
    for(i =0;i<SECTORS_PER_PAGE;i++)
    {
        block_write(swap_block, free_index * SECTORS_PER_PAGE + i, (uint8_t *)kpage + i * BLOCK_SECTOR_SIZE);
    }
    lock_release(&swap_lock);
    return free_index;
}

/*Read the page from disk back to memory and then free the slot*/
void
swap_in (size_t swap_index, void *kpage)
{
    lock_acquire (&swap_lock);

    if(bitmap_test (swap_map, swap_index) == false)
        PANIC ("Trying to swap in an empty slot");

    /*Read all 8 sectors from the disk*/
    size_t i;
    for(i = 0;i<SECTORS_PER_PAGE;i++)
    {
        block_read (swap_block, swap_index * SECTORS_PER_PAGE + i, (uint8_t *)kpage + i * BLOCK_SECTOR_SIZE);
    }
    /*mark the slot back as free, 0*/
    bitmap_reset (swap_map, swap_index);

    lock_release (&swap_lock);
}