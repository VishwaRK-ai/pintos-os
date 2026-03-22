#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include <debug.h>
#include "vm/frame.h"

static struct list frame_table;
static struct lock frame_lock;

/*INitialize frame table*/
void
frame_init (void)
{
    list_init (&frame_table);
    lock_init (&frame_lock);
}

/*Allocate*/

void *
frame_allocate(enum palloc_flags flags, void *upage)
{
    /*Allocate physical page from user spave*/
    void *kpage = palloc_get_page (flags | PAL_USER);

    if(kpage != NULL)
    {
        struct frame_entry *f = malloc (sizeof (struct frame_entry));
        if (f == NULL)
        {
            palloc_free_page(kpage);
            return NULL;
        }
        f->upage = upage;
        f->kpage = kpage;
        f->owner = thread_current ();


        /*Add to global frame table*/
        lock_acquire (&frame_lock);
        list_push_back (&frame_table, &f->elem);
        lock_release(&frame_lock);
    }
    else
    {
        /*TODO: Evict a frame if palloc fails*/
        /*Temporarily panic kernel until eviction is implemented*/
        PANIC ("Out of memory");
    }

    return kpage;
}

/*fn to free the frame and remove it from table*/

void
frame_free (void* kpage)
{
    struct list_elem *e;
    lock_acquire (&frame_lock);

    for(e = list_begin (&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_entry *f = list_entry(e, struct frame_entry, elem);
        if(f->kpage == kpage)
        {
            list_remove(&f->elem);
            free(f);
            palloc_free_page(kpage);
            break;
        }
    }
    lock_release(&frame_lock);
}
