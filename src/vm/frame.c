#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include <debug.h>
#include "vm/frame.h"
#include "vm/swap.h"
#include <string.h>
#include "threads/vaddr.h"

/*Physicl frame entry*/
struct frame_entry {
    void *kpage;    /*physical address of the frame*/
    void *upage;    /*virtual address of the page*/
    struct thread *t;   /*owner thread*/
    bool pinned;        /*if the frame cant be evicted as of now, true*/
    struct list_elem elem;  /*List elemement for the frame table*/
};

static struct list frame_table;
static struct lock frame_lock;
static struct list_elem *clock_ptr; /*pts to the current frame in the clock */
static void *evict_frame(void);
/*INitialize frame table*/
void
frame_init (void)
{
    list_init (&frame_table);
    lock_init (&frame_lock);
    clock_ptr = NULL;// initialize clk ptr
}

/*Allocate*/

void *
frame_allocate(enum palloc_flags flags, void *upage)
{
    lock_acquire (&frame_lock);
    /*Allocate physical page from user spave*/
    void *kpage = palloc_get_page (flags | PAL_USER);

    if(kpage == NULL)
    {
        //Physical memeory is full
        kpage = evict_frame();

        /*If zero page request, zero out the recycled frame*/
        if(flags & PAL_ZERO)
            memset (kpage, 0, PGSIZE);
    }
    struct frame_entry *f = malloc(sizeof *f);

    if(f == NULL)
    {
        /*If cant allocate frame, give physical page back*/
        palloc_free_page(kpage);
        lock_release (&frame_lock);
        return NULL;
    }

    f->upage = upage;
    f->kpage = kpage;
    f->t = thread_current ();
    f->pinned = true;/*pin page so this cant be evicted while it is loading*/

    list_push_back(&frame_table, &f->elem);
    lock_release(&frame_lock);

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
            if(clock_ptr == &f->elem){
                clock_ptr = list_next (clock_ptr);
                if (clock_ptr == list_end(&frame_table)){
                    clock_ptr = list_begin(&frame_table);
                }
            }
            list_remove(&f->elem);
            free(f);
            palloc_free_page(kpage);
            break;
        }
    }
    lock_release(&frame_lock);
}


/*evict frame using clock algorithm and return freed physical page*/
static void *
evict_frame(void)
{
    struct frame_entry *victim = NULL;

    /*if clk ptr is null or at the end, reset it to the beginning*/
    if(clock_ptr == NULL || clock_ptr == list_end(&frame_table))
        clock_ptr = list_begin(&frame_table);

    /*run to clock algo*/
    while(true) {
        struct frame_entry *f = list_entry(clock_ptr, struct frame_entry, elem);
        
        /*skip pinned frames*/
        if(!f->pinned)
        {
            if(pagedir_is_accessed(f->t->pagedir, f->upage))
            {
                //it was accessed recently, give it another chance, set to 0
                pagedir_set_accessed (f->t->pagedir, f->upage, false);
            }
            else
            {
                /*found, access bit was already 0*/
                victim = f;
                /*move the clock hand forward*/
                clock_ptr = list_next (clock_ptr);
                if (clock_ptr == list_end(&frame_table))
                {
                    clock_ptr = list_begin(&frame_table);
                }
                break;
            }
        }
        clock_ptr = list_next(clock_ptr);
        if(clock_ptr == list_end(&frame_table))
            clock_ptr = list_begin(&frame_table);
       
    }
    /*swap victims data out to the disk*/
    size_t swap_idx = swap_out(victim->kpage);

    /*update spt*/
    spt_set_swap (&victim->t->spt, victim->upage, swap_idx);

    /*disconnect physical page from the threads page directory*/
    pagedir_clear_page(victim->t->pagedir, victim->upage);

    /*remove victim from frame table*/
    list_remove (&victim->elem);
    void *freed_kpage = victim->kpage;
    free (victim);

    return freed_kpage;
    
}

/*to unpin a frame so the clock algo can evict it later*/
void
frame_unpin(void *kpage)
{
    struct list_elem *e;
    lock_acquire(&frame_lock);

    for(e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e))
    {
        struct frame_entry *f = list_entry(e, struct frame_entry, elem);
        if(f->kpage == kpage)
        {
            f->pinned = false;
            break;
        }
    }
    lock_release(&frame_lock);
}


/*Remove all frames belonging to dead threads*/
void
frame_free_thread(void)
{
    struct thread *t = thread_current ();
    lock_acquire (&frame_lock);
    struct list_elem *e = list_begin(&frame_table);

    while(e!= list_end(&frame_table))
    {
        struct frame_entry *f = list_entry (e, struct frame_entry, elem);
        if(f->t == t)
        {
            if(clock_ptr == e)
            {
                clock_ptr = list_next (clock_ptr);
            }
            e = list_remove (&f->elem);
            free (f);
        }
        else
            e = list_next (e);
    }

    if(clock_ptr == list_end (&frame_table))
    {
        clock_ptr = list_begin (&frame_table);
    }

    lock_release (&frame_lock);
}