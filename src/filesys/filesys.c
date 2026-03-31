#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/synch.h"

/* Partition that contains the file system. */
struct block *fs_device;
struct lock dir_lock;
static void do_format (void);
struct lock filesys_write_lock;
/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");


  cache_init ();
  lock_init (&dir_lock);
  inode_init ();
  free_map_init ();
  lock_init(&filesys_write_lock);
  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  /* flush all pending writes to disk before shutting down */
  free_map_close ();
  cache_flush ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  char basename[NAME_MAX + 1];

  /* Resolve the path to get the target directory and the final filename */
  struct dir *dir = dir_resolve_path (name, basename);
  bool success = false;

  lock_acquire (&dir_lock);
  success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size,false)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  lock_release (&dir_lock);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (name == NULL || strlen (name) == 0) return NULL;

  char basename[NAME_MAX + 1];
  struct dir *dir = dir_resolve_path (name, basename);
  struct inode *inode = NULL;

  if (dir != NULL)
    {
      if (strcmp (basename, ".") == 0)
        {
          /* They just want to open the directory itself */
          inode = dir_get_inode (dir);
          inode_reopen (inode);
        }
      else
        {
          /* Look up the final file/folder inside the resolved directory */
          lock_acquire (&dir_lock);
          dir_lookup (dir, basename, &inode);
          lock_release (&dir_lock);
        }
    }

  dir_close (dir);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  char basename[NAME_MAX + 1];
  struct dir *dir = dir_resolve_path (name, basename);
  bool success = false;

  lock_acquire (&dir_lock);
  if (dir != NULL)
    {
      success = dir_remove (dir, basename);
    }
  lock_release (&dir_lock);

  dir_close (dir);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
