#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include <string.h>
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"


extern struct lock filesys_lock;
/*Hash for SPT: using Virtual Address as the key*/
static unsigned
spt_hash (const struct hash_elem *e, void *aux UNUSED)
{
    const struct page_entry *p = hash_entry (e, struct page_entry, elem);
    return hash_bytes (&p->upage, sizeof p->upage);
}

/*Comparision function for SPT ie to compare two virtual addresses*/
static bool
spt_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
    const struct page_entry *pa = hash_entry (a, struct page_entry, elem);
    const struct page_entry *pb = hash_entry (b, struct page_entry, elem);

    return pa->upage < pb->upage;
}

/*Destroy function for SPT*/
static void
spt_destroy_func (struct hash_elem *e, void *aux UNUSED)
{
    struct page_entry *p = hash_entry (e, struct page_entry, elem);
    /*TODO: if page is in physical fram , free the fame here , later*/
    free(p);
}

/*INitialize spt for thread*/
void
spt_init(struct hash *spt)
{
    hash_init (spt, spt_hash, spt_less, NULL);
}

/*Destroy spt, freeing all resources*/
void
spt_destroy(struct hash *spt)
{
    hash_destroy (spt, spt_destroy_func);
}
/* Insert file-backed page entry into SPT */
bool
spt_insert_file (struct hash *spt, void *upage, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
    struct page_entry *p = malloc (sizeof *p);
    if (p == NULL) 
        return false;

    p->upage = upage;
    p->type = PAGE_FILE;
    p->writable = writable;
    p->is_loaded = false;
    p->file = file;
    p->offset = offset;
    p->read_bytes = read_bytes;
    p->zero_bytes = zero_bytes;

    /*try to insert it*/
    struct hash_elem *e = hash_insert (spt, &p->elem);
    
    if (e != NULL) {
        /*the page already exists. 
        instead of failing, update the existing entry with the larger read size*/
        struct page_entry *existing = hash_entry(e, struct page_entry, elem);
        
        if (read_bytes > existing->read_bytes) {
            existing->read_bytes = read_bytes;
            existing->zero_bytes = zero_bytes;
        }
        if (writable) {
            existing->writable = true; /*change to writable if needed*/
        }
        
        free(p); /*clean up duplicate struct*/
        return true; /*continue*/
    }
    return true;
}

/*INsert an all zero page entry into spt for stack/bss*/
bool 
spt_insert_zero (struct hash *spt, void *upage)
{
    struct page_entry *p = malloc (sizeof *p);
    if (p == NULL){
        // printf("DeBUG: malloc failed in spt in spt)insert_zero!\n");
        return false;
    }
        

    p->upage = upage;
    p->type = PAGE_ZERO;
    p->writable = true;
    p->is_loaded = false;

    if(hash_insert(spt, &p->elem) != NULL)
    {
        // printf("DEBUG: hash_insert failed! Zero page %p already exits in SPT\n",upage);
        free(p);
        return false;
    }

    return true;
}

/*Lookup virtual address in spt, return entry, NULL if not found*/
struct page_entry *
spt_lookup (struct hash *spt, void *upage)
{
    struct page_entry p;
    struct hash_elem *e;

    /*round of the nearest boundary page to find the key*/
    p.upage = (void *) pg_round_down (upage);

    e = hash_find (spt, &p.elem);
    if (e != NULL)
        return hash_entry (e, struct page_entry, elem);
    else
        return NULL;

}


bool 
handle_mm_fault (struct page_entry *p)
{
    /*Get a free frame from the frame allocator*/

    uint8_t *kpage = frame_allocate (PAL_USER, p->upage);
    if(kpage == NULL)
        return false;

    /*Load the page into the frame*/
    if(p->type == PAGE_FILE)
    {
        lock_acquire(&filesys_lock);

        int bytes_read = file_read_at (p->file, kpage, p->read_bytes, p->offset);
        lock_release (&filesys_lock);

        if(bytes_read != (int) p->read_bytes)
        {
            frame_free(kpage);
            return false;
        }
        memset (kpage + p->read_bytes, 0, p->zero_bytes);
    }
    else if (p->type == PAGE_ZERO)
    {
        memset (kpage, 0, PGSIZE);
    }
    else if (p->type == PAGE_SWAP)
    {
        swap_in(p->swap_index,kpage);
    }


    /*Add page to MMU(hadware's paget table)*/
    if(!install_page (p->upage, kpage, p->writable))
    {
        frame_free (kpage);
        return false;
    }
    
    p->is_loaded = true;
    frame_unpin(kpage);
    return true;
}

/*To mark page as evicted and record location on the swap disk*/
bool
spt_set_swap (struct hash *spt, void *upage, size_t swap_index)
{
    struct page_entry *p = spt_lookup (spt, upage);
    if(p == NULL)
        return false;
    
    p->type = PAGE_SWAP;
    p->swap_index = swap_index;
    p->is_loaded = false;
    return true;
}


/*Grow stack by allocating a new ZERO page at the fault address*/
bool
spt_grow_stack (struct hash *spt, void *fault_addr)
{
    struct page_entry *p = malloc (sizeof *p);
    if(p == NULL) return false;

    p->upage = pg_round_down (fault_addr);
    p->is_loaded = false;
    p->type = PAGE_ZERO;//set stack pages to zero first
    p->writable = true;

    /*If inserted into SPT, allocate frame immediately*/
    if (hash_insert (spt, &p->elem) == NULL)
        return handle_mm_fault (p);
    else
    {
        free(p);
        return false;
    }
}


/*unmap page, write to disk if dirty, and free the frame*/
void
spt_unmap (struct hash *spt, uint32_t *pagedir, void *upage, struct file *file, off_t offset, size_t bytes)
{
    struct page_entry *p = spt_lookup (spt, upage);
    if (p== NULL) return;

    if(p->is_loaded)
    {
        void *kpage = pagedir_get_page(pagedir, p->upage);
        if(kpage != NULL)
        {
            /*If program modifies the memory, write it back to the file*/
            if(pagedir_is_dirty(pagedir, p->upage))
            {
                lock_acquire(&filesys_lock);
                file_write_at(file, kpage, bytes, offset);
                lock_release(&filesys_lock);
            }
            /*free teh physical memory and clear page table*/
            frame_free (kpage);
            pagedir_clear_page (pagedir, p->upage);
        }
    }
    /*Remove the page from the SPT*/
    hash_delete(spt, &p->elem);
    free(p);
}