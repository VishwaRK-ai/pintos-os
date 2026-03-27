#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include <string.h>

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

/*Insert file-backed page entry into SPT*/
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

    return (hash_insert (spt, &p->elem) == NULL);
}

/*INsert an all zero page entry into spt for stack/bss*/
bool 
spt_insert_zero (struct hash *spt, void *upage)
{
    struct page_entry *p = malloc (sizeof *p);
    if (p == NULL)
        return false;

    p->upage = upage;
    p->type = PAGE_ZERO;
    p->writable = true;
    p->is_loaded = false;

    return (hash_insert(spt, &p->elem) == NULL);
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
