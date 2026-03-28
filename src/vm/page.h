#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/off_t.h"

/*LOcation or status of virtual page*/
enum page_status {
    PAGE_FILE,          /*Page data is in a (executable of m-map) file*/ 
    PAGE_SWAP,          /*Page data has been evicted to swap disk*/
    PAGE_ZERO           /*Pgae should be initialized to all zeroes (stack/bss)*/
};


/*Supplementary Page Table Entry*/
struct page_entry {
    void *upage;            /*The virual address of page(The hash key)*/
    enum page_status type;  /*Location/status of the data*/

    bool writable;          /*whether the file is read only or editable*/
    bool is_loaded;         /*Whether the page is currently in a physical frame*/

    /*Variables for PAGE_FILE type*/
    struct file *file;
    off_t offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;

    /*Variable for PAGE_SWAP type*/
    size_t swap_index;

    struct hash_elem elem;  /*hash table element*/
};


/*helper fns to initialize and manipulate and SPT for each thread*/
void spt_init(struct hash *spt);
bool spt_insert_file (struct hash *spt, void *upage, struct file *file, off_t offset, uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool spt_insert_zero (struct hash *spt, void *upage);
struct page_entry *spt_lookup (struct hash *spt, void *upage);
void spt_destroy (struct hash *spt);

bool handle_mm_fault (struct page_entry *p);
bool spt_set_swap (struct hash *spt, void *upage, size_t swap_index);
bool spt_grow_stack (struct hash *spt, void *fault_addr);

void spt_unmap (struct hash *spt, uint32_t *pagedir, void *upage, struct file *file, off_t offset, size_t bytes);
#endif  /*vm/page.h*/
