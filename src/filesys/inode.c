#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdarg.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/synch.h"


/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCKS 123
#define INDIRECT_BLOCKS 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    
    /* Extensible File Pointers */
    block_sector_t direct[DIRECT_BLOCKS];   /* 123 Direct blocks */
    block_sector_t indirect;            /* 1 Single Indirect block */
    block_sector_t doubly_indirect;      /* 1 Doubly Indirect block */
    uint32_t is_dir;                  /* canged to uint32_t for exactly 512 bytes! */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock extend_lock;        /*adding lock*/
    struct lock state_lock;
    struct lock rw_lock;
  };

static struct list open_inodes;

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
/* Returns the disk sector containing byte offset POS within INODE.
   Returns -1 if POS is past the end of the file. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  if (pos >= inode->data.length)
    return (block_sector_t) -1;

  /* Index of the block we want to find */
  off_t index = pos / BLOCK_SECTOR_SIZE;

  /* 1. Direct Blocks (First 123 blocks) */
  if (index < DIRECT_BLOCKS) {
      return inode->data.direct[index];
  }
  index -= DIRECT_BLOCKS;

  /* 2. Single Indirect Blocks (Next 128 blocks) */
  if (index < INDIRECT_BLOCKS) {
      block_sector_t indirect_disk[INDIRECT_BLOCKS];
      cache_read (inode->data.indirect, &indirect_disk, 0, BLOCK_SECTOR_SIZE);
      return indirect_disk[index];
  }
  index -= INDIRECT_BLOCKS;

  /* 3. Doubly Indirect Blocks (128 * 128 blocks) */
  if (index < INDIRECT_BLOCKS * INDIRECT_BLOCKS) {
      off_t first_level_idx = index / INDIRECT_BLOCKS;
      off_t second_level_idx = index % INDIRECT_BLOCKS;

      block_sector_t indirect_disk[INDIRECT_BLOCKS];
      
      /* Read the level 1 pointer block */
      cache_read (inode->data.doubly_indirect, &indirect_disk, 0, BLOCK_SECTOR_SIZE);
      
      /* Read the level 2 pointer block */
      block_sector_t second_level_sector = indirect_disk[first_level_idx];
      cache_read (second_level_sector, &indirect_disk, 0, BLOCK_SECTOR_SIZE);
      
      return indirect_disk[second_level_idx];
  }

  return (block_sector_t) -1;
}

void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Helper to allocate a single block */
static bool
inode_allocate_block (block_sector_t *sector_ptr)
{
    static char zeros[BLOCK_SECTOR_SIZE];
    if (free_map_allocate (1, sector_ptr)) {
        cache_write (*sector_ptr, zeros, 0, BLOCK_SECTOR_SIZE);
        return true;
    }
    return false;
}
/* Allocates sectors for an inode based on its length */
static bool
inode_allocate (struct inode_disk *disk_inode)
{
  size_t num_sectors = bytes_to_sectors (disk_inode->length);
  size_t i;

  if (num_sectors == 0) return true;

  /* 1. Allocate Direct Blocks */
  for (i = 0; i < DIRECT_BLOCKS && i < num_sectors; i++) {
      if (!inode_allocate_block (&disk_inode->direct[i])) return false;
  }
  if (num_sectors <= DIRECT_BLOCKS) return true;
  num_sectors -= DIRECT_BLOCKS;

  /* 2. Allocate Single Indirect Block */
  if (num_sectors > 0) {
      block_sector_t indirect_disk[INDIRECT_BLOCKS];
      memset(indirect_disk, 0, BLOCK_SECTOR_SIZE);
      if (!inode_allocate_block (&disk_inode->indirect)) return false;
      
      for (i = 0; i < INDIRECT_BLOCKS && i < num_sectors; i++) {
          if (!inode_allocate_block (&indirect_disk[i])) return false;
      }
      cache_write (disk_inode->indirect, indirect_disk, 0, BLOCK_SECTOR_SIZE);
      
      if (num_sectors <= INDIRECT_BLOCKS) return true;
      num_sectors -= INDIRECT_BLOCKS;
  }

  /* 3. Allocate Doubly Indirect Blocks */
  if (num_sectors > 0) {
      block_sector_t level1[INDIRECT_BLOCKS];
      memset(level1, 0, BLOCK_SECTOR_SIZE);
      if (!inode_allocate_block (&disk_inode->doubly_indirect)) return false;

      for (i = 0; i < INDIRECT_BLOCKS && num_sectors > 0; i++) {
          block_sector_t level2[INDIRECT_BLOCKS];
          memset(level2, 0, BLOCK_SECTOR_SIZE);
          if (!inode_allocate_block(&level1[i])) return false;

          size_t j;
          for (j = 0; j < INDIRECT_BLOCKS && num_sectors > 0; j++, num_sectors--) {
              if (!inode_allocate_block(&level2[j])) return false;
          }
          cache_write(level1[i], level2, 0, BLOCK_SECTOR_SIZE);
      }
      cache_write(disk_inode->doubly_indirect, level1, 0, BLOCK_SECTOR_SIZE);
  }
  return true;
}
/* expand the file by allocating new sectors up to new_length */
/* expand file without duplicate cache writes */
static bool
inode_expand (struct inode_disk *id, off_t new_length)
{
  size_t new_secs = bytes_to_sectors (new_length);
  size_t i;

  for (i = 0; i < new_secs; i++)
    {
      if (i < DIRECT_BLOCKS)
        {
          if (id->direct[i] == 0) inode_allocate_block (&id->direct[i]);
        }
      else if (i < DIRECT_BLOCKS + INDIRECT_BLOCKS)
        {
          if (id->indirect == 0) inode_allocate_block (&id->indirect);

          block_sector_t ind[INDIRECT_BLOCKS];
          cache_read (id->indirect, ind, 0, BLOCK_SECTOR_SIZE);

          if (ind[i - DIRECT_BLOCKS] == 0)
            {
              if (inode_allocate_block (&ind[i - DIRECT_BLOCKS]))
                cache_write (id->indirect, ind, 0, BLOCK_SECTOR_SIZE);
            }
        }
      else if (i < DIRECT_BLOCKS + INDIRECT_BLOCKS + INDIRECT_BLOCKS * INDIRECT_BLOCKS)
        {
          if (id->doubly_indirect == 0) inode_allocate_block (&id->doubly_indirect);

          block_sector_t level1[INDIRECT_BLOCKS];
          cache_read (id->doubly_indirect, level1, 0, BLOCK_SECTOR_SIZE);

          off_t doubly_index = i - DIRECT_BLOCKS - INDIRECT_BLOCKS;
          off_t level1_idx = doubly_index / INDIRECT_BLOCKS;
          off_t level2_idx = doubly_index % INDIRECT_BLOCKS;

          if (level1[level1_idx] == 0)
            {
              if (inode_allocate_block (&level1[level1_idx]))
                cache_write (id->doubly_indirect, level1, 0, BLOCK_SECTOR_SIZE);
            }

          block_sector_t level2[INDIRECT_BLOCKS];
          cache_read (level1[level1_idx], level2, 0, BLOCK_SECTOR_SIZE);

          if (level2[level2_idx] == 0)
            {
              if (inode_allocate_block (&level2[level2_idx]))
                cache_write (level1[level1_idx], level2, 0, BLOCK_SECTOR_SIZE);
            }
        }
    }
  id->length = new_length;
  return true;
}

/* Initializes an inode with lENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device. Returns true if successful. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is too big. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir; /* We pass this in for directories */

      if (inode_allocate (disk_inode)) 
        {
          cache_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
          success = true; 
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->extend_lock);
  lock_init (&inode->state_lock);
  lock_init (&inode->rw_lock);
  cache_read (inode->sector, &inode->data,0,BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/*deallocate all sectors associated with an inode */
/*deallocate all sectors associated with an inode */
static void
inode_deallocate (struct inode *inode) 
{
  size_t num_sectors = bytes_to_sectors (inode->data.length);
  size_t i;

  if (num_sectors == 0) return;

  /* 1. Free direct blocks */
  for (i = 0; i < DIRECT_BLOCKS && i < num_sectors; i++) 
    {
      if (inode->data.direct[i] != 0) free_map_release (inode->data.direct[i], 1);
    }
  if (num_sectors <= DIRECT_BLOCKS) return;
  num_sectors -= DIRECT_BLOCKS;

  /* 2. Free single indirect block */
  if (num_sectors > 0 && inode->data.indirect != 0) 
    {
      block_sector_t indirect_disk[INDIRECT_BLOCKS];
      cache_read (inode->data.indirect, &indirect_disk, 0, BLOCK_SECTOR_SIZE);
      
      for (i = 0; i < INDIRECT_BLOCKS && i < num_sectors; i++) 
        {
          if (indirect_disk[i] != 0) free_map_release(indirect_disk[i], 1);
        }
      free_map_release(inode->data.indirect, 1);
    }
  if (num_sectors <= INDIRECT_BLOCKS) return;
  num_sectors -= INDIRECT_BLOCKS;

  /* 3. Free doubly indirect blocks */
  if (num_sectors > 0 && inode->data.doubly_indirect != 0)
    {
      block_sector_t level1[INDIRECT_BLOCKS];
      cache_read (inode->data.doubly_indirect, level1, 0, BLOCK_SECTOR_SIZE);
      
      for (i = 0; i < INDIRECT_BLOCKS && num_sectors > 0; i++) 
        {
          size_t blocks_to_check = (num_sectors > INDIRECT_BLOCKS) ? INDIRECT_BLOCKS : num_sectors;
          
          if (level1[i] != 0) {
              block_sector_t level2[INDIRECT_BLOCKS];
              cache_read (level1[i], level2, 0, BLOCK_SECTOR_SIZE);
              size_t j;
              for (j = 0; j < blocks_to_check; j++) {
                  if (level2[j] != 0) free_map_release(level2[j], 1);
              }
              free_map_release(level1[i], 1);
          }
          num_sectors -= blocks_to_check;
      }
      free_map_release(inode->data.doubly_indirect, 1);
    }
}
/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {

      //save before closing
      cache_write(inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_deallocate(inode);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  /* 1. ADD THIS: Acquire the lock so we wait if a writer is busy */
  lock_acquire(&inode->extend_lock);

  while (bytes_read < size) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == (block_sector_t) -1) break;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size-bytes_read < min_left ? size-bytes_read : min_left;
      if (chunk_size <= 0)
        break;

      /*Read directly from the buffer cache, without any bounce buffer*/
      cache_read (sector_idx,buffer +bytes_read, sector_ofs, chunk_size);
      
      /* Advance. */
      // size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  /* 2. ADD THIS: Release the lock when done reading */
  lock_release(&inode->extend_lock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  lock_acquire(&inode->extend_lock);

  /*if writing past the end of the file,need to grow it */
  if (offset + size > inode->data.length) 
    {
      inode->data.length = offset+size;
      inode_expand (&inode->data, offset + size);
      
      /*save the new pointers back to disk via cache */

      cache_write (inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
    }

  while (bytes_written < size) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == (block_sector_t) -1) break;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /*bytes left in the sector*/
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      /* Number of bytes to actually write into this sector. */
      int chunk_size = size-bytes_written < sector_left ? size-bytes_written : sector_left;
      if (chunk_size <= 0)
        break;

      /*write directly into buffer cache*/
      cache_write (sector_idx, buffer +bytes_written, sector_ofs, chunk_size);
      /* Advance. */
      // size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);


  lock_release(&inode->extend_lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire(&inode->state_lock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->state_lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire(&inode->state_lock);

  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->state_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk id;
  cache_read (inode->sector, &id, 0, BLOCK_SECTOR_SIZE);
  return id.length; //ret updated lenth from cache
}


/* Returns true if INODE is a directory, false otherwise. */
bool
inode_is_dir (const struct inode *inode)
{
  struct inode_disk *id = NULL;
  bool is_dir = false;
  
  if (inode == NULL) return false;
  
  block_sector_t sector = inode_get_inumber(inode);
  id = malloc(sizeof *id);
  if (id == NULL) return false;
  
  /* We use the cache to read the disk-based inode data */
  cache_read(sector, id, 0, BLOCK_SECTOR_SIZE);
  is_dir = id->is_dir;
  free(id);
  
  return is_dir;
}