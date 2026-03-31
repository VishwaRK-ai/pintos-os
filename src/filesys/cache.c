#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include <string.h>

#define CACHE_SIZE 64

struct cache_entry {
    bool valid;
    bool dirty;
    bool accessed;
    block_sector_t sector;
    uint8_t data[BLOCK_SECTOR_SIZE];
    struct lock block_lock;
};

static struct cache_entry cache[CACHE_SIZE];
static struct lock cache_lock; // Kept your original variable name
static int clock_hand;

/* Initialize buffer cache */
void
cache_init (void) {
    lock_init (&cache_lock);
    clock_hand = 0;
    for(int i = 0; i < CACHE_SIZE; i++)
    {
        cache[i].valid = false;
        cache[i].dirty = false;
        cache[i].accessed = false;
        lock_init(&cache[i].block_lock);
    }
}

/* Clock algo to find a free frame/evictable cache frame */
/* NOTE: Called with cache_lock held */
// static struct cache_entry *
// evict_and_allocate (block_sector_t sector){
//     while(true)
//     {
//         struct cache_entry *ce = &cache[clock_hand];
        
//         /*If empty slot foun*/
//         if(!ce->valid)
//         {
//             ce->valid = true;
//             ce->sector = sector;
//             ce->dirty = false;
//             ce->accessed = true;
//             clock_hand = (clock_hand + 1) % CACHE_SIZE;
//             lock_acquire(&ce->block_lock); /* Lock the specific block */
//             lock_release(&cache_lock);     /* Release global lock */
//             return ce;
//         }

//         if(ce->accessed)
//             ce->accessed = false; // try again
//         else
//         {
//             /*claim it BEFORE doing disk I/O. */
//             lock_acquire(&ce->block_lock); 
            
//             /*release the global lock before writing to disk 
//                so we don't freeze the whole OS */
//             lock_release(&cache_lock);
//             /*nw evict this block and write to disk if modified */
//             if(ce->dirty)
//             {
//                 block_write(fs_device, ce->sector, ce->data);
//             }
//             ce->valid = true;
//             ce->sector = sector;
//             ce->dirty = false;
//             ce->accessed = true;
//             clock_hand = (clock_hand + 1) % CACHE_SIZE;
//             return ce;
//         }

//         clock_hand = (clock_hand + 1) % CACHE_SIZE;
//     }
// }

/* Retrieve a cache entry, run clock algorithm to evict if necessary */
/* Retrieve a cache entry, run clock algorithm to evict if necessary */
static struct cache_entry *
get_cache_entry (block_sector_t sector)
{
    while (true) 
    {
        lock_acquire(&cache_lock);

        /* 1. Search if it's already in the cache */
        bool found = false;
        for(int i = 0; i < CACHE_SIZE; i++)
        {
            if(cache[i].valid && cache[i].sector == sector)
            {
                cache[i].accessed = true;
                lock_acquire (&cache[i].block_lock); 
                lock_release (&cache_lock);          
                
                /* CRITICAL FIX: While we waited for block_lock, did another thread evict this block? */
                if (cache[i].valid && cache[i].sector == sector) {
                    return &cache[i]; /* It's still ours! Safe to return. */
                }
                
                /* It got evicted while we waited! Release the wrong lock and try again. */
                lock_release(&cache[i].block_lock);
                found = true;
                break; 
            }
        }
        
        if (found) continue; /* Try the whole process again */

        /* 2. Not found, run clock algorithm to find a victim */
        struct cache_entry *ce = &cache[clock_hand];

        if (!ce->valid || !ce->accessed)
        {
            /* CLAIM THIS BLOCK BEFORE ANY OTHER THREAD CAN! */
            lock_acquire(&ce->block_lock);

            /* Save old data before we overwrite the metadata */
            block_sector_t old_sector = ce->sector;
            bool was_dirty = ce->dirty;

            /* Update metadata WHILE HOLDING GLOBAL LOCK so nobody else grabs it! */
            ce->valid = true;
            ce->sector = sector;
            ce->dirty = false;
            ce->accessed = true;
            clock_hand = (clock_hand + 1) % CACHE_SIZE;

            /* Release global lock so others can search the cache while we do slow Disk I/O */
            lock_release(&cache_lock);

            /* Perform Disk I/O while ONLY holding the block lock */
            if (was_dirty) {
                block_write(fs_device, old_sector, ce->data);
            }
            block_read(fs_device, sector, ce->data);

            return ce;
        }

        /* Give a second chance */
        ce->accessed = false;
        clock_hand = (clock_hand + 1) % CACHE_SIZE;
        lock_release(&cache_lock); /* Yield global lock to prevent freezing */
    }
}


/* Read from cache */
/* Reads exactly 'size' bytes from 'sector' at 'offset' into 'buffer' */
void
cache_read (block_sector_t sector, void *buffer, int offset, int size)
{
    struct cache_entry *ce = get_cache_entry (sector);
    memcpy (buffer, ce->data + offset, size);
    lock_release (&ce->block_lock);
}

/* Write to cache */
/* Writes exactly 'size' bytes from 'buffer' to 'sector' at 'offset' */
void
cache_write (block_sector_t sector, const void *buffer, int offset, int size)
{
    struct cache_entry *ce = get_cache_entry(sector);
    memcpy (ce->data + offset, buffer, size);
    ce->dirty = true;
    lock_release (&ce->block_lock);
}

/* Force all dirty cache blocks to be written back to the sec mem */
void
cache_flush (void)
{
    lock_acquire (&cache_lock);
    for(int i = 0; i < CACHE_SIZE; i++)
    {
        if(cache[i].valid && cache[i].dirty)
        {
            block_write (fs_device, cache[i].sector, cache[i].data);
            cache[i].dirty = false;
        }
    }
    lock_release (&cache_lock);
}